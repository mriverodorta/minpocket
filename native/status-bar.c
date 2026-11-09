// SPDX-License-Identifier: GPL-2.0-only
#include "status-bar.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

static int valid_id(const char *id);
static int valid_order(const struct mp_theme *theme)
{
    if (theme->status.indicator_order_count > MP_THEME_STATUS_ORDER_LIMIT) return 0;
    for (unsigned i = 0; i < theme->status.indicator_order_count; ++i) {
        if (!valid_id(theme->status.indicator_order[i])) return 0;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(theme->status.indicator_order[i], theme->status.indicator_order[j])) return 0;
    }
    return 1;
}

static unsigned pixel_scale(const struct mp_theme *theme)
{ return theme->status.pixel_scale ? theme->status.pixel_scale : 1; }

static struct mp_box_style scaled_style(const struct mp_theme *theme)
{
    struct mp_box_style style = theme->status.box;
    unsigned scale = pixel_scale(theme);
    style.pixel_scale = scale;
    style.border.top *= scale; style.border.right *= scale;
    style.border.bottom *= scale; style.border.left *= scale;
    style.radius.top_left *= scale; style.radius.top_right *= scale;
    style.radius.bottom_left *= scale; style.radius.bottom_right *= scale;
    style.shadow.x *= (int)scale; style.shadow.y *= (int)scale;
    style.shadow.spread *= (int)scale; style.shadow.blur *= scale;
    style.blur *= scale; style.backdrop_blur *= scale;
    return style;
}
static struct mp_ui_rect scaled_rect(struct mp_ui_rect r, unsigned scale)
{
    return (struct mp_ui_rect){r.x * (int)scale, r.y * (int)scale, r.width * scale, r.height * scale};
}
struct mp_ui_rect mp_status_bar_screen_bounds(const struct mp_status_bar *bar)
{ return scaled_rect(bar->bounds, pixel_scale(&bar->theme)); }

