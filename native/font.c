// SPDX-License-Identifier: GPL-2.0-only
#include "font.h"
#include "pixel.h"
#include <limits.h>

#include "fonts/monogram/monogram-data.h"

const uint8_t *mp_font_glyph(uint32_t codepoint)
{
    size_t low = 0, high = sizeof monogram_glyphs / sizeof monogram_glyphs[0];
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (monogram_glyphs[middle].codepoint < codepoint)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < sizeof monogram_glyphs / sizeof monogram_glyphs[0] &&
        monogram_glyphs[low].codepoint == codepoint)
        return monogram_glyphs[low].rows;
    return mp_font_glyph('?');
}

static uint32_t next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    unsigned length;
    uint32_t code, minimum;
    if (*s < 0x80) { *text += 1; return *s; }
    if (*s >= 0xc2 && *s <= 0xdf) { length = 2; code = *s & 0x1f; minimum = 0x80; }
    else if (*s >= 0xe0 && *s <= 0xef) { length = 3; code = *s & 0x0f; minimum = 0x800; }
    else if (*s >= 0xf0 && *s <= 0xf4) { length = 4; code = *s & 7; minimum = 0x10000; }
    else { *text += 1; return '?'; }
    for (unsigned i = 1; i < length; ++i) {
        if ((s[i] & 0xc0) != 0x80) { *text += 1; return '?'; }
        code = (code << 6) | (s[i] & 0x3f);
    }
    *text += length;
    return code >= minimum && code <= 0x10ffff && !(code >= 0xd800 && code <= 0xdfff)
        ? code : '?';
}

unsigned mp_font_width(const char *text, unsigned scale)
{
    if (!scale || scale > MP_FONT_MAX_SCALE) return 0;
    unsigned pen = 0, width = 0;
    while (*text) {
        const uint8_t *glyph = mp_font_glyph(next_codepoint(&text));
        unsigned columns = MP_FONT_WIDTH;
        for (unsigned row = 0; row < MP_FONT_ROWS; ++row) {
            if (glyph[row] & 64) columns = MP_FONT_INK_COLUMNS;
            else if ((glyph[row] & 32) && columns < 6) columns = 6;
        }
        if (pen > UINT_MAX - MP_FONT_INK_COLUMNS * scale) return UINT_MAX;
        unsigned end = pen + columns * scale;
        if (end > width) width = end;
        pen += MP_FONT_ADVANCE * scale;
    }
    return width;
}

void mp_font_draw(const struct mp_font_surface *surface, int x, int y,
                  unsigned scale, const char *text, uint32_t color)
{
    if (!scale || scale > MP_FONT_MAX_SCALE) return;
    int64_t pen = x, top = (int64_t)y - MP_FONT_CAP_TOP * scale;
    while (*text && pen < surface->width) {
        const uint8_t *glyph = mp_font_glyph(next_codepoint(&text));
        for (unsigned row = 0; row < MP_FONT_ROWS; ++row) {
            for (unsigned col = 0; col < MP_FONT_INK_COLUMNS; ++col) {
                if (!(glyph[row] & (1u << col))) continue;
                for (unsigned sy = 0; sy < scale; ++sy) {
                    int64_t py = top + row * scale + sy;
                    if (py < 0 || py >= surface->height) continue;
                    for (unsigned sx = 0; sx < scale; ++sx) {
                        int64_t px = pen + col * scale + sx;
                        if (px < 0 || px >= surface->width) continue;
                        surface->pixels[(ptrdiff_t)px * surface->x_stride +
                                        (ptrdiff_t)py * surface->y_stride] = color;
                    }
                }
            }
        }
        pen += MP_FONT_ADVANCE * scale;
    }
}

unsigned mp_font_line_height_px(unsigned height)
{return height&&height<=112?(height*MP_FONT_LINE_ROWS+MP_FONT_CAP_HEIGHT-1)/MP_FONT_CAP_HEIGHT:0;}

