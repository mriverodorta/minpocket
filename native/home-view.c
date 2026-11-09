// SPDX-License-Identifier: GPL-2.0-only
#include "home-view.h"
#define MP_NAVIGATION_PIXELS
#include "navigation-default.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const struct mp_home_card game_cards[]={
#ifdef MP1_TEST_CATALOG
#include "tests/preview-cards.h"
#else
 {"library-empty","No games yet","","Add games to the Games folder on your SD card.",NULL,MP_HOME_OPEN_LIBRARY}
#endif
};
static const struct mp_home_card console_cards[]={
 {"gba","Game Boy Advance","Nintendo / Handheld","Explore the Game Boy Advance library.",NULL,MP_HOME_OPEN_LIBRARY}
};
static const struct mp_home_row default_rows[]={
 {"games","Games",game_cards,sizeof game_cards/sizeof game_cards[0]},{"consoles",MP_HOME_SECONDARY_TITLE,console_cards,1}
};
static unsigned cap(unsigned a,unsigned b){return a<b?a:b;}
static struct mp_ui_motion motion(int value)
{return (struct mp_ui_motion){.from=value,.target=value,.easing=MP_EASE_OUT_CUBIC};}
static void reset_captions(struct mp_home_state *v)
{
 memset(v->captions,0,sizeof v->captions);
 v->captions[0]=(struct mp_home_caption_motion){.used=1,.row=0,.index=0,.opacity=0,.motion=motion(0)};
}
unsigned mp_home_caption_opacity(const struct mp_home_state *v,unsigned row,unsigned index)
{
 for(unsigned i=0;i<MP_HOME_CAPTION_TRANSITIONS;i++)if(v->captions[i].used&&v->captions[i].row==row&&v->captions[i].index==index)
  return (unsigned)v->captions[i].opacity;
 return 1000;
}
static void caption_to(struct mp_home_state *v,unsigned row,unsigned index,int opacity,unsigned duration,const struct mp_ui_clock *clock)
{
 struct mp_home_caption_motion *slot=NULL,*available=NULL;
 for(unsigned i=0;i<MP_HOME_CAPTION_TRANSITIONS;i++){
  struct mp_home_caption_motion *c=&v->captions[i];
  if(c->used&&c->row==row&&c->index==index){slot=c;break;}
  if(!c->used&&!available)available=c;
 }
 if(!slot){
  if(!available)for(unsigned i=0;i<MP_HOME_CAPTION_TRANSITIONS;i++)if(v->captions[i].motion.target==1000){available=&v->captions[i];break;}
  if(!available)return;
  slot=available;int initial=opacity?0:1000;
  *slot=(struct mp_home_caption_motion){.row=row,.index=index,.used=1,.opacity=initial,.motion=motion(initial)};
 }
 slot->opacity=mp_ui_motion_value(&slot->motion,clock);
 mp_ui_motion_to(&slot->motion,opacity,duration,clock);
}
unsigned mp_home_description_wrap_width(const struct mp_home_layout *l,unsigned viewport_width)
{return viewport_width>l->rail_width?(viewport_width-l->rail_width)*l->description_width_percent/100:0;}
void mp_home_view_init(struct mp_home_state *v)
{
 memset(v,0,sizeof *v);v->rows=default_rows;v->count=2;const struct mp_home_layout *l=&mp_home_layout_default;
 unsigned sizes[]={l->title_height,l->metadata_height,l->description_height,l->shelf_text_height,l->card_text_height,l->nav_text_height,l->brand_text_height,mp_theme_default.status.text_height,mp_theme_default.status.text_height,l->badge_text_height,0,0,0};
 memcpy(v->font_height,sizes,sizeof sizes);
 unsigned margins[]={l->title_margin_bottom,l->metadata_margin_bottom,l->description_margin_bottom,l->shelf_margin_bottom,l->card_margin_bottom,l->nav_margin_bottom,l->brand_margin_bottom,l->battery_margin_bottom,l->clock_margin_bottom,l->badge_margin_bottom,l->info_margin_bottom,l->games_margin_bottom,l->consoles_margin_bottom};
 memcpy(v->margin_bottom,margins,sizeof margins);
 unsigned tops[]={l->title_margin_top,l->metadata_margin_top,l->description_margin_top,l->shelf_margin_top,l->card_margin_top,l->nav_margin_top,l->brand_margin_top,l->battery_margin_top,l->clock_margin_top,l->badge_margin_top,l->info_margin_top,l->games_margin_top,l->consoles_margin_top};
 memcpy(v->margin_top,tops,sizeof tops);reset_captions(v);v->generation=1;v->next_ms=UINT64_MAX;
 for(unsigned i=0;i<MP_HOME_ROWS;i++){v->focus[i]=motion(i?0:1000);v->focus_px[i]=i?0:1000;}
}
static const char *font_keys[]={"T1","C1","D1","T2","D2","N1","N2","B1","K1","C2","I1","G1","G2"};
static const char *font_names[]={"Game title","Console / year","Description","Shelf headings","Card titles","Sidebar menu","Sidebar wordmark","Battery percentage","Clock","Console badge","Selected-item stack","Games carousel","Consoles carousel"};
const char *mp_home_item_key(enum mp_home_item_role role){return (unsigned)role<MP_HOME_ITEM_COUNT?font_keys[role]:"??";}
const char *mp_home_item_name(enum mp_home_item_role role){return (unsigned)role<MP_HOME_ITEM_COUNT?font_names[role]:"Unknown";}
int mp_home_item_has_font(enum mp_home_item_role role)
{return (unsigned)role<MP_HOME_TEXT_ROLE_COUNT;}
static int adjust_font(struct mp_home_state *v,enum mp_home_item_role role,int delta)
{
 static const unsigned maximum[]={84,42,42,42,36,28,27,30,30,27};
 if(!mp_home_item_has_font(role))return 0;
 int64_t value=(int64_t)v->font_height[role]+delta;
 if(value<14)value=14;
 if(value>maximum[role])value=maximum[role];
 if(value==v->font_height[role])return 0;
 v->font_height[role]=(unsigned)value;++v->generation;return 1;
}
static int adjust_margin(struct mp_home_state *v,int top,int delta)
{
 if((unsigned)v->debug_target>=MP_HOME_ITEM_COUNT)return 0;
 unsigned *slot=top?&v->margin_top[v->debug_target]:&v->margin_bottom[v->debug_target];
 int64_t value=(int64_t)*slot+delta;if(value<0)value=0;
 if(value>256)value=256;
 if(v->debug_target==MP_HOME_FONT_BATTERY||v->debug_target==MP_HOME_FONT_CLOCK){
  unsigned other=top?v->margin_bottom[v->debug_target]:v->margin_top[v->debug_target];
  if((unsigned)value+other>482)value=482-other;
 }
 if(value==*slot)return 0;
 *slot=(unsigned)value;++v->generation;return 1;
}
int mp_home_margin_adjust(struct mp_home_state *v,int delta){return adjust_margin(v,0,delta);}
int mp_home_margin_top_adjust(struct mp_home_state *v,int delta){return adjust_margin(v,1,delta);}
void mp_home_debug_modifier(struct mp_home_state *v,unsigned modifier,int down)
{modifier&=MP_HOME_MOD_TOP|MP_HOME_MOD_BOTTOM;if(down)v->debug_modifiers|=modifier;else v->debug_modifiers&=~modifier;}
int mp_home_debug_adjust(struct mp_home_state *v,int delta)
{
 if(!v->debug_modifiers)return mp_home_font_adjust(v,delta);
 int changed=0;
 if(v->debug_modifiers&MP_HOME_MOD_TOP)changed|=mp_home_margin_top_adjust(v,delta);
 if(v->debug_modifiers&MP_HOME_MOD_BOTTOM)changed|=mp_home_margin_adjust(v,delta);
 return changed;
}
int mp_home_view_adjust_title(struct mp_home_state *v,int delta){return adjust_font(v,MP_HOME_FONT_TITLE,delta);}
int mp_home_font_adjust(struct mp_home_state *v,int delta){return adjust_font(v,v->debug_target,delta);}
int mp_home_debug_cycle(struct mp_home_state *v,int direction,unsigned visible)
{
 if(!visible)return 0;
 unsigned role=v->debug_target;
 for(unsigned i=0;i<MP_HOME_ITEM_COUNT;i++){
  role=(role+MP_HOME_ITEM_COUNT+(direction<0?-1:1))%MP_HOME_ITEM_COUNT;
  if(visible&(1u<<role)){int changed=role!=(unsigned)v->debug_target;v->debug_target=(enum mp_home_item_role)role;return changed;}
 }
 return 0;
}
unsigned mp_home_title_wrap_width(const struct mp_home_layout *l,unsigned viewport_width)
{return viewport_width>l->rail_width?(viewport_width-l->rail_width)*l->title_width_percent/100:0;}
int mp_home_view_set_rows(struct mp_home_state *v,const struct mp_home_row *rows,unsigned count)
{
 if(!rows||!count||count>MP_HOME_ROWS)return -1;
 for(unsigned i=0;i<count;i++)if(!rows[i].cards||!rows[i].count||!rows[i].id||!rows[i].title)return -1;
 v->rows=rows;v->count=count;v->row=0;v->startup_geometry=0;reset_captions(v);
 for(unsigned i=0;i<count;i++){v->index[i]=v->previous[i]=0;v->horizontal[i]=motion(0);v->horizontal_px[i]=0;v->focus[i]=motion(i?0:1000);v->focus_px[i]=i?0:1000;}
 v->vertical=motion(0);v->vertical_px=0;++v->generation;return 0;
}
struct geometry {
 struct mp_ui_rect main,context,context_paint,shelves,title,metadata,description;
 struct mp_home_text_layout title_text,description_text;
 unsigned card_width,card_height,row_offset[MP_HOME_ROWS],row_top[MP_HOME_ROWS],row_height[MP_HOME_ROWS],heading[MP_HOME_ROWS],heading_ink[MP_HOME_ROWS],heading_y[MP_HOME_ROWS];
};
static enum mp_home_item_role row_role(const struct mp_home_state *v,unsigned row)
{return !strcmp(v->rows[row].id,"consoles")?MP_HOME_LAYOUT_CONSOLES:MP_HOME_LAYOUT_GAMES;}
static unsigned text_box_height(const char *text,unsigned height)
{return text&&*text?mp_font_line_height_px(height):0;}
static struct geometry geometry(const struct mp_home_state *v,const struct mp_home_layout *l,unsigned width,unsigned height)
{
 struct geometry g={0};struct mp_ui_rect root={0,0,width,height},cols[2];
 struct mp_flex_item columns[]={{.basis=l->rail_width,.min=l->rail_width,.max=l->rail_width},{.grow=1,.shrink=1}};
 (void)mp_flex_arrange(root,MP_HORIZONTAL,0,columns,2,cols);
 unsigned battery=v->margin_top[MP_HOME_FONT_BATTERY]+v->margin_bottom[MP_HOME_FONT_BATTERY];
 unsigned clock=v->margin_top[MP_HOME_FONT_CLOCK]+v->margin_bottom[MP_HOME_FONT_CLOCK];
 unsigned header=battery>clock?battery:clock;
 if(v->font_debug&&header<6)header=6;
 unsigned original=l->battery_margin_bottom>l->clock_margin_bottom?l->battery_margin_bottom:l->clock_margin_bottom;
 unsigned base=l->top>original?l->top-original:0;
 unsigned inset=cap(l->inset,cols[1].width/4),top=cap(base+header,height);
 g.main=(struct mp_ui_rect){cols[1].x+(int)inset,(int)top,cols[1].width-2*inset,height>top+inset?height-top-inset:0};
 const struct mp_home_card *card=&v->rows[v->row].cards[v->index[v->row]];
 unsigned tw=cap(mp_home_title_wrap_width(l,width),g.main.width),mw=g.main.width*l->info_width_percent/100;
 unsigned dw=cap(mp_home_description_wrap_width(l,width),g.main.width);
 struct mp_home_text_layout text;int y=g.main.y+(int)v->margin_top[MP_HOME_LAYOUT_INFO];
 g.context=(struct mp_ui_rect){g.main.x,y,g.main.width,0};
 char title[MP_HOME_TEXT_BYTES];mp_home_caption_text(card->title,title,sizeof title);
 y+=(int)v->margin_top[MP_HOME_FONT_TITLE];mp_home_text_wrap(title,tw,v->font_height[MP_HOME_FONT_TITLE],2,MP_HOME_TEXT_ELLIPSIS,&g.title_text);
 /* MT positions the final visible title line. Extra lines extend upward, so
  * changing line count does not add another line to the downstream stack.
  * Layout reserves the same line box for capitals, descenders and accents. */
 if(l->title_anchor_last_line)y-=(int)mp_home_text_last_line_offset(&g.title_text,v->font_height[MP_HOME_FONT_TITLE]);
 g.title=(struct mp_ui_rect){g.main.x,y,tw,g.title_text.box_height};
 int title_paint_top=g.title.y+g.title_text.ink_top;
 y+=(int)(g.title.height+v->margin_bottom[MP_HOME_FONT_TITLE]);
 y+=(int)v->margin_top[MP_HOME_FONT_META];mp_home_text_wrap(card->metadata,mw,v->font_height[MP_HOME_FONT_META],2,0,&text);
 g.metadata=(struct mp_ui_rect){g.main.x,y,mw,text.box_height};y+=(int)(text.box_height+v->margin_bottom[MP_HOME_FONT_META]);
 y+=(int)v->margin_top[MP_HOME_FONT_DESCRIPTION];mp_home_text_wrap(card->description,dw,v->font_height[MP_HOME_FONT_DESCRIPTION],l->description_lines,1,&g.description_text);
 g.description=(struct mp_ui_rect){g.main.x,y,dw,g.description_text.box_height};y+=(int)(g.description.height+v->margin_bottom[MP_HOME_FONT_DESCRIPTION]);
 g.context.height=(unsigned)(y-g.context.y);
 /* Paint overflow includes accents and the upward-anchored title, without
  * changing the parent's layout box or its downstream flex position. */
 g.context_paint=g.context;
 if(title_paint_top<g.context_paint.y){g.context_paint.y=title_paint_top;g.context_paint.height=(unsigned)(y-title_paint_top);}
 y+=(int)v->margin_bottom[MP_HOME_LAYOUT_INFO];
 g.shelves=(struct mp_ui_rect){g.main.x,y,g.main.width,y<(int)height?height-(unsigned)y:0};
 g.card_width=cap(l->card_width,g.main.width>16?g.main.width-16:g.main.width);
 g.card_height=l->card_width?(unsigned)((uint64_t)l->card_height*g.card_width/l->card_width):0;
 unsigned cursor=0;
 for(unsigned row=0;row<v->count;row++){
  enum mp_home_item_role group=row_role(v,row);g.row_offset[row]=cursor;
  unsigned start=cursor+v->margin_top[group],position=start;g.row_top[row]=start;
  int heading=v->rows[row].title[0]&&(l->games_heading_visible||strcmp(v->rows[row].id,"games"));
  if(heading){
   position+=v->margin_top[MP_HOME_FONT_SHELF];g.heading_y[row]=position;
   g.heading_ink[row]=text_box_height(v->rows[row].title,v->font_height[MP_HOME_FONT_SHELF]);
   position+=g.heading_ink[row]+v->margin_bottom[MP_HOME_FONT_SHELF];
  }
  unsigned external=0;int tile=0;
  for(unsigned i=0;i<v->rows[row].count;i++)if(!v->rows[row].cards[i].art){
   tile=1;mp_home_text_wrap(v->rows[row].cards[i].title,g.card_width,v->font_height[MP_HOME_FONT_CARD],2,0,&text);
   unsigned used=v->margin_bottom[MP_HOME_FONT_BADGE]+v->margin_top[MP_HOME_FONT_CARD]+text.height+v->margin_bottom[MP_HOME_FONT_CARD];
   if(used>external)external=used;
  }
  if(tile)position+=v->margin_top[MP_HOME_FONT_BADGE];
  g.heading[row]=position-start;position+=g.card_height+external;
  g.row_height[row]=position-start;
  if(!row&&v->startup_geometry){
   g.heading[row]=v->startup_heading;g.row_height[row]=v->startup_row_height;
   position=start+v->startup_row_height;
  }
  cursor=position+v->margin_bottom[group];
 }
 return g;
}
int mp_home_view_tick(struct mp_home_state *v,const struct mp_home_layout *l,unsigned width,unsigned height,uint64_t now,int animate)
{
 struct mp_ui_clock clock;mp_ui_clock_begin(&clock,now,animate);int changed=0;
 struct geometry g=geometry(v,l,width,height);v->viewport_width=width;v->viewport_height=height;
 int vertical=(int)(g.row_offset[v->row]);if(v->vertical.target!=vertical)mp_ui_motion_to(&v->vertical,vertical,l->animation_ms,&clock);
 struct mp_ui_motion *motions[2+MP_HOME_ROWS*2]={&v->drawer,&v->vertical};int *values[2+MP_HOME_ROWS*2]={&v->drawer_px,&v->vertical_px};
 unsigned count=2;
 for(unsigned r=0;r<v->count;r++){
  unsigned visible=(g.main.width+l->card_gap)/(g.card_width+l->card_gap);if(!visible)visible=1;
  unsigned start=v->index[r]>=visible?v->index[r]-visible+1:0;
  int target=(int)(start*(g.card_width+l->card_gap));
  if(v->horizontal[r].target!=target)mp_ui_motion_to(&v->horizontal[r],target,l->animation_ms,&clock);
  motions[count]=&v->horizontal[r];values[count++]=&v->horizontal_px[r];
  motions[count]=&v->focus[r];values[count++]=&v->focus_px[r];
 }
 for(unsigned i=0;i<count;i++){
  int value=mp_ui_motion_value(motions[i],&clock);if(value!=*values[i]){*values[i]=value;changed=1;}
  mp_ui_motion_schedule(motions[i],&clock);
 }
 for(unsigned i=0;i<MP_HOME_CAPTION_TRANSITIONS;i++){
  struct mp_home_caption_motion *c=&v->captions[i];if(!c->used)continue;
  int value=mp_ui_motion_value(&c->motion,&clock);
  if(value!=c->opacity){c->opacity=value;changed=1;}
  mp_ui_motion_schedule(&c->motion,&clock);
  if(c->opacity==1000&&c->motion.target==1000)c->used=0;
 }
 v->next_ms=clock.next_ms;if(changed)++v->generation;return changed;
}
unsigned mp_home_view_wait(const struct mp_home_state *v,uint64_t now)
{if(v->next_ms==UINT64_MAX)return UINT_MAX;uint64_t wait=v->next_ms>now?v->next_ms-now:0;return wait>UINT_MAX?UINT_MAX:(unsigned)wait;}
enum mp_home_action mp_home_view_key(struct mp_home_state *v,const struct mp_home_layout *l,enum mp_home_key key,uint64_t now)
{
 struct mp_ui_clock clock;mp_ui_clock_begin(&clock,now,1);unsigned old_row=v->row,old=v->index[v->row];
 if(v->menu_open){
  if(key==MP_HOME_UP&&v->menu)v->menu--;
  else if(key==MP_HOME_DOWN&&v->menu+1<MP_NAV_COUNT)v->menu++;
  else if(key==MP_HOME_RIGHT||key==MP_HOME_BACK||(key==MP_HOME_SELECT&&v->menu==MP_NAV_HOME)){
   v->menu_open=0;mp_ui_motion_to(&v->drawer,0,l->animation_ms,&clock);
  } else if(key==MP_HOME_SELECT){
   return v->menu<MP_NAV_COUNT?mp_navigation[v->menu].action:MP_HOME_NONE;
  }
 } else if(key==MP_HOME_LEFT){
  if(old)v->index[v->row]--;
  else {v->menu_open=1;v->menu=MP_NAV_HOME;mp_ui_motion_to(&v->drawer,(int)(l->menu_width-l->rail_width),l->animation_ms,&clock);}
 } else if(key==MP_HOME_RIGHT&&old+1<v->rows[v->row].count)v->index[v->row]++;
 else if(key==MP_HOME_UP&&v->row)v->row--;
 else if(key==MP_HOME_DOWN&&v->row+1<v->count)v->row++;
 else if(key==MP_HOME_SELECT)return v->rows[v->row].cards[v->index[v->row]].action;
 if(v->row!=old_row||v->index[v->row]!=old){
  caption_to(v,old_row,old,1000,l->animation_ms,&clock);
  caption_to(v,v->row,v->index[v->row],0,l->animation_ms,&clock);
 }
 if(v->index[v->row]!=old&&(key==MP_HOME_LEFT||key==MP_HOME_RIGHT)){
  v->previous[v->row]=old;v->focus[v->row]=motion(0);mp_ui_motion_to(&v->focus[v->row],1000,l->animation_ms,&clock);
 }
 for(unsigned row=0;row<v->count;row++){
  if(old_row!=v->row || v->menu_open)v->previous[row]=v->index[row];
  int target=row==v->row&&!v->menu_open?1000:0;
  mp_ui_motion_to(&v->focus[row],target,l->animation_ms,&clock);
 }
 struct geometry g=geometry(v,l,v->viewport_width?v->viewport_width:960,v->viewport_height?v->viewport_height:720);
 mp_ui_motion_to(&v->vertical,(int)(g.row_offset[v->row]),l->animation_ms,&clock);
 unsigned visible=(g.main.width+l->card_gap)/(g.card_width+l->card_gap);if(!visible)visible=1;
 unsigned start=v->index[v->row]>=visible?v->index[v->row]-visible+1:0;
 mp_ui_motion_to(&v->horizontal[v->row],(int)(start*(g.card_width+l->card_gap)),l->animation_ms,&clock);
 ++v->generation;return MP_HOME_NONE;
}
struct painting {struct mp_home_state *v;const struct mp_theme *theme;const struct mp_home_layout *layout;struct geometry g;};
static size_t utf8_step(const char *s)
{
 size_t n=1;while(n<4&&s[n]&&((unsigned char)s[n]&0xc0)==0x80)++n;return n;
}
static size_t trim_right(char *s)
{size_t n=strlen(s);while(n&&isspace((unsigned char)s[n-1]))s[--n]=0;return n;}
static size_t sentence_boundary(const char *line)
{
 size_t result=0;
 for(size_t i=0;line[i];i++){
  if(line[i]!='.'&&line[i]!='!'&&line[i]!='?')continue;
  if(line[i]=='.'){
   if((i&&line[i-1]=='.')||line[i+1]=='.')continue;
   size_t start=i;while(start&&(isalnum((unsigned char)line[start-1])||line[start-1]=='.'))--start;
   size_t n=i-start;char word[16]={0};
   if(n==1&&isupper((unsigned char)line[start]))continue;
   if(n<sizeof word){
    for(size_t k=0;k<n;k++)word[k]=(char)tolower((unsigned char)line[start+k]);
    const char *abbreviations[]={"mr","mrs","ms","dr","prof","st","vs","etc","e.g","i.e","no","vol","co"};
    int abbreviated=0;for(unsigned k=0;k<sizeof abbreviations/sizeof abbreviations[0];k++)if(!strcmp(word,abbreviations[k]))abbreviated=1;
    if(abbreviated)continue;
   }
  }
  size_t end=i+1;
  while(line[end]=='\"'||line[end]=='\''||line[end]==')'||line[end]==']')++end;
  if(!strncmp(line+end,"”",3)||!strncmp(line+end,"’",3))end+=3;
  if(!line[end]||isspace((unsigned char)line[end]))result=end;
 }
 return result;
}
void mp_home_text_wrap(const char *source,unsigned width,unsigned height,unsigned max_lines,int truncate,struct mp_home_text_layout *layout)
{
 memset(layout,0,sizeof *layout);
 if(!source||!height||height>112||!width)return;
 if(max_lines>MP_HOME_TEXT_LINES)max_lines=MP_HOME_TEXT_LINES;
 unsigned advance=(MP_FONT_ADVANCE*height+3)/MP_FONT_CAP_HEIGHT;
 const char *cursor=source;
 while(*cursor&&layout->count<max_lines){
  while(*cursor==' '||*cursor=='\t')++cursor;
  if(!*cursor)break;
  unsigned n=0,take=0,space=0,pen=0,extent=0;
  while(cursor[n]&&cursor[n]!='\n'&&n<MP_HOME_TEXT_BYTES-5){
   size_t length=utf8_step(cursor+n);unsigned end=n+(unsigned)length;if(end>=MP_HOME_TEXT_BYTES)break;
   char glyph[5];memcpy(glyph,cursor+n,length);glyph[length]=0;
   unsigned candidate=pen+mp_font_width_px(glyph,height);if(candidate<extent)candidate=extent;
   if(candidate>width)break;
   if(cursor[n]==' '||cursor[n]=='\t')space=n;
   take=end;n=end;extent=candidate;pen+=advance;
  }
  if(!take)break;
  if(cursor[take]&&cursor[take]!='\n'&&!isspace((unsigned char)cursor[take])&&space)take=space;
  char *line=layout->lines[layout->count++];memcpy(line,cursor,take);line[take]=0;trim_right(line);
  cursor+=take;if(*cursor=='\n'||*cursor==' '||*cursor=='\t')++cursor;
 }
 while(isspace((unsigned char)*cursor))++cursor;
 layout->truncated=!!*cursor;
 if(truncate&&layout->truncated&&layout->count){
  char *last=layout->lines[layout->count-1];size_t boundary=truncate==MP_HOME_TEXT_SENTENCE_ELLIPSIS?sentence_boundary(last):0;
  if(boundary){last[boundary]=0;layout->sentence_end=1;}
  else {
   size_t n=trim_right(last);char marked[MP_HOME_TEXT_BYTES];
   for(;;){
    if(n+4<=sizeof marked){memcpy(marked,last,n);memcpy(marked+n,"...",4);if(mp_font_width_px(marked,height)<=width){memcpy(last,marked,n+4);break;}}
    if(!n){last[0]=0;break;}
    size_t word=n;while(word&&!isspace((unsigned char)last[word-1]))--word;
    if(word){n=word;while(n&&isspace((unsigned char)last[n-1]))--n;}
    else {--n;while(n&&((unsigned char)last[n]&0xc0)==0x80)--n;}
    last[n]=0;
   }
  }
 }
 if(layout->count){
  int first=0,last=0,found=0;unsigned pitch=mp_font_line_height_px(height);
  for(unsigned i=0;i<layout->count;i++){
   int top,bottom;mp_font_ink_bounds_px(layout->lines[i],height,&top,&bottom);
   if(bottom<=top)continue;
   top+=(int)(i*pitch);bottom+=(int)(i*pitch);
   if(!found||top<first)first=top;
   if(!found||bottom>last)last=bottom;
   found=1;
  }
  layout->ink_top=first;layout->ink_height=(unsigned)(last-first);
  layout->line_height=pitch;layout->height=layout->box_height=layout->count*pitch;
 }
}
unsigned mp_home_text_last_line_offset(const struct mp_home_text_layout *layout,unsigned height)
{return layout&&layout->count?(layout->count-1)*mp_font_line_height_px(height):0;}
static unsigned wrapped_alpha(const struct mp_paint *p,int x,int y,unsigned width,unsigned height,const char *source,uint32_t color,unsigned max_lines,unsigned alpha)
{
 struct mp_home_text_layout layout;mp_home_text_wrap(source,width,height,max_lines,0,&layout);
 if(p)for(unsigned i=0;i<layout.count;i++)mp_font_draw_px_alpha(&p->surface,x-p->x,y-p->y+(int)(i*layout.line_height),height,layout.lines[i],color,alpha);
 return layout.box_height;
}
static unsigned wrapped_px(const struct mp_paint *p,int x,int y,unsigned width,unsigned height,const char *source,uint32_t color,unsigned max_lines)
{return wrapped_alpha(p,x,y,width,height,source,color,max_lines,255);}
void mp_home_caption_text(const char *title,char *output,size_t size)
{
 if(!size)return;
 while(isspace((unsigned char)*title))++title;
 const char *end=title+strlen(title);while(end>title&&isspace((unsigned char)end[-1]))--end;
 const char *split=strchr(title,':');size_t delimiter=1;
 if(!split){
  const char *separators[]={" - "," – "," — ","："};
  for(unsigned i=0;i<sizeof separators/sizeof separators[0];i++){
   const char *candidate=strstr(title,separators[i]);
   if(candidate&&(!split||candidate<split)){split=candidate;delimiter=strlen(separators[i]);}
  }
 }
 const char *first_end=split?split:end,*second=split?split+delimiter:end;
 while(first_end>title&&isspace((unsigned char)first_end[-1]))--first_end;
 while(second<end&&isspace((unsigned char)*second))++second;
 size_t used=0;
 for(const char *at=title;at<first_end&&used+1<size;) {
  size_t length=1;while(at+length<first_end&&((unsigned char)at[length]&0xc0)==0x80)++length;
  if(used+length>=size)break;
  memcpy(output+used,at,length);used+=length;at+=length;
 }
 if(split&&second<end&&used+1<size){
  output[used++]='\n';
  for(const char *at=second;at<end&&used+1<size;){
   size_t length=1;while(at+length<end&&((unsigned char)at[length]&0xc0)==0x80)++length;
   if(used+length>=size)break;
   memcpy(output+used,at,length);used+=length;at+=length;
  }
 }
 output[used]=0;
}
static const struct mp_home_card *selected(struct painting *c)
{return &c->v->rows[c->v->row].cards[c->v->index[c->v->row]];}
static void background(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;const struct mp_home_layout *l=c->layout;
 unsigned width=c->v->viewport_width,height=c->v->viewport_height;
 unsigned w=mp_home_artwork_width(l,width),h=height*l->artwork_height_percent/100;
 uint32_t color=c->theme->status.box.background;
 /* Artwork writes opaque, already faded colors. Omit its covered rectangle
  * only when no other painter can read the background before artwork runs.
  * Equal layers are safe: artwork registers immediately after background. */
 unsigned layer=l->artwork_layer;
 int covered=(selected(c)->art||(c->v->startup_background&&!strcmp(selected(c)->id,c->v->startup_id)))&&w&&h&&h<=2160&&layer>=l->background_layer&&
     layer<=l->details_layer&&layer<=l->shelves_layer&&layer<=l->focus_layer&&
     layer<=l->navigation_layer&&layer<=l->status_layer&&
     (!c->v->font_debug||mp_home_item_has_font(c->v->debug_target)||layer<=9);
 int left=(int)width-(int)w+c->v->drawer_px,right=left+(int)w;
 if(left<0)left=0;
 if(right>(int)width)right=(int)width;
 if(!covered||right<=left){mp_paint_fill(p,(struct mp_ui_rect){0,0,width,height},color);return;}
 unsigned bottom=cap(h,height);
 mp_paint_fill(p,(struct mp_ui_rect){0,0,(unsigned)left,height},color);
 mp_paint_fill(p,(struct mp_ui_rect){right,0,width-(unsigned)right,height},color);
 mp_paint_fill(p,(struct mp_ui_rect){left,(int)bottom,(unsigned)(right-left),height-bottom},color);
}
static struct mp_asset_spec background_spec(const struct mp_game_art *art,const struct mp_theme *theme,const struct mp_home_layout *l,unsigned width,unsigned height)
{return (struct mp_asset_spec){art,mp_game_art_identity(art),MP_ASSET_CURVED_BACKGROUND,width,height,theme->status.box.background,l->mask_clear_left_percent,l->mask_vertical_inset_percent,l->mask_text_shade_percent};}
static struct mp_asset_spec card_spec(const struct mp_game_art *art,unsigned width,unsigned height)
{
 unsigned w=width,h=(unsigned)((uint64_t)w*art->height/art->width);
 if(h>height){h=height;w=(unsigned)((uint64_t)h*art->width/art->height);}
 return (struct mp_asset_spec){art,mp_game_art_identity(art),MP_ASSET_CARD,w,h,0,0,0,0};
}
static void paint_asset(const struct mp_paint *p,const struct mp_asset_lease *lease,int left,int top)
{
 int x0=p->x>left?p->x-left:0,y0=p->y>top?p->y-top:0;
 int x1=p->x+(int)p->surface.width-left,y1=p->y+(int)p->surface.height-top;
 if(x1>(int)lease->width)x1=(int)lease->width;
 if(y1>(int)lease->height)y1=(int)lease->height;
 if(x0>=x1||y0>=y1)return;
 for(int x=x0;x<x1;x++){
  uint32_t *dst=p->surface.pixels+(left+x-p->x)*p->surface.x_stride+(top+y0-p->y)*p->surface.y_stride;
  const uint32_t *src=lease->pixels+(size_t)x*lease->height+y0;
  if(p->surface.y_stride==1)memcpy(dst,src,(size_t)(y1-y0)*sizeof *src);
  else for(int y=y0;y<y1;y++){*dst=*src++;dst+=p->surface.y_stride;}
 }
}
static int cache_matches(const struct mp_home_state *v,const struct mp_theme *theme,const struct mp_home_layout *l,const struct mp_game_art *art,unsigned w,unsigned h)
{
 return v->art_cache&&v->art_cached_art==art&&v->art_cache_width==w&&v->art_cache_height==h&&
  v->art_cache_background==theme->status.box.background&&v->cached_fade_left==l->mask_clear_left_percent&&
  v->cached_fade_bottom==l->mask_vertical_inset_percent&&v->cached_top_contrast==l->mask_text_shade_percent;
}
static void artwork(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;struct mp_home_state *v=c->v;const struct mp_game_art *art=mp_game_background(selected(c)->art);
 unsigned w=mp_home_artwork_width(c->layout,v->viewport_width),h=v->viewport_height*c->layout->artwork_height_percent/100;if(!w||!h)return;
 int left=(int)v->viewport_width-(int)w+v->drawer_px;
 struct mp_asset_spec spec=background_spec(art,c->theme,c->layout,w,h);struct mp_asset_lease lease;
 if(art&&v->assets&&mp_asset_cache_acquire(v->assets,&spec,&lease)){paint_asset(p,&lease,left,0);mp_asset_cache_release(&lease);return;}
 if(v->startup_background&&!strcmp(selected(c)->id,v->startup_id)){
  lease=(struct mp_asset_lease){NULL,0,v->startup_background,960,720};paint_asset(p,&lease,left,0);return;
 }
 if(!art)return;
 if(cache_matches(v,c->theme,c->layout,art,w,h)){
  lease=(struct mp_asset_lease){NULL,0,v->art_cache,w,h};paint_asset(p,&lease,left,0);return;
 }
 int x0=p->x>left?p->x-left:0,y0=p->y>0?p->y:0;
 int x1=p->x+(int)p->surface.width-left,y1=p->y+(int)p->surface.height;
 if(x1>(int)w)x1=(int)w;
 if(y1>(int)h)y1=(int)h;
 if(x0>=x1||y0>=y1)return;
 uint32_t *dst=p->surface.pixels+(left+x0-p->x)*p->surface.x_stride+(y0-p->y)*p->surface.y_stride;
 (void)mp_asset_render_region(&spec,(unsigned)x0,(unsigned)y0,(unsigned)(x1-x0),(unsigned)(y1-y0),dst,p->surface.x_stride,p->surface.y_stride);
}
void mp_home_view_dispose(struct mp_home_state *v)
{mp_asset_cache_close(v->assets);v->assets=NULL;free(v->art_cache);v->art_cache=NULL;v->art_cached_art=NULL;}
int mp_home_assets_start(struct mp_home_state *v,const char *root,int (*prepare_root)(void *),void *context)
{
 if(v->assets)return 0;
 v->assets=mp_asset_cache_open(root,8*1024*1024,prepare_root,context);return v->assets?0:-1;
}
int mp_home_view_prepare_cache(struct mp_home_state *v,const struct mp_theme *theme,const struct mp_home_layout *l)
{
 const struct mp_game_art *art=mp_game_background(v->rows[v->row].cards[v->index[v->row]].art);
 unsigned w=mp_home_artwork_width(l,v->viewport_width),h=v->viewport_height*l->artwork_height_percent/100;
 if(v->assets){
  if(art){struct mp_asset_spec spec=background_spec(art,theme,l,w,h);(void)mp_asset_cache_request(v->assets,&spec);}
  struct geometry g=geometry(v,l,v->viewport_width,v->viewport_height);
  for(unsigned row=0;row<v->count;row++)for(unsigned i=0;i<v->rows[row].count;i++){
   const struct mp_game_art *source=v->rows[row].cards[i].art;if(!source)continue;
   for(unsigned focus=0;focus<2;focus++){
    unsigned cw=g.card_width,ch=g.card_height;
    if(focus){cw+=cw*(l->focus_percent-100)/100;ch+=ch*(l->focus_percent-100)/100;}
    struct mp_asset_spec spec=card_spec(source,cw,ch);(void)mp_asset_cache_request(v->assets,&spec);
   }
  }
  unsigned next=(v->index[v->row]+1)%v->rows[v->row].count;
  const struct mp_game_art *next_art=mp_game_background(v->rows[v->row].cards[next].art);
  if(next_art){struct mp_asset_spec spec=background_spec(next_art,theme,l,w,h);(void)mp_asset_cache_request(v->assets,&spec);}
  return 0;
 }
 if(!art||cache_matches(v,theme,l,art,w,h))return 0;
 if(!w||!h||w>3840||h>2160)return -1;
 uint32_t *pixels=malloc((size_t)w*h*sizeof *pixels);if(!pixels)return -1;
 struct mp_paint paint={.surface={pixels,w,h,(int)h,1},.x=(int)v->viewport_width-(int)w,.y=0};
 struct painting c={.v=v,.theme=theme,.layout=l};
 int drawer=v->drawer_px;v->drawer_px=0;artwork(&paint,&c);v->drawer_px=drawer;
 free(v->art_cache);v->art_cache=pixels;v->art_cache_width=w;v->art_cache_height=h;v->art_cached_art=art;
 v->art_cache_background=theme->status.box.background;v->cached_fade_left=l->mask_clear_left_percent;
 v->cached_fade_bottom=l->mask_vertical_inset_percent;v->cached_top_contrast=l->mask_text_shade_percent;
 return 0;
}
static int target_bounds(const struct mp_home_state *v,const struct geometry *g,enum mp_home_item_role role,struct mp_ui_rect *box)
{
 if(role==MP_HOME_FONT_TITLE)*box=g->title;
 else if(role==MP_HOME_FONT_META)*box=g->metadata;
 else if(role==MP_HOME_FONT_DESCRIPTION)*box=g->description;
 else if(role==MP_HOME_LAYOUT_INFO)*box=g->context;
 else if(role==MP_HOME_LAYOUT_GAMES||role==MP_HOME_LAYOUT_CONSOLES){
  for(unsigned row=0;row<v->count;row++)if(row_role(v,row)==role){
   *box=(struct mp_ui_rect){g->main.x,g->shelves.y+(int)g->row_top[row]-v->vertical_px,g->main.width,g->row_height[row]};box->x+=v->drawer_px;return 1;
  }
  return 0;
 } else return 0;
 box->x+=v->drawer_px;return 1;
}
int mp_home_debug_bounds(const struct mp_home_state *v,const struct mp_home_layout *l,unsigned width,unsigned height,enum mp_home_item_role role,struct mp_ui_rect *box)
{struct geometry g=geometry(v,l,width,height);return target_bounds(v,&g,role,box);}
unsigned mp_home_debug_visible(const struct mp_home_state *v,const struct mp_home_layout *l,unsigned width,unsigned height)
{
 struct geometry g=geometry(v,l,width,height);unsigned mask=0;
 if(g.title.height&&g.title.y<(int)height&&g.title.x+v->drawer_px<(int)width)mask|=1u<<MP_HOME_FONT_TITLE;
 if(g.metadata.height&&g.metadata.y<(int)height&&g.metadata.x+v->drawer_px<(int)width)mask|=1u<<MP_HOME_FONT_META;
 if(g.description.height&&g.description.y<(int)height&&g.description.x+v->drawer_px<(int)width)mask|=1u<<MP_HOME_FONT_DESCRIPTION;
 for(unsigned row=0;row<v->count;row++){
  int top=g.shelves.y+(int)(g.row_top[row])-v->vertical_px;
  if(g.heading_ink[row]&&top<(int)height&&top+(int)v->font_height[MP_HOME_FONT_SHELF]>=g.shelves.y-12)mask|=1u<<MP_HOME_FONT_SHELF;
  int image_top=top+(int)g.heading[row];
  if(image_top<(int)height&&image_top+(int)g.card_height>=g.shelves.y-12)
   for(unsigned i=0;i<v->rows[row].count;i++){
    int left=g.shelves.x+v->drawer_px+(int)(i*(g.card_width+l->card_gap))-v->horizontal_px[row];
    if(left<(int)width&&left+(int)g.card_width>(int)l->rail_width&&!v->rows[row].cards[i].art){mask|=1u<<MP_HOME_FONT_BADGE;break;}
   }
  if(image_top<(int)height&&image_top+(int)g.card_height>=g.shelves.y-12)
   for(unsigned i=0;i<v->rows[row].count;i++){
    int left=g.shelves.x+v->drawer_px+(int)(i*(g.card_width+l->card_gap))-v->horizontal_px[row];
    if(left<(int)width&&left+(int)g.card_width>(int)l->rail_width&&(!v->rows[row].cards[i].art||mp_home_caption_opacity(v,row,i)))mask|=1u<<MP_HOME_FONT_CARD;
   }
 }
 struct mp_ui_rect box;
 if(target_bounds(v,&g,MP_HOME_LAYOUT_INFO,&box)&&box.y<(int)height&&box.y+(int)box.height>0)mask|=1u<<MP_HOME_LAYOUT_INFO;
 for(unsigned role=MP_HOME_LAYOUT_GAMES;role<=MP_HOME_LAYOUT_CONSOLES;role++)
  if(target_bounds(v,&g,(enum mp_home_item_role)role,&box)&&box.y<(int)height&&box.y+(int)box.height>g.shelves.y-12&&box.x<(int)width)mask|=1u<<role;
 if(l->rail_width+(unsigned)v->drawer_px>150)mask|=(1u<<MP_HOME_FONT_NAV)|(1u<<MP_HOME_FONT_BRAND);
 return mask;
}
static uint32_t font_color(const struct painting *c,enum mp_home_item_role role,uint32_t normal)
{return c->v->font_debug&&c->v->debug_target==role?MP_FONT_DEBUG_RED:normal;}
static void details(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;const struct mp_home_card *card=selected(c);
 unsigned title_height=c->v->font_height[MP_HOME_FONT_TITLE];
 for(unsigned i=0;i<c->g.title_text.count;i++)
  mp_font_draw_px(&p->surface,c->g.title.x+c->v->drawer_px-p->x,c->g.title.y-p->y+(int)(i*c->g.title_text.line_height),title_height,c->g.title_text.lines[i],font_color(c,MP_HOME_FONT_TITLE,c->theme->colors.status_text));
 (void)wrapped_px(p,c->g.metadata.x+c->v->drawer_px,c->g.metadata.y,c->g.metadata.width,c->v->font_height[MP_HOME_FONT_META],card->metadata,font_color(c,MP_HOME_FONT_META,c->theme->status.box.frame.mid),2);
 unsigned height=c->v->font_height[MP_HOME_FONT_DESCRIPTION];
 for(unsigned i=0;i<c->g.description_text.count;i++)
  mp_font_draw_px(&p->surface,c->g.description.x+c->v->drawer_px-p->x,c->g.description.y-p->y+(int)(i*c->g.description_text.line_height),height,c->g.description_text.lines[i],font_color(c,MP_HOME_FONT_DESCRIPTION,c->theme->colors.status_text));
}
static void card_caption(const struct mp_paint *p,struct painting *c,const struct mp_home_card *card,struct mp_ui_rect r,unsigned row,unsigned index)
{
 unsigned opacity=mp_home_caption_opacity(c->v,row,index);if(!opacity)return;
 unsigned padding=cap(c->layout->caption_padding,r.width/4),width=r.width-2*padding;
 char title[256];mp_home_caption_text(card->title,title,sizeof title);
 unsigned height=c->v->font_height[MP_HOME_FONT_CARD];
 unsigned ink=wrapped_px(NULL,0,0,width,height,title,0,2);
 unsigned top_padding=padding+c->v->margin_top[MP_HOME_FONT_CARD],bottom_padding=padding+c->v->margin_bottom[MP_HOME_FONT_CARD];
 unsigned strip=cap(ink+top_padding+bottom_padding,r.height);
 struct mp_paint clipped;if(!mp_paint_clip(p,r,&clipped))return;
 struct mp_ui_rect box={r.x,r.y+(int)(r.height-strip),r.width,strip};
 mp_paint_fill_alpha(&clipped,box,c->theme->status.box.background,(c->layout->caption_background_alpha*opacity+500)/1000);
 (void)wrapped_alpha(&clipped,box.x+(int)padding,box.y+(int)top_padding,width,height,title,font_color(c,MP_HOME_FONT_CARD,c->theme->colors.status_text),2,(opacity*255+500)/1000);
}
/* Coordinates depend on dimensions, not image identity, palette or position.
 * A bounded cache shares maps across cards and frames without a raster cache. */