static int valid_theme(const struct mp_theme *theme, unsigned width, unsigned height)
{
    if (!theme || !valid_order(theme)) return 0;
    struct mp_layout_style content_layout;
    return !mp_box_layout(&theme->status.box, &theme->status.layout, &content_layout) &&
        theme->status.pixel_scale <= 2 && !theme->status.box.pixel_scale &&
        width >= pixel_scale(theme) && width <= INT_MAX && height >= pixel_scale(theme) && height <= INT_MAX &&
        (theme->status.icon_height == 20 || theme->status.icon_height == 30) &&
        theme->status.text_height && theme->status.text_height <= theme->status.icon_height &&
        theme->status.text_gap <= 64 && theme->status.transition_ms <= 2000 &&
        (unsigned)theme->status.transition_easing <= MP_EASE_IN_OUT_CUBIC &&
        (theme->status.visible == 0 || theme->status.visible == 1) &&
        (theme->status.animations == 0 || theme->status.animations == 1) &&
        (theme->status.collapse_hidden == 0 || theme->status.collapse_hidden == 1) &&
        mp_box_style_valid(&theme->status.box) && mp_layout_style_valid(&theme->status.layout) &&
        theme->status.layout.max_width <= MP_STATUS_MAX_WIDTH &&
        theme->status.layout.max_height <= MP_STATUS_MAX_HEIGHT;
}
static int valid_metrics(const struct mp_indicator_metrics *metrics, const struct mp_theme *theme)
{
    (void)theme;
    return metrics->height && metrics->height <= 36 &&
        metrics->max_width && metrics->max_width <= MP_STATUS_MAX_WIDTH && metrics->width <= metrics->max_width;
}
static int reserved_bounds(const struct mp_status_bar *bar, const struct mp_theme *theme,
                            const struct mp_indicator_metrics *metrics, unsigned count, struct mp_ui_rect *bounds)
{
    if (!count) { *bounds = (struct mp_ui_rect){0}; return 1; }
    struct mp_layout_style effective;
    if (mp_box_layout(&theme->status.box, &theme->status.layout, &effective)) return 0;
    const struct mp_layout_style *layout = &effective;
    unsigned main = (count - 1) * layout->gap, cross = 0;
    for (unsigned i = 0; i < count; ++i) {
        unsigned outer=metrics[i].height+(i<bar->count?bar->items[i].margin_top+bar->items[i].margin_bottom:0);
        unsigned m = layout->orientation == MP_HORIZONTAL ? metrics[i].max_width : outer;
        unsigned c = layout->orientation == MP_HORIZONTAL ? outer : metrics[i].max_width;
        main += m; if (c > cross) cross = c;
    }
    return !mp_layout_bounds(layout, layout->orientation == MP_HORIZONTAL ? main : cross,
        layout->orientation == MP_HORIZONTAL ? cross : main, bar->viewport_width / pixel_scale(theme), bar->viewport_height / pixel_scale(theme), bounds) &&
        bounds->width <= MP_STATUS_MAX_WIDTH && bounds->height <= MP_STATUS_MAX_HEIGHT;
}
static int ensure_pixels(struct mp_status_bar *bar, struct mp_ui_rect bounds)
{
    size_t size = (size_t)bounds.width * bounds.height;
    if (size <= MP_STATUS_INLINE_PIXELS || size <= bar->large_capacity) return 1;
    uint8_t *pixels = realloc(bar->large_pixels, 2 * size);
    if (!pixels) return 0;
    bar->large_pixels = pixels; bar->large_capacity = size;
    return 1;
}
static uint8_t *raster_pixels(struct mp_status_bar *bar)
{
    return (size_t)bar->bounds.width * bar->bounds.height > MP_STATUS_INLINE_PIXELS ? bar->large_pixels : bar->pixels;
}
static int find(const struct mp_status_bar *bar, const char *id)
{
    if (!id) return -1;
    for (unsigned i = 0; i < bar->count; ++i)
        if (!strcmp(bar->items[i].indicator.id, id)) return (int)i;
    return -1;
}

