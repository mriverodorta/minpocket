/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MINPOCKET_DISPLAY_PROFILE_H
#define MINPOCKET_DISPLAY_PROFILE_H
#include <linux/ktime.h>

enum minpocket_display_phase {
	MP_PANEL_POWER, MP_PANEL_COMMANDS, MP_PANEL_PREPARE,
	MP_DSI_CONFIG, MP_DSI_PHY_INIT, MP_DSI_PHY_POWER, MP_DSI_LOCK, MP_DSI_STOP,
	MP_DRM_ENABLE, MP_DRM_PLANES, MP_DRM_VBLANK, MP_DRM_CLEANUP, MP_DISPLAY_PHASES,
};
void minpocket_display_record(enum minpocket_display_phase phase, u64 start, int result);
#endif
