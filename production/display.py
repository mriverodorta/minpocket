"""Overlap first-boot panel reset and measure the MP1 display activation path."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])

def patch(name, old, new):
    path = root / name
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Expected one display anchor in {name}: {old}")
    path.write_text(text.replace(old, new, 1))

panel = "drivers/gpu/drm/panel/panel-miniloong-pocket1.c"
patch(panel, "#include <linux/delay.h>",
      "#include <linux/delay.h>\n#include <linux/workqueue.h>\n#include <linux/minpocket-display.h>")
patch(panel, " unsigned int mode_count;", " unsigned int mode_count;\n struct work_struct prepower_work;\n bool prepowered;")
power = """ gpiod_set_value_cansleep(ctx->enable_gpio, 1);
 fsleep(5000);
 gpiod_set_value_cansleep(ctx->reset_gpio, 1);
 fsleep(1000);
 gpiod_set_value_cansleep(ctx->reset_gpio, 0);
 fsleep(10000);"""
patch(panel, "static int miniloong_prepare(struct drm_panel *panel)", """static void miniloong_power_reset(struct miniloong_panel *ctx)
{
 u64 start = ktime_get_boottime_ns();
""" + power + """
 ctx->prepowered = true;
 minpocket_display_record(MP_PANEL_POWER, start, 0);
}

static void miniloong_prepower(struct work_struct *work)
{
 struct miniloong_panel *ctx = container_of(work, struct miniloong_panel, prepower_work);
 miniloong_power_reset(ctx);
}

