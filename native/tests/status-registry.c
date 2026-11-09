// SPDX-License-Identifier: GPL-2.0-only
/* Independent providers: this test links no battery, Wi-Fi or service code. */
#include "status-bar.h"
#include "status-theme.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct mp_theme test_theme;

struct example {
    unsigned width, height, maximum, ticks, paints, revision;
    int visible, animated, fail;
    uint64_t visibility_ms;
    uint8_t color;
};
static int measure(const void *context, const struct mp_theme *theme, struct mp_indicator_metrics *metrics)
{
    (void)theme;
    const struct example *example = context;
    *metrics = (struct mp_indicator_metrics){example->width, example->height, example->maximum};
    return example->fail;
}
static void tick(void *context, const struct mp_theme *theme, struct mp_ui_clock *clock,
                 struct mp_indicator_frame *frame)
{
    (void)theme;
    struct example *example = context;
    ++example->ticks;
    *frame = (struct mp_indicator_frame){.visible = example->visible, .visibility_ms = example->visibility_ms,
        .stamp = ((uint64_t)example->revision << 8) | (example->animated ? clock->now_ms / 100 % 3 : 0)};
    if (example->animated) mp_ui_clock_request(clock, 100 - (unsigned)(clock->now_ms % 100));
}
static void paint(const void *context, const struct mp_theme *theme, struct mp_ui_index_surface *window)
{
    (void)theme;
    struct example *example = (struct example *)context;
    ++example->paints;
    uint8_t sprite[32 * 20];
    memset(sprite, example->color, sizeof sprite);
    /* Oversized artwork must remain confined to this indicator's window. */
    mp_ui_index_sprite(window, -2, -2, sprite, 32, 20, 32);
}
static const struct mp_status_indicator_ops operations = {measure, tick, paint};
static struct mp_status_bar bar;
static uint32_t pixels[3][960 * 720];
static struct mp_font_surface surface(unsigned index)
{
    return (struct mp_font_surface){pixels[index], 960, 720, 1, 960};
}
static void compare_buffers(struct mp_status_bar_cache caches[2], unsigned index)
{
    struct mp_font_surface canvas = surface(index), reference = surface(2);
    mp_status_bar_paint(&bar, &canvas, &caches[index]);
    mp_ui_fill(&reference, (struct mp_ui_rect){0, 0, 960, 720}, bar.theme.colors.surface);
    struct mp_status_bar_cache full = {0};
    mp_status_bar_paint(&bar, &reference, &full);
    mp_status_bar_cache_dispose(&full);
    assert(!memcmp(pixels[index], pixels[2], sizeof pixels[index]));
}

