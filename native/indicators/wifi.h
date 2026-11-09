// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_WIFI_INDICATOR_H
#define MINPOCKET_WIFI_INDICATOR_H
#include "../wifi.h"
#include "../status-indicator.h"
struct mp_wifi_indicator {
    struct mp_wifi wifi;
    uint64_t visibility_ms;
    unsigned frame;
    uint32_t native_revision;
    uint8_t native_frames[MP_WIFI_CACHE_FRAMES][MP_WIFI_NATIVE_PIXELS];
};
void mp_wifi_indicator_init(struct mp_wifi_indicator *indicator);
void mp_wifi_indicator_update(struct mp_wifi_indicator *indicator,
                              struct mp_wifi_reading reading, uint64_t now_ms);
extern const struct mp_status_indicator_ops mp_wifi_indicator_ops;
#endif
