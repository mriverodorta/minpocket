"""Measure early U-Boot and the relocated startup on the MP1."""
import os
from pathlib import Path
import re
import sys

if sys.argv[1] in ("--cache-nodes", "--verify-nodes"):
    import libfdt

    tree = libfdt.Fdt(Path(sys.argv[2]).read_bytes())
    root = Path(sys.argv[3])
    paths = (root / "drivers/core/minpocket-early-paths").read_text().splitlines()
    offsets = [tree.path_offset(path) for path in paths]
    header = root / "drivers/core/minpocket-early-nodes.h"
    contents = "#define MINPOCKET_EARLY_OFFSETS " + ", ".join(map(str, offsets)) + "\n"
    if sys.argv[1] == "--verify-nodes":
        if header.read_text() != contents:
            raise SystemExit("U-Boot control DT changed after caching early nodes")
    else:
        header.write_text(contents)
    print("Verified MP1 control DT offsets:", dict(zip(paths, offsets)))
    sys.exit()

root = Path(sys.argv[1])
deferred = os.environ.get("MP1_DEFER_EXTENDED_SCAN") == "1"
serial_deferred = os.environ.get("MP1_DEFER_SERIAL") == "1"
pinctrl_deferred = os.environ.get("MP1_DEFER_PINCTRL") == "1"
dmc_profile = os.environ.get("MP1_PROFILE_DMC") == "1"
single_pass = os.environ.get("MP1_SINGLE_PASS_EARLY_SCAN") == "1"


def patch(path, edits):
    file = root / path
    source = file.read_text()
    for old, new in edits:
        if source.count(old) != 1:
            raise SystemExit(f"Expected one timing anchor in {path}: {old}")
        source = source.replace(old, new, 1)
    file.write_text(source)


patch("arch/arm/include/asm/global_data.h", [
    ("struct arch_global_data {", """struct minpocket_call_sample {
	u64 ticks;
	char name[32];
};

struct arch_global_data {
	struct minpocket_call_sample minpocket_calls[2][3];
	u64 minpocket_post_scan_ticks;
	u64 minpocket_post_serial_ticks;"""),
])

(root / "include/minpocket-profile.h").write_text(r"""
#ifndef __MINPOCKET_PROFILE_H
#define __MINPOCKET_PROFILE_H

#include <linux/types.h>

enum minpocket_group { MINPOCKET_DM, MINPOCKET_SERIAL, MINPOCKET_BIND = MINPOCKET_SERIAL };

static inline u64 minpocket_ticks(void)
{
	u64 ticks;

	asm volatile("isb; mrs %0, cntvct_el0" : "=r" (ticks));
	return ticks;
}

void minpocket_record_call(enum minpocket_group group, const char *name, u64 elapsed);

#define MP_PROFILE(group, name, expression) ({ \
	bool early = !(gd->flags & GD_FLG_RELOC); \
	u64 start = early ? minpocket_ticks() : 0; \
	__auto_type result = (expression); \
	if (early) \
		minpocket_record_call(group, name, minpocket_ticks() - start); \
	result; \
})

#endif
""")

for path in ("common/board_f.c", "drivers/core/root.c", "drivers/serial/serial-uclass.c"):
    patch(path, [("#include <asm/global_data.h>",
                  "#include <asm/global_data.h>\n#include <minpocket-profile.h>")])

patch("common/board_f.c", [
    ("static int initf_dm(void)", """void minpocket_record_call(enum minpocket_group group, const char *name, u64 elapsed)
{
	struct minpocket_call_sample *samples = gd->arch.minpocket_calls[group];

	for (int slot = 0; slot < ARRAY_SIZE(gd->arch.minpocket_calls[group]); slot++) {
		if (elapsed <= samples[slot].ticks)
			continue;
		memmove(&samples[slot + 1], &samples[slot],
			(ARRAY_SIZE(gd->arch.minpocket_calls[group]) - slot - 1) * sizeof(*samples));
		samples[slot].ticks = elapsed;
		memset(samples[slot].name, 0, sizeof(samples[slot].name));
		strncpy(samples[slot].name, name, sizeof(samples[slot].name) - 1);
		break;
	}
}

static int initf_dm(void)"""),
    ("ret = dm_autoprobe();",
     'ret = MP_PROFILE(MINPOCKET_DM, "dm_autoprobe", dm_autoprobe());'),
])

