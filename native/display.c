// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#include "display.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

static int select_drm_output(struct mp1_display *display)
{
    int fd = display->fd;
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources)
        return -1;

    int found = 0;
    for (int i = 0; i < resources->count_connectors && !found; ++i) {
        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (!connector)
            continue;
        if (connector->connector_type == DRM_MODE_CONNECTOR_DSI &&
            connector->connection == DRM_MODE_CONNECTED) {
            drmModeModeInfo normal = {0}, fast = {0};
            for (int j = 0; j < connector->count_modes; ++j) {
                drmModeModeInfo *candidate = &connector->modes[j];
                if (candidate->hdisplay != MP1_HEIGHT || candidate->vdisplay != MP1_WIDTH)
                    continue;
                if (candidate->clock == 74250)
                    normal = *candidate;
                else if (candidate->clock == 99000 &&
                         candidate->htotal == 808 && candidate->vtotal == 1021)
                    fast = *candidate;
            }
            if (normal.clock) {
                for (int k = 0; k < connector->count_encoders && !found; ++k) {
                    drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoders[k]);
                    if (!encoder)
                        continue;
                    for (int n = 0; n < resources->count_crtcs && n < 32; ++n) {
                        if (!(encoder->possible_crtcs & (1u << n)))
                            continue;
                        display->connector = connector->connector_id;
                        display->crtc = resources->crtcs[n];
                        display->crtc_index = n;
                        display->mode = display->mode_60 = normal;
                        display->mode_120 = fast;
                        found = 1;
                        break;
                    }
                    drmModeFreeEncoder(encoder);
                }
            }
        }
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources);
    return found ? 0 : -1;
}

static int create_frame(int fd, struct mp1_frame *frame, struct mp1_display_profile *profile)
{
    struct drm_mode_create_dumb buffer = {
        .width = MP1_HEIGHT, .height = MP1_WIDTH, .bpp = 32
    };
    uint32_t fb;
    uint64_t start = profile ? mp1_boot_ns() : 0;
    if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &buffer))
        return -1;
    if (profile) {
        profile->allocation = mp1_boot_ns() - start;
        start = mp1_boot_ns();
    }
    if (buffer.pitch < MP1_HEIGHT * sizeof(uint32_t) ||
        buffer.size < (uint64_t)buffer.pitch * MP1_WIDTH ||
        drmModeAddFB(fd, MP1_HEIGHT, MP1_WIDTH, 24, 32,
                     buffer.pitch, buffer.handle, &fb))
        goto destroy;
    if (profile) {
        profile->framebuffer = mp1_boot_ns() - start;
        start = mp1_boot_ns();
    }

    struct drm_mode_map_dumb map = { .handle = buffer.handle };
    if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map))
        goto remove;
    if (profile) {
        profile->map_ioctl = mp1_boot_ns() - start;
        start = mp1_boot_ns();
    }
    void *pixels = mmap(NULL, buffer.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
    if (pixels == MAP_FAILED)
        goto remove;
    if (profile)
        profile->mmap = mp1_boot_ns() - start;

    *frame = (struct mp1_frame){
        .fb = fb, .pitch = buffer.pitch, .size = buffer.size, .pixels = pixels
    };
    return 0;

remove:
    drmModeRmFB(fd, fb);
destroy: {
    struct drm_mode_destroy_dumb handle = { .handle = buffer.handle };
    ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &handle);
    return -1;
}
}

static int open_card(struct mp1_display *display, const char *path)
{
    uint64_t start = mp1_boot_ns();
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    display->profile.open = mp1_boot_ns() - start;
    display->fd = fd;
    start = mp1_boot_ns();
    int result = drmSetMaster(fd);
    display->profile.master = mp1_boot_ns() - start;
    if (result)
        goto failed;
    start = mp1_boot_ns();
    result = select_drm_output(display);
    display->profile.discovery = mp1_boot_ns() - start;
    uint64_t monotonic = 0;
    if (!drmGetCap(fd, DRM_CAP_TIMESTAMP_MONOTONIC, &monotonic))
        display->monotonic_timestamps = monotonic == 1;
    if (!result && !create_frame(fd, &display->front, &display->profile))
        return 0;

failed:
    close(fd);
    display->fd = -1;
    return -1;
}

