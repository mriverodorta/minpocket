// SPDX-License-Identifier: GPL-2.0-only
#include "font.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--dump")) {
        for (unsigned code = 0; code <= 8595; ++code)
            if (fwrite(mp_font_glyph(code), 1, MP_FONT_ROWS, stdout) != MP_FONT_ROWS) return 1;
        return 0;
    }
    const uint8_t expected_a[12] = {0,0,0,14,17,17,17,31,17,17,0,0};
    assert(!memcmp(mp_font_glyph('A'), expected_a, sizeof expected_a));
    assert(memcmp(mp_font_glyph('g'), mp_font_glyph('G'), MP_FONT_ROWS));
    assert(!memcmp(mp_font_glyph(0x10ffff), mp_font_glyph('?'), MP_FONT_ROWS));
    assert(mp_font_width("100%", 3) == 69);
    assert(mp_font_width("Español", 2) == 82);
    assert(mp_font_width("", 3) == 0);
    assert(mp_font_width("x", 0) == 0);
    assert(mp_font_width("x", 9) == 0);

    enum { W = 64, H = 48, N = W * H, GUARD = 16 };
    uint32_t normal[N + 2 * GUARD], rotated[N + 2 * GUARD];
    for (unsigned i = 0; i < N + 2 * GUARD; ++i) normal[i] = rotated[i] = 0x123456;
    struct mp_font_surface a = {normal + GUARD, W, H, 1, W};
    struct mp_font_surface b = {rotated + GUARD + (W - 1) * H, W, H, -H, 1};
    /* Exercise accents above the capital top, descenders, edge clipping,
     * all valid UTF-8 lengths, malformed input, and portrait/landscape strides. */
    const char *samples[] = {"Ag 100%", "Été ñ ď →", "\xf0\x9f\x94\x8b", "\xc2", "\xe2\x82",
        "\xf0\x9f", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
    for (unsigned s = 0; s < sizeof samples / sizeof samples[0]; ++s)
        for (int x = -19; x < W; x += 13)
            for (int y = -9; y < H; y += 17)
                for (unsigned scale = 1; scale <= MP_FONT_MAX_SCALE; ++scale) {
                    mp_font_draw(&a, x, y, scale, samples[s], 0xc0caf5);
                    mp_font_draw(&b, x, y, scale, samples[s], 0xc0caf5);
                }
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x)
            assert(normal[GUARD + y * W + x] == rotated[GUARD + (W - 1 - x) * H + y]);
    for (unsigned i = 0; i < GUARD; ++i) {
        assert(normal[i] == 0x123456 && normal[GUARD + N + i] == 0x123456);
        assert(rotated[i] == 0x123456 && rotated[GUARD + N + i] == 0x123456);
    }
    puts("Monogram UTF-8, clipping and rotated-surface checks passed.");
    return 0;
}