patch("drivers/core/root.c", [
    (f"ret = {call};", f'ret = MP_PROFILE(MINPOCKET_DM, "{name}", {call});')
    for name, call in [
        ("dm_init", "dm_init(CONFIG_IS_ENABLED(OF_LIVE))"),
        ("dm_scan_plat", "dm_scan_plat(pre_reloc_only)"),
        ("dm_scan_other", "dm_scan_other(pre_reloc_only)"),
    ]
])

early_scan = (
    'MP_PROFILE(MINPOCKET_DM, "early boot devices", minpocket_scan_early_devices())'
    if deferred else
    'MP_PROFILE(MINPOCKET_DM, "dm_extended_scan", dm_extended_scan(true))'
)
patch("drivers/core/root.c", [
    ("ret = dm_extended_scan(pre_reloc_only);", f"""if (pre_reloc_only) {{
			ret = {early_scan};
		}} else {{
			u64 start = minpocket_ticks();

			ret = dm_extended_scan(false);
			gd->arch.minpocket_post_scan_ticks = minpocket_ticks() - start;
		}}"""),
])

if deferred:
    patch("drivers/core/root.c", [
        ("static int dm_scan(bool pre_reloc_only)", """/*
 * Bind the MP1 control tree's bootph-all root devices in tree order.
 * The UART and its clock, syscon and pinctrl providers must be present
 * before serial_init(). The full scan runs in initr_dm().
 */
static int minpocket_scan_early_devices(void)
{
	static const char *const paths[] = {
		"/xin24m", "/syscon@fdc20000", "/syscon@fdc60000",
		"/clock-controller@fdd00000", "/clock-controller@fdd20000",
		"/serial@fe660000", "/pinctrl", "/dmc",
	};

	for (int i = 0; i < ARRAY_SIZE(paths); i++) {
		ofnode node = ofnode_path(paths[i]);
		int ret;

		if (!ofnode_valid(node))
			return -ENOENT;
		if (!ofnode_is_enabled(node))
			continue;
		ret = lists_bind_fdt(gd->dm_root, node, NULL, NULL, true);
		if (ret)
			return ret;
	}

	return 0;
}

static int dm_scan(bool pre_reloc_only)"""),
    ])

if pinctrl_deferred:
    # Debug UART sets its mux directly and the DM UART now probes after relocation.
    patch("drivers/core/root.c", [
        ('"/serial@fe660000", "/pinctrl", "/dmc",', '"/serial@fe660000", "/dmc",'),
    ])

if serial_deferred:
    # Late serial leaves the three early serial slots free for binding samples.
    patch("drivers/core/root.c", [
        ("ofnode node = ofnode_path(paths[i]);",
         "u64 start = minpocket_ticks();\n\t\tofnode node = ofnode_path(paths[i]);"),
        ("ret = lists_bind_fdt(gd->dm_root, node, NULL, NULL, true);",
         "ret = lists_bind_fdt(gd->dm_root, node, NULL, NULL, true);\n"
         "\t\tminpocket_record_call(MINPOCKET_BIND, paths[i], minpocket_ticks() - start);"),
    ])

