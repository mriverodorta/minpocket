// SPDX-License-Identifier: GPL-2.0-only
#include "scene.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t pixels[120*80],reverse[120*80];
struct block {struct mp_ui_rect rect;uint32_t color;};
static void paint(const struct mp_paint *p,void *ctx){struct block *b=ctx;mp_paint_fill(p,b->rect,b->color);}
int main(void){
 struct mp_font_surface a={pixels,120,80,1,120},b={reverse+119*80,120,80,-80,1};
 struct mp_scene scene={0};struct mp_ui_rect full={0,0,120,80};
 struct block top={{10,10,50,50},0xff0000},bottom={full,0x0000ff},equal={{20,20,10,10},0x00ff00};
 assert(!mp_scene_add(&scene,10,full,paint,&top));assert(!mp_scene_add(&scene,9,full,paint,&bottom));
 assert(!mp_scene_add(&scene,10,full,paint,&equal));assert(mp_scene_add(&scene,11,full,paint,&top));
 mp_scene_render(&scene,&a);mp_scene_render(&scene,&b);
 assert(pixels[0]==0x0000ff&&pixels[15*120+15]==0xff0000&&pixels[25*120+25]==0x00ff00);
 for(int y=0;y<80;y++)for(int x=0;x<120;x++)assert(pixels[y*120+x]==reverse[(119-x)*80+y]);
 memset(pixels,0,sizeof pixels);scene.count=0;assert(!mp_scene_add(&scene,3,(struct mp_ui_rect){20,10,10,10},paint,&bottom));mp_scene_render(&scene,&a);
 assert(pixels[10*120+20]==0x0000ff&&pixels[9*120+20]==0&&pixels[10*120+19]==0);
 struct mp_flex_item items[]={{.basis=20,.min=20,.max=20},{.grow=1,.shrink=1},{.grow=2,.shrink=1}};struct mp_ui_rect r[3];
 assert(!mp_flex_arrange((struct mp_ui_rect){0,0,120,80},MP_HORIZONTAL,5,items,3,r));assert(r[0].width==20&&r[1].width==30&&r[2].width==60&&r[2].x==60);
 struct mp_flex_item constrained[]={{.grow=1,.max=20},{.grow=1}};
 assert(!mp_flex_arrange(full,MP_HORIZONTAL,0,constrained,2,r));assert(r[0].width==20&&r[1].width==100);
 struct mp_flex_item shrink[]={{.basis=100,.shrink=1,.min=70},{.basis=100,.shrink=1}};
 assert(!mp_flex_arrange(full,MP_HORIZONTAL,0,shrink,2,r));assert(r[0].width==70&&r[1].width==50);
 struct mp_flex_item pct[]={{.percent=25},{.grow=1}};
 assert(!mp_flex_arrange(full,MP_VERTICAL,10,pct,2,r));assert(r[0].height==20&&r[1].height==50&&r[1].y==30);
 puts("Scene: global layer order, stable ties, clip/portrait pixels and bounded flex redistribution passed.");
}
