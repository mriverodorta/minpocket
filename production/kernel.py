"""Apply MP1 driver changes and lightweight startup measurements."""

from pathlib import Path
import os
import re
import sys

root = Path(sys.argv[1])


def patch(name, old, new):
    path = root / name
    source = path.read_text()
    if source.count(old) != 1:
        raise SystemExit(f"Expected one MP1 production anchor in {name}: {old}")
    path.write_text(source.replace(old, new, 1))


pinctrl = "drivers/pinctrl/pinctrl-rockchip.c"
source = (root / pinctrl).read_text()
table = re.search(r"static const struct of_device_id rockchip_pinctrl_dt_match\[\] = \{\n.*?\n\};",
                  source, re.S)
if not table or table[0].count('"rockchip,rk3568-pinctrl"') != 1:
    raise SystemExit("Expected the RK3568 pin controller table")
patch(pinctrl, table[0], """static const struct of_device_id rockchip_pinctrl_dt_match[] = {
	{ .compatible = "rockchip,rk3568-pinctrl", .data = &rk3568_pin_ctrl },
	{},
};""")

patch("drivers/gpu/drm/panel/panel-miniloong-pocket1.c", """ gpiod_set_value_cansleep(ctx->enable_gpio, 1);
 msleep(120);
 gpiod_set_value_cansleep(ctx->reset_gpio, 1);
 msleep(120);
 gpiod_set_value_cansleep(ctx->reset_gpio, 0);
 msleep(120);""", """ gpiod_set_value_cansleep(ctx->enable_gpio, 1);
 fsleep(5000);
 gpiod_set_value_cansleep(ctx->reset_gpio, 1);
 fsleep(1000);
 gpiod_set_value_cansleep(ctx->reset_gpio, 0);
 fsleep(10000);""")

bridge = "drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c"
patch(bridge, "\tdw_mipi_dsi_wait_for_two_frames(adjusted_mode);",
      "\tif (drm_mode_vrefresh(adjusted_mode) > 60)\n\t\tdw_mipi_dsi_wait_for_two_frames(adjusted_mode);")
patch("drivers/gpu/drm/rockchip/rockchip_drm_drv.c", "\tdrm_client_setup(drm_dev, NULL);",
      "\t/* MinPocket sets the panel mode from its DRM launcher. */")
import subprocess
subprocess.run([sys.executable, str(Path(__file__).parent / "display.py"), str(root)], check=True)
(root / "include/linux/minpocket-display.h").write_text(
    (Path(__file__).parent / "display-profile.h").read_text())
(root / "init/minpocket-display.c").write_text(
    (Path(__file__).parent / "display-profile.c").read_text())

sd_host = "drivers/mmc/host/dw_mmc-rockchip.c"
patch(sd_host, "static int dw_mci_rockchip_probe(", """static bool minpocket_sd_ready;
module_param_named(minpocket_sd_ready, minpocket_sd_ready, bool, 0200);

static int dw_mci_rockchip_probe(""")
patch(sd_host, "\tif (!pdev->dev.of_node)",
      '\tif (of_property_read_bool(pdev->dev.of_node, "minipocket,defer-sd") &&\n'
      '\t    !READ_ONCE(minpocket_sd_ready))\n\t\treturn -ENODEV;\n\n'
      '\tif (!pdev->dev.of_node)')
(root / "init/minpocket-clock.c").write_text(
    (Path(__file__).parent / "boot-clock.c").read_text())
(root / "init/minpocket-boost.c").write_text(
    (Path(__file__).parent / "boot-boost.c").read_text())
makefile = root / "init/Makefile"
makefile.write_text(makefile.read_text() + "\nobj-y += minpocket-clock.o minpocket-display.o minpocket-boost.o\n")