static int valid_id(const char *id)
{
    if (!id || !*id) return 0;
    unsigned size = 0;
    for (; size <= MP_STATUS_ID_LENGTH && id[size]; ++size) {
        char c = id[size];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return size <= MP_STATUS_ID_LENGTH;
}

static void apply_order(struct mp_status_bar *bar)
{
    unsigned position = 0;
    for (unsigned i = 0; i < bar->theme.status.indicator_order_count; ++i) {
        const char *id = bar->theme.status.indicator_order[i];
        if (find(bar, id) >= 0) (void)mp_status_bar_move(bar, id, position++);
    }
    /* Unlisted providers retain their relative order after the named slots.
     * Reordering never resets provider contexts or availability timestamps. */
}

int mp_status_bar_set_theme(struct mp_status_bar *bar, const struct mp_theme *theme)
{
    if (!valid_theme(theme, bar->viewport_width, bar->viewport_height)) return MP_STATUS_INVALID;
    struct mp_indicator_metrics metrics[MP_STATUS_MAX_INDICATORS];
    for (unsigned i = 0; i < bar->count; ++i) {
        const struct mp_status_indicator *indicator = &bar->items[i].indicator;
        if (indicator->ops->measure(indicator->context, theme, &metrics[i]) || !valid_metrics(&metrics[i], theme))
            return MP_STATUS_INVALID;
    }
    struct mp_ui_rect bounds;
    if (!reserved_bounds(bar, theme, metrics, bar->count, &bounds)) return MP_STATUS_OVERFLOW;
    if (!ensure_pixels(bar, bounds)) return MP_STATUS_NOMEM;
    bar->theme = *theme;
    memcpy(bar->palette, theme->icon_palette, sizeof theme->icon_palette);
    bar->palette[MP_THEME_STATUS_TEXT] = theme->colors.status_text;
    bar->palette[MP_THEME_DEBUG_TEXT] = MP_FONT_DEBUG_RED;
    for (unsigned i = 0; i < bar->count; ++i) {
        bar->items[i].metrics = metrics[i];
        bar->items[i].reserved_width = metrics[i].max_width;
        bar->items[i].reserved_height = metrics[i].height;
    }
    apply_order(bar);
    bar->dirty = 1;
    return MP_STATUS_OK;
}

int mp_status_bar_init(struct mp_status_bar *bar, const struct mp_theme *theme,
                       unsigned viewport_width, unsigned viewport_height)
{
    if (!valid_theme(theme, viewport_width, viewport_height)) return MP_STATUS_INVALID;
    memset(bar, 0, sizeof *bar);
    bar->viewport_width = viewport_width;
    bar->viewport_height = viewport_height;
    mp_ui_clock_begin(&bar->clock, 0, 1);
    return mp_status_bar_set_theme(bar, theme);
}

int mp_status_bar_register(struct mp_status_bar *bar, const struct mp_status_indicator *indicator,
                           unsigned position, uint64_t now_ms)
{
    if (!indicator || !valid_id(indicator->id) || !indicator->ops || !indicator->ops->measure ||
        !indicator->ops->tick || !indicator->ops->paint || position > bar->count) return MP_STATUS_INVALID;
    if (find(bar, indicator->id) >= 0) return MP_STATUS_DUPLICATE;
    if (bar->count == MP_STATUS_MAX_INDICATORS) return MP_STATUS_FULL;
    struct mp_indicator_metrics metrics;
    if (indicator->ops->measure(indicator->context, &bar->theme, &metrics) ||
        !valid_metrics(&metrics, &bar->theme)) return MP_STATUS_INVALID;
    struct mp_indicator_metrics all[MP_STATUS_MAX_INDICATORS];
    for (unsigned i = 0; i < bar->count; ++i) all[i] = bar->items[i].metrics, all[i].max_width = bar->items[i].reserved_width, all[i].height=bar->items[i].reserved_height;
    all[bar->count] = metrics;
    struct mp_ui_rect bounds;
    if (!reserved_bounds(bar, &bar->theme, all, bar->count + 1, &bounds)) return MP_STATUS_OVERFLOW;
    if (!ensure_pixels(bar, bounds)) return MP_STATUS_NOMEM;
    memmove(&bar->items[position + 1], &bar->items[position], (bar->count - position) * sizeof bar->items[0]);
    bar->items[position] = (struct mp_status_bar_item){.indicator = *indicator, .metrics = metrics,
        .reserved_width = metrics.max_width, .reserved_height=metrics.height, .registered_ms = now_ms, .enabled = 1};
    ++bar->count;
    apply_order(bar);
    bar->dirty = 1;
    return MP_STATUS_OK;
}

static void retire(struct mp_status_bar *bar, unsigned position)
{
    memmove(&bar->items[position], &bar->items[position + 1], (bar->count - position - 1) * sizeof bar->items[0]);
    --bar->count;
    memset(&bar->items[bar->count], 0, sizeof bar->items[0]);
    bar->dirty = 1;
}

int mp_status_bar_unregister(struct mp_status_bar *bar, const char *id, uint64_t now_ms)
{
    int position = find(bar, id);
    if (position < 0) return MP_STATUS_NOT_FOUND;
    struct mp_status_bar_item *item = &bar->items[position];
    if (!item->removing) { item->removing = 1; item->remove_ms = now_ms; }
    if (!item->extent) retire(bar, (unsigned)position);
    return MP_STATUS_OK;
}

int mp_status_bar_contains(const struct mp_status_bar *bar, const char *id)
{
    return find(bar, id) >= 0;
}

int mp_status_bar_move(struct mp_status_bar *bar, const char *id, unsigned position)
{
    int previous = find(bar, id);
    if (previous < 0) return MP_STATUS_NOT_FOUND;
    if (position >= bar->count) return MP_STATUS_INVALID;
    if ((unsigned)previous == position) return MP_STATUS_OK;
    struct mp_status_bar_item item = bar->items[previous];
    if ((unsigned)previous < position)
        memmove(&bar->items[previous], &bar->items[previous + 1], (position - (unsigned)previous) * sizeof item);
    else
        memmove(&bar->items[position + 1], &bar->items[position], ((unsigned)previous - position) * sizeof item);
    bar->items[position] = item;
    bar->dirty = 1;
    return MP_STATUS_OK;
}

int mp_status_bar_item_layout(const struct mp_status_bar *bar, const char *id, struct mp_ui_rect *box)
{
    int position = find(bar, id);
    if (position < 0) return MP_STATUS_NOT_FOUND;
    if (!box) return MP_STATUS_INVALID;
    *box = bar->items[position].box;
    return MP_STATUS_OK;
}

void mp_status_bar_dispose(struct mp_status_bar *bar)
{
    free(bar->large_pixels); free(bar->scaled_pixels); mp_box_workspace_dispose(&bar->effects);
    memset(bar, 0, sizeof *bar);
}
void mp_status_bar_cache_dispose(struct mp_status_bar_cache *cache)
{
    mp_box_cache_dispose(&cache->box); memset(cache, 0, sizeof *cache);
}
int mp_status_bar_set_item_margins(struct mp_status_bar *bar,const char *id,unsigned top,unsigned bottom)
{
 int at=find(bar,id);if(at<0)return MP_STATUS_NOT_FOUND;
 if(top>256||bottom>256)return MP_STATUS_INVALID;
 struct mp_status_bar_item *item=&bar->items[at];
 if(item->margin_top==top&&item->margin_bottom==bottom)return MP_STATUS_OK;
 unsigned previous_top=item->margin_top,previous_bottom=item->margin_bottom;
 item->margin_top=top;item->margin_bottom=bottom;
 struct mp_indicator_metrics all[MP_STATUS_MAX_INDICATORS];
 for(unsigned i=0;i<bar->count;i++){all[i]=bar->items[i].metrics;all[i].max_width=bar->items[i].reserved_width;all[i].height=bar->items[i].reserved_height;}
 struct mp_ui_rect bounds;int result=MP_STATUS_OK;
 if(!reserved_bounds(bar,&bar->theme,all,bar->count,&bounds))result=MP_STATUS_OVERFLOW;
 else if(!ensure_pixels(bar,bounds))result=MP_STATUS_NOMEM;
 if(result){item->margin_top=previous_top;item->margin_bottom=previous_bottom;return result;}
 bar->dirty=1;return MP_STATUS_OK;
}
int mp_status_bar_set_enabled(struct mp_status_bar *bar, const char *id, int enabled, uint64_t now_ms)
{
    int at = find(bar, id);
    if (at < 0) return MP_STATUS_NOT_FOUND;
    if (enabled != 0 && enabled != 1) return MP_STATUS_INVALID;
    if (bar->items[at].enabled != enabled) bar->items[at].enabled = enabled, bar->items[at].enabled_ms = now_ms;
    return MP_STATUS_OK;
}
static int rasterize(struct mp_status_bar *bar)
{
    uint8_t small_candidate[MP_STATUS_INLINE_PIXELS], child[MP_STATUS_MAX_WIDTH * 36];
    struct mp_ui_rect previous = bar->bounds;
    struct mp_ui_row_item row[MP_STATUS_MAX_INDICATORS];
    struct mp_ui_rect boxes[MP_STATUS_MAX_INDICATORS];
    struct mp_layout_style effective;
    if (mp_box_layout(&bar->theme.status.box, &bar->theme.status.layout, &effective)) return 0;
    const struct mp_layout_style *layout = &effective;
    int vertical = layout->orientation == MP_VERTICAL;
    unsigned main = 0, cross = 0;
    for (unsigned i = 0; i < bar->count; ++i) {
        struct mp_status_bar_item *item = &bar->items[i];
        unsigned gap = item->gap;
        unsigned before = item->extent < gap ? item->extent : gap;
        row[i] = vertical ? (struct mp_ui_row_item){item->metrics.width, item->extent - before, before} :
            (struct mp_ui_row_item){item->extent - before, item->metrics.height+item->margin_top+item->margin_bottom, before};
        main += item->extent;
        unsigned size = vertical ? item->metrics.width : item->metrics.height+item->margin_top+item->margin_bottom;
        if (item->extent && size > cross) cross = size;
    }
    /* Registration reserves the maximum raster budget. The visible window
     * follows current native sizes and animated extents inside that budget. */
    bar->bounds = (struct mp_ui_rect){0};
    if (main && bar->theme.status.visible && mp_layout_bounds(layout, vertical ? cross : main,
        vertical ? main : cross, bar->viewport_width / pixel_scale(&bar->theme), bar->viewport_height / pixel_scale(&bar->theme), &bar->bounds)) return 0;
    size_t size = (size_t)bar->bounds.width * bar->bounds.height;
    uint8_t *candidate = size > MP_STATUS_INLINE_PIXELS ? bar->large_pixels + bar->large_capacity : small_candidate;
    uint8_t *pixels = raster_pixels(bar);
    if (bar->bounds.width && bar->bounds.height) mp_layout_arrange(layout, bar->bounds, row, bar->count, boxes);
    else memset(boxes, 0, sizeof boxes);
    memset(candidate, 0, size);
    struct mp_ui_index_surface target = {candidate, bar->bounds.width, bar->bounds.height, bar->bounds.width};
    for (unsigned i = 0; i < bar->count; ++i) {
        struct mp_status_bar_item *item = &bar->items[i];
        item->box = boxes[i];
        if (!item->valid || !boxes[i].width || !boxes[i].height) continue;
        unsigned gap = item->gap;
        unsigned visible = item->paint_extent > gap ? item->paint_extent - gap : 0;
        unsigned w = vertical ? item->metrics.width : (visible < item->metrics.width ? visible : item->metrics.width);
        unsigned remaining=vertical?(visible>item->margin_top?visible-item->margin_top:0):item->metrics.height;
        unsigned h=remaining<item->metrics.height?remaining:item->metrics.height;
        if (!w || !h) continue;
        int left = boxes[i].x - bar->bounds.x, top = boxes[i].y - bar->bounds.y+(int)item->margin_top;
        if (left >= (int)layout->padding.left && top >= (int)layout->padding.top &&
            left + (int)w <= (int)target.width - (int)layout->padding.right &&
            top + (int)h <= (int)target.height - (int)layout->padding.bottom) {
            struct mp_ui_index_surface window = {target.pixels + (unsigned)top * target.stride + (unsigned)left,
                w, h, target.stride};
            item->indicator.ops->paint(item->indicator.context, &bar->theme, &window);
            continue;
        }
        memset(child, 0, w * h);
        struct mp_ui_index_surface window = {child, w, h, w};
        item->indicator.ops->paint(item->indicator.context, &bar->theme, &window);
        /* Padding stays empty even when a fixed-size theme clips overflow. */
        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
            int dx = left + (int)x, dy = top + (int)y;
            if (dx < (int)layout->padding.left || dy < (int)layout->padding.top ||
                dx >= (int)target.width - (int)layout->padding.right ||
                dy >= (int)target.height - (int)layout->padding.bottom) continue;
            target.pixels[(unsigned)dy * target.stride + (unsigned)dx] = child[y * w + x];
        }
    }
    ++bar->stats.raster_builds;
    int full = bar->dirty || previous.x != bar->bounds.x || previous.y != bar->bounds.y ||
        previous.width != bar->bounds.width || previous.height != bar->bounds.height;
    int changed = full || memcmp(candidate, pixels, size);
    if (changed) {
        struct mp_ui_rect damage = {0, 0, bar->bounds.width, bar->bounds.height};
        if (!full) {
            unsigned left = bar->bounds.width, top = bar->bounds.height, right = 0, bottom = 0;
            for (unsigned y = 0; y < bar->bounds.height; ++y) for (unsigned x = 0; x < bar->bounds.width; ++x)
                if (candidate[y * bar->bounds.width + x] != pixels[y * bar->bounds.width + x]) {
                    if (x < left) left = x;
                    if (y < top) top = y;
                    if (x + 1 > right) right = x + 1;
                    if (y + 1 > bottom) bottom = y + 1;
                }
            damage = (struct mp_ui_rect){(int)left, (int)top, right - left, bottom - top};
        }
        memcpy(pixels, candidate, size);
        ++bar->generation;
        unsigned at = (unsigned)(bar->generation % MP_STATUS_DAMAGE_HISTORY);
        bar->damage[at].generation = bar->generation; bar->damage[at].box = damage;
    }
    return changed;
}

