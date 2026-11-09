// SPDX-License-Identifier: GPL-2.0-only
#include "style.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "moonstone-native12.h"

static unsigned maximum(unsigned a, unsigned b) { return a > b ? a : b; }
static unsigned minimum(unsigned a, unsigned b) { return a < b ? a : b; }
static int edges_valid(struct mp_ui_edges e, unsigned limit)
{ return e.top <= limit && e.right <= limit && e.bottom <= limit && e.left <= limit; }
int mp_box_style_valid(const struct mp_box_style *s)
{
    if (!s || s->pixel_scale > 2 || (unsigned)s->frame.art > MP_FRAME_MOONSTONE) return 0;
    unsigned scale = s->pixel_scale ? s->pixel_scale : 1;
    if (s->frame.art == MP_FRAME_MOONSTONE &&
        (s->frame.highlight > 0xffffff || s->frame.light > 0xffffff || s->frame.mid > 0xffffff ||
         s->frame.face > 0xffffff || s->frame.shadow > 0xffffff || s->frame.recess > 0xffffff ||
         s->border.top || s->border.right || s->border.left != s->border.bottom ||
         s->border.left < 6 * scale || s->border.left > 32 * scale || s->border_style != MP_BORDER_SOLID ||
         s->radius.top_left || s->radius.top_right || s->radius.bottom_left || s->radius.bottom_right)) return 0;
    return s && s->background <= 0xffffff && s->border_color <= 0xffffff && s->shadow.color <= 0xffffff &&
        s->background_alpha <= 255 && s->border_alpha <= 255 && s->opacity <= 255 && s->shadow.alpha <= 255 &&
        edges_valid(s->border, 64 * scale) && s->radius.top_left <= 256 * scale && s->radius.top_right <= 256 * scale &&
        s->radius.bottom_right <= 256 * scale && s->radius.bottom_left <= 256 * scale &&
        (unsigned)s->border_style <= MP_BORDER_DOTTED && (unsigned)s->blend_mode <= MP_BLEND_ADD &&
        s->blur <= 8 * scale && s->backdrop_blur <= 8 * scale && s->shadow.blur <= 8 * scale &&
        s->shadow.x >= -128 * (int)scale && s->shadow.x <= 128 * (int)scale && s->shadow.y >= -128 * (int)scale && s->shadow.y <= 128 * (int)scale &&
        s->shadow.spread >= -32 * (int)scale && s->shadow.spread <= 32 * (int)scale && (s->clip_children == 0 || s->clip_children == 1);
}
int mp_layout_style_valid(const struct mp_layout_style *s)
{
    return s && (unsigned)s->orientation <= MP_VERTICAL && (unsigned)s->direction <= MP_REVERSE &&
        (unsigned)s->justify <= MP_SPACE_EVENLY && (unsigned)s->align <= MP_ALIGN_STRETCH &&
        (unsigned)s->anchor_x <= MP_ALIGN_END && (unsigned)s->anchor_y <= MP_ALIGN_END &&
        (unsigned)s->overflow <= MP_OVERFLOW_CLIP && edges_valid(s->padding, 128) && edges_valid(s->margin, 4096) &&
        s->gap <= 64 && s->width <= 4096 && s->height <= 4096 && s->min_width <= s->max_width &&
        s->min_height <= s->max_height && s->max_width && s->max_height &&
        s->max_width <= 4096 && s->max_height <= 4096 &&
        (!s->width || (s->width >= s->min_width && s->width <= s->max_width)) &&
        (!s->height || (s->height >= s->min_height && s->height <= s->max_height)) &&
        s->offset_x >= -4096 && s->offset_x <= 4096 && s->offset_y >= -4096 && s->offset_y <= 4096;
}
int mp_box_layout(const struct mp_box_style *box, const struct mp_layout_style *layout,
                   struct mp_layout_style *content_layout)
{
    if (!content_layout || !mp_box_style_valid(box) || !mp_layout_style_valid(layout)) return -1;
    struct mp_layout_style result = *layout;
    result.padding.top += box->border.top;
    result.padding.right += box->border.right;
    result.padding.bottom += box->border.bottom;
    result.padding.left += box->border.left;
    if (!mp_layout_style_valid(&result)) return -1;
    *content_layout = result;
    return 0;
}
static int anchored(enum mp_alignment alignment, unsigned viewport, unsigned before, unsigned after, unsigned size)
{
    int available = (int)viewport - (int)before - (int)after;
    return (int)before + (alignment == MP_ALIGN_CENTER ? (available - (int)size) / 2 :
        alignment == MP_ALIGN_END ? available - (int)size : 0);
}
int mp_layout_bounds(const struct mp_layout_style *s, unsigned iw, unsigned ih, unsigned vw, unsigned vh,
                     struct mp_ui_rect *box)
{
    if (!box || !mp_layout_style_valid(s) || vw > INT_MAX || vh > INT_MAX || iw > 4096 || ih > 4096) return -1;
    unsigned need_w = iw + s->padding.left + s->padding.right;
    unsigned need_h = ih + s->padding.top + s->padding.bottom;
    unsigned w = s->width ? s->width : maximum(need_w, s->min_width);
    unsigned h = s->height ? s->height : maximum(need_h, s->min_height);
    if (s->overflow == MP_OVERFLOW_REJECT && (w > s->max_width || h > s->max_height || w < need_w || h < need_h)) return -1;
    w = minimum(w, s->max_width); h = minimum(h, s->max_height);
    if (w < s->padding.left + s->padding.right || h < s->padding.top + s->padding.bottom) return -1;
    int x = anchored(s->anchor_x, vw, s->margin.left, s->margin.right, w) + s->offset_x;
    int y = anchored(s->anchor_y, vh, s->margin.top, s->margin.bottom, h) + s->offset_y;
    if (s->overflow == MP_OVERFLOW_REJECT && (x < 0 || y < 0 || (int64_t)x + w > vw || (int64_t)y + h > vh)) return -1;
    *box = (struct mp_ui_rect){x, y, w, h};
    return 0;
}
void mp_layout_arrange(const struct mp_layout_style *s, struct mp_ui_rect box,
                       const struct mp_ui_row_item *items, unsigned count, struct mp_ui_rect *boxes)
{
    int vertical = s->orientation == MP_VERTICAL;
    unsigned iw = box.width - s->padding.left - s->padding.right;
    unsigned ih = box.height - s->padding.top - s->padding.bottom;
    int main = (int)(vertical ? ih : iw), cross = (int)(vertical ? iw : ih), used = 0;
    unsigned active = 0;
    for (unsigned i = 0; i < count; ++i) {
        used += (int)(vertical ? items[i].height : items[i].width) + (int)items[i].gap_before;
        if ((vertical ? items[i].height : items[i].width) || items[i].gap_before) ++active;
    }
    int extra = main > used ? main - used : 0;
    int base = s->justify == MP_JUSTIFY_END ? main - used : s->justify == MP_JUSTIFY_CENTER ? (main - used) / 2 : 0;
    unsigned rank = 0;
    int cursor = 0;
    for (unsigned n = 0; n < count; ++n) {
        unsigned i = s->direction == MP_REVERSE ? count - 1 - n : n;
        unsigned size = vertical ? items[i].height : items[i].width;
        int distribution = 0;
        if (active && s->justify == MP_SPACE_AROUND) distribution = extra * (int)(2 * rank + 1) / (int)(2 * active);
        if (active && s->justify == MP_SPACE_EVENLY) distribution = extra * (int)(rank + 1) / (int)(active + 1);
        if (active > 1 && s->justify == MP_SPACE_BETWEEN) distribution = extra * (int)rank / (int)(active - 1);
        /* A reversed sequence moves the original leading gap after its item. */
        if (s->direction == MP_FORWARD) cursor += (int)items[i].gap_before;
        unsigned item_cross = vertical ? items[i].width : items[i].height;
        int offset = s->align == MP_ALIGN_CENTER ? (cross - (int)item_cross) / 2 :
            s->align == MP_ALIGN_END ? cross - (int)item_cross : 0;
        if (s->align == MP_ALIGN_STRETCH) item_cross = (unsigned)cross;
        int x = box.x + (int)s->padding.left, y = box.y + (int)s->padding.top;
        boxes[i] = vertical ? (struct mp_ui_rect){x + offset, y + base + cursor + distribution, item_cross, size} :
            (struct mp_ui_rect){x + base + cursor + distribution, y + offset, size, item_cross};
        cursor += (int)size;
        if (s->direction == MP_REVERSE) cursor += (int)items[i].gap_before;
        if (size || items[i].gap_before) ++rank;
    }
}

