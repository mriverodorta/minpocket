/* SPDX-License-Identifier: GPL-2.0-only */
/* Synthetic test data only. No downloaded images, metadata or game files. */
static const uint8_t example_pixels[240*160] = {0,1,2,3,2,1,0,3};
static const uint32_t example_palette[256] = {0x24283b,0x7dcfff,0x7aa2f7,0xc0caf5};
/* Retain the existing test ABI; the pixels are entirely synthetic. */
const struct mp_game_art mp_fusion_art={240,160,example_pixels,example_palette};
static const struct mp_game_assets asset_catalog[]={
 {"example-one",&mp_fusion_art,&mp_fusion_art,
 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
 "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"}
};
