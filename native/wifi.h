// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_WIFI_H
#define MINPOCKET_WIFI_H
#include <stdint.h>

enum {
    MP_WIFI_ICON_WIDTH = 31, MP_WIFI_ICON_HEIGHT = 20,
    MP_WIFI_ICON_PIXELS = MP_WIFI_ICON_WIDTH * MP_WIFI_ICON_HEIGHT,
    MP_WIFI_NATIVE_WIDTH = 47, MP_WIFI_NATIVE_HEIGHT = 30,
    MP_WIFI_NATIVE_PIXELS = MP_WIFI_NATIVE_WIDTH * MP_WIFI_NATIVE_HEIGHT,
    MP_WIFI_JOIN_FRAMES = 20,
    MP_WIFI_CACHE_FRAMES = 1 + MP_WIFI_JOIN_FRAMES,
    MP_WIFI_NO_SIGNAL = -999
};
enum mp_wifi_state {
    MP_WIFI_UNKNOWN, MP_WIFI_OFFLINE, MP_WIFI_DISCONNECTED,
    MP_WIFI_CONNECTING, MP_WIFI_CONNECTED
};
struct mp_wifi_reading {
    enum mp_wifi_state state;
    int signal_dbm;
};
struct mp_wifi {
    struct mp_wifi_reading reading;
    unsigned level;
    uint32_t revision;
    uint64_t join_ms;
    uint8_t frames[MP_WIFI_CACHE_FRAMES][MP_WIFI_ICON_PIXELS];
};
struct mp_wifi_visual {
    unsigned frame;
    uint64_t stamp;
};
void mp_wifi_init(struct mp_wifi *wifi);
void mp_wifi_update(struct mp_wifi *wifi, struct mp_wifi_reading reading, uint64_t now_ms);
struct mp_wifi_visual mp_wifi_visual(const struct mp_wifi *wifi, uint64_t now_ms, int animate);
unsigned mp_wifi_wait_ms(const struct mp_wifi *wifi, uint64_t now_ms, int animate);
enum mp_wifi_state mp_wifi_parse_status(const char *reply);
int mp_wifi_parse_signal(const char *reply);
const char *mp_wifi_state_name(enum mp_wifi_state state);
/* Linux service adapter. It sends only STATUS and SIGNAL_POLL, never commands
 * that change association. Start the monitor after the second display refresh. */
struct mp_wifi_reading mp_wifi_read(void);
int mp_wifi_monitor_start(int display_fd, int battery_fd);
#endif