static int miniloong_prepare(struct drm_panel *panel)""")
patch(panel, " int ret;\n" + power + '\n ret = miniloong_write_seq(ctx, "panel-init-sequence");', """ int ret;
 u64 start = ktime_get_boottime_ns(), commands;
 /* The first reset overlaps unrelated kernel initialization. Commands still
  * wait for the bridge to enable the DSI transmitter. */
 flush_work(&ctx->prepower_work);
 if (!ctx->prepowered)
  miniloong_power_reset(ctx);
 commands = ktime_get_boottime_ns();
 ret = miniloong_write_seq(ctx, "panel-init-sequence");
 minpocket_display_record(MP_PANEL_COMMANDS, commands, ret);
 minpocket_display_record(MP_PANEL_PREPARE, start, ret);""")
patch(panel, " if (delay)\n   msleep(delay);", " if (delay)\n   fsleep((unsigned long)delay * 1000);")
patch(panel, " if (ret) {\n  gpiod_set_value_cansleep(ctx->reset_gpio, 1);",
      " if (ret) {\n  ctx->prepowered = false;\n  gpiod_set_value_cansleep(ctx->reset_gpio, 1);")
patch(panel, ' int ret = miniloong_write_seq(ctx, "panel-exit-sequence");',
      ' int ret;\n cancel_work_sync(&ctx->prepower_work);\n ret = miniloong_write_seq(ctx, "panel-exit-sequence");\n ctx->prepowered = false;')
patch(panel, " ctx->dsi = dsi;", " ctx->dsi = dsi;\n INIT_WORK(&ctx->prepower_work, miniloong_prepower);")
patch(panel, " if (ret)\n  drm_panel_remove(&ctx->panel);\n return ret;",
      " if (ret)\n  drm_panel_remove(&ctx->panel);\n else\n  queue_work(system_dfl_wq, &ctx->prepower_work);\n return ret;")
patch(panel, " mipi_dsi_detach(dsi);", " cancel_work_sync(&ctx->prepower_work);\n mipi_dsi_detach(dsi);")

bridge = "drivers/gpu/drm/bridge/synopsys/dw-mipi-dsi.c"
patch(bridge, "#include <linux/clk.h>", "#include <linux/clk.h>\n#include <linux/minpocket-display.h>")
patch(bridge, "static void dw_mipi_dsi_dphy_enable(struct dw_mipi_dsi *dsi)\n{\n\tu32 val;\n\tint ret;",
      "static void dw_mipi_dsi_dphy_enable(struct dw_mipi_dsi *dsi)\n{\n\tu32 val;\n\tint ret;\n\tu64 start;")
patch(bridge, "\tret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS, val,\n\t\t\t\t val & PHY_LOCK, 1000, PHY_STATUS_TIMEOUT_US);",
      "\tstart = ktime_get_boottime_ns();\n\tret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS, val,\n\t\t\t\t val & PHY_LOCK, 50, PHY_STATUS_TIMEOUT_US);\n\tminpocket_display_record(MP_DSI_LOCK, start, ret);")
patch(bridge, "\tret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS,\n\t\t\t\t val, val & PHY_STOP_STATE_CLK_LANE, 1000,\n\t\t\t\t PHY_STATUS_TIMEOUT_US);",
      "\tstart = ktime_get_boottime_ns();\n\tret = readl_poll_timeout(dsi->base + DSI_PHY_STATUS,\n\t\t\t\t val, val & PHY_STOP_STATE_CLK_LANE, 50,\n\t\t\t\t PHY_STATUS_TIMEOUT_US);\n\tminpocket_display_record(MP_DSI_STOP, start, ret);")
patch(bridge, "\tu32 lanes = dw_mipi_dsi_get_lanes(dsi);",
      "\tu32 lanes = dw_mipi_dsi_get_lanes(dsi);\n\tu64 start = ktime_get_boottime_ns();")
patch(bridge, "\tret = phy_ops->init(priv_data);",
      "\tminpocket_display_record(MP_DSI_CONFIG, start, 0);\n\tstart = ktime_get_boottime_ns();\n\tret = phy_ops->init(priv_data);\n\tminpocket_display_record(MP_DSI_PHY_INIT, start, ret);")
patch(bridge, "\tdw_mipi_dsi_dphy_enable(dsi);", """\t/* External PHY readiness is meaningful only after its power-on callback. */
\tif (phy_ops->power_on) {
\t\tstart = ktime_get_boottime_ns();
\t\tphy_ops->power_on(dsi->plat_data->priv_data);
\t\tminpocket_display_record(MP_DSI_PHY_POWER, start, 0);
\t}
\tdw_mipi_dsi_dphy_enable(dsi);""")
patch(bridge, "\n\tif (phy_ops->power_on)\n\t\tphy_ops->power_on(dsi->plat_data->priv_data);", "")
text = (root / bridge).read_text()
old = text[:text.index("static ssize_t dw_mipi_dsi_host_transfer(")]
new, count = re.subn(r"1000(?=,\s*CMD_PKT_STATUS_TIMEOUT_US)", "50", old)
if count != 5:
    raise SystemExit(f"Expected five DSI command FIFO poll intervals, got {count}")
patch(bridge, old, new)

atomic = "drivers/gpu/drm/drm_atomic_helper.c"
patch(atomic, "#include <linux/dma-fence.h>", "#include <linux/dma-fence.h>\n#include <linux/minpocket-display.h>")
start = (root / atomic).read_text().index("void drm_atomic_helper_commit_tail_rpm(")
text = (root / atomic).read_text()
end = text.index("\nEXPORT_SYMBOL(drm_atomic_helper_commit_tail_rpm);", start)
old = text[start:end]
new = old.replace("struct drm_device *dev = state->dev;", "struct drm_device *dev = state->dev;\n\tu64 start;")
for name, call in (("ENABLE", "drm_atomic_helper_commit_modeset_enables(dev, state);"),
                   ("PLANES", "drm_atomic_helper_commit_planes(dev, state,\n\t\t\t\t\tDRM_PLANE_COMMIT_ACTIVE_ONLY);"),
                   ("VBLANK", "drm_atomic_helper_wait_for_vblanks(dev, state);"),
                   ("CLEANUP", "drm_atomic_helper_cleanup_planes(dev, state);")):
    if new.count(call) != 1:
        raise SystemExit(f"Expected one DRM {name} call")
    new = new.replace(call, "start = ktime_get_boottime_ns();\n\t" + call +
                      f"\n\tminpocket_display_record(MP_DRM_{name}, start, 0);", 1)
new = new.replace("drm_atomic_helper_wait_for_vblanks(dev, state);",
                  "drm_atomic_helper_wait_for_flip_done(dev, state);")
patch(atomic, old, new)
