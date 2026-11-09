// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_STYLE_H
#define MINPOCKET_STYLE_H
#include "ui.h"
#include <stddef.h>

struct mp_ui_edges { unsigned top, right, bottom, left; };
struct mp_ui_corners { unsigned top_left, top_right, bottom_right, bottom_left; };
enum mp_blend_mode { MP_BLEND_NORMAL, MP_BLEND_MULTIPLY, MP_BLEND_SCREEN, MP_BLEND_OVERLAY,
    MP_BLEND_DARKEN, MP_BLEND_LIGHTEN, MP_BLEND_DIFFERENCE, MP_BLEND_EXCLUSION, MP_BLEND_ADD };
enum mp_border_style { MP_BORDER_SOLID, MP_BORDER_DASHED, MP_BORDER_DOTTED };
enum mp_frame_art { MP_FRAME_NONE, MP_FRAME_MOONSTONE };
struct mp_pixel_frame {
    enum mp_frame_art art;
    uint32_t highlight, light, mid, face, shadow, recess;
};
/* RGB colors and independent 0..255 alpha. Opacity applies to the whole group,
 * including its shadow and backdrop effect. Blur is a separable box blur. */
struct mp_box_style {
    /* Internal integer render scale. Authored dimensions use 0/1. */
    unsigned pixel_scale;
    uint32_t background, border_color;
    unsigned background_alpha, border_alpha, opacity;
    struct mp_ui_edges border;
    struct mp_ui_corners radius;
    enum mp_border_style border_style;
    /* Native corner cap plus repeating edge tiles; never stretch artwork. */
    struct mp_pixel_frame frame;
    struct { uint32_t color; unsigned alpha, blur; int x, y, spread; } shadow;
    enum mp_blend_mode blend_mode;
    unsigned blur, backdrop_blur;
    int clip_children;
};
enum mp_orientation { MP_HORIZONTAL, MP_VERTICAL };
enum mp_direction { MP_FORWARD, MP_REVERSE };
enum mp_alignment { MP_ALIGN_START, MP_ALIGN_CENTER, MP_ALIGN_END, MP_ALIGN_STRETCH };
enum mp_justify { MP_JUSTIFY_START, MP_JUSTIFY_CENTER, MP_JUSTIFY_END,
    MP_SPACE_BETWEEN, MP_SPACE_AROUND, MP_SPACE_EVENLY };
enum mp_overflow { MP_OVERFLOW_REJECT, MP_OVERFLOW_CLIP };
struct mp_layout_style {
    enum mp_orientation orientation;
    enum mp_direction direction;
    enum mp_justify justify;
    enum mp_alignment align, anchor_x, anchor_y;
    struct mp_ui_edges padding, margin;
    unsigned gap;
    /* Zero width/height uses intrinsic size. Bounds include padding. */
    unsigned width, height, min_width, min_height, max_width, max_height;
    int offset_x, offset_y;
    enum mp_overflow overflow;
};
int mp_box_style_valid(const struct mp_box_style *style);
int mp_layout_style_valid(const struct mp_layout_style *style);
/* Border widths sit outside the requested padding. Output dimensions remain
 * outer-box dimensions. Combined border/padding insets are bounded to 128px. */
int mp_box_layout(const struct mp_box_style *box, const struct mp_layout_style *layout,
                   struct mp_layout_style *content_layout);
int mp_layout_bounds(const struct mp_layout_style *style, unsigned intrinsic_width, unsigned intrinsic_height,
                     unsigned viewport_width, unsigned viewport_height, struct mp_ui_rect *box);
/* Item gaps are supplied separately so callers can animate them. Stretch
 * changes the allocation, never the native artwork's scale. */
void mp_layout_arrange(const struct mp_layout_style *style, struct mp_ui_rect box,
                       const struct mp_ui_row_item *items, unsigned count, struct mp_ui_rect *boxes);
struct mp_ui_rect mp_box_paint_bounds(const struct mp_box_style *style, struct mp_ui_rect box);
struct mp_box_cache { uint32_t *backdrop; size_t capacity; struct mp_ui_rect bounds; int valid; };
struct mp_box_workspace { uint32_t *storage; size_t capacity; };
/* One backdrop cache per preserved framebuffer. Pass scene_valid=0 after
 * redrawing the scene. Dispose caches before releasing their surfaces. */
int mp_box_cache_prepare(struct mp_box_cache *cache, const struct mp_font_surface *surface,
                         struct mp_ui_rect bounds, int scene_valid);
void mp_box_cache_restore(const struct mp_box_cache *cache, const struct mp_font_surface *surface);
void mp_box_cache_dispose(struct mp_box_cache *cache);
void mp_box_workspace_dispose(struct mp_box_workspace *workspace);
int mp_box_is_opaque_plain(const struct mp_box_style *style);
/* Fast, exact subset for opaque indexed components. Region coordinates are
 * relative to the box. Request the full region after style/geometry changes. */
int mp_box_can_index_paint(const struct mp_box_style *style);
int mp_box_index_paint(const struct mp_box_style *style, const struct mp_font_surface *surface,
                       struct mp_ui_rect box, const struct mp_ui_index_surface *content,
                       const uint32_t *palette, unsigned palette_size, struct mp_ui_rect region,
                       struct mp_box_cache *cache, int scene_valid);
int mp_box_paint(const struct mp_box_style *style, const struct mp_font_surface *surface,
                 struct mp_ui_rect box, const struct mp_ui_index_surface *content,
                 const uint32_t *palette, unsigned palette_size, struct mp_box_cache *cache,
                 struct mp_box_workspace *workspace, int scene_valid);
#endif