int mp_status_bar_tick(struct mp_status_bar *bar, uint64_t now_ms, int animate)
{
    mp_ui_clock_begin(&bar->clock, now_ms, animate && bar->theme.status.animations && bar->theme.status.visible);
    int changed = bar->dirty;
    unsigned previous_phase = 0;
    for (unsigned i = 0; i < bar->count;) {
        struct mp_status_bar_item *item = &bar->items[i];
        struct mp_indicator_metrics metrics;
        int valid = !item->indicator.ops->measure(item->indicator.context, &bar->theme, &metrics) &&
            valid_metrics(&metrics, &bar->theme) && metrics.max_width <= item->reserved_width && metrics.height <= item->reserved_height;
        struct mp_indicator_frame frame = {0};
        struct mp_ui_clock child_clock;
        mp_ui_clock_begin(&child_clock, now_ms, bar->clock.animate);
        if (valid) item->indicator.ops->tick(item->indicator.context, &bar->theme, &child_clock, &frame);
        else metrics = item->metrics;
        int visible = valid && frame.visible && !item->removing && item->enabled && bar->theme.status.visible;
        struct mp_ui_clock event = bar->clock;
        uint64_t epoch = item->removing ? item->remove_ms : frame.visibility_ms;
        if (epoch < item->enabled_ms) epoch = item->enabled_ms;
        if (epoch < item->registered_ms) epoch = item->registered_ms;
        event.now_ms = epoch > now_ms ? now_ms : epoch;
        item->visibility.easing = bar->theme.status.transition_easing;
        mp_ui_motion_to(&item->visibility, visible ? 1000 : 0, bar->theme.status.transition_ms, &event);
        unsigned phase = (unsigned)mp_ui_motion_value(&item->visibility, &bar->clock);
        if (item->removing && !phase) { retire(bar, i); changed = 1; continue; }
        if (valid && (visible || phase) && bar->theme.status.visible)
            mp_ui_clock_request(&bar->clock, mp_ui_clock_wait(&child_clock, now_ms));
        unsigned gap = (bar->theme.status.layout.gap * previous_phase + 500) / 1000;
        unsigned main = bar->theme.status.layout.orientation == MP_VERTICAL ? metrics.height+item->margin_top+item->margin_bottom : metrics.width;
        unsigned paint_extent = valid ? ((main + gap) * phase + 500) / 1000 : 0;
        unsigned extent = bar->theme.status.collapse_hidden ? paint_extent : main + gap;
        if (item->valid != valid || item->extent != extent || item->paint_extent != paint_extent || item->gap != gap || item->frame.stamp != frame.stamp ||
            item->metrics.width != metrics.width || item->metrics.height != metrics.height) changed = 1;
        item->valid = valid;
        item->metrics = metrics;
        item->frame = frame;
        item->extent = extent; item->paint_extent = paint_extent; item->gap = gap;
        if (extent) previous_phase = bar->theme.status.collapse_hidden ? phase : 1000;
        mp_ui_motion_schedule(&item->visibility, &bar->clock);
        ++i;
    }
    if (!changed) return 0;
    int pixels_changed = rasterize(bar);
    bar->dirty = 0;
    return pixels_changed;
}

