// SPDX-License-Identifier: GPL-2.0-only
#include "game-art.h"
#ifdef MP1_TEST_CATALOG
#include "tests/fixtures.h"
#else
#include "runtime-home.h"
#endif
const struct mp_game_assets *mp_game_asset_catalog(size_t *count)
{
#ifdef MP1_TEST_CATALOG
 *count=sizeof asset_catalog/sizeof asset_catalog[0];return asset_catalog;
#else
 return mp_runtime_assets(count);
#endif
}
const struct mp_game_art *mp_game_background(const struct mp_game_art *card)
{size_t count;const struct mp_game_assets *items=mp_game_asset_catalog(&count);for(size_t i=0;i<count;i++)if(items[i].card==card&&card)return items[i].background;return card;}
const char *mp_game_art_identity(const struct mp_game_art *art)
{size_t count;const struct mp_game_assets *items=mp_game_asset_catalog(&count);if(!art)return NULL;for(size_t i=0;i<count;i++){if(items[i].card==art)return items[i].card_identity;if(items[i].background==art)return items[i].background_identity;}return NULL;}