int main(void)
{
    test_theme = mp_test_legacy_status_theme();

    mp_status_bar_dispose(&bar);
    assert(!mp_status_bar_init(&bar, &test_theme, 960, 720));
    struct example examples[9];
    const char *ids[] = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    for (unsigned i = 0; i < 9; ++i)
        examples[i] = (struct example){.width = 5, .height = 20, .maximum = 5, .visible = 1, .color = 4};
    struct mp_status_indicator indicator = {"one", &operations, &examples[0]};
    assert(mp_status_bar_register(&bar, NULL, 0, 0) == MP_STATUS_INVALID);
    assert(!mp_status_bar_register(&bar, &indicator, 0, 0));
    assert(mp_status_bar_register(&bar, &indicator, 1, 0) == MP_STATUS_DUPLICATE);
    indicator.id = "bad id";
    assert(mp_status_bar_register(&bar, &indicator, 1, 0) == MP_STATUS_INVALID);
    indicator.id = "two";
    assert(mp_status_bar_register(&bar, &indicator, 2, 0) == MP_STATUS_INVALID);
    struct mp_status_indicator_ops incomplete = operations;
    incomplete.paint = NULL;
    indicator.ops = &incomplete;
    assert(mp_status_bar_register(&bar, &indicator, 1, 0) == MP_STATUS_INVALID);
    indicator.ops = &operations;
    for (unsigned i = 1; i < 8; ++i) {
        indicator = (struct mp_status_indicator){ids[i], &operations, &examples[i]};
        assert(!mp_status_bar_register(&bar, &indicator, i, 0));
    }
    indicator = (struct mp_status_indicator){ids[8], &operations, &examples[8]};
    assert(mp_status_bar_register(&bar, &indicator, 8, 0) == MP_STATUS_FULL);
    assert(bar.count == 8);
    struct mp_theme rejected = test_theme;
    rejected.status.layout.gap = 64;
    assert(mp_status_bar_set_theme(&bar, &rejected) == MP_STATUS_OVERFLOW);
    assert(bar.theme.status.layout.gap == 12 && bar.count == 8);
    assert(mp_status_bar_unregister(&bar, "missing", 0) == MP_STATUS_NOT_FOUND);
    /* Removing an item that never appeared retires it immediately. */
    assert(!mp_status_bar_unregister(&bar, "one", 0));
    assert(!mp_status_bar_contains(&bar, "one") && bar.count == 7);

    struct mp_theme narrow = test_theme;
    narrow.status.layout.max_width = 50;
    mp_status_bar_dispose(&bar);
    assert(!mp_status_bar_init(&bar, &narrow, 960, 720));
    examples[0].width = examples[0].maximum = examples[1].width = examples[1].maximum = 21;
    indicator = (struct mp_status_indicator){"first", &operations, &examples[0]};
    assert(!mp_status_bar_register(&bar, &indicator, 0, 0));
    indicator.id = "overflow"; indicator.context = &examples[1];
    assert(mp_status_bar_register(&bar, &indicator, 1, 0) == MP_STATUS_OVERFLOW);
    assert(bar.count == 1 && mp_status_bar_contains(&bar, "first"));
    examples[1].height = 37;
    assert(mp_status_bar_register(&bar, &indicator, 1, 0) == MP_STATUS_INVALID);

    mp_status_bar_dispose(&bar);
    assert(!mp_status_bar_init(&bar, &test_theme, 960, 720));
    for (unsigned i = 0; i < 3; ++i) {
        examples[i] = (struct example){.width = 9 + 5 * i, .height = 12 + 4 * i,
            .maximum = 21, .visible = 1, .color = (uint8_t)(4 + i)};
        indicator = (struct mp_status_indicator){ids[i], &operations, &examples[i]};
        assert(!mp_status_bar_register(&bar, &indicator, i, 0));
    }
    for (unsigned i = 0; i < 3; ++i) {
        struct mp_font_surface canvas = surface(i);
        mp_ui_fill(&canvas, (struct mp_ui_rect){0, 0, 960, 720}, bar.theme.colors.surface);
    }
    struct mp_status_bar_cache caches[2] = {{0}, {0}};
    for (unsigned t = 0; t <= 1000; t += 10) {
        mp_status_bar_tick(&bar, t, 1);
        compare_buffers(caches, t / 10 % 2);
    }
    struct mp_ui_rect first, second, third;
    assert(!mp_status_bar_item_layout(&bar, "one", &first));
    assert(!mp_status_bar_item_layout(&bar, "two", &second));
    assert(!mp_status_bar_item_layout(&bar, "three", &third));
    assert(first.width == 9 && second.width == 14 && third.width == 19);
    assert(third.x + (int)third.width == 955 && second.x + (int)second.width + 12 == third.x);
    assert(first.height == 12 && second.height == 16 && third.height == 20);
    assert(pixels[0][15 * 960 + (unsigned)(second.x + (int)second.width + 1)] == bar.theme.status.box.background);
    assert(pixels[0][(5 + 12) * 960 + (unsigned)first.x] == bar.theme.status.box.background);
    uint64_t generation = bar.generation, since = bar.items[0].visibility.start_ms;
    assert(!mp_status_bar_tick(&bar, 1100, 1) && bar.generation == generation);
    assert(mp_status_bar_wait_ms(&bar, 1100) == UINT_MAX);
    assert(!mp_status_bar_move(&bar, "one", 2));
    mp_status_bar_tick(&bar, 1200, 1);
    assert(bar.items[2].visibility.start_ms == since);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    assert(!mp_status_bar_item_layout(&bar, "one", &first));
    assert(first.x + (int)first.width == 955);
    examples[1].width = 20; ++examples[1].revision;
    mp_status_bar_tick(&bar, 1300, 1);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    /* Runtime malformed measurement is hidden and never painted out of bounds. */
    unsigned paints = examples[1].paints;
    examples[1].width = 30;
    mp_status_bar_tick(&bar, 1400, 1);
    assert(examples[1].paints == paints);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    examples[1].width = 20;
    mp_status_bar_tick(&bar, 1500, 0);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    assert(!mp_status_bar_unregister(&bar, "two", 1600));
    assert(mp_status_bar_contains(&bar, "two"));
    for (unsigned t = 1600; t <= 2500; t += 10) {
        mp_status_bar_tick(&bar, t, 1);
        compare_buffers(caches, t / 10 % 2);
    }
    assert(!mp_status_bar_contains(&bar, "two"));
    unsigned retired_ticks = examples[1].ticks;
    mp_status_bar_tick(&bar, 2600, 1);
    assert(examples[1].ticks == retired_ticks);
    /* A hidden provider's internal timer cannot keep the container awake. */
    examples[0].visible = 0; examples[0].visibility_ms = 2700; examples[0].animated = 1;
    mp_status_bar_tick(&bar, 3500, 1);
    assert(mp_status_bar_wait_ms(&bar, 3500) == UINT_MAX);
    indicator = (struct mp_status_indicator){"late", &operations, &examples[3]};
    examples[3] = (struct example){.width = 11, .height = 20, .maximum = 11, .visible = 1, .color = 5};
    assert(!mp_status_bar_register(&bar, &indicator, bar.count, 4000));
    mp_status_bar_tick(&bar, 4000, 1);
    assert(!mp_status_bar_item_layout(&bar, "late", &first) && first.width == 0);
    mp_status_bar_tick(&bar, 4800, 1);
    assert(!mp_status_bar_item_layout(&bar, "late", &first) && first.width == 11);
    struct mp_theme moved = test_theme;
    moved.status.layout.margin.top = 44; moved.status.layout.gap = 8; moved.icon_palette[5] = 0xff8800;
    assert(!mp_status_bar_set_theme(&bar, &moved));
    mp_status_bar_tick(&bar, 5000, 0);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    while (bar.count) assert(!mp_status_bar_unregister(&bar, bar.items[0].indicator.id, 5100)),
        mp_status_bar_tick(&bar, 6000, 0);
    compare_buffers(caches, 0); compare_buffers(caches, 1);
    assert(!bar.bounds.width && mp_status_bar_wait_ms(&bar, 6000) == UINT_MAX);
    for (unsigned i=0;i<2;++i) mp_status_bar_cache_dispose(&caches[i]);
    mp_status_bar_dispose(&bar);
    puts("Registry: independent providers, duplicate/capacity/budget rejection, clipping, ordering, live removal, late insertion, idle deadlines and two-buffer recovery passed.");
    return 0;
}
