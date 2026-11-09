// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_HOME_VIEW_H
#define MP_HOME_VIEW_H
#include "home.h"
#include "home-layout.h"
#include "scene.h"
#include "game-art.h"
#include "asset-cache.h"
enum { MP_HOME_ROWS=8,MP_HOME_CAPTION_TRANSITIONS=16 };
enum mp_home_item_role {MP_HOME_FONT_TITLE,MP_HOME_FONT_META,MP_HOME_FONT_DESCRIPTION,
 MP_HOME_FONT_SHELF,MP_HOME_FONT_CARD,MP_HOME_FONT_NAV,MP_HOME_FONT_BRAND,
 MP_HOME_FONT_BATTERY,MP_HOME_FONT_CLOCK,MP_HOME_FONT_BADGE,MP_HOME_TEXT_ROLE_COUNT,
 MP_HOME_LAYOUT_INFO=MP_HOME_TEXT_ROLE_COUNT,MP_HOME_LAYOUT_GAMES,MP_HOME_LAYOUT_CONSOLES,MP_HOME_ITEM_COUNT};
enum mp_home_key { MP_HOME_UP,MP_HOME_DOWN,MP_HOME_LEFT,MP_HOME_RIGHT,MP_HOME_BACK,MP_HOME_SELECT };
struct mp_home_card { const char *id,*title,*metadata,*description; const struct mp_game_art *art; enum mp_home_action action; };
struct mp_home_row { const char *id,*title; const struct mp_home_card *cards; unsigned count; };
struct mp_home_caption_motion { unsigned row,index;int used,opacity;struct mp_ui_motion motion; };
enum {MP_HOME_SCALE_MAPS=6,MP_HOME_SCALE_EDGE=384};
struct mp_home_scale_map {
 unsigned source_width,source_height,width,height;
 unsigned x[MP_HOME_SCALE_EDGE],row[MP_HOME_SCALE_EDGE];
};
struct mp_home_state {
 const uint32_t *startup_background;char startup_id[80];
 unsigned startup_geometry,startup_heading,startup_row_height;
 struct mp_asset_cache *assets;
 struct mp_home_scale_map scale_maps[MP_HOME_SCALE_MAPS];
 unsigned scale_next,scale_builds,scale_hits;
 struct mp_home_caption_motion captions[MP_HOME_CAPTION_TRANSITIONS];
 const struct mp_home_row *rows; unsigned count,row,index[MP_HOME_ROWS],previous[MP_HOME_ROWS],menu;
 int menu_open;struct mp_ui_motion drawer,vertical,horizontal[MP_HOME_ROWS],focus[MP_HOME_ROWS];
 int drawer_px,vertical_px,horizontal_px[MP_HOME_ROWS],focus_px[MP_HOME_ROWS];
 unsigned font_height[MP_HOME_ITEM_COUNT],margin_bottom[MP_HOME_ITEM_COUNT],margin_top[MP_HOME_ITEM_COUNT];enum mp_home_item_role debug_target;int font_debug;unsigned debug_modifiers;
 unsigned generation;uint64_t next_ms;unsigned viewport_width,viewport_height;
 uint32_t *art_cache;unsigned art_cache_width,art_cache_height;
 const struct mp_game_art *art_cached_art;uint32_t art_cache_background;
 unsigned cached_fade_left,cached_fade_bottom,cached_top_contrast;
};
/* Derived from the same geometry and focus bounds used by painting. The saved
 * row metrics preserve downstream layout without retaining off-screen cards. */
struct mp_home_startup_plan {unsigned first,last,visible_mask,row_heading,row_height;};
int mp_home_view_startup_plan(const struct mp_home_state *,const struct mp_home_layout *,unsigned width,unsigned height,struct mp_home_startup_plan *);
void mp_home_view_init(struct mp_home_state *);
int mp_home_view_adjust_title(struct mp_home_state *,int delta);
const char *mp_home_item_key(enum mp_home_item_role);
const char *mp_home_item_name(enum mp_home_item_role);
int mp_home_debug_cycle(struct mp_home_state *,int direction,unsigned visible_mask);
enum {MP_HOME_MOD_TOP=1,MP_HOME_MOD_BOTTOM=2};
int mp_home_item_has_font(enum mp_home_item_role);
int mp_home_debug_adjust(struct mp_home_state *,int delta);
void mp_home_debug_modifier(struct mp_home_state *,unsigned modifier,int down);
int mp_home_margin_top_adjust(struct mp_home_state *,int delta);
int mp_home_margin_adjust(struct mp_home_state *,int delta);
int mp_home_font_adjust(struct mp_home_state *,int delta);
unsigned mp_home_debug_visible(const struct mp_home_state *,const struct mp_home_layout *,unsigned width,unsigned height);
enum {MP_HOME_TEXT_LINES=8,MP_HOME_TEXT_BYTES=256};
/* height/box_height are fixed line-box layout metrics; ink_height/ink_top
 * describe paint only and must not determine flex spacing. */
struct mp_home_text_layout {char lines[MP_HOME_TEXT_LINES][MP_HOME_TEXT_BYTES];unsigned count,height,line_height,box_height,ink_height;int ink_top,truncated,sentence_end;};
/* Capital-top position of the final line, relative to the fixed line-box block. */
unsigned mp_home_text_last_line_offset(const struct mp_home_text_layout *,unsigned height);
enum mp_home_text_overflow {MP_HOME_TEXT_CLIP=0,MP_HOME_TEXT_SENTENCE_ELLIPSIS=1,MP_HOME_TEXT_ELLIPSIS=2};
void mp_home_text_wrap(const char *,unsigned width,unsigned height,unsigned max_lines,int truncate,struct mp_home_text_layout *);
int mp_home_debug_bounds(const struct mp_home_state *,const struct mp_home_layout *,unsigned width,unsigned height,enum mp_home_item_role,struct mp_ui_rect *);
unsigned mp_home_caption_opacity(const struct mp_home_state *,unsigned row,unsigned index);
void mp_home_caption_text(const char *title,char *output,size_t size);
unsigned mp_home_description_wrap_width(const struct mp_home_layout *,unsigned viewport_width);
unsigned mp_home_title_wrap_width(const struct mp_home_layout *,unsigned viewport_width);
int mp_home_assets_start(struct mp_home_state *,const char *root,int (*prepare_root)(void *),void *context);
/* Allocate selected artwork only after the second confirmed display refresh. */
int mp_home_view_prepare_cache(struct mp_home_state *,const struct mp_theme *,const struct mp_home_layout *);
void mp_home_view_dispose(struct mp_home_state *);
int mp_home_view_set_rows(struct mp_home_state *,const struct mp_home_row *,unsigned);
int mp_home_view_tick(struct mp_home_state *,const struct mp_home_layout *,unsigned width,unsigned height,uint64_t now_ms,int animate);
unsigned mp_home_view_wait(const struct mp_home_state *,uint64_t now_ms);
enum mp_home_action mp_home_view_key(struct mp_home_state *,const struct mp_home_layout *,enum mp_home_key,uint64_t now_ms);
/* Overlay registers in the same global scene at the configured top layer.
 * A status-only update may paint against its preserved per-buffer backdrop. */
void mp_home_view_draw(struct mp_home_state *,const struct mp_theme *,const struct mp_home_layout *,
                      const struct mp_font_surface *,mp_scene_painter overlay,void *context);
#endif
