// SPDX-License-Identifier: GPL-2.0-only
#include "clock.h"
#include <string.h>

static unsigned text_width(unsigned height)
{ return (MP_FONT_WIDTH * height + 3) / MP_FONT_CAP_HEIGHT + 4 * ((MP_FONT_ADVANCE * height + 3) / MP_FONT_CAP_HEIGHT); }
void mp_clock_indicator_set_text_height(struct mp_clock_indicator *s,unsigned height)
{s->text_height_override=height>=14&&height<=30?height:0;}
void mp_clock_indicator_set_text_selected(struct mp_clock_indicator *s,int selected)
{s->text_selected=!!selected;}
static unsigned text_height(const struct mp_clock_indicator *s,const struct mp_theme *theme)
{return s->text_height_override?s->text_height_override:theme->status.text_height;}
void mp_clock_indicator_init(struct mp_clock_indicator *indicator)
{
    memset(indicator, 0, sizeof *indicator);
    indicator->hour = indicator->minute = indicator->cached_hour = indicator->cached_minute = -1;
}
void mp_clock_indicator_update(struct mp_clock_indicator *indicator, int hour, int minute,
    unsigned next_minute_ms, uint64_t now_ms)
{
    int ready = hour >= 0 && hour < 24 && minute >= 0 && minute < 60;
    if (ready != indicator->ready) indicator->visibility_ms = now_ms;
    indicator->ready = ready;
    if (!ready) return;
    indicator->hour = hour; indicator->minute = minute;
    unsigned delay = !next_minute_ms ? 1 : next_minute_ms > 60000 ? 60000 : next_minute_ms;
    indicator->next_minute_ms = UINT64_MAX - now_ms < delay ? UINT64_MAX : now_ms + delay;
}
static int measure(const void *context, const struct mp_theme *theme, struct mp_indicator_metrics *metrics)
{
    const struct mp_clock_indicator *indicator = context;
    unsigned height=text_height(indicator,theme),maximum=indicator->text_height_override?30:height;
    unsigned width = text_width(height);
    if (width > MP_CLOCK_TEXT_WIDTH || height > MP_CLOCK_TEXT_HEIGHT) return -1;
    *metrics = (struct mp_indicator_metrics){indicator->hour >= 0 ? width : 0, theme->status.icon_height, text_width(maximum)};
    return 0;
}
static void tick(void *context, const struct mp_theme *theme, struct mp_ui_clock *clock,
    struct mp_indicator_frame *frame)
{
    struct mp_clock_indicator *indicator = context;
    unsigned height = text_height(indicator,theme);
    if (indicator->hour >= 0 && (indicator->hour != indicator->cached_hour ||
        indicator->minute != indicator->cached_minute || height != indicator->text_height || indicator->cached_selected != indicator->text_selected)) {
        indicator->label[0] = (char)('0' + indicator->hour / 10);
        indicator->label[1] = (char)('0' + indicator->hour % 10);
        indicator->label[2] = ':';
        indicator->label[3] = (char)('0' + indicator->minute / 10);
        indicator->label[4] = (char)('0' + indicator->minute % 10);
        indicator->label[5] = 0;
        indicator->cached_hour = indicator->hour; indicator->cached_minute = indicator->minute;
        indicator->cached_selected = indicator->text_selected;
        indicator->text_height = height; indicator->text_width = text_width(height);
        memset(indicator->pixels, 0, sizeof indicator->pixels);
        unsigned width = (MP_FONT_WIDTH * height + 3) / MP_FONT_CAP_HEIGHT;
        unsigned advance = (MP_FONT_ADVANCE * height + 3) / MP_FONT_CAP_HEIGHT;
        for (unsigned i = 0; i < 5; ++i) {
            const uint8_t *glyph = mp_font_glyph((unsigned char)indicator->label[i]);
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
                if (glyph[MP_FONT_CAP_TOP + y * MP_FONT_CAP_HEIGHT / height] & (1u << (x * MP_FONT_WIDTH / width)))
                    indicator->pixels[y * indicator->text_width + i * advance + x] = indicator->text_selected ? MP_THEME_DEBUG_TEXT : MP_THEME_STATUS_TEXT;
        }
        ++indicator->stats.label_builds;
    }
    if (indicator->ready) {
        uint64_t remaining = indicator->next_minute_ms > clock->now_ms ? indicator->next_minute_ms - clock->now_ms : 1000;
        mp_ui_clock_request(clock, remaining > 60000 ? 60000 : (unsigned)remaining);
    }
    *frame = (struct mp_indicator_frame){indicator->ready, indicator->stats.label_builds, indicator->visibility_ms};
}
static void paint(const void *context, const struct mp_theme *theme, struct mp_ui_index_surface *window)
{
    (void)theme;
    const struct mp_clock_indicator *indicator = context;
    mp_ui_index_sprite(window, 0, 0, indicator->pixels, indicator->text_width, indicator->text_height, indicator->text_width);
}
const struct mp_status_indicator_ops mp_clock_indicator_ops = {measure, tick, paint};