static const struct mp_home_scale_map *card_scale_map(struct mp_home_state *v,const struct mp_game_art *art,unsigned width,unsigned height)
{
 if(!width||!height||width>MP_HOME_SCALE_EDGE||height>MP_HOME_SCALE_EDGE)return NULL;
 for(unsigned i=0;i<MP_HOME_SCALE_MAPS;i++){
  const struct mp_home_scale_map *map=&v->scale_maps[i];
  if(map->width==width&&map->height==height&&map->source_width==art->width&&map->source_height==art->height){++v->scale_hits;return map;}
 }
 struct mp_home_scale_map *map=&v->scale_maps[v->scale_next];v->scale_next=(v->scale_next+1)%MP_HOME_SCALE_MAPS;
 map->source_width=art->width;map->source_height=art->height;map->width=width;map->height=height;
 for(unsigned x=0;x<width;x++)map->x[x]=(unsigned)((uint64_t)x*art->width/width);
 for(unsigned y=0;y<height;y++)map->row[y]=(unsigned)((uint64_t)y*art->height/height)*art->width;
 ++v->scale_builds;return map;
}
static void card_image_direct(const struct mp_paint *p,struct painting *c,const struct mp_home_card *card,struct mp_ui_rect r,unsigned row,unsigned index)
{
 const struct mp_pixel_frame *f=&c->theme->status.box.frame;
 mp_paint_fill(p,r,f->recess);
 if(card->art){
  const struct mp_game_art *a=card->art;
  unsigned w=r.width,h=(unsigned)((uint64_t)w*a->height/a->width);
  if(h>r.height){h=r.height;w=(unsigned)((uint64_t)h*a->width/a->height);}
  int left=r.x+(int)(r.width-w)/2,top=r.y+(int)(r.height-h)/2;
  int x0=p->x>left?p->x-left:0,y0=p->y>top?p->y-top:0;
  int x1=p->x+(int)p->surface.width-left,y1=p->y+(int)p->surface.height-top;
  if(x1>(int)w)x1=(int)w;
  if(y1>(int)h)y1=(int)h;
  struct mp_asset_lease lease={0};int prepared=0;
  if(c->v->assets){struct mp_asset_spec spec=card_spec(a,r.width,r.height);prepared=mp_asset_cache_acquire(c->v->assets,&spec,&lease);}
  if(prepared){paint_asset(p,&lease,left,top);mp_asset_cache_release(&lease);}
  else {
  const struct mp_home_scale_map *map=x0<x1&&y0<y1?card_scale_map(c->v,a,w,h):NULL;
  if(map){
   for(int x=x0;x<x1;x++){
    const uint8_t *source=a->pixels+map->x[x];
    uint32_t *dst=p->surface.pixels+(left+x-p->x)*p->surface.x_stride+(top+y0-p->y)*p->surface.y_stride;
    for(int y=y0;y<y1;y++){*dst=a->palette[source[map->row[y]]];dst+=p->surface.y_stride;}
   }
  } else {
  /* Fixed-size row blocks keep stack use bounded for any image dimensions. */
  for(int first=y0;first<y1&&x0<x1;first+=256){
   int end=first+256;if(end>y1)end=y1;
   unsigned source_row[256];
   for(int y=first;y<end;y++)source_row[y-first]=(unsigned)((uint64_t)(unsigned)y*a->height/h)*a->width;
   for(int x=x0;x<x1;x++){
    unsigned sx=(unsigned)((uint64_t)(unsigned)x*a->width/w);
    const uint8_t *source=a->pixels+sx;
    uint32_t *dst=p->surface.pixels+(left+x-p->x)*p->surface.x_stride+(top+first-p->y)*p->surface.y_stride;
    for(int y=first;y<end;y++){
     *dst=a->palette[source[source_row[y-first]]];
     dst+=p->surface.y_stride;
    }
   }
  }
  }
  }
  card_caption(p,c,card,r,row,index);
 } else {
  /* A console tile is interface geometry, not invented game box artwork. */
  unsigned body_w=cap(r.width-20,150),body_h=cap(r.height-20,92);
  struct mp_ui_rect body={r.x+(int)(r.width-body_w)/2,r.y+(int)(r.height-body_h)/2,body_w,body_h};
  mp_paint_fill(p,body,f->face);mp_paint_fill(p,(struct mp_ui_rect){body.x+body.width/4,body.y+10,body.width/2,body.height-20},f->shadow);
  mp_font_draw_px(&p->surface,body.x+body.width/4+8-p->x,body.y+(int)body.height/2-(int)c->v->font_height[MP_HOME_FONT_BADGE]/2-p->y,c->v->font_height[MP_HOME_FONT_BADGE],"GBA",font_color(c,MP_HOME_FONT_BADGE,f->highlight));
 }
}
/* Reuse the coordinate maps while composing translucent captions in RAM.
 * The display receives opaque writes; no full-card raster is retained. */
