// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_STATUS_INDICATOR_H
#define MINPOCKET_STATUS_INDICATOR_H
#include "theme.h"
#include "ui.h"

struct mp_indicator_metrics { unsigned width, height, max_width; };
struct mp_indicator_frame {
    int visible;
    uint64_t stamp, visibility_ms;
};
struct mp_status_indicator_ops {
    /* Pure measurement: no mutation. Used to reject a registration or theme
     * that cannot fit before changing the container or provider state. */
    int (*measure)(const void *context, const struct mp_theme *theme, struct mp_indicator_metrics *metrics);
    /* Update caches with the shared timestamp; request only needed deadlines.
     * Keep visibility_ms stable unless availability changes. */
    void (*tick)(void *context, const struct mp_theme *theme, struct mp_ui_clock *clock,
                 struct mp_indicator_frame *frame);
    /* Local origin (0,0), native-size pixels, clipped to this child's visible
     * window. Zero is transparent; 1..12 are theme art, 13 is status text; 14 is development selection text. */
    void (*paint)(const void *context, const struct mp_theme *theme, struct mp_ui_index_surface *window);
};
struct mp_status_indicator {
    const char *id;
    const struct mp_status_indicator_ops *ops;
    void *context;
};
/* Descriptors are copied. IDs and ops stay immutable. Caller-owned contexts,
 * IDs and ops must remain alive
 * until contains(id) is false. Providers are trusted native C, not sandboxed
 * plugins. No registry mutation or service I/O from inside these callbacks. */
#endif