static struct mp_ui_rect intersect(struct mp_ui_rect a, struct mp_ui_rect b)
{
    int x = a.x > b.x ? a.x : b.x, y = a.y > b.y ? a.y : b.y;
    int64_t r = (int64_t)a.x + a.width, q = (int64_t)b.x + b.width;
    int64_t d = (int64_t)a.y + a.height, e = (int64_t)b.y + b.height;
    if (q < r) r = q;
    if (e < d) d = e;
    return (struct mp_ui_rect){x, y, r > x ? (unsigned)(r - x) : 0, d > y ? (unsigned)(d - y) : 0};
}
static int same_box(struct mp_ui_rect a, struct mp_ui_rect b)
{ return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
struct mp_ui_rect mp_box_paint_bounds(const struct mp_box_style *s, struct mp_ui_rect b)
{
    if (!b.width || !b.height) return b;
    unsigned halo = maximum(s->blur, s->backdrop_blur);
    struct mp_ui_rect r = {b.x - (int)halo, b.y - (int)halo, b.width + 2 * halo, b.height + 2 * halo};
    int spread = s->shadow.spread + (int)s->shadow.blur;
    if (s->shadow.alpha && (int)b.width + 2 * spread > 0 && (int)b.height + 2 * spread > 0) {
        struct mp_ui_rect shadow = {b.x + s->shadow.x - spread, b.y + s->shadow.y - spread,
            (unsigned)((int)b.width + 2 * spread), (unsigned)((int)b.height + 2 * spread)};
        r = mp_ui_rect_union(r, shadow);
    }
    return r;
}
void mp_box_cache_restore(const struct mp_box_cache *c, const struct mp_font_surface *s)
{
    if (!c->valid) return;
    unsigned outer=(s->y_stride==1||s->y_stride==-1)?c->bounds.width:c->bounds.height;
    unsigned inner=(s->y_stride==1||s->y_stride==-1)?c->bounds.height:c->bounds.width;
    for(unsigned a=0;a<outer;a++)for(unsigned b=0;b<inner;b++){
        unsigned x=(s->y_stride==1||s->y_stride==-1)?a:b,y=(s->y_stride==1||s->y_stride==-1)?b:a;
        s->pixels[(ptrdiff_t)(c->bounds.x+(int)x)*s->x_stride+(ptrdiff_t)(c->bounds.y+(int)y)*s->y_stride]=c->backdrop[y*c->bounds.width+x];
    }
}
int mp_box_cache_prepare(struct mp_box_cache *c, const struct mp_font_surface *s, struct mp_ui_rect b, int scene_valid)
{
    b = intersect(b, (struct mp_ui_rect){0, 0, s->width, s->height});
    if (scene_valid && c->valid && same_box(c->bounds, b)) return 0;
    size_t area = (size_t)b.width * b.height;
    if (area > 1024 * 1024) return -1;
    if (area > c->capacity) {
        /* Preserve the old snapshot if allocation fails. */
        size_t capacity = c->capacity ? c->capacity : 1024;
        while (capacity < area) capacity *= 2;
        uint32_t *p = malloc(capacity * sizeof *p);
        if (!p) return -1;
        if (scene_valid) mp_box_cache_restore(c, s);
        free(c->backdrop); c->backdrop = p; c->capacity = capacity;
    } else if (scene_valid) mp_box_cache_restore(c, s);
    c->bounds = b; c->valid = 1;
    /* Read scanout columns contiguously; uncached row-wise reads are costly. */
    unsigned outer=(s->y_stride==1||s->y_stride==-1)?b.width:b.height;
    unsigned inner=(s->y_stride==1||s->y_stride==-1)?b.height:b.width;
    for(unsigned a=0;a<outer;a++)for(unsigned n=0;n<inner;n++){
        unsigned x=(s->y_stride==1||s->y_stride==-1)?a:n,y=(s->y_stride==1||s->y_stride==-1)?n:a;
        c->backdrop[y*b.width+x]=s->pixels[(ptrdiff_t)(b.x+(int)x)*s->x_stride+(ptrdiff_t)(b.y+(int)y)*s->y_stride];
    }
    return 0;
}
void mp_box_cache_dispose(struct mp_box_cache *c) { free(c->backdrop); memset(c, 0, sizeof *c); }
void mp_box_workspace_dispose(struct mp_box_workspace *w) { free(w->storage); memset(w, 0, sizeof *w); }
int mp_box_is_opaque_plain(const struct mp_box_style *s)
{
    return s->frame.art == MP_FRAME_NONE && s->background_alpha == 255 && s->opacity == 255 && !s->border.top && !s->border.right &&
        !s->border.bottom && !s->border.left && !s->radius.top_left && !s->radius.top_right &&
        !s->radius.bottom_left && !s->radius.bottom_right && !s->shadow.alpha && !s->blur &&
        !s->backdrop_blur && s->blend_mode == MP_BLEND_NORMAL;
}
/* Premultiplied ARGB in effect buffers. Framebuffer pixels are opaque RGB. */
static uint32_t premultiply(uint32_t rgb, unsigned a)
{
    return a << 24 | (((rgb >> 16 & 255) * a + 127) / 255) << 16 |
        (((rgb >> 8 & 255) * a + 127) / 255) << 8 | ((rgb & 255) * a + 127) / 255;
}
static uint32_t over(uint32_t dst, uint32_t src)
{
    unsigned a = 255 - (src >> 24); uint32_t out = 0;
    for (unsigned shift = 0; shift <= 24; shift += 8)
        out |= minimum(255, (src >> shift & 255) + ((dst >> shift & 255) * a + 127) / 255) << shift;
    return out;
}
static void box_blur(uint32_t *p, uint32_t *tmp, unsigned w, unsigned h, unsigned radius)
{
    if (!radius || !w || !h) return;
    unsigned diameter = radius * 2 + 1;
    /* Sliding windows with replicated edge pixels. Cost is independent of radius. */
    for (unsigned axis = 0; axis < 2; ++axis) {
        unsigned lines = axis ? w : h, length = axis ? h : w;
        for (unsigned line = 0; line < lines; ++line) {
            unsigned sum[4] = {0};
            for (int d = -(int)radius; d <= (int)radius; ++d) {
                unsigned v = d < 0 ? 0 : minimum((unsigned)d, length - 1);
                uint32_t pixel = p[axis ? v * w + line : line * w + v];
                for (unsigned c = 0; c < 4; ++c) sum[c] += pixel >> (8 * c) & 255;
            }
            for (unsigned at = 0; at < length; ++at) {
                uint32_t pixel = 0;
                for (unsigned c = 0; c < 4; ++c) pixel |= ((sum[c] + diameter / 2) / diameter) << (8 * c);
                tmp[axis ? at * w + line : line * w + at] = pixel;
                unsigned remove = at < radius ? 0 : at - radius;
                unsigned add = minimum(at + radius + 1, length - 1);
                uint32_t old = p[axis ? remove * w + line : line * w + remove];
                uint32_t next = p[axis ? add * w + line : line * w + add];
                for (unsigned c = 0; c < 4; ++c) sum[c] = sum[c] - (old >> (8 * c) & 255) + (next >> (8 * c) & 255);
            }
        }
        memcpy(p, tmp, (size_t)w * h * sizeof *p);
    }
}
static int inside(int x, int y, struct mp_ui_rect b, struct mp_ui_corners radii)
{
    x -= b.x; y -= b.y;
    if (x < 0 || y < 0 || (unsigned)x >= b.width || (unsigned)y >= b.height) return 0;
    /* Scale all radii together when opposite corners exceed an edge. */
    unsigned scale = 65536;
    unsigned pairs[4] = {radii.top_left + radii.top_right, radii.bottom_left + radii.bottom_right,
        radii.top_left + radii.bottom_left, radii.top_right + radii.bottom_right};
    for (unsigned i = 0; i < 4; ++i) if (pairs[i]) scale = minimum(scale, (i < 2 ? b.width : b.height) * 65536 / pairs[i]);
    unsigned r[4] = {radii.top_left * scale / 65536, radii.top_right * scale / 65536,
        radii.bottom_right * scale / 65536, radii.bottom_left * scale / 65536};
    for (unsigned c = 0; c < 4; ++c) {
        int dx = c == 0 || c == 3 ? (int)r[c] - 1 - x : x - ((int)b.width - (int)r[c]);
        int dy = c < 2 ? (int)r[c] - 1 - y : y - ((int)b.height - (int)r[c]);
        if (dx >= 0 && dy >= 0 && (unsigned)(dx * dx + dy * dy) >= r[c] * r[c]) return 0;
    }
    return 1;
}
static unsigned blend(unsigned d, unsigned s, enum mp_blend_mode mode)
{
    switch (mode) {
    case MP_BLEND_MULTIPLY: return (d * s + 127) / 255;
    case MP_BLEND_SCREEN: return 255 - ((255 - d) * (255 - s) + 127) / 255;
    case MP_BLEND_OVERLAY: return d <= 127 ? (2 * d * s + 127) / 255 : 255 - (2 * (255 - d) * (255 - s) + 127) / 255;
    case MP_BLEND_DARKEN: return minimum(d, s);
    case MP_BLEND_LIGHTEN: return maximum(d, s);
    case MP_BLEND_DIFFERENCE: return d > s ? d - s : s - d;
    case MP_BLEND_EXCLUSION: return d + s - (2 * d * s + 127) / 255;
    case MP_BLEND_ADD: return minimum(255, d + s);
    default: return s;
    }
}
/* An eight-pixel authored corner and native-width straight strips. Resizing
 * changes strip length and repeats stone joints; it never scales icon pixels. */
static char moonstone_pixel(const struct mp_box_style *s, int x, int y, struct mp_ui_rect box)
{
    static const char corner[8][9] = {
        ".hfffffd", ".hlffffm", "..hffffm", "..hlfffm",
        "...hfffm", "...hlffm", "....hmmf", "....ssss"
    };
    x -= box.x; y -= box.y;
    if (x < 0 || y < 0 || (unsigned)x >= box.width || (unsigned)y >= box.height) return '.';
    unsigned scale = s->pixel_scale ? s->pixel_scale : 1;
    x /= (int)scale; y /= (int)scale;
    unsigned t = s->border.left / scale;
    int base = (int)(box.height / scale) - (int)t;
    /* The approved default owns a native 12px cap and 53px edge tiles.
     * These are final pixels, not the old 8px artwork stretched at runtime. */
    if (t == 12) {
        if ((unsigned)x < 12 && y >= base) return moonstone_native12[y - base][x];
        if ((unsigned)x < 12) {
            if (x < 2) return 's';
            if (x == 2) return 'h';
            if (x == 11) return 'd';
            if (y % 53 == 44) return 's';
            if ((y % 53 == 45 || y % 53 == 46) && (x == 3 || x == 4)) return 'm';
            return 'f';
        }
        if (y >= base) {
            unsigned row = (unsigned)(y - base), joint = ((unsigned)x - 12) % 53;
            if (row < 2) return 'h';
            if (row == 2) return 'm';
            if (row == 11) return 's';
            if (joint == 32) return 's';
            if ((joint == 33 || joint == 34) && (row == 3 || row == 4)) return 'm';
            return 'f';
        }
        return 0;
    }
    if ((unsigned)x < t && y >= base) {
        unsigned cx = minimum(7, (unsigned)x * 8 / t);
        unsigned cy = minimum(7, (unsigned)(y - base) * 8 / t);
        return corner[cy][cx];
    }
    if ((unsigned)x < t) {
        if (!x) return 's';
        if (x == 1) return 'h';
        if ((unsigned)x == t - 1) return 'd';
        if (y % 35 == 29) return 's';
        if (y % 35 == 30 && x == 2) return 'm';
        return 'f';
    }
    if (y >= base) {
        unsigned row = (unsigned)(y - base);
        if (!row) return 'h';
        if (row == 1) return 'm';
        if (row == t - 1) return 's';
        unsigned joint = ((unsigned)x - t) % 35;
        if (joint == 21) return 's';
        if (joint == 22 && row == 2) return 'm';
        return 'f';
    }
    return 0;
}
static uint32_t frame_color(const struct mp_pixel_frame *f, char pixel)
{
    switch (pixel) {
    case 'h': return f->highlight;
    case 'l': return f->light;
    case 'm': return f->mid;
    case 'f': return f->face;
    case 'd': return f->recess;
    default: return f->shadow;
    }
}
static int box_contains(const struct mp_box_style *s, int x, int y, struct mp_ui_rect box)
{
    return inside(x, y, box, s->radius) &&
        (s->frame.art != MP_FRAME_MOONSTONE || moonstone_pixel(s, x, y, box) != '.');
}
static int transparent_plain(const struct mp_box_style *s)
{
    return s->background_alpha==0 && s->frame.art==MP_FRAME_NONE &&
        !s->border.top && !s->border.right && !s->border.bottom && !s->border.left;
}
int mp_box_can_index_paint(const struct mp_box_style *s)
{
    return mp_box_style_valid(s) && (s->background_alpha == 255 || transparent_plain(s)) && s->opacity == 255 &&
        ((!s->border.top && !s->border.right && !s->border.bottom && !s->border.left) || s->border_alpha == 255) &&
        !s->radius.top_left && !s->radius.top_right && !s->radius.bottom_left && !s->radius.bottom_right &&
        s->border_style == MP_BORDER_SOLID && !s->shadow.alpha && !s->blur && !s->backdrop_blur &&
        s->blend_mode == MP_BLEND_NORMAL;
}
int mp_box_index_paint(const struct mp_box_style *s, const struct mp_font_surface *surface,
                       struct mp_ui_rect box, const struct mp_ui_index_surface *content,
                       const uint32_t *palette, unsigned palette_size, struct mp_ui_rect region,
                       struct mp_box_cache *cache, int scene_valid)
{
    if (!mp_box_can_index_paint(s) || box.width > 4096 || box.height > 4096) return -1;
    struct mp_ui_rect clipped = intersect(box, (struct mp_ui_rect){0, 0, surface->width, surface->height});
    if (!scene_valid || !cache->valid || !same_box(clipped, cache->bounds))
        region = (struct mp_ui_rect){0, 0, box.width, box.height};
    if (mp_box_cache_prepare(cache, surface, box, scene_valid)) return -1;
    if (!box.width || !box.height) return 0;
    region = intersect(region, (struct mp_ui_rect){0, 0, box.width, box.height});
    if (transparent_plain(s)) {
        /* Opaque indexed icons over a transparent box need no blend workspace.
         * Follow contiguous scanout Y, including negative portrait X strides. */
        for (unsigned x=0;x<region.width;x++) for(unsigned y=0;y<region.height;y++) {
            unsigned cx=(unsigned)region.x+x,cy=(unsigned)region.y+y;
            int dx=box.x+(int)cx,dy=box.y+(int)cy;
            if(dx<0||dy<0||(unsigned)dx>=surface->width||(unsigned)dy>=surface->height)continue;
            unsigned index=cx<content->width&&cy<content->height?content->pixels[cy*content->stride+cx]:0;
            uint32_t color=index&&index<palette_size?palette[index]:
                cache->backdrop[(unsigned)(dy-clipped.y)*clipped.width+(unsigned)(dx-clipped.x)];
            surface->pixels[(ptrdiff_t)dx*surface->x_stride+(ptrdiff_t)dy*surface->y_stride]=color;
        }
        return 0;
    }
    if (mp_box_is_opaque_plain(s) && content->width == box.width && content->height == box.height) {
        struct mp_ui_index_surface source = {content->pixels + (unsigned)region.y * content->stride + (unsigned)region.x,
            region.width, region.height, content->stride};
        mp_ui_palette_paint(surface, box.x + region.x, box.y + region.y, &source, palette, palette_size, s->background);
        return 0;
    }
    for (unsigned y = 0; y < region.height; ++y) for (unsigned x = 0; x < region.width; ++x) {
        unsigned cx = (unsigned)region.x + x, cy = (unsigned)region.y + y;
        int dx = box.x + (int)cx, dy = box.y + (int)cy;
        if (dx < 0 || dy < 0 || (unsigned)dx >= surface->width || (unsigned)dy >= surface->height) continue;
        unsigned index = cx < content->width && cy < content->height ? content->pixels[cy * content->stride + cx] : 0;
        char pixel = s->frame.art == MP_FRAME_MOONSTONE ? moonstone_pixel(s, dx, dy, box) : 0;
        uint32_t color = s->background;
        if (index && index < palette_size && (!s->clip_children || pixel != '.')) color = palette[index];
        else if (pixel == '.') color = cache->backdrop[(unsigned)(dy - clipped.y) * clipped.width + (unsigned)(dx - clipped.x)] & 0xffffff;
        else if (pixel) color = frame_color(&s->frame, pixel);
        else if (s->frame.art == MP_FRAME_NONE && (cx < s->border.left || cy < s->border.top ||
            (int64_t)cx >= (int64_t)box.width - s->border.right || (int64_t)cy >= (int64_t)box.height - s->border.bottom)) color = s->border_color;
        surface->pixels[(ptrdiff_t)dx * surface->x_stride + (ptrdiff_t)dy * surface->y_stride] = color;
    }
    return 0;
}
int mp_box_paint(const struct mp_box_style *s, const struct mp_font_surface *surface,
                 struct mp_ui_rect box, const struct mp_ui_index_surface *content,
                 const uint32_t *palette, unsigned palette_size, struct mp_box_cache *cache,
                 struct mp_box_workspace *workspace, int scene_valid)
{
    if (!mp_box_style_valid(s) || box.width > 4096 || box.height > 4096) return -1;
    struct mp_ui_rect bounds = intersect(mp_box_paint_bounds(s, box), (struct mp_ui_rect){0, 0, surface->width, surface->height});
    size_t area = (size_t)bounds.width * bounds.height;
    if (area > 1024 * 1024) return -1;
    if (area > workspace->capacity) {
        size_t capacity = workspace->capacity ? workspace->capacity : 1024;
        while (capacity < area) capacity *= 2;
        uint32_t *p = realloc(workspace->storage, capacity * sizeof *p * 4);
        if (!p) return -1;
        workspace->storage = p; workspace->capacity = capacity;
    }
    if (mp_box_cache_prepare(cache, surface, bounds, scene_valid)) return -1;
    if (!area) return 0;
    uint32_t *layer = workspace->storage, *temp = layer + area, *art = temp + area, *back = art + area;
    memset(layer, 0, area * sizeof *layer); memset(art, 0, area * sizeof *art);
    int sw = (int)box.width + 2 * s->shadow.spread, sh = (int)box.height + 2 * s->shadow.spread;
    struct mp_ui_rect shadow = {box.x + s->shadow.x - s->shadow.spread, box.y + s->shadow.y - s->shadow.spread,
        sw > 0 ? (unsigned)sw : 0, sh > 0 ? (unsigned)sh : 0};
    struct mp_ui_corners sr = s->radius;
    unsigned *corner[] = {&sr.top_left, &sr.top_right, &sr.bottom_right, &sr.bottom_left};
    for (unsigned i = 0; i < 4; ++i) *corner[i] = (int)*corner[i] + s->shadow.spread > 0 ? (unsigned)((int)*corner[i] + s->shadow.spread) : 0;
    if (s->shadow.alpha) {
        for (unsigned y = 0; y < bounds.height; ++y) for (unsigned x = 0; x < bounds.width; ++x)
            if (inside(bounds.x + (int)x, bounds.y + (int)y, shadow, sr))
                layer[y * bounds.width + x] = premultiply(s->shadow.color, s->shadow.alpha);
        box_blur(layer, temp, bounds.width, bounds.height, s->shadow.blur);
    }
    struct mp_ui_rect inner = {box.x + (int)s->border.left, box.y + (int)s->border.top,
        box.width > s->border.left + s->border.right ? box.width - s->border.left - s->border.right : 0,
        box.height > s->border.top + s->border.bottom ? box.height - s->border.top - s->border.bottom : 0};
    struct mp_ui_corners ir = s->radius;
    ir.top_left -= minimum(ir.top_left, maximum(s->border.left, s->border.top));
    ir.top_right -= minimum(ir.top_right, maximum(s->border.right, s->border.top));
    ir.bottom_left -= minimum(ir.bottom_left, maximum(s->border.left, s->border.bottom));
    ir.bottom_right -= minimum(ir.bottom_right, maximum(s->border.right, s->border.bottom));
    for (unsigned y = 0; y < bounds.height; ++y) for (unsigned x = 0; x < bounds.width; ++x) {
        int dx = bounds.x + (int)x, dy = bounds.y + (int)y; size_t at = (size_t)y * bounds.width + x;
        if (box_contains(s, dx, dy, box)) {
            layer[at] = over(layer[at], premultiply(s->background, s->background_alpha));
            if (s->frame.art == MP_FRAME_MOONSTONE) {
                char pixel = moonstone_pixel(s, dx, dy, box);
                if (pixel) layer[at] = over(layer[at], premultiply(frame_color(&s->frame, pixel), s->border_alpha));
            } else {
                int mark = s->border_style == MP_BORDER_SOLID ||
                    (s->border_style == MP_BORDER_DASHED ? ((dx / (int)(s->pixel_scale ? s->pixel_scale : 1) + dy / (int)(s->pixel_scale ? s->pixel_scale : 1)) & 7) < 4 :
                    ((dx / (int)(s->pixel_scale ? s->pixel_scale : 1) + dy / (int)(s->pixel_scale ? s->pixel_scale : 1)) & 1) == 0);
                if (mark && !inside(dx, dy, inner, ir)) layer[at] = over(layer[at], premultiply(s->border_color, s->border_alpha));
            }
        }
        int cx = dx - box.x, cy = dy - box.y;
        if (cx >= 0 && cy >= 0 && (unsigned)cx < content->width && (unsigned)cy < content->height) {
            unsigned index = content->pixels[(unsigned)cy * content->stride + (unsigned)cx];
            if (index && index < palette_size) art[at] = 0xff000000 | palette[index];
        }
    }
    box_blur(art, temp, bounds.width, bounds.height, s->blur);
    if (s->backdrop_blur) {
        for (size_t at = 0; at < area; ++at) back[at] = 0xff000000 | cache->backdrop[at];
        box_blur(back, temp, bounds.width, bounds.height, s->backdrop_blur);
    }
    for (unsigned y = 0; y < bounds.height; ++y) for (unsigned x = 0; x < bounds.width; ++x) {
        size_t at = (size_t)y * bounds.width + x; int dx = bounds.x + (int)x, dy = bounds.y + (int)y;
        int in = box_contains(s, dx, dy, box);
        if (!s->clip_children || in) layer[at] = over(layer[at], art[at]);
        uint32_t original = cache->backdrop[at], base = s->backdrop_blur && in ? back[at] : original;
        unsigned a = layer[at] >> 24; uint32_t output = 0;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            unsigned d = base >> shift & 255, source = a ? minimum(255, ((layer[at] >> shift & 255) * 255 + a / 2) / a) : 0;
            unsigned result = (d * (255 - a) + blend(d, source, s->blend_mode) * a + 127) / 255;
            result = ((original >> shift & 255) * (255 - s->opacity) + result * s->opacity + 127) / 255;
            output |= result << shift;
        }
        surface->pixels[(ptrdiff_t)dx * surface->x_stride + (ptrdiff_t)dy * surface->y_stride] = output;
    }
    return 0;
}