int mp1_display_open(struct mp1_display *display)
{
    *display = (struct mp1_display){ .fd = -1 };
    for (unsigned i = 0; i < 8; ++i) {
        char path[32];
        snprintf(path, sizeof path, "/dev/dri/card%u", i);
        if (!open_card(display, path))
            return 0;
    }
    return -1;
}

int mp1_display_show(struct mp1_display *display)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    display->modeset_start_monotonic = (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    return drmModeSetCrtc(display->fd, display->crtc, display->front.fb, 0, 0,
                          &display->connector, 1, &display->mode);
}

int mp1_display_set_refresh(struct mp1_display *display, unsigned hz)
{
    drmModeModeInfo next;
    if (display->flip_pending)
        return -1;
    if (hz == 60)
        next = display->mode_60;
    else if (hz == 120 && display->mode_120.clock)
        next = display->mode_120;
    else
        return -1;

    int ret = drmModeSetCrtc(display->fd, display->crtc, display->front.fb, 0, 0,
                             &display->connector, 1, &next);
    if (!ret)
        display->mode = next;
    return ret;
}

int mp1_display_wait_refresh(struct mp1_display *display)
{
    drmVBlank wait = {0};
    wait.request.type = DRM_VBLANK_RELATIVE |
        (display->crtc_index << DRM_VBLANK_HIGH_CRTC_SHIFT);
    wait.request.sequence = 1;
    int result = drmWaitVBlank(display->fd, &wait);
    if (!result)
        display->last_sequence = wait.reply.sequence;
    return result;
}

int mp1_display_confirm_first(struct mp1_display *display)
{
    drmVBlank query = {0};
    query.request.type = DRM_VBLANK_RELATIVE |
        (display->crtc_index << DRM_VBLANK_HIGH_CRTC_SHIFT);
    /* The blocking modeset already waits for scanout completion. Reuse its confirmed
     * refresh only when the timestamp proves it occurred after this modeset. */
    if (display->monotonic_timestamps && !drmWaitVBlank(display->fd, &query)) {
        uint64_t time = (uint64_t)query.reply.tval_sec * 1000000000 +
                        (uint64_t)query.reply.tval_usec * 1000;
        if (query.reply.sequence && time >= display->modeset_start_monotonic) {
            display->last_sequence = query.reply.sequence;
            return 0;
        }
    }
    return mp1_display_wait_refresh(display);
}

struct mp1_frame *mp1_display_back_buffer(struct mp1_display *display)
{
    if (display->flip_pending)
        return NULL;
    if (!display->back.fb && create_frame(display->fd, &display->back, NULL))
        return NULL;
    return &display->back;
}

int mp1_display_flip(struct mp1_display *display)
{
    if (display->flip_pending || !display->back.fb)
        return -1;
    if (drmModePageFlip(display->fd, display->crtc, display->back.fb,
                        DRM_MODE_PAGE_FLIP_EVENT, display))
        return -1;
    display->flip_pending = 1;
    return 0;
}

static void flip_done(int fd, unsigned sequence, unsigned sec, unsigned usec, void *data)
{
    (void)fd;
    (void)sequence;
    (void)sec;
    (void)usec;
    struct mp1_display *display = data;
    struct mp1_frame previous = display->front;
    display->front = display->back;
    display->back = previous;
    display->flip_pending = 0;
}

void mp1_display_dispatch(struct mp1_display *display)
{
    drmEventContext events = {
        .version = DRM_EVENT_CONTEXT_VERSION, .page_flip_handler = flip_done
    };
    drmHandleEvent(display->fd, &events);
}
