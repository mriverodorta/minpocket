// SPDX-License-Identifier: GPL-2.0-only
#include "scene.h"
#include "pixel.h"
#include <limits.h>
int mp_scene_add(struct mp_scene *s,unsigned layer,struct mp_ui_rect clip,mp_scene_painter paint,void *ctx)
{
 if(!s||!paint||layer<1||layer>MP_SCENE_LAYERS||s->count>=MP_SCENE_COMMANDS)return -1;
 s->commands[s->count++]=(struct mp_scene_command){layer,clip,paint,ctx};return 0;
}
void mp_scene_render(const struct mp_scene *s,const struct mp_font_surface *dst)
{
 for(unsigned layer=1;layer<=MP_SCENE_LAYERS;layer++)for(unsigned i=0;i<s->count;i++){
  const struct mp_scene_command *c=&s->commands[i];if(c->layer!=layer)continue;
  int x=c->clip.x<0?0:c->clip.x,y=c->clip.y<0?0:c->clip.y;
  int64_t r=(int64_t)c->clip.x+c->clip.width,b=(int64_t)c->clip.y+c->clip.height;
  if(r>dst->width)r=dst->width;
  if(b>dst->height)b=dst->height;
  if(r<=x||b<=y)continue;
  struct mp_paint p={{dst->pixels+x*dst->x_stride+y*dst->y_stride,(unsigned)(r-x),(unsigned)(b-y),dst->x_stride,dst->y_stride},x,y};
  c->paint(&p,c->context);
 }
}
int mp_paint_clip(const struct mp_paint *parent,struct mp_ui_rect rect,struct mp_paint *child)
{
 int x=rect.x>parent->x?rect.x:parent->x,y=rect.y>parent->y?rect.y:parent->y;
 int64_t right=(int64_t)rect.x+rect.width,bottom=(int64_t)rect.y+rect.height;
 int64_t pr=(int64_t)parent->x+parent->surface.width,pb=(int64_t)parent->y+parent->surface.height;
 if(right>pr)right=pr;
 if(bottom>pb)bottom=pb;
 if(right<=x||bottom<=y)return 0;
 *child=(struct mp_paint){.surface={parent->surface.pixels+((ptrdiff_t)x-parent->x)*parent->surface.x_stride+((ptrdiff_t)y-parent->y)*parent->surface.y_stride,(unsigned)(right-x),(unsigned)(bottom-y),parent->surface.x_stride,parent->surface.y_stride},.x=x,.y=y};
 return 1;
}
void mp_paint_fill_alpha(const struct mp_paint *p,struct mp_ui_rect r,uint32_t color,unsigned alpha)
{
 if(!alpha)return;
 if(alpha>=255){mp_paint_fill(p,r,color);return;}
 struct mp_paint clipped;if(!mp_paint_clip(p,r,&clipped))return;
 for(unsigned x=0;x<clipped.surface.width;x++)for(unsigned y=0;y<clipped.surface.height;y++){
  uint32_t *pixel=clipped.surface.pixels+x*clipped.surface.x_stride+y*clipped.surface.y_stride;
  *pixel=mp_pixel_blend(*pixel,color,alpha);
 }
}
void mp_paint_fill(const struct mp_paint *p,struct mp_ui_rect r,uint32_t color)
{r.x-=p->x;r.y-=p->y;mp_ui_fill(&p->surface,r,color);}
void mp_paint_text(const struct mp_paint *p,int x,int y,unsigned scale,const char *text,uint32_t color)
{mp_font_draw(&p->surface,x-p->x,y-p->y,scale,text,color);}
int mp_flex_arrange(struct mp_ui_rect box,enum mp_orientation direction,unsigned gap,
                   const struct mp_flex_item *items,unsigned count,struct mp_ui_rect *result)
{
 if(count>MP_FLEX_ITEMS||direction>MP_VERTICAL||!items||!result)return -1;
 unsigned total=direction==MP_HORIZONTAL?box.width:box.height;
 if(total>16384||box.width>16384||box.height>16384||gap>16384)return -1;
 unsigned sizes[MP_FLEX_ITEMS],minimum[MP_FLEX_ITEMS],maximum[MP_FLEX_ITEMS];
 int64_t used=count?(int64_t)gap*(count-1):0;
 for(unsigned i=0;i<count;i++){
  if(items[i].percent>100||items[i].grow>1000||items[i].shrink>1000)return -1;
  minimum[i]=items[i].min;maximum[i]=items[i].max?items[i].max:16384;
  if(minimum[i]>maximum[i]||maximum[i]>16384||items[i].basis>16384)return -1;
  uint64_t basis=items[i].percent?(uint64_t)total*items[i].percent/100:items[i].basis;
  sizes[i]=basis<minimum[i]?minimum[i]:basis>maximum[i]?maximum[i]:(unsigned)basis;used+=sizes[i];
 }
 /* Freeze constrained items and redistribute remaining space. An infeasible
  * min-size set overflows the container; the scene's clip owns that overflow. */
 for(unsigned pass=0;pass<=count&&used!=total;pass++){
  int growing=used<total;int64_t free=used<total?total-used:used-total;uint64_t weights=0;
  for(unsigned i=0;i<count;i++)if(growing?sizes[i]<maximum[i]:sizes[i]>minimum[i])
   weights+=growing?items[i].grow:(uint64_t)items[i].shrink*sizes[i];
  if(!weights)break;
  int64_t changed=0;uint64_t carry=0;
  for(unsigned i=0;i<count;i++){
   unsigned room=growing?maximum[i]-sizes[i]:sizes[i]-minimum[i];
   uint64_t weight=room?(growing?items[i].grow:(uint64_t)items[i].shrink*sizes[i]):0;
   uint64_t numerator=(uint64_t)free*weight+carry;unsigned take=(unsigned)(numerator/weights);carry=numerator%weights;
   if(take>room)take=room;
   if(growing)sizes[i]+=take;else sizes[i]-=take;changed+=take;
  }
  if(!changed)break;
  used+=growing?changed:-changed;
 }
 int64_t cursor=direction==MP_HORIZONTAL?box.x:box.y;
 for(unsigned i=0;i<count;i++){
  if(cursor<INT_MIN||cursor>INT_MAX)return -1;
  result[i]=direction==MP_HORIZONTAL?(struct mp_ui_rect){(int)cursor,box.y,sizes[i],box.height}:
       (struct mp_ui_rect){box.x,(int)cursor,box.width,sizes[i]};
  cursor+=sizes[i]+gap;
 }
 return 0;
}