unsigned mp_font_width_px(const char *text,unsigned height)
{
    if(!height||height>112)return 0;
    unsigned pen=0,width=0,advance=(MP_FONT_ADVANCE*height+3)/MP_FONT_CAP_HEIGHT;
    while(*text){
        const uint8_t *glyph=mp_font_glyph(next_codepoint(&text));unsigned columns=MP_FONT_WIDTH;
        for(unsigned row=0;row<MP_FONT_ROWS;row++){
            if(glyph[row]&64)columns=MP_FONT_INK_COLUMNS;
            else if((glyph[row]&32)&&columns<6)columns=6;
        }
        unsigned ink=(columns*height+6)/MP_FONT_CAP_HEIGHT;
        if(pen>UINT_MAX-ink)return UINT_MAX;
        if(pen+ink>width)width=pen+ink;
        if(pen>UINT_MAX-advance)return UINT_MAX;
        pen+=advance;
    }
    return width;
}
void mp_font_draw_px(const struct mp_font_surface *surface,int x,int y,unsigned height,const char *text,uint32_t color)
{
    if(!height||height>112)return;
    int64_t pen=x;unsigned advance=(MP_FONT_ADVANCE*height+3)/MP_FONT_CAP_HEIGHT;
    int above=(MP_FONT_CAP_TOP*(int)height+6)/MP_FONT_CAP_HEIGHT;
    int below=((MP_FONT_ROWS-MP_FONT_CAP_TOP)*(int)height+6)/MP_FONT_CAP_HEIGHT;
    while(*text&&pen<surface->width){
        const uint8_t *glyph=mp_font_glyph(next_codepoint(&text));
        for(unsigned dx=0;dx<height;dx++){
            int64_t px=pen+dx;if(px<0||px>=surface->width)continue;
            unsigned col=dx*MP_FONT_CAP_HEIGHT/height;
            for(int dy=-above;dy<below;dy++){
                int64_t py=(int64_t)y+dy;if(py<0||py>=surface->height)continue;
                int scaled=dy*MP_FONT_CAP_HEIGHT;
                int row=MP_FONT_CAP_TOP+(scaled<0?-((-scaled+(int)height-1)/(int)height):scaled/(int)height);
                if(row>=0&&row<MP_FONT_ROWS&&(glyph[row]&(1u<<col)))
                    surface->pixels[(ptrdiff_t)px*surface->x_stride+(ptrdiff_t)py*surface->y_stride]=color;
            }
        }
        pen+=advance;
    }
}

void mp_font_draw_px_alpha(const struct mp_font_surface *surface,int x,int y,unsigned height,const char *text,uint32_t color,unsigned alpha)
{
    if(!alpha)return;
    if(alpha>=255){mp_font_draw_px(surface,x,y,height,text,color);return;}
    if(!height||height>112)return;
    int64_t pen=x;unsigned advance=(MP_FONT_ADVANCE*height+3)/MP_FONT_CAP_HEIGHT;
    int above=(MP_FONT_CAP_TOP*(int)height+6)/MP_FONT_CAP_HEIGHT;
    int below=((MP_FONT_ROWS-MP_FONT_CAP_TOP)*(int)height+6)/MP_FONT_CAP_HEIGHT;
    while(*text&&pen<surface->width){
        const uint8_t *glyph=mp_font_glyph(next_codepoint(&text));
        for(unsigned dx=0;dx<height;dx++){
            int64_t px=pen+dx;if(px<0||px>=surface->width)continue;
            unsigned col=dx*MP_FONT_CAP_HEIGHT/height;
            for(int dy=-above;dy<below;dy++){
                int64_t py=(int64_t)y+dy;if(py<0||py>=surface->height)continue;
                int scaled=dy*MP_FONT_CAP_HEIGHT;
                int row=MP_FONT_CAP_TOP+(scaled<0?-((-scaled+(int)height-1)/(int)height):scaled/(int)height);
                if(row>=0&&row<MP_FONT_ROWS&&(glyph[row]&(1u<<col))){
                    uint32_t *pixel=&surface->pixels[(ptrdiff_t)px*surface->x_stride+(ptrdiff_t)py*surface->y_stride];
                    *pixel=mp_pixel_blend(*pixel,color,alpha);
                }
            }
        }
        pen+=advance;
    }
}

void mp_font_ink_bounds_px(const char *text,unsigned height,int *top,int *bottom)
{
 *top=*bottom=0;if(!height||height>112)return;
 uint8_t rows[MP_FONT_ROWS]={0};
 while(*text){const uint8_t *glyph=mp_font_glyph(next_codepoint(&text));for(unsigned i=0;i<MP_FONT_ROWS;i++)rows[i]|=glyph[i]&127;}
 int above=(MP_FONT_CAP_TOP*(int)height+6)/MP_FONT_CAP_HEIGHT;
 int below=((MP_FONT_ROWS-MP_FONT_CAP_TOP)*(int)height+6)/MP_FONT_CAP_HEIGHT;
 int found=0;
 for(int dy=-above;dy<below;dy++){
  int scaled=dy*MP_FONT_CAP_HEIGHT;
  int row=MP_FONT_CAP_TOP+(scaled<0?-((-scaled+(int)height-1)/(int)height):scaled/(int)height);
  if(row>=0&&row<MP_FONT_ROWS&&rows[row]){if(!found)*top=dy;*bottom=dy+1;found=1;}
 }
}