if single_pass:
    patch("drivers/core/root.c", [
        ("""	for (int i = 0; i < ARRAY_SIZE(paths); i++) {
		u64 start = minpocket_ticks();
		ofnode node = ofnode_path(paths[i]);
		int ret;
""", """	ofnode root = ofnode_path("/");
	ofnode node = root;

	for (int i = 0; i < ARRAY_SIZE(paths); i++) {
		u64 start = minpocket_ticks();
		int ret;

		node = i ? ofnode_next_subnode(node) : ofnode_first_subnode(root);
		while (ofnode_valid(node)) {
			const char *name = ofnode_get_name(node);

			if (name && !strcmp(name, paths[i] + 1))
				break;
			node = ofnode_next_subnode(node);
		}
		/* Keep binding in the same order if the control tree is reordered. */
		if (!ofnode_valid(node))
			node = ofnode_path(paths[i]);
"""),
    ])

if os.environ.get("MP1_TEST_FIRST_FRAME") == "1":
    driver = root / "drivers/core/root.c"
    table = driver.read_text().split("static const char *const paths[] = {", 1)[1].split("};", 1)[0]
    paths = re.findall(r'"(/[^"]+)"', table)
    driver.with_name("minpocket-early-paths").write_text("\n".join(paths) + "\n")
    driver.with_name("minpocket-early-nodes.h").write_text(
        "#define MINPOCKET_EARLY_OFFSETS " + ", ".join("-1" for _ in paths) + "\n"
    )
    scan = driver.read_text()
    start = scan.index("\tofnode root = ofnode_path")
    end = scan.index("\n\t\tif (!ofnode_valid(node))\n\t\t\treturn -ENOENT;", start)
    patch("drivers/core/root.c", [
        ("#include <minpocket-profile.h>", '#include <minpocket-profile.h>\n#include "minpocket-early-nodes.h"'),
        (scan[start:end], """	static const int offsets[] = { MINPOCKET_EARLY_OFFSETS };

	BUILD_BUG_ON(ARRAY_SIZE(paths) != ARRAY_SIZE(offsets));
	for (int i = 0; i < ARRAY_SIZE(paths); i++) {
		u64 start = minpocket_ticks();
		ofnode node = ofnode_null();
		const char *name;
		int ret;

		if (!of_live_active() && offsets[i] >= 0)
			node = offset_to_ofnode(offsets[i]);
		name = ofnode_valid(node) ? ofnode_get_name(node) : NULL;
		if (!name || strcmp(name, paths[i] + 1))
			node = ofnode_path(paths[i]);"""),
    ])

if dmc_profile:
    patch("arch/arm/include/asm/global_data.h", [
        ("struct arch_global_data {", """enum minpocket_dmc_step {
	MINPOCKET_DMC_LOOKUP,
	MINPOCKET_DMC_SELECT,
	MINPOCKET_DMC_BIND,
	MINPOCKET_DMC_PROBE,
	MINPOCKET_DMC_INFO,
	MINPOCKET_DMC_STEPS,
};

struct arch_global_data {
	u64 minpocket_dmc_ticks[MINPOCKET_DMC_STEPS];"""),
    ])
    patch("drivers/core/root.c", [
        ("ret = lists_bind_fdt(gd->dm_root, node, NULL, NULL, true);",
         """if (!strcmp(paths[i], "/dmc"))
			gd->arch.minpocket_dmc_ticks[MINPOCKET_DMC_LOOKUP] = minpocket_ticks() - start;
		ret = lists_bind_fdt(gd->dm_root, node, NULL, NULL, true);"""),
    ])
    patch("drivers/core/lists.c", [
        ("#include <debug_uart.h>",
         "#include <asm/global_data.h>\n#include <minpocket-profile.h>\n#include <debug_uart.h>\n\nDECLARE_GLOBAL_DATA_PTR;"),
        ("int compat_length, i;", """int compat_length, i;
	u64 start = pre_reloc_only ? minpocket_ticks() : 0;
	bool profile_dmc;"""),
        ("name = ofnode_get_name(node);", """name = ofnode_get_name(node);
	profile_dmc = pre_reloc_only && parent == gd->dm_root && !strcmp(name, "dmc");"""),
        ("\t\t\tret = device_bind_with_driver_data(parent, entry, name,",
         """			u64 bind_start = 0;

			if (profile_dmc) {
				bind_start = minpocket_ticks();
				gd->arch.minpocket_dmc_ticks[MINPOCKET_DMC_SELECT] = bind_start - start;
			}
			ret = device_bind_with_driver_data(parent, entry, name,"""),
        ("&dev);\n\t\t\tif (!drv && ret == -ENODEV)",
         """&dev);
			if (profile_dmc)
				gd->arch.minpocket_dmc_ticks[MINPOCKET_DMC_BIND] = minpocket_ticks() - bind_start;
			if (!drv && ret == -ENODEV)"""),
    ])
    patch("arch/arm/mach-rockchip/sdram.c", [
        ("int dram_init(void)", """#include <minpocket-profile.h>

int dram_init(void)"""),
        ("ret = uclass_get_device(UCLASS_RAM, 0, &dev);",
         """u64 start = minpocket_ticks();

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	gd->arch.minpocket_dmc_ticks[MINPOCKET_DMC_PROBE] = minpocket_ticks() - start;"""),
        ("ret = ram_get_info(dev, &ram);",
         """start = minpocket_ticks();
	ret = ram_get_info(dev, &ram);
	gd->arch.minpocket_dmc_ticks[MINPOCKET_DMC_INFO] = minpocket_ticks() - start;"""),
    ])

