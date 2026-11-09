// SPDX-License-Identifier: GPL-2.0-only
#include "home-view.h"
#include "navigation-default.h"
#include "status-bar.h"
#include "indicators/battery.h"
#include "indicators/wifi.h"
#include "indicators/clock.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t normal[960*720],portrait[960*720],uncached[960*720];
struct overlay {struct mp_status_bar *bar;struct mp_status_bar_cache *cache;};
static void status(const struct mp_paint *p,void *ctx){struct overlay *o=ctx;assert(mp_status_bar_paint(o->bar,&p->surface,o->cache)>=0);}
int main(int argc,char **argv){
 static struct mp_home_state v;const struct mp_home_layout *l=&mp_home_layout_default;mp_home_view_init(&v);
 mp_home_view_tick(&v,l,960,720,0,0);unsigned stable=v.generation;assert(!mp_home_view_tick(&v,l,960,720,1000,1)&&v.generation==stable&&mp_home_view_wait(&v,1000)==UINT_MAX);
 assert(mp_home_view_key(&v,l,MP_HOME_SELECT,1000)==MP_HOME_OPEN_GAME);
 mp_home_view_key(&v,l,MP_HOME_LEFT,1000);assert(v.menu_open);mp_home_view_tick(&v,l,960,720,1110,1);assert(v.drawer_px>0&&v.drawer_px<(int)(l->menu_width-l->rail_width));
 mp_home_view_key(&v,l,MP_HOME_BACK,1110);mp_home_view_tick(&v,l,960,720,1500,1);assert(!v.menu_open&&!v.drawer_px);
 mp_home_view_key(&v,l,MP_HOME_DOWN,1500);mp_home_view_tick(&v,l,960,720,1800,1);assert(v.row==1&&v.vertical_px>0&&mp_home_view_key(&v,l,MP_HOME_SELECT,1800)==MP_HOME_OPEN_LIBRARY);
 mp_home_view_key(&v,l,MP_HOME_LEFT,1800);assert(v.menu==MP_NAV_HOME);
 mp_home_view_key(&v,l,MP_HOME_UP,1800);mp_home_view_key(&v,l,MP_HOME_UP,1800);
 assert(v.menu==MP_NAV_SEARCH&&mp_home_view_key(&v,l,MP_HOME_SELECT,1800)==MP_HOME_OPEN_SEARCH);
 const enum mp_home_action destinations[]={MP_HOME_OPEN_LIBRARY,MP_HOME_OPEN_FAVORITES,MP_HOME_OPEN_COLLECTIONS,MP_HOME_OPEN_DISCOVERY,MP_HOME_OPEN_SETTINGS};
 mp_home_view_key(&v,l,MP_HOME_DOWN,1800);
 for(unsigned i=0;i<5;i++){mp_home_view_key(&v,l,MP_HOME_DOWN,1800);assert(mp_home_view_key(&v,l,MP_HOME_SELECT,1800)==destinations[i]);}
 mp_home_view_key(&v,l,MP_HOME_DOWN,1800);assert(v.menu==MP_NAV_SETTINGS);
 for(unsigned i=0;i<5;i++)mp_home_view_key(&v,l,MP_HOME_UP,1800);
 assert(v.menu==MP_NAV_HOME);
 mp_home_view_key(&v,l,MP_HOME_SELECT,1800);assert(!v.menu_open);
 mp_home_view_key(&v,l,MP_HOME_LEFT,1800);
 mp_home_view_key(&v,l,MP_HOME_RIGHT,1800);mp_home_view_tick(&v,l,960,720,2100,1);assert(v.row==1&&v.index[1]==0);
 mp_home_view_init(&v);mp_home_view_tick(&v,l,960,720,0,0);
 struct mp_font_surface a={normal,960,720,1,960},b={portrait+959*720,960,720,-720,1};
 for(unsigned mode=0;mode<4;mode++){
  mp_home_view_dispose(&v);mp_home_view_init(&v);mp_home_view_tick(&v,l,960,720,0,0);
  if(mode==1||mode==2){mp_home_view_key(&v,l,MP_HOME_LEFT,1000);mp_home_view_tick(&v,l,960,720,mode==1?1400:1080,1);}
  if(mode==3){mp_home_view_key(&v,l,MP_HOME_DOWN,1000);mp_home_view_tick(&v,l,960,720,1400,1);}
  mp_home_view_draw(&v,&mp_theme_default,l,&a,NULL,NULL);mp_home_view_draw(&v,&mp_theme_default,l,&b,NULL,NULL);
  for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++)assert(normal[y*960+x]==portrait[(959-x)*720+y]);
  memcpy(uncached,normal,sizeof uncached);
  assert(!mp_home_view_prepare_cache(&v,&mp_theme_default,l));
  mp_home_view_draw(&v,&mp_theme_default,l,&a,NULL,NULL);mp_home_view_draw(&v,&mp_theme_default,l,&b,NULL,NULL);
  assert(!memcmp(normal,uncached,sizeof normal));
  for(unsigned y=0;y<720;y++)for(unsigned x=0;x<960;x++)assert(normal[y*960+x]==portrait[(959-x)*720+y]);

 }
 /* Synthetic catalog records exercise horizontal focus and scroll without
  * putting invented games into the production preview. */
 struct mp_home_card fixtures[5];
 for(unsigned i=0;i<5;i++)fixtures[i]=(struct mp_home_card){"fixture","Test title","Test metadata","Test description",&mp_fusion_art,MP_HOME_OPEN_GAME};
 struct mp_home_row rows[]={ {"test-row","Test shelf",fixtures,5} };
 assert(!mp_home_view_set_rows(&v,rows,1));
 for(unsigned i=0;i<4;i++)mp_home_view_key(&v,l,MP_HOME_RIGHT,3000+i*300);
 mp_home_view_tick(&v,l,960,720,4400,1);assert(v.index[0]==4&&v.horizontal_px[0]>0);
 mp_home_view_key(&v,l,MP_HOME_LEFT,4500);assert(!v.menu_open&&v.index[0]==3);
 mp_home_view_tick(&v,l,720,480,4800,0);
 struct mp_font_surface small={normal,720,480,1,720};
 mp_home_view_draw(&v,&mp_theme_default,l,&small,NULL,NULL);
 /* A changed background must bypass stale raster pixels and rebuild exactly. */
 assert(!mp_home_view_prepare_cache(&v,&mp_theme_default,l));
 struct mp_theme changed=mp_theme_default;changed.status.box.background=0x112233;
 mp_home_view_draw(&v,&changed,l,&small,NULL,NULL);memcpy(uncached,normal,720*480*sizeof *normal);
 assert(!mp_home_view_prepare_cache(&v,&changed,l));
 mp_home_view_draw(&v,&changed,l,&small,NULL,NULL);assert(!memcmp(normal,uncached,720*480*sizeof *normal));
 mp_home_view_dispose(&v);mp_home_view_init(&v);
 if(argc<2){puts("Home: sidebar push/reversal, routes/return, vertical shelves, idle scheduling and portrait pixels passed.");return 0;}
 unsigned mode=argc>2?(unsigned)atoi(argv[2]):0;
 mp_home_view_init(&v);mp_home_view_tick(&v,l,960,720,0,0);
 if(mode==1||mode==2){mp_home_view_key(&v,l,MP_HOME_LEFT,1000);mp_home_view_tick(&v,l,960,720,mode==1?1400:1080,1);}
 if(mode==3){mp_home_view_key(&v,l,MP_HOME_DOWN,1000);mp_home_view_tick(&v,l,960,720,1400,1);}
 if(argc>3)mp_home_view_adjust_title(&v,atoi(argv[3])-(int)v.font_height[MP_HOME_FONT_TITLE]);
 struct mp_status_bar bar;struct mp_status_bar_cache cache={0};struct mp_battery_indicator battery;struct mp_wifi_indicator wifi;struct mp_clock_indicator clock;
 assert(!mp_status_bar_init(&bar,&mp_theme_default,960,720));mp_battery_indicator_init(&battery);mp_wifi_indicator_init(&wifi);mp_clock_indicator_init(&clock);
 struct mp_status_indicator items[]={{"battery",&mp_battery_indicator_ops,&battery},{"wifi",&mp_wifi_indicator_ops,&wifi},{"clock",&mp_clock_indicator_ops,&clock}};
 for(int i=0;i<3;i++)assert(!mp_status_bar_register(&bar,&items[i],i,0));
 mp_battery_indicator_update(&battery,(struct mp_battery_reading){75,MP_BATTERY_CHARGING},0);mp_wifi_indicator_update(&wifi,(struct mp_wifi_reading){MP_WIFI_CONNECTED,-45},0);mp_clock_indicator_update(&clock,21,48,60000,0);mp_status_bar_tick(&bar,2000,0);
 struct overlay overlay={&bar,&cache};mp_home_view_draw(&v,&mp_theme_default,l,&a,status,&overlay);
 FILE *f=fopen(argv[1],"wb");assert(f);for(unsigned i=0;i<960*720;i++){uint32_t c=normal[i];unsigned char rgba[]={c>>16,c>>8,c,255};assert(fwrite(rgba,1,4,f)==4);}fclose(f);mp_status_bar_cache_dispose(&cache);mp_status_bar_dispose(&bar);return 0;
}
