// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_DISPLAY_H
#define MINPOCKET_DISPLAY_H

#include <stddef.h>
#include <stdint.h>
#include <xf86drmMode.h>
#include "boot-profile.h"

enum { MP1_WIDTH = 960, MP1_HEIGHT = 720 };

struct mp1_frame {
    uint32_t fb, pitch;
    size_t size;
    uint32_t *pixels;
};

struct mp1_display {
    int fd;
    uint32_t connector, crtc;
    drmModeModeInfo mode, mode_60, mode_120;
    unsigned crtc_index;
    struct mp1_frame front, back;
    int flip_pending;
    uint64_t modeset_start_monotonic;
    uint32_t last_sequence;
    int monotonic_timestamps;
    struct mp1_display_profile profile;
};

int mp1_display_open(struct mp1_display *display);
struct mp1_frame *mp1_display_back_buffer(struct mp1_display *display);
int mp1_display_flip(struct mp1_display *display);
void mp1_display_dispatch(struct mp1_display *display);
int mp1_display_wait_refresh(struct mp1_display *display);
int mp1_display_confirm_first(struct mp1_display *display);
int mp1_display_show(struct mp1_display *display);
int mp1_display_set_refresh(struct mp1_display *display, unsigned hz);

#endif