patch("drivers/serial/serial-uclass.c", [
    ("if (!uclass_get_device_by_of_offset(UCLASS_SERIAL, node, devp))",
     'if (!MP_PROFILE(MINPOCKET_SERIAL, "stdout device lookup/probe",\n'
     '\t\t\tuclass_get_device_by_of_offset(UCLASS_SERIAL, node, devp)))'),
    ("if (node > 0 && !lists_bind_fdt(gd->dm_root, offset_to_ofnode(node),\n"
     "\t\t\t\t\tdevp, NULL, false)) {",
     'if (node > 0 && !MP_PROFILE(MINPOCKET_SERIAL, "stdout bind",\n'
     '\t\t\tlists_bind_fdt(gd->dm_root, offset_to_ofnode(node), devp, NULL, false))) {'),
    ("!device_probe(*devp))",
     '!MP_PROFILE(MINPOCKET_SERIAL, "stdout fallback probe", device_probe(*devp)))'),
    ("ret = fetch_baud_from_dtb();",
     'ret = MP_PROFILE(MINPOCKET_SERIAL, "DT baud lookup", fetch_baud_from_dtb());'),
    ("\tserial_find_console_or_panic();\n\tif (gd->cur_serial_dev)",
     '\tu64 console_start = minpocket_ticks();\n\tserial_find_console_or_panic();\n'
     '\tif (!(gd->flags & GD_FLG_RELOC))\n'
     '\t\tminpocket_record_call(MINPOCKET_SERIAL, "console selection", minpocket_ticks() - console_start);\n'
     '\tif (gd->cur_serial_dev)'),
    ("\tserial_setbrg();\n#endif\n\n\treturn 0;",
     '\tu64 baud_start = minpocket_ticks();\n\tserial_setbrg();\n'
     '\tif (!(gd->flags & GD_FLG_RELOC))\n'
     '\t\tminpocket_record_call(MINPOCKET_SERIAL, "serial_setbrg", minpocket_ticks() - baud_start);\n'
     '#endif\n\n\treturn 0;'),
])

if serial_deferred:
    patch("common/board_f.c", [
        ("\tINITCALL(serial_init);", "\t/* Select the serial console after relocation. */"),
    ])

patch("common/board_r.c", [
    ("#include <asm/global_data.h>", "#include <asm/global_data.h>\n#include <minpocket-profile.h>"),
    ("static void initcall_run_r(void)", """static int minpocket_serial_initialize(void)
{
	u64 start = minpocket_ticks();
	int ret = serial_initialize();

	gd->arch.minpocket_post_serial_ticks = minpocket_ticks() - start;
	return ret;
}

static void initcall_run_r(void)"""),
    ("INITCALL(serial_initialize);", "INITCALL(minpocket_serial_initialize);"),
])

