// SPDX-License-Identifier: GPL-2.0-only
#include "wifi.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "wifi-art.h"

static const char *field(const char *reply, const char *key)
{
    size_t length = strlen(key);
    for (const char *line = reply; line && *line; ) {
        if (!strncmp(line, key, length) && line[length] == '=') return line + length + 1;
        line = strchr(line, '\n');
        if (line) ++line;
    }
    return NULL;
}

enum mp_wifi_state mp_wifi_parse_status(const char *reply)
{
    const char *state = field(reply, "wpa_state");
    if (!state) return MP_WIFI_UNKNOWN;
    size_t size = strcspn(state, "\r\n");
    static const struct { const char *name; enum mp_wifi_state state; } states[] = {
        {"COMPLETED", MP_WIFI_CONNECTED}, {"INTERFACE_DISABLED", MP_WIFI_OFFLINE},
        {"DISCONNECTED", MP_WIFI_DISCONNECTED}, {"INACTIVE", MP_WIFI_DISCONNECTED},
        {"SCANNING", MP_WIFI_CONNECTING}, {"AUTHENTICATING", MP_WIFI_CONNECTING},
        {"ASSOCIATING", MP_WIFI_CONNECTING}, {"ASSOCIATED", MP_WIFI_CONNECTING},
        {"4WAY_HANDSHAKE", MP_WIFI_CONNECTING}, {"GROUP_HANDSHAKE", MP_WIFI_CONNECTING}
    };
    for (unsigned i = 0; i < sizeof states / sizeof states[0]; ++i)
        if (strlen(states[i].name) == size && !strncmp(state, states[i].name, size))
            return states[i].state;
    return MP_WIFI_UNKNOWN;
}

int mp_wifi_parse_signal(const char *reply)
{
    const char *value = field(reply, "RSSI");
    if (!value || *value != '-') return MP_WIFI_NO_SIGNAL;
    char *end;
    long signal = strtol(value, &end, 10);
    if (end == value || (*end && *end != '\r' && *end != '\n') || signal < -127 || signal >= 0)
        return MP_WIFI_NO_SIGNAL;
    return (int)signal;
}

const char *mp_wifi_state_name(enum mp_wifi_state state)
{
    static const char *const names[] = {"unknown", "off", "disconnected", "connecting", "connected"};
    return (unsigned)state < sizeof names / sizeof names[0] ? names[state] : names[0];
}

static unsigned signal_level(int signal, unsigned previous)
{
    if (signal < -127 || signal >= 0) return 0;
    /* Three-dBm hysteresis prevents bars flickering at a threshold. An initial
     * reading uses -55/-70 dBm; reconnects never inherit an old signal level. */
    if (previous == 3 && signal >= -58) return 3;
    if (previous == 2 && signal >= -73 && signal < -52) return 2;
    if (previous == 1 && signal < -67) return 1;
    return signal >= -55 ? 3 : signal >= -70 ? 2 : 1;
}

static void artwork(enum mp_wifi_state state, unsigned level, unsigned stage, uint8_t *pixels)
{
    static const uint8_t fade[] = {3, 12, 11, 10, 5};
    memcpy(pixels, wifi_default_pixels, MP_WIFI_ICON_PIXELS);
    for (unsigned i = 0; i < MP_WIFI_ICON_PIXELS; ++i) {
        if (pixels[i] < 3) continue;
        unsigned required = wifi_default_bands[i] + 1u;
        if (state != MP_WIFI_CONNECTED && state != MP_WIFI_CONNECTING) pixels[i] = 3;
        else if (required > level) pixels[i] = 3;
        else if (state == MP_WIFI_CONNECTING && required == level) pixels[i] = fade[stage];
    }
    if (state == MP_WIFI_OFFLINE)
        for (unsigned y = 0; y < 18; ++y) {
            unsigned x = (17 - y) * (MP_WIFI_ICON_WIDTH - 2) / 17;
            pixels[y * MP_WIFI_ICON_WIDTH + x] = 6;
            pixels[y * MP_WIFI_ICON_WIDTH + x + 1] = 6;
        }
}

static void build_cache(struct mp_wifi *wifi)
{
    artwork(wifi->reading.state, wifi->level, 4, wifi->frames[0]);
    for (unsigned i = 0; i < MP_WIFI_JOIN_FRAMES; ++i)
        artwork(MP_WIFI_CONNECTING, i / 5, i % 5, wifi->frames[1 + i]);
    ++wifi->revision;
}

void mp_wifi_init(struct mp_wifi *wifi)
{
    memset(wifi, 0, sizeof *wifi);
    wifi->reading.signal_dbm = MP_WIFI_NO_SIGNAL;
    build_cache(wifi);
}

void mp_wifi_update(struct mp_wifi *wifi, struct mp_wifi_reading reading, uint64_t now_ms)
{
    if ((unsigned)reading.state > MP_WIFI_CONNECTED) reading.state = MP_WIFI_UNKNOWN;
    if (reading.signal_dbm < -127 || reading.signal_dbm >= 0) reading.signal_dbm = MP_WIFI_NO_SIGNAL;
    unsigned level = reading.state == MP_WIFI_CONNECTED ? signal_level(reading.signal_dbm,
        wifi->reading.state == MP_WIFI_CONNECTED ? wifi->level : 0) : 0;
    if (reading.state == MP_WIFI_CONNECTING && wifi->reading.state != MP_WIFI_CONNECTING)
        wifi->join_ms = now_ms;
    int rebuild = reading.state != wifi->reading.state || level != wifi->level;
    wifi->reading = reading;
    wifi->level = level;
    if (rebuild) build_cache(wifi);
}

struct mp_wifi_visual mp_wifi_visual(const struct mp_wifi *wifi, uint64_t now_ms, int animate)
{
    struct mp_wifi_visual visual = {0};
    if (animate && wifi->reading.state == MP_WIFI_CONNECTING)
        visual.frame = 1 + (unsigned)((now_ms - wifi->join_ms) % 2000) / 100;
    visual.stamp = ((uint64_t)wifi->revision << 8) | visual.frame;
    return visual;
}

unsigned mp_wifi_wait_ms(const struct mp_wifi *wifi, uint64_t now_ms, int animate)
{
    if (!animate) return UINT_MAX;
    if (wifi->reading.state == MP_WIFI_CONNECTING) return 100 - (unsigned)((now_ms - wifi->join_ms) % 100);
    return UINT_MAX;
}
