// SPDX-License-Identifier: GPL-2.0-only
#include "battery.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned text_width(unsigned height, unsigned characters)
{
    return (MP_FONT_WIDTH * height + 3) / MP_FONT_CAP_HEIGHT +
        (characters - 1) * ((MP_FONT_ADVANCE * height + 3) / MP_FONT_CAP_HEIGHT);
}

void mp_battery_indicator_set_text_height(struct mp_battery_indicator *s,unsigned height)
{s->text_height_override=height>=14&&height<=30?height:0;}
void mp_battery_indicator_set_text_selected(struct mp_battery_indicator *s,int selected)
{s->text_selected=!!selected;}
static unsigned text_height(const struct mp_battery_indicator *s,const struct mp_theme *theme)
{return s->text_height_override?s->text_height_override:theme->status.text_height;}
void mp_battery_indicator_init(struct mp_battery_indicator *indicator)
{
    memset(indicator, 0, sizeof *indicator);
    indicator->small_level = indicator->label_level = INT_MIN;
    indicator->sample = (struct mp_battery_reading){-1, MP_BATTERY_UNKNOWN};
    mp_battery_init(&indicator->battery);
}

void mp_battery_indicator_update(struct mp_battery_indicator *indicator,
                                 struct mp_battery_reading reading, uint64_t now_ms)
{
    int ready = reading.level >= 0 && reading.level <= 100;
    if (ready != indicator->ready) indicator->visibility_ms = now_ms;
    indicator->ready = ready;
    indicator->sample = ready ? reading : (struct mp_battery_reading){-1, MP_BATTERY_UNKNOWN};
    if (!ready) reading = (struct mp_battery_reading){indicator->battery.reading.level, MP_BATTERY_UNKNOWN};
    mp_battery_update(&indicator->battery, reading, now_ms);
}

static int measure(const void *context, const struct mp_theme *theme, struct mp_indicator_metrics *metrics)
{
    const struct mp_battery_indicator *indicator = context;
    int level = indicator->battery.reading.level;
    unsigned height=text_height(indicator,theme),maximum=indicator->text_height_override?30:height;
    unsigned art_width = theme->status.icon_height == 30 ? MP_BATTERY_NATIVE_WIDTH : MP_BATTERY_SMALL_WIDTH;
    unsigned width = level >= 0 && level <= 100 ?
        text_width(height, level == 100 ? 4 : level >= 10 ? 3 : 2) +
        theme->status.text_gap + art_width : 0;
    *metrics = (struct mp_indicator_metrics){width, theme->status.icon_height,
        text_width(maximum, 4) + theme->status.text_gap + art_width};
    return 0;
}

static void prepare(struct mp_battery_indicator *indicator, const struct mp_theme *theme)
{
    if (indicator->small_level != indicator->battery.reading.level || indicator->art_height != theme->status.icon_height) {
        if (theme->status.icon_height == 30) {
            uint8_t built[MP_BATTERY_MAX_FRAMES] = {1};
            mp_battery_native_art(indicator->battery.reading.level, 100, indicator->small_frames[0]);
            for (unsigned p = 1; p <= indicator->battery.width + 10; ++p) {
                unsigned frame = indicator->battery.positions[p];
                if (built[frame]) continue;
                /* Preserve the approved animation clock/phase mapping while
                 * drawing native fill columns directly. No image sampling. */
                int phase = ((int)p - 1 - 10) * 46;
                int start = phase < 0 ? -((-phase + 11) / 22) : (phase + 11) / 22;
                mp_battery_native_art(indicator->battery.reading.level, start, indicator->small_frames[frame]);
                built[frame] = 1;
            }
        } else {
            for (unsigned i = 0; i < indicator->battery.frame_count; ++i)
                mp_battery_small_art(indicator->battery.frames[i], indicator->small_frames[i]);
        }
        indicator->small_level = indicator->battery.reading.level;
        indicator->art_height = theme->status.icon_height;
        ++indicator->stats.battery_builds;
    }
    unsigned height = text_height(indicator,theme);
    if (indicator->label_level == indicator->battery.reading.level && indicator->label_height == height && indicator->cached_selected == indicator->text_selected) return;
    indicator->label_level = indicator->battery.reading.level;
    indicator->label_height = height;
    indicator->cached_selected = indicator->text_selected;
    ++indicator->stats.label_builds;
    memset(indicator->percentage, 0, sizeof indicator->percentage);
    if (indicator->battery.reading.level < 0 || indicator->battery.reading.level > 100) {
        indicator->percentage_width = 0;
        return;
    }
    char label[8];
    snprintf(label, sizeof label, "%d%%", indicator->battery.reading.level);
    unsigned width = (MP_FONT_WIDTH * height + 3) / MP_FONT_CAP_HEIGHT;
    unsigned advance = (MP_FONT_ADVANCE * height + 3) / MP_FONT_CAP_HEIGHT;
    indicator->percentage_width = text_width(height, (unsigned)strlen(label));
    for (unsigned i = 0; label[i]; ++i) {
        const uint8_t *glyph = mp_font_glyph((unsigned char)label[i]);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                if (glyph[MP_FONT_CAP_TOP + y * MP_FONT_CAP_HEIGHT / height] &
                    (1u << (x * MP_FONT_WIDTH / width)))
                    indicator->percentage[y * indicator->percentage_width + i * advance + x] = indicator->text_selected ? MP_THEME_DEBUG_TEXT : MP_THEME_STATUS_TEXT;
    }
}

static void tick(void *context, const struct mp_theme *theme, struct mp_ui_clock *clock,
                 struct mp_indicator_frame *frame)
{
    struct mp_battery_indicator *indicator = context;
    prepare(indicator, theme);
    indicator->frame = mp_battery_frame(&indicator->battery, clock->now_ms, clock->animate);
    mp_ui_clock_request(clock, mp_battery_wait_ms(&indicator->battery, clock->now_ms, clock->animate));
    *frame = (struct mp_indicator_frame){.visible = indicator->ready,
        .stamp = ((uint64_t)indicator->battery.revision << 32) | ((uint64_t)indicator->stats.label_builds << 8) | indicator->frame,
        .visibility_ms = indicator->visibility_ms};
}

static void paint(const void *context, const struct mp_theme *theme, struct mp_ui_index_surface *window)
{
    const struct mp_battery_indicator *indicator = context;
    mp_ui_index_sprite(window, 0, 0, indicator->percentage, indicator->percentage_width,
        indicator->label_height, indicator->percentage_width);
    if (indicator->percentage_width)
        mp_ui_index_sprite(window, (int)(indicator->percentage_width + theme->status.text_gap), 0,
            indicator->small_frames[indicator->frame], theme->status.icon_height == 30 ? MP_BATTERY_NATIVE_WIDTH : MP_BATTERY_SMALL_WIDTH,
            theme->status.icon_height, theme->status.icon_height == 30 ? MP_BATTERY_NATIVE_WIDTH : MP_BATTERY_SMALL_WIDTH);
}

const struct mp_status_indicator_ops mp_battery_indicator_ops = {measure, tick, paint};