boot = root / "arch/arm/lib/bootm.c"
source = boot.read_text()
anchor = '\t\t\tfdt_setprop_inplace(images->ft_addr, chosen,\n\t\t\t\t\t    "minpocket,boot-counter", samples, sizeof(samples));'
assert anchor in source
boot.write_text(source.replace(anchor, anchor + '''
			struct {
				fdt32_t duration_us;
				char name[32];
			} profile[6];

			for (int i = 0; i < ARRAY_SIZE(profile); i++) {
				const struct minpocket_call_sample *sample = &gd->arch.minpocket_calls[i / 3][i % 3];

				profile[i].duration_us = cpu_to_fdt32(rate ? sample->ticks * 1000000 / rate : 0);
				memcpy(profile[i].name, sample->name, sizeof(profile[i].name));
			}
			fdt_setprop_inplace(images->ft_addr, chosen,
					    "minpocket,board-profile", profile, sizeof(profile));''', 1))

source = boot.read_text()
anchor = '\t\t\tfdt_setprop_inplace(images->ft_addr, chosen,\n\t\t\t\t\t    "minpocket,board-profile", profile, sizeof(profile));'
assert anchor in source
boot.write_text(source.replace(anchor, anchor + f"""
			fdt32_t scan[4] = {{
				cpu_to_fdt32({2 if pinctrl_deferred else int(deferred)}),
				cpu_to_fdt32(rate ? gd->arch.minpocket_post_scan_ticks * 1000000 / rate : 0),
				cpu_to_fdt32({int(serial_deferred)}),
				cpu_to_fdt32(rate ? gd->arch.minpocket_post_serial_ticks * 1000000 / rate : 0),
			}};
			fdt_setprop_inplace(images->ft_addr, chosen,
					    "minpocket,extended-scan", scan, sizeof(scan));""", 1))

if dmc_profile:
    patch("arch/arm/lib/bootm.c", [
        ('"minpocket,extended-scan", scan, sizeof(scan));', '''"minpocket,extended-scan", scan, sizeof(scan));
			fdt32_t dmc[MINPOCKET_DMC_STEPS];

			for (int i = 0; i < ARRAY_SIZE(dmc); i++)
				dmc[i] = cpu_to_fdt32(rate ? gd->arch.minpocket_dmc_ticks[i] * 1000000 / rate : 0);
			fdt_setprop_inplace(images->ft_addr, chosen,
					    "minpocket,dmc-profile", dmc, sizeof(dmc));'''),
    ])

