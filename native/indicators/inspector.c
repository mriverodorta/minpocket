// SPDX-License-Identifier: GPL-2.0-only
#include "inspector.h"
#include <stdio.h>
#include <string.h>
void mp_inspector_update(struct mp_inspector_indicator *s,const char *key,int has_font,unsigned font,unsigned top,unsigned bottom,unsigned modifiers,int visible,uint64_t now)
{
 visible=!!visible;has_font=!!has_font;
 if(s->visible!=visible){s->visible=visible;s->visibility_ms=now;++s->revision;}
 if(strncmp(s->key,key,2)||s->font!=font||s->top!=top||s->bottom!=bottom||s->has_font!=has_font||s->modifiers!=modifiers){
  s->key[0]=key[0];s->key[1]=key[1];s->key[2]=0;s->has_font=has_font;s->font=font;s->top=top;s->bottom=bottom;s->modifiers=modifiers;++s->revision;
 }
}
static int measure(const void *context,const struct mp_theme *theme,struct mp_indicator_metrics *m)
{(void)context;(void)theme;unsigned width=mp_font_width_px("MT:256px MB:256px",14);*m=(struct mp_indicator_metrics){width,36,width};return 0;}
static void tick(void *context,const struct mp_theme *theme,struct mp_ui_clock *clock,struct mp_indicator_frame *f)
{
 (void)theme;(void)clock;struct mp_inspector_indicator *s=context;
 if(s->revision!=s->cached_revision){
  s->width=mp_font_width_px("MT:256px MB:256px",14);
  uint32_t ink[224*36]={0};struct mp_font_surface surface={ink,s->width,36,1,s->width};
  char font[20],top[20],bottom[20];
  if(s->has_font)snprintf(font,sizeof font,"F:%upx",s->font);else snprintf(font,sizeof font,"F:--px");
  snprintf(top,sizeof top,"MT:%upx",s->top);snprintf(bottom,sizeof bottom,"MB:%upx",s->bottom);
  mp_font_draw_px(&surface,0,0,14,s->key,MP_THEME_STATUS_TEXT);
  mp_font_draw_px(&surface,36,0,14,font,!s->modifiers&&s->has_font?MP_THEME_DEBUG_TEXT:MP_THEME_STATUS_TEXT);
  mp_font_draw_px(&surface,0,18,14,top,s->modifiers&1?MP_THEME_DEBUG_TEXT:MP_THEME_STATUS_TEXT);
  mp_font_draw_px(&surface,(int)mp_font_width_px("MT:256px",14)+12,18,14,bottom,s->modifiers&2?MP_THEME_DEBUG_TEXT:MP_THEME_STATUS_TEXT);
  memset(s->pixels,0,sizeof s->pixels);
  for(unsigned i=0;i<s->width*36;i++)s->pixels[i]=(uint8_t)ink[i];
  s->cached_revision=s->revision;
 }
 *f=(struct mp_indicator_frame){s->visible,s->revision,s->visibility_ms};
}
static void paint(const void *context,const struct mp_theme *theme,struct mp_ui_index_surface *surface)
{(void)theme;const struct mp_inspector_indicator *s=context;mp_ui_index_sprite(surface,0,0,s->pixels,s->width,36,s->width);}
const struct mp_status_indicator_ops mp_inspector_indicator_ops={measure,tick,paint};
