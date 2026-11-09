// SPDX-License-Identifier: GPL-2.0-only
#include "home.h"
#include "game-art.h"
#include <string.h>

/* All UI colors derive from existing theme roles. Game pixels retain their
 * original palette. Geometry uses logical pixels, including portrait strides. */
static void rect(const struct mp_font_surface *s, int x, int y, unsigned w, unsigned h, uint32_t c)
{
    mp_ui_fill(s, (struct mp_ui_rect){x,y,w,h}, c);
}
static void label(const struct mp_font_surface *s, int x, int y, unsigned scale, const char *v, uint32_t c)
{
    mp_font_draw(s,x,y,scale,v,c);
}
static void window(const struct mp_font_surface *s, const struct mp_theme *t,
                   int x, int y, unsigned w, unsigned h, int focus)
{
    const struct mp_pixel_frame *p=&t->status.box.frame;
    rect(s,x+6,y+6,w,h,p->shadow);
    rect(s,x,y,w,h,p->face);
    rect(s,x+3,y+3,w-6,h-6,p->recess);
    rect(s,x,y,w-3,3,focus?p->highlight:p->mid);
    rect(s,x,y,3,h-3,focus?p->highlight:p->mid);
    rect(s,x+3,y+3,w-6,3,p->face);
    rect(s,x+3,y+6,3,h-9,p->face);
    rect(s,x+6,y+h-6,w-9,3,p->shadow);
    rect(s,x+w-6,y+6,3,h-9,p->shadow);
    /* Discrete light/shadow facets, shared with the Moonstone family. */
    rect(s,x,y,9,6,focus?p->light:p->mid);
    rect(s,x+w-9,y+h-6,9,6,p->shadow);
}
unsigned mp_home_focus_move(unsigned focus,int horizontal,int direction)
{
    if(focus>3)focus=0;
    if(horizontal) {
        if(!focus)return focus;
        if(direction>0)return focus==3?1:focus+1;
        return focus==1?3:focus-1;
    }
    return direction>0?(focus?focus:1):0;
}
enum mp_home_action mp_home_activate(unsigned focus)
{
    static const enum mp_home_action actions[]={MP_HOME_OPEN_GAME,MP_HOME_OPEN_LIBRARY,
        MP_HOME_OPEN_DISPLAY,MP_HOME_OPEN_POWER};
    return focus<4?actions[focus]:MP_HOME_NONE;
}
void mp_home_draw(const struct mp_font_surface *s,const struct mp_theme *t,
                  enum mp_home_view view,unsigned focus)
{
 (void)view;(void)focus;
 rect(s,0,0,s->width,s->height,t->status.box.background);
 mp_font_draw_px(s,32,116,36,"Library",t->colors.status_text);
 mp_font_draw_px(s,32,190,19,"Games load from your SD card.",t->colors.muted);
}

/* Selected catalog record, with a bounded excerpt. No game runtime is implied. */
static void excerpt(const struct mp_font_surface *s,int x,int y,unsigned width,const char *source,uint32_t color)
{
    while(*source==' ')++source;
    for(unsigned line=0;line<9&&*source;line++){
        char part[160];size_t fit=0,word=0;
        for(size_t n=1;source[n-1]&&n<sizeof part;n++){
            memcpy(part,source,n);part[n]=0;
            if(mp_font_width_px(part,21)>width)break;
            fit=n;if(source[n-1]==' ')word=n;
        }
        if(!fit)break;
        if(source[fit]&&word)fit=word;
        memcpy(part,source,fit);part[fit]=0;
        mp_font_draw_px(s,x,y+(int)line*30,21,part,color);source+=fit;
        while(*source==' ')++source;
    }
}
void mp_home_game_draw(const struct mp_font_surface *s,const struct mp_theme *t,
                      const char *title,const char *metadata,const char *description,const struct mp_game_art *art)
{
    const struct mp_pixel_frame *p=&t->status.box.frame;
    rect(s,0,0,s->width,s->height,t->status.box.background);
    label(s,32,25,4,"MINPOCKET",t->colors.status_text);
    rect(s,32,70,896,2,p->face);
    mp_font_draw_px(s,32,116,36,title,t->colors.status_text);
    mp_font_draw_px(s,32,175,21,metadata,p->highlight);
    window(s,t,32,224,508,348,0);
    if(art){
        unsigned w=480,h=(unsigned)((uint64_t)w*art->height/art->width);
        if(h>320){h=320;w=(unsigned)((uint64_t)h*art->width/art->height);}
        int left=44+(int)(480-w)/2,top=236+(int)(320-h)/2;
        for(unsigned x=0;x<w;x++)for(unsigned y=0;y<h;y++){
            int dx=left+(int)x,dy=top+(int)y;
            if(dx>=0&&dy>=0&&(unsigned)dx<s->width&&(unsigned)dy<s->height)
                s->pixels[dx*s->x_stride+dy*s->y_stride]=art->palette[art->pixels[(unsigned)((uint64_t)y*art->height/h)*art->width+(unsigned)((uint64_t)x*art->width/w)]];
        }
    }
    excerpt(s,572,236,340,description,t->colors.status_text);
    label(s,572,538,3,"Catalog preview",p->highlight);
    label(s,32,598,3,"Game runtime not installed. Play is unavailable.",t->colors.status_text);
}
