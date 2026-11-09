// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_GAME_ART_H
#define MP_GAME_ART_H
#include <stdint.h>
#include <stddef.h>
struct mp_game_art { unsigned width,height; const uint8_t *pixels; const uint32_t *palette; };
extern const struct mp_game_art mp_fusion_art;
extern const struct mp_game_art mp_preview_final_fantasy_vi;
extern const struct mp_game_art mp_preview_golden_sun_the_lost_age;
extern const struct mp_game_art mp_preview_breath_of_fire_iii;
extern const struct mp_game_art mp_preview_valkyrie_profile_2_silmeria;
struct mp_game_assets {const char *id;const struct mp_game_art *card,*background;const char *card_identity,*background_identity;};
const struct mp_game_assets *mp_game_asset_catalog(size_t *count);
const struct mp_game_art *mp_game_background(const struct mp_game_art *card);
const char *mp_game_art_identity(const struct mp_game_art *art);
#endif