static void card_image(const struct mp_paint *p,struct painting *c,const struct mp_home_card *card,struct mp_ui_rect r,unsigned row,unsigned index)
{
 if(!card->art||r.width>MP_HOME_SCALE_EDGE||!mp_home_caption_opacity(c->v,row,index)){
  card_image_direct(p,c,card,r,row,index);return;
 }
 struct mp_paint clipped;if(!mp_paint_clip(p,r,&clipped))return;
 unsigned padding=cap(c->layout->caption_padding,r.width/4),width=r.width-2*padding;
 char title[256];mp_home_caption_text(card->title,title,sizeof title);
 unsigned ink=wrapped_px(NULL,0,0,width,c->v->font_height[MP_HOME_FONT_CARD],title,0,2);
 unsigned strip=cap(ink+2*padding+c->v->margin_top[MP_HOME_FONT_CARD]+c->v->margin_bottom[MP_HOME_FONT_CARD],r.height);
 int split=r.y+(int)(r.height-strip);
 struct mp_paint above;
 if(mp_paint_clip(p,(struct mp_ui_rect){r.x,r.y,r.width,r.height-strip},&above))
  card_image_direct(&above,c,card,r,row,index);
 int first=split>clipped.y?split:clipped.y,end=clipped.y+(int)clipped.surface.height;
 uint32_t pixels[MP_HOME_SCALE_EDGE*32];
 for(int y=first;y<end;y+=32){
  unsigned height=cap((unsigned)(end-y),32);
  struct mp_paint band={.surface={pixels,clipped.surface.width,height,(ptrdiff_t)height,1},.x=clipped.x,.y=y};
  card_image_direct(&band,c,card,r,row,index);
  for(unsigned x=0;x<band.surface.width;x++){
   uint32_t *dst=clipped.surface.pixels+x*clipped.surface.x_stride+(y-clipped.y)*clipped.surface.y_stride;
   const uint32_t *src=pixels+x*height;
   if(clipped.surface.y_stride==1)memcpy(dst,src,height*sizeof *src);
   else for(unsigned iy=0;iy<height;iy++){*dst=*src++;dst+=clipped.surface.y_stride;}
  }
 }
}
static struct mp_ui_rect card_bounds(struct painting *c,unsigned row,unsigned index,int growth)
{
 unsigned w=c->g.card_width,h=c->g.card_height;
 int x=c->g.shelves.x+c->v->drawer_px+(int)(index*(w+c->layout->card_gap))-c->v->horizontal_px[row];
 int y=c->g.shelves.y+(int)(c->g.row_top[row])-c->v->vertical_px+(int)c->g.heading[row];
 unsigned extra_w=w*(c->layout->focus_percent-100)*(unsigned)growth/100000;
 unsigned extra_h=h*(c->layout->focus_percent-100)*(unsigned)growth/100000;
 return (struct mp_ui_rect){x-(int)extra_w/2,y-(int)extra_h/2,w+extra_w,h+extra_h};
}
int mp_home_view_startup_plan(const struct mp_home_state *view,const struct mp_home_layout *layout,unsigned width,unsigned height,struct mp_home_startup_plan *plan)
{
 if(!view||!layout||!plan||!view->count||!width||!height||view->rows[0].count>32||view->index[0]>=view->rows[0].count)return -1;
 struct painting painting={.v=(struct mp_home_state *)view,.layout=layout,.g=geometry(view,layout,width,height)};
 unsigned selected=view->index[0];
 *plan=(struct mp_home_startup_plan){selected,selected+1,0,painting.g.heading[0],painting.g.row_height[0]};
 int clip_top=painting.g.shelves.y-12;if(clip_top<0)clip_top=0;
 for(unsigned i=0;i<view->rows[0].count;i++){
  struct mp_ui_rect box=card_bounds(&painting,0,i,i==selected?1000:0);
  if(!view->rows[0].cards[i].art)box.height+=painting.g.row_height[0]-painting.g.heading[0]-painting.g.card_height;
  if(i==selected){box.x-=3;box.y-=3;box.width+=6;box.height+=6;}
  if(box.x<(int)width&&box.x+(int)box.width>(int)layout->rail_width&&box.y<(int)height&&box.y+(int)box.height>clip_top){
   if(i<plan->first)plan->first=i;
   if(i>=plan->last)plan->last=i+1;
   plan->visible_mask|=1u<<i;
  }
 }
 return 0;
}
static void shelves(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;const struct mp_pixel_frame *f=&c->theme->status.box.frame;
 for(unsigned row=0;row<c->v->count;row++){
  if(c->g.heading_ink[row])(void)wrapped_px(p,c->g.shelves.x+c->v->drawer_px,c->g.shelves.y+(int)c->g.heading_y[row]-c->v->vertical_px,c->g.main.width,c->v->font_height[MP_HOME_FONT_SHELF],c->v->rows[row].title,font_color(c,MP_HOME_FONT_SHELF,c->theme->colors.status_text),1);
  unsigned step=c->g.card_width+c->layout->card_gap;if(!step)continue;
  unsigned start=(unsigned)(c->v->horizontal_px[row]>0?c->v->horizontal_px[row]:0)/step;
  unsigned visible=c->g.main.width/step+3;
  for(unsigned i=start;i<c->v->rows[row].count&&i<start+visible;i++){
   struct mp_ui_rect r=card_bounds(c,row,i,0);if(r.y>(int)c->v->viewport_height||r.y+(int)r.height<0)continue;
   int amount=c->v->focus_px[row];
   int focused=amount||(row==c->v->row&&!c->v->menu_open);
   int covered=focused&&(i==c->v->index[row]||
       (c->v->previous[row]!=c->v->index[row]&&amount<1000&&i==c->v->previous[row]));
   /* The later opaque focus card covers its complete base rectangle. */
   if(!covered||c->layout->focus_layer<c->layout->shelves_layer)
    card_image(p,c,&c->v->rows[row].cards[i],r,row,i);
   if(!c->v->rows[row].cards[i].art)(void)wrapped_px(p,r.x,r.y+(int)r.height+(int)(c->v->margin_bottom[MP_HOME_FONT_BADGE]+c->v->margin_top[MP_HOME_FONT_CARD]),r.width,c->v->font_height[MP_HOME_FONT_CARD],c->v->rows[row].cards[i].title,font_color(c,MP_HOME_FONT_CARD,row==c->v->row&&i==c->v->index[row]?c->theme->colors.status_text:f->mid),2);
  }
 }
}
static void focus(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;
 for(unsigned row=0;row<c->v->count;row++){
  int amount=c->v->focus_px[row];
  if(!amount&&(row!=c->v->row||c->v->menu_open))continue;
  struct mp_ui_rect r=card_bounds(c,row,c->v->index[row],amount);
  if(c->v->previous[row]!=c->v->index[row]&&amount<1000){
   struct mp_ui_rect old=card_bounds(c,row,c->v->previous[row],1000-amount);
   card_image(p,c,&c->v->rows[row].cards[c->v->previous[row]],old,row,c->v->previous[row]);
  }
  if(row==c->v->row&&!c->v->menu_open)
   mp_paint_fill(p,(struct mp_ui_rect){r.x-3,r.y-3,r.width+6,r.height+6},c->theme->status.box.frame.highlight);
  card_image(p,c,&c->v->rows[row].cards[c->v->index[row]],r,row,c->v->index[row]);
 }
}
static void navigation(const struct mp_paint *p,void *ctx)
{
 struct painting *c=ctx;const struct mp_pixel_frame *f=&c->theme->status.box.frame;
 unsigned w=c->layout->rail_width+(unsigned)c->v->drawer_px;
 /* The navigation rail shares the global background and artwork layers. */
 unsigned base=c->layout->menu_top>=c->layout->brand_text_height+c->layout->brand_margin_bottom?c->layout->menu_top-c->layout->brand_text_height-c->layout->brand_margin_bottom:0;
 unsigned brand_top=base+c->v->margin_top[MP_HOME_FONT_BRAND];
 if(w>150)(void)wrapped_px(p,18,(int)brand_top,c->g.main.width,c->v->font_height[MP_HOME_FONT_BRAND],"MINPOCKET",font_color(c,MP_HOME_FONT_BRAND,c->theme->colors.status_text),1);
 unsigned row_height=32;
 for(unsigned i=0;i<MP_NAV_COUNT;i++){unsigned used=text_box_height(mp_navigation[i].name,c->v->font_height[MP_HOME_FONT_NAV])+2;if(used>row_height)row_height=used;}
 unsigned start=brand_top+text_box_height("MINPOCKET",c->v->font_height[MP_HOME_FONT_BRAND])+c->v->margin_bottom[MP_HOME_FONT_BRAND];
 for(unsigned i=0;i<MP_NAV_COUNT;i++){
  unsigned pitch=c->v->margin_top[MP_HOME_FONT_NAV]+row_height+c->v->margin_bottom[MP_HOME_FONT_NAV];
  int y=(int)(start+c->v->margin_top[MP_HOME_FONT_NAV]+i*pitch);
  int active=c->v->menu_open&&c->v->menu==i;
  if(active)mp_paint_fill(p,(struct mp_ui_rect){6,y-12,w>12?w-12:0,56},c->theme->colors.status_text);
  const uint32_t colors[]={0,f->shadow,f->mid,f->highlight,f->light};
  for(unsigned row=0;row<16;row++)for(unsigned col=0;col<16;col++){
   unsigned role=mp_navigation[i].pixels[row][col];if(!role)continue;
   uint32_t ink=active?f->recess:colors[role];
   mp_paint_fill(p,(struct mp_ui_rect){((int)c->layout->rail_width-32)/2+(int)col*2,y+(int)row*2,2,2},ink);
  }
  if(w>150)(void)wrapped_px(p,64,y+5,c->layout->menu_width>64?c->layout->menu_width-64:0,c->v->font_height[MP_HOME_FONT_NAV],mp_navigation[i].name,font_color(c,MP_HOME_FONT_NAV,active?f->recess:c->theme->colors.status_text),1);
 }
}
static void debug_outline(const struct mp_paint *p,void *context)
{
 struct painting *c=context;struct mp_ui_rect box;
 if(!c->v->font_debug||mp_home_item_has_font(c->v->debug_target)||!target_bounds(c->v,&c->g,c->v->debug_target,&box)||!box.width||!box.height)return;
 mp_paint_fill(p,(struct mp_ui_rect){box.x,box.y,box.width,1},MP_FONT_DEBUG_RED);
 mp_paint_fill(p,(struct mp_ui_rect){box.x,box.y+(int)box.height-1,box.width,1},MP_FONT_DEBUG_RED);
 mp_paint_fill(p,(struct mp_ui_rect){box.x,box.y,1,box.height},MP_FONT_DEBUG_RED);
 mp_paint_fill(p,(struct mp_ui_rect){box.x+(int)box.width-1,box.y,1,box.height},MP_FONT_DEBUG_RED);
}
void mp_home_view_draw(struct mp_home_state *v,const struct mp_theme *theme,const struct mp_home_layout *layout,
                       const struct mp_font_surface *surface,mp_scene_painter overlay,void *context)
{
 v->viewport_width=surface->width;v->viewport_height=surface->height;
 struct painting c={v,theme,layout,geometry(v,layout,surface->width,surface->height)};
 struct mp_scene scene={0};struct mp_ui_rect full={0,0,surface->width,surface->height};
 struct mp_ui_rect main={ (int)layout->rail_width,0,surface->width>layout->rail_width?surface->width-layout->rail_width:0,surface->height};
 (void)mp_scene_add(&scene,layout->background_layer,full,background,&c);
 (void)mp_scene_add(&scene,layout->artwork_layer,full,artwork,&c);
 struct mp_ui_rect info=c.g.context_paint;info.x+=v->drawer_px;unsigned title_width=cap(mp_home_title_wrap_width(layout,surface->width),info.width);
 unsigned body_width=info.width*layout->info_width_percent/100;info.width=title_width>body_width?title_width:body_width;
 unsigned description_width=cap(mp_home_description_wrap_width(layout,surface->width),c.g.context.width);if(description_width>info.width)info.width=description_width;
 (void)mp_scene_add(&scene,layout->details_layer,info,details,&c);
 struct mp_ui_rect rows=main;rows.y=c.g.shelves.y-12;rows.height=surface->height>(unsigned)rows.y?surface->height-(unsigned)rows.y:0;
 (void)mp_scene_add(&scene,layout->shelves_layer,rows,shelves,&c);
 (void)mp_scene_add(&scene,layout->focus_layer,rows,focus,&c);
 (void)mp_scene_add(&scene,layout->navigation_layer,full,navigation,&c);
 if(v->font_debug&&!mp_home_item_has_font(v->debug_target))(void)mp_scene_add(&scene,9,v->debug_target==MP_HOME_LAYOUT_INFO?full:rows,debug_outline,&c);
 if(overlay)(void)mp_scene_add(&scene,layout->status_layer,full,overlay,context);
 mp_scene_render(&scene,surface);
}