if os.environ.get("MP1_REMOTE") == "1":
    patch("arch/arm/include/asm/global_data.h", [
        ("struct arch_global_data {", """struct arch_global_data {
	u64 minpocket_reloc_stamps[5];
	u64 minpocket_reloc_calls_ticks;
	struct minpocket_call_sample minpocket_reloc_calls[8];
	bool minpocket_dm_active;
	u64 minpocket_dm_ticks[5];
	struct minpocket_call_sample minpocket_dm_probes[8];
	struct {
		u64 ticks[4];
		int microvolts;
	} minpocket_io[9];"""),
    ])
    patch("include/minpocket-profile.h", [
        ("void minpocket_record_call", """enum minpocket_reloc_stage {
	MP_RELOC_INIT_DONE, MP_RELOC_MAIN, MP_RELOC_BOOTDELAY,
	MP_RELOC_BOOTDELAY_DONE, MP_RELOC_SCRIPT,
};

void minpocket_mark_reloc_stage(enum minpocket_reloc_stage stage);

enum minpocket_dm_step {
	MP_DM_ROOT, MP_DM_BIND_PLAT, MP_DM_BIND_OTHER, MP_DM_EVENT, MP_DM_AUTOPROBE,
};

void minpocket_record_dm_probe(const char *name, u64 elapsed);

#define MP_DM_PROFILE(step, call) ({ \\
	bool active = gd->arch.minpocket_dm_active; \\
	u64 started = active ? minpocket_ticks() : 0; \\
	int result = (call); \\
	if (active) \\
		gd->arch.minpocket_dm_ticks[step] = minpocket_ticks() - started; \\
	result; \\
})

void minpocket_record_call"""),
    ])
    patch("common/board_f.c", [
        ("""void minpocket_record_call(enum minpocket_group group, const char *name, u64 elapsed)
{
	struct minpocket_call_sample *samples = gd->arch.minpocket_calls[group];

	for (int slot = 0; slot < ARRAY_SIZE(gd->arch.minpocket_calls[group]); slot++) {""",
         """static void minpocket_record_top(struct minpocket_call_sample *samples,
				int count, const char *name, u64 elapsed)
{
	for (int slot = 0; slot < count; slot++) {"""),
        ("(ARRAY_SIZE(gd->arch.minpocket_calls[group]) - slot - 1) * sizeof(*samples));",
         "(count - slot - 1) * sizeof(*samples));"),
        ("static int initf_dm(void)", """void minpocket_record_call(enum minpocket_group group, const char *name, u64 elapsed)
{
	minpocket_record_top(gd->arch.minpocket_calls[group],
			     ARRAY_SIZE(gd->arch.minpocket_calls[group]), name, elapsed);
}

void minpocket_record_reloc_call(const char *name, u64 elapsed)
{
	gd->arch.minpocket_reloc_calls_ticks += elapsed;
	minpocket_record_top(gd->arch.minpocket_reloc_calls,
			     ARRAY_SIZE(gd->arch.minpocket_reloc_calls), name, elapsed);
}

void minpocket_record_dm_probe(const char *name, u64 elapsed)
{
	minpocket_record_top(gd->arch.minpocket_dm_probes,
			     ARRAY_SIZE(gd->arch.minpocket_dm_probes), name, elapsed);
}

static int initf_dm(void)"""),
    ])
    patch("include/minpocket-profile.h", [
        ("void minpocket_mark_reloc_stage", """void minpocket_record_reloc_call(const char *name, u64 elapsed);
void minpocket_mark_reloc_stage"""),
    ])
    patch("common/board_r.c", [
        ("\tret = dm_init_and_scan(false);", """	gd->arch.minpocket_dm_active = true;
	ret = dm_init_and_scan(false);"""),
        ("""\tif (ret)
		return ret;

	return dm_autoprobe();""", """	if (!ret)
		ret = MP_DM_PROFILE(MP_DM_AUTOPROBE, dm_autoprobe());
	gd->arch.minpocket_dm_active = false;
	return ret;"""),
    ])
    patch("drivers/core/root.c", [
        (f"ret = {profile};", f"ret = MP_DM_PROFILE({step}, {profile});")
        for step, profile in (
            ("MP_DM_ROOT", 'MP_PROFILE(MINPOCKET_DM, "dm_init", dm_init(CONFIG_IS_ENABLED(OF_LIVE)))'),
            ("MP_DM_BIND_PLAT", 'MP_PROFILE(MINPOCKET_DM, "dm_scan_plat", dm_scan_plat(pre_reloc_only))'),
            ("MP_DM_BIND_OTHER", 'MP_PROFILE(MINPOCKET_DM, "dm_scan_other", dm_scan_other(pre_reloc_only))'),
            ("MP_DM_EVENT", "event_notify_null(gd->flags & GD_FLG_RELOC ?\n"
                            "\t\t\t\t\tEVT_DM_POST_INIT_R :\n"
                            "\t\t\t\t\tEVT_DM_POST_INIT_F)"),
        )
    ])
    patch("drivers/core/device.c", [
        ("#include <asm/global_data.h>", "#include <asm/global_data.h>\n#include <minpocket-profile.h>"),
        ("int device_probe(struct udevice *dev)", "static int device_probe_impl(struct udevice *dev)"),
        ("void *dev_get_plat(const struct udevice *dev)", """int device_probe(struct udevice *dev)
{
	if (gd->arch.minpocket_dm_active && dev && !device_active(dev)) {
		u64 start = minpocket_ticks();
		int ret = device_probe_impl(dev);

		minpocket_record_dm_probe(dev->name, minpocket_ticks() - start);
		return ret;
	}

	return device_probe_impl(dev);
}

void *dev_get_plat(const struct udevice *dev)"""),
    ])
    patch("drivers/misc/rockchip-io-domain.c", [
        ("#include <dm.h>", """#include <dm.h>
#include <asm/global_data.h>
#include <minpocket-profile.h>

DECLARE_GLOBAL_DATA_PTR;

#define MP_IO_PROFILE(step, call) ({ \\
	u64 start = sample ? minpocket_ticks() : 0; \\
	int result = (call); \\
	if (sample) \\
		sample->ticks[step] = minpocket_ticks() - start; \\
	result; \\
})"""),
        ("\t\tret = device_get_supply_regulator(dev, supply_name, &reg);", """		typeof(gd->arch.minpocket_io[0]) *sample = NULL;

		if (gd->arch.minpocket_dm_active && soc_data == &soc_data_rk3568_pmu &&
		    i < ARRAY_SIZE(gd->arch.minpocket_io))
			sample = &gd->arch.minpocket_io[i];

		ret = MP_IO_PROFILE(0, device_get_supply_regulator(dev, supply_name, &reg));"""),
        ("\t\tret = regulator_autoset(reg);", "\t\tret = MP_IO_PROFILE(1, regulator_autoset(reg));"),
        ("\t\tuV = regulator_get_value(reg);", """		uV = MP_IO_PROFILE(2, regulator_get_value(reg));
		if (sample)
			sample->microvolts = uV;"""),
        ("\t\tret = soc_data->write(grf, soc_data->grf_offset, i, uV);",
         "\t\tret = MP_IO_PROFILE(3, soc_data->write(grf, soc_data->grf_offset, i, uV));"),
    ])
    board_r = (root / "common/board_r.c").read_text()
    start = board_r.index("static void initcall_run_r(void)")
    end = board_r.index("\nvoid board_init_r(", start)
    initcalls = board_r[start:end]
    profiled = re.sub(
        r"(?m)^\t(INITCALL(?:_EVT)?)\((\w+)\);",
        lambda m: (f'\tMP_RELOC_CALL("{m[2]}", {m[1]}({m[2]}));'
                   if m[2] != "run_main_loop"
                   else "\tminpocket_mark_reloc_stage(MP_RELOC_INIT_DONE);\n" + m[0]),
        initcalls,
    )
    patch("common/board_r.c", [
        (initcalls, r"""void minpocket_mark_reloc_stage(enum minpocket_reloc_stage stage)
{
	gd->arch.minpocket_reloc_stamps[stage] = minpocket_ticks();
}

#define MP_RELOC_CALL(name, call) do { \
	u64 start = minpocket_ticks(); \
	call; \
	minpocket_record_reloc_call(name, minpocket_ticks() - start); \
} while (0)

""" + profiled),
    ])
    patch("common/main.c", [
        ("#include <init.h>", "#include <init.h>\n#include <minpocket-profile.h>"),
        ("\tbootstage_mark_name(BOOTSTAGE_ID_MAIN_LOOP", """	minpocket_mark_reloc_stage(MP_RELOC_MAIN);
	bootstage_mark_name(BOOTSTAGE_ID_MAIN_LOOP"""),
        ("\ts = bootdelay_process();", """	minpocket_mark_reloc_stage(MP_RELOC_BOOTDELAY);
	s = bootdelay_process();
	minpocket_mark_reloc_stage(MP_RELOC_BOOTDELAY_DONE);"""),
    ])
    patch("common/autoboot.c", [
        ("#include <autoboot.h>", "#include <autoboot.h>\n#include <minpocket-profile.h>"),
        ("\t\trun_command_list(s, -1, 0);\n\n\t\tif (lock)", """		minpocket_mark_reloc_stage(MP_RELOC_SCRIPT);
		run_command_list(s, -1, 0);

		if (lock)"""),
    ])
    patch("arch/arm/lib/bootm.c", [
        ('"minpocket,board-profile", profile, sizeof(profile));',
         '''"minpocket,board-profile", profile, sizeof(profile));
			u64 previous = gd->arch.minpocket_reloc_done_ticks;
			fdt32_t phases[7];
			bool valid = rate && previous;

			for (int i = 0; i < ARRAY_SIZE(phases) - 1 && valid; i++) {
				u64 end = i < ARRAY_SIZE(gd->arch.minpocket_reloc_stamps) ?
					gd->arch.minpocket_reloc_stamps[i] :
					gd->arch.minpocket_sd_start_ticks;

				if (end < previous || end > ticks) {
					valid = false;
					break;
				}
				phases[i] = cpu_to_fdt32((end - previous) * 1000000 / rate);
				previous = end;
			}
			if (valid) {
				typeof(profile[0]) calls[ARRAY_SIZE(gd->arch.minpocket_reloc_calls)];

				phases[6] = cpu_to_fdt32(gd->arch.minpocket_reloc_calls_ticks * 1000000 / rate);
				for (int i = 0; i < ARRAY_SIZE(calls); i++) {
					const struct minpocket_call_sample *sample = &gd->arch.minpocket_reloc_calls[i];

					calls[i].duration_us = cpu_to_fdt32(sample->ticks * 1000000 / rate);
					memcpy(calls[i].name, sample->name, sizeof(calls[i].name));
				}
				fdt_setprop_inplace(images->ft_addr, chosen,
						    "minpocket,relocated-phases", phases, sizeof(phases));
				fdt_setprop_inplace(images->ft_addr, chosen,
						    "minpocket,relocated-calls", calls, sizeof(calls));
				struct {
					fdt32_t timings[6];
					typeof(profile[0]) probes[ARRAY_SIZE(gd->arch.minpocket_dm_probes)];
				} dm;

				dm.timings[0] = cpu_to_fdt32(gd->arch.minpocket_post_scan_ticks * 1000000 / rate);
				for (int i = 0; i < ARRAY_SIZE(gd->arch.minpocket_dm_ticks); i++)
					dm.timings[i + 1] = cpu_to_fdt32(gd->arch.minpocket_dm_ticks[i] * 1000000 / rate);
				for (int i = 0; i < ARRAY_SIZE(dm.probes); i++) {
					const struct minpocket_call_sample *sample = &gd->arch.minpocket_dm_probes[i];

					dm.probes[i].duration_us = cpu_to_fdt32(sample->ticks * 1000000 / rate);
					memcpy(dm.probes[i].name, sample->name, sizeof(dm.probes[i].name));
				}
				fdt_setprop_inplace(images->ft_addr, chosen, "minpocket,relocated-dm",
						    &dm, sizeof(dm));
				fdt32_t io[ARRAY_SIZE(gd->arch.minpocket_io)][5];

				for (int i = 0; i < ARRAY_SIZE(io); i++) {
					const typeof(gd->arch.minpocket_io[0]) *sample =
						&gd->arch.minpocket_io[i];

					for (int j = 0; j < ARRAY_SIZE(sample->ticks); j++)
						io[i][j] = cpu_to_fdt32(sample->ticks[j] * 1000000 / rate);
					io[i][4] = cpu_to_fdt32(sample->microvolts);
				}
				fdt_setprop_inplace(images->ft_addr, chosen, "minpocket,io-domains",
						    io, sizeof(io));
			}'''),
    ])