if os.environ.get("MP1_PRODUCTION_DEV") == "1":
    for filename, signature, probe_anchor in (
        ("drivers/mmc/host/dw_mmc-rockchip.c", "static int dw_mci_rockchip_probe(",
         "\tif (!pdev->dev.of_node)"),
        ("drivers/mmc/core/pwrseq_simple.c", "static int mmc_pwrseq_simple_probe(",
         "\tpwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);"),
    ):
        patch(filename, signature, """static bool minpocket_wifi_ready;
module_param_named(minpocket_wifi_ready, minpocket_wifi_ready, bool, 0200);

""" + signature)
        patch(filename, probe_anchor,
              '\tif (of_property_read_bool(pdev->dev.of_node, "minipocket,defer-wifi") &&\n'
              '\t    !READ_ONCE(minpocket_wifi_ready))\n\t\treturn -ENODEV;\n\n'
              + probe_anchor)


    # Keep codec defaults out of PMIC discovery. The deferred codec probe owns them.
    core = "drivers/mfd/rk8xx-core.c"
    table = (root / core).read_text().split("rk817_pre_init_reg[] = {", 1)[1].split("};", 1)[0]
    codec_begin = "\t/* Codec specific registers */\n"
    codec_end = "\t{RK817_GPIO_INT_CFG"
    codec_init = table[table.index(codec_begin):table.index(codec_end)]
    register = r"\{ (RK817_CODEC_\w+), MASK_ALL, (0x[0-9a-f]+) \},"
    codec_registers = re.sub(register, r"{ \1, \2 },", codec_init)
    for row in codec_init.splitlines()[1:]:
        if not row.lstrip().startswith("/*") and not re.fullmatch(register, row.strip()):
            raise SystemExit(f"Unexpected RK817 codec register entry: {row}")
    patch(core, codec_init, "")
    codec = "sound/soc/codecs/rk817_codec.c"
    probe = "static int rk817_platform_probe(struct platform_device *pdev)"
    patch(codec, probe, """/* Program the codec when its driver loads. */
static const struct reg_sequence rk817_codec_defaults[] = {
""" + codec_registers.removeprefix(codec_begin) + """};

""" + probe)
    register_component = "\tret = devm_snd_soc_register_component(&pdev->dev, &soc_codec_dev_rk817,"
    patch(codec, register_component, """
\tfor (size_t i = 0; i < ARRAY_SIZE(rk817_codec_defaults); i++) {
\t\tconst struct reg_sequence *entry = &rk817_codec_defaults[i];

\t\tret = regmap_update_bits(rk808->regmap, entry->reg, MASK_ALL, entry->def);
\t\tif (ret) {
\t\t\tdev_err_probe(&pdev->dev, ret, "Failed to initialize codec register %#x\\n",
\t\t\t\t      entry->reg);
\t\t\tgoto err_clk;
\t\t}
\t}

""" + register_component)

# Register the same built-in drivers after the launcher confirms its second frame.
    late_drivers = (
        ("drivers/rtc/rtc-rk808.c", "rk808_rtc_driver", "rtc"),
        ("drivers/power/supply/rk817_charger.c", "rk817_charger_driver", "charger"),
        ("drivers/gpu/drm/panfrost/panfrost_drv.c", "panfrost_driver", "gpu"),
        ("sound/soc/codecs/rk817_codec.c", "rk817_codec_driver", "codec"),
        ("sound/soc/rockchip/rockchip_i2s_tdm.c", "rockchip_i2s_tdm_driver", "i2s"),
        ("sound/soc/codecs/simple-amplifier.c", "simple_amp_driver", "amplifier"),
        ("sound/soc/generic/simple-card.c", "simple_card", "audio_card"),
    )
    prototypes = []
    for filename, driver, name in late_drivers:
        function = f"minpocket_register_{name}"
        prototypes.append(f"int {function}(void);\n")
        patch(filename, f"module_platform_driver({driver});",
              f"#include <linux/minpocket-late.h>\n\n"
              f"int {function}(void)\n{{\n\treturn platform_driver_register(&{driver});\n}}")
    (root / "include/linux/minpocket-late.h").write_text("".join(prototypes))
    (root / "init/minpocket-late.c").write_text(
        (Path(__file__).parent / "dev/late-drivers.c").read_text())
    makefile = root / "init/Makefile"
    makefile.write_text(makefile.read_text() + "\nobj-y += minpocket-late.o\n")
