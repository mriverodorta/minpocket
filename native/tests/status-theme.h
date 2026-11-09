// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_TEST_STATUS_THEME_H
#define MINPOCKET_TEST_STATUS_THEME_H
#include "theme.h"
/* Explicit archived 20px fixture, independent of the chosen native default.
 * Never replace the frozen pixel reference when the owner changes size. */
static struct mp_theme mp_test_legacy_status_theme(void)
{
    struct mp_theme theme = mp_theme_default;
    theme.status.pixel_scale = 1;
    theme.status.box.frame.art = MP_FRAME_MOONSTONE;
    theme.status.box.background_alpha = 255;
    theme.status.layout.margin = (struct mp_ui_edges){0};
    theme.status.icon_height = 20; theme.status.text_height = 18;
    theme.status.text_gap = 12; theme.status.layout.gap = 12;
    theme.status.box.border = (struct mp_ui_edges){0, 0, 8, 8};
    theme.status.layout.padding = (struct mp_ui_edges){5, 5, 5, 5};
    return theme;
}
#endif
