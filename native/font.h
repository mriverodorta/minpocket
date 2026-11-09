// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_FONT_H
#define MINPOCKET_FONT_H

#include <stddef.h>
#include <stdint.h>

enum { MP_FONT_WIDTH = 5, MP_FONT_INK_COLUMNS = 7, MP_FONT_ROWS = 12, MP_FONT_ADVANCE = 6,
       MP_FONT_CAP_TOP = 3, MP_FONT_CAP_HEIGHT = 7, MP_FONT_LINE_ROWS = 10, MP_FONT_MAX_SCALE = 8 };

/* Strides are in uint32_t pixels; negative X supports the MP1's portrait scanout.
 * pixels points at logical (0, 0). The caller owns the surface allocation. */
struct mp_font_surface {
    uint32_t *pixels;
    unsigned width, height;
    ptrdiff_t x_stride, y_stride;
};

const uint8_t *mp_font_glyph(uint32_t codepoint);
unsigned mp_font_width(const char *text, unsigned scale);
/* Physical capital height, in pixels. Nearest-neighbor Monogram, no smoothing. */
/* MinPocket line-box advance, independent of the rendered string. The
 * capital top is the drawing origin; accent ink can overhang above it. */
unsigned mp_font_line_height_px(unsigned height);
unsigned mp_font_width_px(const char *text,unsigned height);
void mp_font_draw_px(const struct mp_font_surface *,int x,int y,unsigned height,const char *,uint32_t);
void mp_font_draw_px_alpha(const struct mp_font_surface *,int x,int y,unsigned height,const char *,uint32_t,unsigned alpha);
/* Exact rendered ink interval relative to the capital-top drawing origin. */
void mp_font_ink_bounds_px(const char *,unsigned height,int *top,int *bottom);
/* y is the capital top, not the full twelve-row cell's top. */
void mp_font_draw(const struct mp_font_surface *surface, int x, int y,
                  unsigned scale, const char *text, uint32_t color);

#endif
