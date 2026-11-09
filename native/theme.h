// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_THEME_H
#define MINPOCKET_THEME_H
#include <stdint.h>
#include "style.h"

enum { MP_THEME_ICON_COLORS = 13, MP_THEME_STATUS_TEXT = 13, MP_THEME_DEBUG_TEXT = 14, MP_THEME_COLORS = 15 };
/* Temporary development font-selection ink, outside icon artwork colors. */
#define MP_FONT_DEBUG_RED UINT32_C(0xf7768e)
enum { MP_THEME_STATUS_ORDER_LIMIT = 8 };

struct mp_theme {
    struct { uint32_t surface, text, muted, accent, panel, boundary, status_text; } colors;
    struct { unsigned title, page, menu, body, hint; } font_scale;
    struct {
        struct mp_box_style box;
        struct mp_layout_style layout;
        unsigned icon_height, text_height, text_gap, transition_ms;
        unsigned pixel_scale; /* 0/1 = native; 2 = exact 2x for the whole group. */
        const char *indicator_order[MP_THEME_STATUS_ORDER_LIMIT];
        unsigned indicator_order_count;
        enum mp_ui_easing transition_easing;
        int visible, animations, collapse_hidden;
    } status;
    uint32_t icon_palette[MP_THEME_ICON_COLORS];
};
/* Compiled from the theme JSON. No runtime JSON parsing or filesystem reads. */
extern const struct mp_theme mp_theme_default;
#endif
