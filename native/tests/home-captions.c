// SPDX-License-Identifier: GPL-2.0-only
#include "home-view.h"
#include "pixel.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
static uint32_t normal[960*720],portrait[960*720];
static void compare(struct mp_home_state *home)
{
 struct mp_font_surface a={normal,960,720,1,960},b={portrait+959*720,960,720,-720,1};
 mp_home_view_draw(home,&mp_theme_default,&mp_home_layout_default,&a,NULL,NULL);
 mp_home_view_draw(home,&mp_theme_default,&mp_home_layout_default,&b,NULL,NULL);
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++)assert(normal[y*960+x]==portrait[(959-x)*720+y]);
}
int main(void)
{
 char text[256];
 mp_home_caption_text("  Golden Sun :  The Lost Age  ",text,sizeof text);assert(!strcmp(text,"Golden Sun\nThe Lost Age"));
 mp_home_caption_text("Valkyrie Profile 2: Silmeria",text,sizeof text);assert(!strcmp(text,"Valkyrie Profile 2\nSilmeria"));
 mp_home_caption_text("  Breath of Fire III  ",text,sizeof text);assert(!strcmp(text,"Breath of Fire III"));
 mp_home_caption_text("A - B",text,sizeof text);assert(!strcmp(text,"A\nB"));
 mp_home_caption_text("A — B",text,sizeof text);assert(!strcmp(text,"A\nB"));
 mp_home_caption_text("A – B",text,sizeof text);assert(!strcmp(text,"A\nB"));
 mp_home_caption_text("A： B",text,sizeof text);assert(!strcmp(text,"A\nB"));
 char small[2]={42,42};mp_home_caption_text("Été",small,sizeof small);assert(!small[0]);
 small[0]=42;mp_home_caption_text("A",small,0);assert(small[0]==42);
 assert(mp_pixel_blend(0x123456,0xabcdef,0)==0x123456);
 assert(mp_pixel_blend(0x123456,0xabcdef,255)==0xabcdef);
 assert(mp_pixel_blend(0,0xffffff,128)==0x808080);
 struct mp_font_surface a={normal,960,720,1,960},b={portrait+959*720,960,720,-720,1};
 for(unsigned i=0;i<960*720;i++)normal[i]=portrait[i]=0x123456;
 mp_font_draw_px_alpha(&a,-3,-2,19,"Été Ag",0xffffff,128);
 mp_font_draw_px_alpha(&b,-3,-2,19,"Été Ag",0xffffff,128);
 struct mp_paint p={.surface=a},q={.surface=b};
 mp_paint_fill_alpha(&p,(struct mp_ui_rect){950,710,50,50},0xffffff,128);
 mp_paint_fill_alpha(&q,(struct mp_ui_rect){950,710,50,50},0xffffff,128);
 for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++)assert(normal[y*960+x]==portrait[(959-x)*720+y]);
 struct mp_home_state home;mp_home_view_init(&home);const struct mp_home_layout *l=&mp_home_layout_default;
 mp_home_view_tick(&home,l,960,720,0,0);assert(!mp_home_caption_opacity(&home,0,0)&&mp_home_caption_opacity(&home,0,1)==1000);
 assert(mp_home_view_wait(&home,0)==UINT_MAX);
 mp_home_view_key(&home,l,MP_HOME_RIGHT,1000);mp_home_view_tick(&home,l,960,720,1055,1);
 unsigned first=mp_home_caption_opacity(&home,0,0),second=mp_home_caption_opacity(&home,0,1);
 assert(first>0&&first<1000&&second>0&&second<1000&&first+second==1000);compare(&home);
 mp_home_view_key(&home,l,MP_HOME_RIGHT,1055);assert(mp_home_caption_opacity(&home,0,0)==first&&mp_home_caption_opacity(&home,0,1)==second);
 mp_home_view_tick(&home,l,960,720,1080,1);assert(mp_home_caption_opacity(&home,0,0)>first);compare(&home);
 unsigned interrupted=mp_home_caption_opacity(&home,0,1);
 mp_home_view_key(&home,l,MP_HOME_LEFT,1080);assert(mp_home_caption_opacity(&home,0,1)==interrupted);
 mp_home_view_tick(&home,l,960,720,1120,1);assert(mp_home_caption_opacity(&home,0,1)<interrupted);compare(&home);
 mp_home_view_tick(&home,l,960,720,1500,1);assert(!mp_home_caption_opacity(&home,0,1)&&mp_home_caption_opacity(&home,0,0)==1000&&mp_home_caption_opacity(&home,0,2)==1000);
 assert(mp_home_view_wait(&home,1500)==UINT_MAX);
 mp_home_view_key(&home,l,MP_HOME_LEFT,1600);mp_home_view_tick(&home,l,960,720,2000,1);
 mp_home_view_key(&home,l,MP_HOME_LEFT,2100);mp_home_view_tick(&home,l,960,720,2500,1);assert(home.menu_open&&!mp_home_caption_opacity(&home,0,0));compare(&home);
 mp_home_view_key(&home,l,MP_HOME_BACK,2600);mp_home_view_key(&home,l,MP_HOME_DOWN,3000);mp_home_view_tick(&home,l,960,720,3500,1);
 assert(mp_home_caption_opacity(&home,0,0)==1000);compare(&home);
 mp_home_view_key(&home,l,MP_HOME_UP,3600);mp_home_view_tick(&home,l,960,720,4000,1);assert(!mp_home_caption_opacity(&home,0,0));
 mp_home_view_dispose(&home);
 puts("Caption fades: endpoints, interrupted retargets, independent cards, sidebar, row changes, idle scheduling, subtitle splitting and alpha portrait pixels passed.");
}
