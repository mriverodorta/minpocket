// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_UI_H
#define MINPOCKET_UI_H
#include "font.h"
#include <stdint.h>

struct mp_ui_rect { int x, y; unsigned width, height; };
struct mp_ui_row_item { unsigned width, height, gap_before; };
struct mp_ui_index_surface { uint8_t *pixels; unsigned width, height, stride; };
/* One monotonic timestamp and earliest deadline for a whole component tree. */
struct mp_ui_clock { uint64_t now_ms, next_ms; int animate; };
enum mp_ui_easing { MP_EASE_OUT_CUBIC, MP_EASE_LINEAR, MP_EASE_IN_OUT_CUBIC };
struct mp_ui_motion { int from, target; uint64_t start_ms; unsigned duration_ms; enum mp_ui_easing easing; };

void mp_ui_clock_begin(struct mp_ui_clock *clock, uint64_t now_ms, int animate);
void mp_ui_clock_request(struct mp_ui_clock *clock, unsigned wait_ms);
unsigned mp_ui_clock_wait(const struct mp_ui_clock *clock, uint64_t now_ms);
int mp_ui_motion_value(const struct mp_ui_motion *motion, const struct mp_ui_clock *clock);
void mp_ui_motion_to(struct mp_ui_motion *motion, int target, unsigned duration_ms,
                     const struct mp_ui_clock *clock);
void mp_ui_motion_schedule(struct mp_ui_motion *motion, struct mp_ui_clock *clock);
/* Intrinsic widths and per-item gaps; zero-width items can still allocate a
 * growing gap during insertion. Coordinates are signed and vertically top-aligned. */
int mp_ui_row_right(const struct mp_ui_row_item *items, unsigned count,
                    int right, int top, struct mp_ui_rect *boxes);
struct mp_ui_rect mp_ui_rect_union(struct mp_ui_rect a, struct mp_ui_rect b);
void mp_ui_index_sprite(struct mp_ui_index_surface *surface, int x, int y,
                        const uint8_t *pixels, unsigned width, unsigned height, unsigned stride);
void mp_ui_fill(const struct mp_font_surface *surface, struct mp_ui_rect box, uint32_t color);
void mp_ui_palette_paint(const struct mp_font_surface *surface, int x, int y,
                         const struct mp_ui_index_surface *source, const uint32_t *palette,
                         unsigned palette_size, uint32_t background);
#endif
