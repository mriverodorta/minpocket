// SPDX-License-Identifier: GPL-2.0-only
#include "ui.h"
#include <limits.h>

void mp_ui_clock_begin(struct mp_ui_clock *clock, uint64_t now_ms, int animate)
{
    *clock = (struct mp_ui_clock){.now_ms = now_ms, .next_ms = UINT64_MAX, .animate = !!animate};
}

void mp_ui_clock_request(struct mp_ui_clock *clock, unsigned wait_ms)
{
    if (wait_ms == UINT_MAX || UINT64_MAX - clock->now_ms < wait_ms) return;
    uint64_t deadline = clock->now_ms + wait_ms;
    if (deadline < clock->next_ms) clock->next_ms = deadline;
}

unsigned mp_ui_clock_wait(const struct mp_ui_clock *clock, uint64_t now_ms)
{
    if (clock->next_ms == UINT64_MAX) return UINT_MAX;
    uint64_t wait = clock->next_ms > now_ms ? clock->next_ms - now_ms : 0;
    return wait > UINT_MAX ? UINT_MAX : (unsigned)wait;
}

int mp_ui_motion_value(const struct mp_ui_motion *motion, const struct mp_ui_clock *clock)
{
    if (!clock->animate || !motion->duration_ms) return motion->target;
    uint64_t elapsed = clock->now_ms > motion->start_ms ? clock->now_ms - motion->start_ms : 0;
    if (elapsed >= motion->duration_ms) return motion->target;
    uint64_t remaining = motion->duration_ms - elapsed, duration = motion->duration_ms;
    /* Fixed-point cubic ease-out. UI motion uses bounded pixel distances and
     * durations; no floating-point work or cumulative frame-step drift. */
    uint64_t divisor = duration * duration * duration;
    int distance = motion->target - motion->from;
    uint64_t magnitude = (uint64_t)(distance < 0 ? -distance : distance);
    if (motion->easing == MP_EASE_LINEAR || motion->easing == MP_EASE_IN_OUT_CUBIC) {
        uint64_t progress;
        if (motion->easing == MP_EASE_LINEAR) progress = (magnitude * elapsed + duration / 2) / duration;
        else if (elapsed * 2 < duration)
            progress = (4 * magnitude * elapsed * elapsed * elapsed + divisor / 2) / divisor;
        else progress = magnitude - (4 * magnitude * remaining * remaining * remaining + divisor / 2) / divisor;
        return distance >= 0 ? motion->from + (int)progress : motion->from - (int)progress;
    }
    uint64_t tail = (magnitude * remaining * remaining * remaining + divisor / 2) / divisor;
    return distance >= 0 ? motion->target - (int)tail : motion->target + (int)tail;
}

void mp_ui_motion_to(struct mp_ui_motion *motion, int target, unsigned duration_ms,
                     const struct mp_ui_clock *clock)
{
    if (target == motion->target) return;
    int from = mp_ui_motion_value(motion, clock);
    *motion = (struct mp_ui_motion){.from = from, .target = target,
        .start_ms = clock->now_ms, .duration_ms = duration_ms > 2000 ? 2000 : duration_ms, .easing = motion->easing};
}

void mp_ui_motion_schedule(struct mp_ui_motion *motion, struct mp_ui_clock *clock)
{
    if (!clock->animate) {
        motion->from = motion->target;
        motion->duration_ms = 0;
        return;
    }
    if (motion->from == motion->target) return;
    uint64_t elapsed = clock->now_ms > motion->start_ms ? clock->now_ms - motion->start_ms : 0;
    if (elapsed < motion->duration_ms) {
        unsigned remaining = motion->duration_ms - (unsigned)elapsed;
        mp_ui_clock_request(clock, remaining < 16 ? remaining : 16);
    }
}

int mp_ui_row_right(const struct mp_ui_row_item *items, unsigned count,
                    int right, int top, struct mp_ui_rect *boxes)
{
    for (unsigned i = count; i-- > 0;) {
        right -= (int)items[i].width;
        boxes[i] = (struct mp_ui_rect){right, top, items[i].width, items[i].height};
        right -= (int)items[i].gap_before;
    }
    return right;
}

void mp_ui_index_sprite(struct mp_ui_index_surface *surface, int x, int y,
                        const uint8_t *pixels, unsigned width, unsigned height, unsigned stride)
{
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            int dx = x + (int)col, dy = y + (int)row;
            if (dx < 0 || dy < 0 || (unsigned)dx >= surface->width || (unsigned)dy >= surface->height) continue;
            uint8_t color = pixels[row * stride + col];
            if (color) surface->pixels[(unsigned)dy * surface->stride + (unsigned)dx] = color;
        }
}

struct mp_ui_rect mp_ui_rect_union(struct mp_ui_rect a, struct mp_ui_rect b)
{
    if (!a.width || !a.height) return b;
    if (!b.width || !b.height) return a;
    int x = a.x < b.x ? a.x : b.x, y = a.y < b.y ? a.y : b.y;
    int right_a = a.x + (int)a.width, right_b = b.x + (int)b.width;
    int bottom_a = a.y + (int)a.height, bottom_b = b.y + (int)b.height;
    return (struct mp_ui_rect){x, y, (unsigned)((right_a > right_b ? right_a : right_b) - x),
        (unsigned)((bottom_a > bottom_b ? bottom_a : bottom_b) - y)};
}

void mp_ui_fill(const struct mp_font_surface *surface, struct mp_ui_rect box, uint32_t color)
{
    int left = box.x < 0 ? 0 : box.x, top = box.y < 0 ? 0 : box.y;
    int64_t right = (int64_t)box.x + box.width, bottom = (int64_t)box.y + box.height;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    /* Follow physical scanout memory. The MP1's logical Y is contiguous;
     * row-first logical fills repeatedly flush tiny write-combining stores. */
    if (surface->y_stride == 1 || surface->y_stride == -1) {
        for (int x = left; x < right; ++x)
            for (int y = top; y < bottom; ++y)
                surface->pixels[(ptrdiff_t)x * surface->x_stride + (ptrdiff_t)y * surface->y_stride] = color;
    } else {
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                surface->pixels[(ptrdiff_t)x * surface->x_stride + (ptrdiff_t)y * surface->y_stride] = color;
    }
}

void mp_ui_palette_paint(const struct mp_font_surface *surface, int x, int y,
                         const struct mp_ui_index_surface *source, const uint32_t *palette,
                         unsigned palette_size, uint32_t background)
{
    for (unsigned row = 0; row < source->height; ++row)
        for (unsigned col = 0; col < source->width; ++col) {
            int dx = x + (int)col, dy = y + (int)row;
            if (dx < 0 || dy < 0 || (unsigned)dx >= surface->width || (unsigned)dy >= surface->height) continue;
            uint8_t index = source->pixels[row * source->stride + col];
            uint32_t color = index && index < palette_size ? palette[index] : background;
            surface->pixels[(ptrdiff_t)dx * surface->x_stride + (ptrdiff_t)dy * surface->y_stride] = color;
        }
}
