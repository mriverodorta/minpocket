// SPDX-License-Identifier: GPL-2.0-only
#ifndef MINPOCKET_SCENE_H
#define MINPOCKET_SCENE_H
#include "style.h"
enum { MP_SCENE_LAYERS=10, MP_SCENE_COMMANDS=64, MP_FLEX_ITEMS=32 };
/* Layers are global within a scene, independent of insertion order. Equal
 * layers retain registration order. A layer is an ordering bucket, not a
 * full-screen allocation. Clipping never changes a command's layer. */
struct mp_paint { struct mp_font_surface surface; int x, y; };
typedef void (*mp_scene_painter)(const struct mp_paint *, void *);
struct mp_scene_command { unsigned layer; struct mp_ui_rect clip; mp_scene_painter paint; void *context; };
struct mp_scene { struct mp_scene_command commands[MP_SCENE_COMMANDS]; unsigned count; };
int mp_scene_add(struct mp_scene *,unsigned layer,struct mp_ui_rect clip,mp_scene_painter,void *);
void mp_scene_render(const struct mp_scene *,const struct mp_font_surface *);
int mp_paint_clip(const struct mp_paint *,struct mp_ui_rect,struct mp_paint *);
void mp_paint_fill_alpha(const struct mp_paint *,struct mp_ui_rect,uint32_t,unsigned alpha);
void mp_paint_fill(const struct mp_paint *,struct mp_ui_rect,uint32_t);
void mp_paint_text(const struct mp_paint *,int x,int y,unsigned scale,const char *,uint32_t);
/* Native flex subset: nested row/column composition, pixel/percentage basis,
 * grow/shrink, min/max, gap, cross-axis alignment and bounded clipping. Callers
 * can arrange children recursively. No CSS parser, heap or frame buffers. */
struct mp_flex_item { unsigned basis, percent, grow, shrink, min, max; };
int mp_flex_arrange(struct mp_ui_rect box,enum mp_orientation,unsigned gap,
                    const struct mp_flex_item *,unsigned count,struct mp_ui_rect *);
#endif
