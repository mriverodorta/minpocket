// SPDX-License-Identifier: GPL-2.0-only
#include "indicators/clock.h"
#include "status-bar.h"
#include "indicators/battery.h"
#include "indicators/wifi.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
enum { W=960,H=720 };
static struct mp_status_bar bar;
static struct mp_clock_indicator indicator;
static struct mp_battery_indicator battery;
static struct mp_wifi_indicator wifi;
static uint32_t pixels[3][W*H],background[W*H];
static struct mp_status_bar_cache caches[2];
static void compare(unsigned n)
{
 struct mp_font_surface dst={pixels[n]+(W-1)*H,W,H,-H,1},ref={pixels[2]+(W-1)*H,W,H,-H,1};
 assert(mp_status_bar_paint(&bar,&dst,&caches[n])>=0);
 memcpy(pixels[2],background,sizeof background);struct mp_status_bar_cache clean={0};
 assert(mp_status_bar_paint(&bar,&ref,&clean)>=0);assert(!memcmp(pixels[n],pixels[2],sizeof background));
 mp_status_bar_cache_dispose(&clean);assert(!bar.scaled_pixels);
}
int main(void)
{
 mp_clock_indicator_init(&indicator);struct mp_indicator_metrics metrics;
 assert(!mp_clock_indicator_ops.measure(&indicator,&mp_theme_default,&metrics));assert(metrics.width==0&&metrics.max_width==111);
 struct mp_ui_clock clock;struct mp_indicator_frame frame;
 mp_ui_clock_begin(&clock,0,1);mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);
 assert(!frame.visible&&!indicator.stats.label_builds&&mp_ui_clock_wait(&clock,0)==UINT_MAX);
 for(unsigned minute=0;minute<1440;++minute){
  uint64_t now=1000+(uint64_t)minute*60000;
  mp_clock_indicator_update(&indicator,(int)(minute/60),(int)(minute%60),60000,now);
  mp_ui_clock_begin(&clock,now,1);mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);
  char expected[6];snprintf(expected,sizeof expected,"%02u:%02u",minute/60,minute%60);
  assert(frame.visible&&!strcmp(expected,indicator.label));assert(indicator.stats.label_builds==minute+1);
  assert(indicator.text_height==27&&indicator.text_width==111&&mp_ui_clock_wait(&clock,now)==60000);
  for(unsigned second=1;second<60;++second){
   mp_clock_indicator_update(&indicator,(int)(minute/60),(int)(minute%60),60000-second*1000,now+second*1000);
   mp_ui_clock_begin(&clock,now+second*1000,1);mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);
   assert(indicator.stats.label_builds==minute+1&&mp_ui_clock_wait(&clock,clock.now_ms)==60000-second*1000);
  }
 }
 /* Wall-clock correction and midnight do not restart visibility motion. */
 uint64_t visible=indicator.visibility_ms;
 mp_clock_indicator_update(&indicator,0,0,60000,90000000);mp_ui_clock_begin(&clock,90000000,1);
 mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);assert(!strcmp(indicator.label,"00:00")&&visible==indicator.visibility_ms);
 unsigned builds=indicator.stats.label_builds;
 mp_clock_indicator_update(&indicator,24,0,0,90001000);mp_ui_clock_begin(&clock,90001000,1);
 mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);
 assert(!frame.visible&&!strcmp(indicator.label,"00:00")&&indicator.stats.label_builds==builds);
 assert(mp_ui_clock_wait(&clock,clock.now_ms)==UINT_MAX);
 mp_clock_indicator_update(&indicator,9,5,0,90002000);mp_ui_clock_begin(&clock,90002000,1);
 mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);assert(!strcmp(indicator.label,"09:05")&&mp_ui_clock_wait(&clock,clock.now_ms)==1);
 mp_ui_clock_begin(&clock,90003000,1);mp_clock_indicator_ops.tick(&indicator,&mp_theme_default,&clock,&frame);
 assert(mp_ui_clock_wait(&clock,clock.now_ms)==1000); /* no overdue-deadline spin */
 struct mp_theme largest=mp_theme_default;largest.status.text_height=30;
 assert(!mp_clock_indicator_ops.measure(&indicator,&largest,&metrics)&&metrics.max_width==125);
 mp_clock_indicator_ops.tick(&indicator,&largest,&clock,&frame);assert(indicator.text_height==30&&indicator.text_width==125);
 for(unsigned i=0;i<W*H;++i)background[i]=(i%120)<<16|(i%80)<<8|i%90;
 for(unsigned i=0;i<3;++i)memcpy(pixels[i],background,sizeof background);
 assert(!mp_status_bar_init(&bar,&mp_theme_default,W,H));mp_clock_indicator_init(&indicator);
 mp_battery_indicator_init(&battery);mp_wifi_indicator_init(&wifi);
 struct mp_status_indicator items[]={{"clock",&mp_clock_indicator_ops,&indicator},{"battery",&mp_battery_indicator_ops,&battery},{"wifi",&mp_wifi_indicator_ops,&wifi}};
 for(unsigned i=0;i<3;++i)assert(!mp_status_bar_register(&bar,&items[i],i,0));
 mp_clock_indicator_update(&indicator,23,59,60000,1000);
 mp_battery_indicator_update(&battery,(struct mp_battery_reading){75,MP_BATTERY_DISCHARGING},1000);
 mp_wifi_indicator_update(&wifi,(struct mp_wifi_reading){MP_WIFI_CONNECTED,-45},1000);
 mp_status_bar_tick(&bar,1000,0);assert(bar.bounds.width==337&&bar.bounds.height==30);
 assert(!strcmp(bar.items[0].indicator.id,"battery")&&!strcmp(bar.items[1].indicator.id,"wifi")&&!strcmp(bar.items[2].indicator.id,"clock"));
 uint64_t epoch=indicator.visibility_ms;
 struct mp_theme reordered=mp_theme_default;
 reordered.status.indicator_order[0]="clock";reordered.status.indicator_order[1]="wifi";reordered.status.indicator_order[2]="battery";
 assert(!mp_status_bar_set_theme(&bar,&reordered));assert(!strcmp(bar.items[0].indicator.id,"clock")&&indicator.visibility_ms==epoch);
 reordered.status.indicator_order[1]="clock";
 assert(mp_status_bar_set_theme(&bar,&reordered)==MP_STATUS_INVALID);
 assert(!mp_status_bar_set_theme(&bar,&mp_theme_default));mp_status_bar_tick(&bar,1000,0);
 for(unsigned i=0;i<2;++i)compare(i);
 uint64_t generation=bar.generation;builds=indicator.stats.label_builds;
 for(unsigned second=1;second<60;++second){
  mp_clock_indicator_update(&indicator,23,59,60000-second*1000,1000+second*1000);
  assert(!mp_status_bar_tick(&bar,1000+second*1000,0));
 }
 assert(bar.generation==generation&&indicator.stats.label_builds==builds);
 mp_clock_indicator_update(&indicator,0,0,60000,61000);assert(mp_status_bar_tick(&bar,61000,0));
 assert(!strcmp(indicator.label,"00:00")&&bar.bounds.width==337);
 for(unsigned i=0;i<2;++i)compare(i);
 mp_battery_indicator_update(&battery,(struct mp_battery_reading){100,MP_BATTERY_FULL},62000);
 mp_status_bar_tick(&bar,62000,0);assert(bar.bounds.width==360&&bar.bounds.width<=mp_theme_default.status.layout.max_width);
 struct mp_theme vertical=mp_theme_default;vertical.status.layout.orientation=MP_VERTICAL;
 assert(!mp_status_bar_set_theme(&bar,&vertical));mp_status_bar_tick(&bar,63000,0);
 assert(bar.bounds.width==166&&bar.bounds.height==126);
 for(unsigned i=0;i<2;++i)compare(i);
 mp_clock_indicator_update(&indicator,-1,-1,0,64000);mp_status_bar_tick(&bar,64000,0);
 assert(bar.bounds.height==78);for(unsigned i=0;i<2;++i)compare(i);
 mp_clock_indicator_update(&indicator,12,0,60000,65000);mp_status_bar_tick(&bar,65000,0);
 assert(!strcmp(indicator.label,"12:00")&&bar.bounds.height==126);
 for(unsigned i=0;i<2;++i){compare(i);mp_status_bar_cache_dispose(&caches[i]);}
 mp_status_bar_dispose(&bar);
 puts("Clock: all 1440 HH:MM values, minute-only caches, deadlines, invalid/recovery, midnight/time jumps, both layouts and two-buffer restoration passed.");
}
