// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_BATTERY_INDICATOR_H
#define MINPOCKET_BATTERY_INDICATOR_H
#include "../battery.h"
#include "../status-indicator.h"

enum { MP_BATTERY_TEXT_WIDTH = 104 };
struct mp_battery_indicator {
    int text_selected,cached_selected;
    struct mp_battery battery;
    struct mp_battery_reading sample;
    int ready, small_level, label_level;
    uint64_t visibility_ms;
    unsigned frame, percentage_width, label_height, art_height, text_height_override;
    uint8_t small_frames[MP_BATTERY_MAX_FRAMES][MP_BATTERY_NATIVE_PIXELS];
    uint8_t percentage[MP_BATTERY_TEXT_WIDTH * 30];
    struct { unsigned battery_builds, label_builds; } stats;
};
void mp_battery_indicator_init(struct mp_battery_indicator *indicator);
void mp_battery_indicator_update(struct mp_battery_indicator *indicator,
                                 struct mp_battery_reading reading, uint64_t now_ms);
void mp_battery_indicator_set_text_height(struct mp_battery_indicator *,unsigned height);
void mp_battery_indicator_set_text_selected(struct mp_battery_indicator *,int selected);
extern const struct mp_status_indicator_ops mp_battery_indicator_ops;
#endif
