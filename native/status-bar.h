// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_STATUS_BAR_H
#define MINPOCKET_STATUS_BAR_H
#include "status-indicator.h"

enum { MP_STATUS_MAX_WIDTH = 640, MP_STATUS_MAX_HEIGHT = 512, MP_STATUS_INLINE_PIXELS = 640 * 32, MP_STATUS_MAX_INDICATORS = 8,
       MP_STATUS_ID_LENGTH = 31, MP_STATUS_DAMAGE_HISTORY = 8 };
enum mp_status_result { MP_STATUS_OK = 0, MP_STATUS_INVALID = -1, MP_STATUS_DUPLICATE = -2,
    MP_STATUS_FULL = -3, MP_STATUS_OVERFLOW = -4, MP_STATUS_NOT_FOUND = -5, MP_STATUS_NOMEM = -6 };
struct mp_status_bar_cache { uint64_t generation; struct mp_ui_rect bounds; int valid; struct mp_box_cache box; };
struct mp_status_bar_item {
    struct mp_status_indicator indicator;
    struct mp_indicator_metrics metrics;
    struct mp_indicator_frame frame;
    struct mp_ui_motion visibility;
    struct mp_ui_rect box;
    uint64_t registered_ms, remove_ms, enabled_ms;
    unsigned reserved_width, reserved_height, extent, paint_extent, gap, margin_top, margin_bottom;
    int valid, removing, enabled;
};
struct mp_status_bar {
    struct mp_theme theme;
    unsigned viewport_width, viewport_height, count;
    struct mp_status_bar_item items[MP_STATUS_MAX_INDICATORS];
    struct mp_ui_clock clock;
    struct mp_ui_rect bounds;
    uint64_t generation;
    int dirty;
    uint32_t palette[MP_THEME_COLORS];
    uint8_t pixels[MP_STATUS_INLINE_PIXELS];
    uint8_t *large_pixels;
    size_t large_capacity;
    uint8_t *scaled_pixels;
    size_t scaled_capacity;
    uint64_t scaled_generation;
    struct mp_box_workspace effects;
    struct { uint64_t generation; struct mp_ui_rect box; } damage[MP_STATUS_DAMAGE_HISTORY];
    struct { unsigned raster_builds, paints; } stats;
};

/* Default raster storage is inline. Large layouts and effect caches allocate
 * lazily. Initialize after the boot frames; dispose before reinitializing.
 * Register caller-owned providers between ticks, never from a callback. */
int mp_status_bar_init(struct mp_status_bar *bar, const struct mp_theme *theme,
                       unsigned viewport_width, unsigned viewport_height);
int mp_status_bar_set_theme(struct mp_status_bar *bar, const struct mp_theme *theme);
/* Layout/effect storage is lazy and only needed after the boot frames. */
void mp_status_bar_dispose(struct mp_status_bar *bar);
void mp_status_bar_cache_dispose(struct mp_status_bar_cache *cache);
/* Margins participate in parent flow; provider paint coordinates remain local. */
int mp_status_bar_set_item_margins(struct mp_status_bar *,const char *id,unsigned top,unsigned bottom);
int mp_status_bar_set_enabled(struct mp_status_bar *bar, const char *id, int enabled, uint64_t now_ms);
int mp_status_bar_register(struct mp_status_bar *bar, const struct mp_status_indicator *indicator,
                           unsigned position, uint64_t now_ms);
/* Unregister starts an exit. Keep the provider's context, ID and ops alive
 * until contains(id) returns false. Hidden items retire immediately. */
int mp_status_bar_unregister(struct mp_status_bar *bar, const char *id, uint64_t now_ms);
int mp_status_bar_contains(const struct mp_status_bar *bar, const char *id);
/* Change order without restarting a provider's availability transition. */
int mp_status_bar_move(struct mp_status_bar *bar, const char *id, unsigned position);
int mp_status_bar_item_layout(const struct mp_status_bar *bar, const char *id, struct mp_ui_rect *box);
int mp_status_bar_tick(struct mp_status_bar *bar, uint64_t now_ms, int animate);
/* Raster/layout coordinates stay logical; this returns physical screen pixels. */
struct mp_ui_rect mp_status_bar_screen_bounds(const struct mp_status_bar *bar);
unsigned mp_status_bar_wait_ms(const struct mp_status_bar *bar, uint64_t now_ms);
/* Each preserved framebuffer owns a cache. Invalidate it after a scene redraw. */
int mp_status_bar_paint(struct mp_status_bar *bar, const struct mp_font_surface *surface,
                        struct mp_status_bar_cache *cache);
#endif