unsigned mp_status_bar_wait_ms(const struct mp_status_bar *bar, uint64_t now_ms)
{
    return mp_ui_clock_wait(&bar->clock, now_ms);
}

int mp_status_bar_paint(struct mp_status_bar *bar, const struct mp_font_surface *surface,
                        struct mp_status_bar_cache *cache)
{
    if (!bar->generation || (cache->valid && cache->generation == bar->generation)) return 0;
    unsigned scale = pixel_scale(&bar->theme);
    struct mp_box_style rendered_style = scaled_style(&bar->theme);
    const struct mp_box_style *style = &rendered_style;
    struct mp_ui_rect bounds = mp_status_bar_screen_bounds(bar);
    struct mp_ui_index_surface whole = {raster_pixels(bar), bar->bounds.width, bar->bounds.height, bar->bounds.width};
    if (scale > 1) {
        size_t size = (size_t)bounds.width * bounds.height;
        if (size > bar->scaled_capacity) {
            size_t capacity = bar->scaled_capacity ? bar->scaled_capacity : 1024;
            while (capacity < size) capacity *= 2;
            uint8_t *pixels = realloc(bar->scaled_pixels, capacity);
            if (!pixels) return MP_STATUS_NOMEM;
            bar->scaled_pixels = pixels; bar->scaled_capacity = capacity;
        }
        if (bar->scaled_generation != bar->generation) {
            for (unsigned y = 0; y < bounds.height; ++y)
                for (unsigned x = 0; x < bounds.width; ++x)
                    bar->scaled_pixels[(size_t)y * bounds.width + x] =
                        whole.pixels[(y / scale) * whole.stride + x / scale];
            bar->scaled_generation = bar->generation;
        }
        whole = (struct mp_ui_index_surface){bar->scaled_pixels, bounds.width, bounds.height, bounds.width};
    }
    if (!mp_box_can_index_paint(style)) {
        if (mp_box_paint(style, surface, bounds, &whole, bar->palette, MP_THEME_COLORS,
            &cache->box, &bar->effects, cache->valid)) return MP_STATUS_NOMEM;
        cache->valid = 1; cache->generation = bar->generation; cache->bounds = bounds;
        ++bar->stats.paints; return 1;
    }
    struct mp_ui_rect box = {0, 0, bounds.width, bounds.height};
    if (cache->valid && cache->bounds.x == bounds.x && cache->bounds.y == bounds.y &&
        cache->bounds.width == bounds.width && cache->bounds.height == bounds.height &&
        cache->generation < bar->generation && bar->generation - cache->generation <= MP_STATUS_DAMAGE_HISTORY) {
        box = (struct mp_ui_rect){0};
        for (uint64_t generation = cache->generation + 1; generation <= bar->generation; ++generation) {
            unsigned at = (unsigned)(generation % MP_STATUS_DAMAGE_HISTORY);
            if (bar->damage[at].generation != generation) {
                box = (struct mp_ui_rect){0, 0, bounds.width, bounds.height};
                break;
            }
            box = mp_ui_rect_union(box, scaled_rect(bar->damage[at].box, scale));
        }
    }
    if (mp_box_index_paint(style, surface, bounds, &whole, bar->palette, MP_THEME_COLORS, box,
        &cache->box, cache->valid)) return MP_STATUS_NOMEM;
    cache->valid = 1; cache->generation = bar->generation; cache->bounds = bounds;
    ++bar->stats.paints;
    return 1;
}
