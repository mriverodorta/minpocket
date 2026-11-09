// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_CLOCK_INDICATOR_H
#define MINPOCKET_CLOCK_INDICATOR_H
#include "../status-indicator.h"
enum { MP_CLOCK_TEXT_WIDTH = 128, MP_CLOCK_TEXT_HEIGHT = 30 };
struct mp_clock_indicator {
    int text_selected,cached_selected;
    int ready, hour, minute, cached_hour, cached_minute;
    uint64_t visibility_ms, next_minute_ms;
    unsigned text_height, text_width, text_height_override;
    char label[6];
    uint8_t pixels[MP_CLOCK_TEXT_WIDTH * MP_CLOCK_TEXT_HEIGHT];
    struct { unsigned label_builds; } stats;
};
void mp_clock_indicator_init(struct mp_clock_indicator *indicator);
/* Host supplies local wall time after the boot frames. Invalid values hide
 * the slot. Keep the last valid label during exit. No service I/O in callbacks.
 * Format is currently zero-padded 24-hour HH:MM, with no blinking or seconds. */
void mp_clock_indicator_update(struct mp_clock_indicator *indicator, int hour, int minute,
    unsigned next_minute_ms, uint64_t now_ms);
void mp_clock_indicator_set_text_height(struct mp_clock_indicator *,unsigned height);
void mp_clock_indicator_set_text_selected(struct mp_clock_indicator *,int selected);
extern const struct mp_status_indicator_ops mp_clock_indicator_ops;
#endif
