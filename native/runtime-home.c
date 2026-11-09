// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "runtime-home.h"
#include "asset-platform.h"
#include "vendor/lz4/lz4.h"
#ifdef MP_LIBRARY_TOOL
#include "vendor/lz4/lz4hc.h"
#endif
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#ifdef MP_LIBRARY_TOOL
#include "asset-codec.h"
#include <sys/resource.h>
#endif
_Static_assert(sizeof(struct mp_home_disk_header)==512,"Home header must fit one sector");
_Static_assert(sizeof(struct mp_disk_game)==2628,"Catalog record ABI");
struct mapped_art {void *map;size_t bytes;struct mp_game_art art;char identity[65];};
struct runtime_game {struct mp_disk_game disk;struct mp_home_card card;struct mapped_art image,background;};
struct mp_runtime {
 struct runtime_game *games;struct mp_home_card *cards;struct mp_game_assets *assets;unsigned count;
 struct mp_home_row rows[2];void *boot_raw;struct mp_home_card console;
 unsigned long long decode_us[4];
 unsigned startup_images,startup_bytes,startup_raw_bytes;
 unsigned boot_first;struct stat catalog_stat;unsigned long long next_poll,next_job,seen_generation;pid_t child;char selected[80],boot_selected[80];int scroll;unsigned initialized;
};
static struct mp_runtime *active;
#if defined(__aarch64__)
/* The MP1's fixed Cortex-A55 supports the Arm IEEE CRC32 instructions. */
__attribute__((target("+crc")))
unsigned mp_crc32(const void *data,size_t size)
{
 const unsigned char *p=data;unsigned c=~0u;
 while(size>=8){unsigned long long value;memcpy(&value,p,8);__asm__("crc32x %w0, %w0, %x1":"+r"(c):"r"(value));p+=8;size-=8;}
 while(size--){unsigned value=*p++;__asm__("crc32b %w0, %w0, %w1":"+r"(c):"r"(value));}
 return ~c;
}
#else
unsigned mp_crc32(const void *data,size_t size)
{
 unsigned table[256];
 for(unsigned i=0;i<256;i++){unsigned c=i;for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1u));table[i]=c;}
 const unsigned char *p=data;unsigned c=~0u;
 for(size_t i=0;i<size;i++)c=table[(c^p[i])&255]^(c>>8);
 return ~c;
}
#endif
#include "navigation-default.h"
unsigned long long mp_home_abi(void)
{const unsigned char *p=(const void *)&mp_home_layout_default;unsigned long long h=14695981039346656037ull;for(size_t i=0;i<sizeof mp_home_layout_default;i++)h=(h^p[i])*1099511628211ull;return h^mp_theme_default.status.box.background^MP_HOME_ABI^MP_ASSET_PREPROCESS_ABI^MP_NAVIGATION_ABI;}
static int home_blocks(const struct mp_home_disk_header *h,struct mp_home_blocks *blocks)
{
 memcpy(blocks,h->reserved,sizeof *blocks);
 if(!memcmp(blocks->magic,"\0\0\0\0\0\0\0",8))return 0;
 if(memcmp(blocks->magic,"MPLZ4B1",8)||blocks->count<2||blocks->count>MP_HOME_BLOCKS)return -1;
 size_t compressed=0,raw=0;
 for(unsigned i=0;i<blocks->count;i++){
  unsigned c=blocks->block[i].compressed_bytes,n=blocks->block[i].raw_bytes;
  if(!c||!n||c>h->compressed_bytes-compressed||n>h->raw_bytes-raw)return -1;
  compressed+=c;raw+=n;
 }
 return compressed==h->compressed_bytes&&raw==h->raw_bytes?(int)blocks->count:-1;
}
int mp_home_header_valid(const struct mp_home_disk_header *h)
{
 if(memcmp(h->magic,"MPHOME1",8)||h->version!=1||h->header_bytes!=512||!h->compressed_bytes||h->compressed_bytes>MP_HOME_SLOT_BYTES-512||!h->raw_bytes||h->raw_bytes>MP_HOME_RAW_MAX||h->abi!=MP_HOME_ABI)return 0;
 struct mp_home_disk_header c=*h;c.header_crc=0;struct mp_home_blocks blocks;
 return mp_crc32(&c,sizeof c)==h->header_crc&&home_blocks(h,&blocks)>=0;
}
struct decode_job {const char *source;char *destination;unsigned compressed_bytes,raw_bytes;int result;};
static void *decode_block(void *context)
{
 struct decode_job *job=context;
 job->result=LZ4_decompress_safe(job->source,job->destination,(int)job->compressed_bytes,(int)job->raw_bytes);
 return NULL;
}
static int decode_home(const struct mp_home_disk_header *h,const char *source,char *destination)
{
 struct mp_home_blocks blocks;int count=home_blocks(h,&blocks);
 if(count<0)return -1;
 if(!count)return LZ4_decompress_safe(source,destination,(int)h->compressed_bytes,(int)h->raw_bytes);
 struct decode_job jobs[MP_HOME_BLOCKS];pthread_t threads[MP_HOME_BLOCKS];unsigned started[MP_HOME_BLOCKS]={0};
 for(int i=0;i<count;i++){
  jobs[i]=(struct decode_job){source,destination,blocks.block[i].compressed_bytes,blocks.block[i].raw_bytes,-1};
  source+=jobs[i].compressed_bytes;destination+=jobs[i].raw_bytes;
 }
 /* Each block has an independent LZ4 dictionary and disjoint output. Failure
  * to create a worker falls back to the same bounded decoder on this thread. */
 for(int i=1;i<count;i++)started[i]=!pthread_create(&threads[i],NULL,decode_block,&jobs[i]);
 decode_block(&jobs[0]);
 for(int i=1;i<count;i++){if(started[i])pthread_join(threads[i],NULL);else decode_block(&jobs[i]);}
 for(int i=0;i<count;i++)if(jobs[i].result!=(int)jobs[i].raw_bytes)return -1;
 return (int)h->raw_bytes;
}
static int token(const char *s,size_t n)
{if(!memchr(s,0,n)||!s[0])return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='_')return 0;return 1;}
static int valid_game(const struct mp_disk_game *g)
{return token(g->id,sizeof g->id)&&memchr(g->title,0,sizeof g->title)&&memchr(g->metadata,0,sizeof g->metadata)&&memchr(g->description,0,sizeof g->description)&&memchr(g->rom,0,sizeof g->rom)&&(!g->asset[0]||token(g->asset,sizeof g->asset));}
static void *read_file(const char *path,size_t max,size_t *size)
{int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;if(fd<0)return NULL;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(size_t)st.st_size>max){close(fd);return NULL;}void *data=malloc((size_t)st.st_size+1);size_t off=0;if(!data){close(fd);return NULL;}while(off<(size_t)st.st_size){ssize_t n=read(fd,(char *)data+off,(size_t)st.st_size-off);if(n<0&&errno==EINTR)continue;if(n<=0){free(data);close(fd);return NULL;}off+=(size_t)n;}close(fd);*size=off;return data;}
#ifdef MP_LIBRARY_TOOL
static int atomic_file(const char *path,const void *data,size_t size)
{char tmp[1024];if(snprintf(tmp,sizeof tmp,"%s.%ld.tmp",path,(long)getpid())>=(int)sizeof tmp)return -1;int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0)return -1;size_t off=0;int rc=0;while(off<size){ssize_t n=write(fd,(const char *)data+off,size-off);if(n<0&&errno==EINTR)continue;if(n<=0){rc=-1;break;}off+=(size_t)n;}if(fsync(fd))rc=-1;if(close(fd))rc=-1;if(!rc)rc=rename(tmp,path);if(rc)unlink(tmp);else {char directory[1024];snprintf(directory,sizeof directory,"%s",path);char *slash=strrchr(directory,'/');if(slash){*slash=0;int parent=open(directory,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(parent>=0){if(fsync(parent))rc=-1;close(parent);}}}return rc;}
#endif
static int art_memory(struct mapped_art *m,void *data,size_t bytes)
{if(bytes<sizeof(struct mp_art_header)+1024)return -1;struct mp_art_header h;memcpy(&h,data,sizeof h);if(memcmp(h.magic,"MPART1\0",8)||!h.width||!h.height||h.width>1280||h.height>720||h.bytes!=1024+h.width*h.height||bytes!=sizeof h+h.bytes||mp_crc32((char *)data+sizeof h,h.bytes)!=h.crc)return -1;for(unsigned i=0;i<64;i++)if(!isxdigit((unsigned char)h.identity[i]))return -1;memcpy(m->identity,h.identity,64);m->identity[64]=0;m->art=(struct mp_game_art){h.width,h.height,(unsigned char *)data+sizeof h+1024,(uint32_t *)((char *)data+sizeof h)};return 0;}
static int art_load(struct mapped_art *m,const char *root,const char *asset,const char *role)
{if(m->art.width||!asset[0])return 0;char path[1024];if(snprintf(path,sizeof path,"%s/Library/Assets/%s.%s.mps",root,asset,role)>=(int)sizeof path)return -1;int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;if(fd<0)return -1;if(fstat(fd,&st)||st.st_size<0||st.st_size>1024*1024){close(fd);return -1;}void *map=mmap(NULL,(size_t)st.st_size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(map==MAP_FAILED)return -1;if(art_memory(m,map,(size_t)st.st_size)){munmap(map,(size_t)st.st_size);return -1;}m->map=map;m->bytes=(size_t)st.st_size;return 0;}
static void make_card(struct runtime_game *g)
{g->card=(struct mp_home_card){g->disk.id,g->disk.title,g->disk.metadata,g->disk.description,g->image.art.width?&g->image.art:NULL,(g->disk.flags&2)?MP_HOME_OPEN_LIBRARY:MP_HOME_OPEN_GAME};}
static int setup(struct mp_runtime *r)
{r->cards=calloc(r->count?r->count:1,sizeof *r->cards);r->assets=calloc(r->count?r->count:1,sizeof *r->assets);if(!r->cards||!r->assets)return -1;for(unsigned i=0;i<r->count;i++){make_card(&r->games[i]);r->cards[i]=r->games[i].card;}r->console=(struct mp_home_card){"gba","Game Boy Advance","Nintendo / Handheld","",NULL,MP_HOME_OPEN_LIBRARY};r->rows[0]=(struct mp_home_row){"games","Games",r->cards,r->count};r->rows[1]=(struct mp_home_row){"consoles",MP_HOME_SECONDARY_TITLE,&r->console,1};return 0;}
const struct mp_game_assets *mp_runtime_assets(size_t *count){*count=active?active->count:0;return active?active->assets:NULL;}
static void update_assets(struct mp_runtime *r)
{for(unsigned i=0;i<r->count;i++){struct runtime_game *g=&r->games[i];r->cards[i].art=g->image.art.width?&g->image.art:NULL;r->assets[i]=(struct mp_game_assets){g->disk.id,r->cards[i].art,g->background.art.width?&g->background.art:NULL,g->image.identity,g->background.identity};}}
static int visible_sources(struct mp_runtime *r,const char *root,unsigned selected)
{int changed=0;unsigned first=selected>4?selected-4:0,last=selected+5;if(last>r->count)last=r->count;for(unsigned i=first;i<last;i++){unsigned before=r->games[i].image.art.width;(void)art_load(&r->games[i].image,root,r->games[i].disk.asset,"card");changed|=before!=r->games[i].image.art.width;}for(unsigned i=selected?selected-1:0;i<r->count&&i<=selected+1;i++){unsigned before=r->games[i].background.art.width;(void)art_load(&r->games[i].background,root,r->games[i].disk.asset,"background");changed|=before!=r->games[i].background.art.width;}update_assets(r);return changed;}
static int load_catalog(struct mp_runtime *r,const char *root)
{char path[1024];snprintf(path,sizeof path,"%s/Library/catalog.mpl",root);size_t size;void *data=read_file(path,16*1024*1024,&size);if(!data||size<sizeof(struct mp_catalog_header)){free(data);return -1;}struct mp_catalog_header h;memcpy(&h,data,sizeof h);if(memcmp(h.magic,"MPLIB1\0",8)||h.version!=1||h.count>MP_LIBRARY_MAX||h.bytes!=h.count*sizeof(struct mp_disk_game)||size!=sizeof h+h.bytes||mp_crc32((char *)data+sizeof h,h.bytes)!=h.crc){free(data);return -1;}r->games=calloc(h.count?h.count:1,sizeof *r->games);if(!r->games){free(data);return -1;}r->count=h.count;for(unsigned i=0;i<h.count;i++){memcpy(&r->games[i].disk,(char *)data+sizeof h+i*sizeof(struct mp_disk_game),sizeof(struct mp_disk_game));if(!valid_game(&r->games[i].disk)){free(data);return -1;}}free(data);if(setup(r))return -1;(void)stat(path,&r->catalog_stat);return 0;}

static void clear_catalog(struct mp_runtime *r)
{
 for(unsigned i=0;i<r->count;i++){
  if(r->games[i].image.map)munmap(r->games[i].image.map,r->games[i].image.bytes);
  if(r->games[i].background.map)munmap(r->games[i].background.map,r->games[i].background.bytes);
 }
 free(r->games);free(r->cards);free(r->assets);
 r->games=NULL;r->cards=NULL;r->assets=NULL;r->count=0;
}
static unsigned long long decode_clock(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (unsigned long long)t.tv_sec*1000000+(unsigned long long)t.tv_nsec/1000;}
static int boot_decode(struct mp_runtime *r,struct mp_home_state *home)
{
 unsigned long long stamp=decode_clock();
 int fd=open("/minpocket-startup",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st;
 if(fd<0)return -1;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<512||st.st_size>MP_HOME_SLOT_BYTES){close(fd);return -1;}
 size_t size=(size_t)st.st_size;
 unsigned char *file=mmap(NULL,size,PROT_READ,MAP_PRIVATE,fd,0);close(fd);
 if(file==MAP_FAILED)return -1;
 r->decode_us[0]=decode_clock()-stamp;stamp=decode_clock();
 struct mp_home_disk_header h;memcpy(&h,file,512);
 if(!mp_home_header_valid(&h)||size!=512+h.compressed_bytes||mp_crc32(file+512,h.compressed_bytes)!=h.crc){munmap(file,size);return -1;}
 r->decode_us[1]=decode_clock()-stamp;stamp=decode_clock();
 void *raw_data=malloc(h.raw_bytes);
 int decoded=raw_data?decode_home(&h,(char *)file+512,raw_data):-1;
 munmap(file,size);if(decoded!=(int)h.raw_bytes){free(raw_data);return -1;}
 r->decode_us[2]=decode_clock()-stamp;stamp=decode_clock();
 r->boot_raw=raw_data;struct mp_home_raw_header raw;
 if(h.raw_bytes<sizeof raw)return -1;
 memcpy(&raw,raw_data,sizeof raw);
 int visible=!memcmp(raw.magic,"MPVIEW2",8);
 if((!visible&&memcmp(raw.magic,"MPVIEW1",8))||raw.abi!=mp_home_abi()||!raw.count||raw.count>MP_HOME_VISIBLE_MAX||raw.selected>=raw.count||raw.reserved||raw.background_bytes!=960*720*4||raw.scroll<(visible?-2000000:0)||raw.scroll>2000000||raw.first>MP_LIBRARY_MAX-raw.count)return -1;
 size_t off=sizeof raw;struct mp_home_startup_geometry geometry={0};
 if(visible){
  if(h.raw_bytes-off<sizeof geometry)return -1;
  memcpy(&geometry,(char *)raw_data+off,sizeof geometry);off+=sizeof geometry;
  if(geometry.row_heading>geometry.row_height||geometry.row_height>8192)return -1;
 }
 r->count=raw.count;r->games=calloc(r->count,sizeof *r->games);
 if(!r->games){r->count=0;return -1;}
 for(unsigned i=0;i<r->count;i++){
  if(off+sizeof(struct mp_disk_game)+4>h.raw_bytes)return -1;
  memcpy(&r->games[i].disk,(char *)raw_data+off,sizeof(struct mp_disk_game));off+=sizeof(struct mp_disk_game);
  unsigned bytes;memcpy(&bytes,(char *)raw_data+off,4);off+=4;
  if(!valid_game(&r->games[i].disk)||bytes>h.raw_bytes-off)return -1;
  if(bytes&&art_memory(&r->games[i].image,(char *)raw_data+off,bytes))return -1;
  r->startup_images+=bytes!=0;
  off+=(bytes+3)&~3u;if(off>h.raw_bytes)return -1;
 }
 if(raw.background_bytes!=h.raw_bytes-off||setup(r))return -1;
 update_assets(r);mp_home_view_set_rows(home,r->rows,2);home->index[0]=home->previous[0]=raw.selected;home->captions[0].index=raw.selected;
 home->horizontal_px[0]=raw.scroll;home->horizontal[0]=(struct mp_ui_motion){.from=raw.scroll,.target=raw.scroll,.easing=MP_EASE_OUT_CUBIC};
 home->startup_background=(uint32_t *)((char *)raw_data+off);
 home->startup_geometry=(unsigned)visible;home->startup_heading=geometry.row_heading;home->startup_row_height=geometry.row_height;
 r->startup_bytes=512+h.compressed_bytes;r->startup_raw_bytes=h.raw_bytes;
 r->decode_us[3]=decode_clock()-stamp;
 snprintf(home->startup_id,sizeof home->startup_id,"%s",r->games[raw.selected].disk.id);
 snprintf(r->boot_selected,sizeof r->boot_selected,"%s",r->games[raw.selected].disk.id);r->seen_generation=h.generation;r->boot_first=raw.first;return 0;
}
struct mp_runtime *mp_runtime_boot(struct mp_home_state *home)
{
 struct mp_runtime *r=calloc(1,sizeof *r);if(!r)return NULL;
 if(boot_decode(r,home)){clear_catalog(r);free(r->boot_raw);r->boot_raw=NULL;r->startup_images=0;}
 active=r;unlink("/minpocket-startup");return r;
}
void mp_runtime_report(const struct mp_runtime *r)
{
 FILE *f=fopen("/run/minpocket-home.json","w");if(!f)return;
 fprintf(f,"{\"startup_loaded\":%s,\"startup_records\":%u,\"startup_generation\":%llu,\"startup_selected_id\":\"%s\",\"theme_abi\":%llu,\"startup_images\":%u,\"startup_bytes\":%u,\"startup_raw_bytes\":%u}\n",r&&r->boot_raw?"true":"false",r?r->count:0,r?r->seen_generation:0,r?r->boot_selected:"",mp_home_abi(),r?r->startup_images:0,r?r->startup_bytes:0,r?r->startup_raw_bytes:0);
 fclose(f);
 f=fopen("/run/minpocket-home-decode","w");if(f){
  fprintf(f,"map_us=%llu crc_us=%llu lz4_us=%llu parse_us=%llu\n",r?r->decode_us[0]:0,r?r->decode_us[1]:0,r?r->decode_us[2]:0,r?r->decode_us[3]:0);fclose(f);
 }
}
static void drop_maps(struct mp_runtime *r,unsigned selected)
{
 for(unsigned i=0;i<r->count;i++)if(i+5<selected||i>selected+5){
  struct mapped_art *a=&r->games[i].image,*b=&r->games[i].background;
  if(a->map)madvise(a->map,a->bytes,MADV_DONTNEED);
  if(b->map)madvise(b->map,b->bytes,MADV_DONTNEED);
 }
}
int mp_runtime_poll(struct mp_runtime *r,struct mp_home_state *home,unsigned long long now)
{
 if(!r||now<r->next_poll)return 0;
 r->next_poll=now+500;int changed=0;
 if(r->child){int status;pid_t done=waitpid(r->child,&status,WNOHANG);if(done==r->child||(done<0&&errno==ECHILD))r->child=0;}
 if(!mp_asset_data_ready(NULL)){
  if(home->startup_geometry)r->next_poll=now+16;
  return 0;
 }
 char selected[80];snprintf(selected,sizeof selected,"%s",home->rows[0].cards[home->index[0]].id);
 int scroll=home->horizontal_px[0]+(int)r->boot_first*(int)(mp_home_layout_default.card_width+mp_home_layout_default.card_gap);struct stat st;
 if(!stat("/media/Data/Library/catalog.mpl",&st)&&(!r->initialized||st.st_mtim.tv_sec!=r->catalog_stat.st_mtim.tv_sec||st.st_mtim.tv_nsec!=r->catalog_stat.st_mtim.tv_nsec||st.st_size!=r->catalog_stat.st_size||st.st_ino!=r->catalog_stat.st_ino)){
  struct mp_runtime next={0};
  if(!load_catalog(&next,"/media/Data")&&next.count){
   unsigned saved_row=home->row,saved_vertical=(unsigned)home->vertical_px;
   unsigned selection=0;for(unsigned i=0;i<next.count;i++)if(!strcmp(selected,next.games[i].disk.id))selection=i;
   (void)visible_sources(&next,"/media/Data",selection);
   mp_home_view_dispose(home);
   clear_catalog(r);r->games=next.games;r->cards=next.cards;r->assets=next.assets;r->count=next.count;
   r->console=next.console;r->rows[0]=next.rows[0];r->rows[1]=next.rows[1];r->rows[1].cards=&r->console;
   r->catalog_stat=next.catalog_stat;r->initialized=1;r->boot_first=0;active=r;
   mp_home_view_set_rows(home,r->rows,2);home->index[0]=home->previous[0]=selection;home->captions[0].index=selection;
   if(saved_row<home->count){home->row=saved_row;home->vertical_px=(int)saved_vertical;home->vertical=(struct mp_ui_motion){.from=(int)saved_vertical,.target=(int)saved_vertical,.easing=MP_EASE_OUT_CUBIC};for(unsigned row=0;row<home->count;row++){int focus=row==saved_row?1000:0;home->focus_px[row]=focus;home->focus[row]=(struct mp_ui_motion){.from=focus,.target=focus,.easing=MP_EASE_OUT_CUBIC};}}
   home->horizontal_px[0]=scroll;home->horizontal[0]=(struct mp_ui_motion){.from=scroll,.target=scroll,.easing=MP_EASE_OUT_CUBIC};
   mp_home_assets_start(home,"/media/Data/Cache/Assets/v1",mp_asset_data_ready,NULL);changed=1;
  }else clear_catalog(&next);
 }
 if(r->count){unsigned selection=home->index[0];if(selection>=r->count)selection=0;changed|=visible_sources(r,"/media/Data",selection);drop_maps(r,selection);}
 const char *id=home->rows[0].cards[home->index[0]].id;
 if(strcmp(r->selected,id)||r->scroll!=home->horizontal_px[0]){snprintf(r->selected,sizeof r->selected,"%s",id);r->scroll=home->horizontal_px[0];r->next_job=now+1500;}
 if(!r->child&&now>=r->next_job){
  r->next_job=now+10000;char value[32];snprintf(value,sizeof value,"%d",r->scroll);pid_t pid=fork();
  if(!pid){execl("/sbin/minpocket-library","minpocket-library","refresh","/media/Data",r->selected,value,(char *)NULL);_exit(127);}
  if(pid>0)r->child=pid;
 }
 return changed;
}

#ifdef MP_LIBRARY_TOOL
static int ensure_library(const char *root)
{
 char path[1024];snprintf(path,sizeof path,"%s/Games",root);
 if(mkdir(path,0700)&&errno!=EEXIST)return -1;
 snprintf(path,sizeof path,"%s/Library",root);
 return mkdir(path,0700)&&errno!=EEXIST?-1:0;
}
static int write_catalog(struct mp_runtime *r,const char *root,unsigned long long generation)
{
 size_t bytes=r->count*sizeof(struct mp_disk_game),size=sizeof(struct mp_catalog_header)+bytes;
 unsigned char *data=calloc(1,size);if(!data)return -1;
 for(unsigned i=0;i<r->count;i++)memcpy(data+sizeof(struct mp_catalog_header)+i*sizeof(struct mp_disk_game),&r->games[i].disk,sizeof(struct mp_disk_game));
 struct mp_catalog_header h={.magic="MPLIB1",.version=1,.count=r->count,.bytes=(unsigned)bytes,.generation=generation};
 h.crc=mp_crc32(data+sizeof h,bytes);memcpy(data,&h,sizeof h);
 char path[1024];snprintf(path,sizeof path,"%s/Library/catalog.mpl",root);
 int rc=atomic_file(path,data,size);free(data);return rc;
}
static int import_catalog(const char *root,const char *source)
{
 struct mp_runtime r={0};if(load_catalog(&r,source)){clear_catalog(&r);return -1;}
 char path[1024];snprintf(path,sizeof path,"%s/Library/Assets",root);
 if(mkdir(path,0700)&&errno!=EEXIST){clear_catalog(&r);return -1;}
 int rc=-1;
 for(unsigned i=0;i<r.count;i++){
  struct runtime_game *g=&r.games[i];if(!g->disk.asset[0])continue;
  const char *roles[]={"card","background"};struct mapped_art *art[]={&g->image,&g->background};
  for(unsigned role=0;role<2;role++){
   if(art_load(art[role],source,g->disk.asset,roles[role]))goto done;
   if(snprintf(path,sizeof path,"%s/Library/Assets/%s.%s.mps",root,g->disk.asset,roles[role])>=(int)sizeof path||atomic_file(path,art[role]->map,art[role]->bytes))goto done;
  }
 }
 rc=write_catalog(&r,root,(unsigned long long)time(NULL));
 done:clear_catalog(&r);return rc;
}
static const char *console_for(const char *name)
{
 const char *dot=strrchr(name,'.');if(!dot)return NULL;
 if(!strcasecmp(dot,".zip"))return "Archive";
 if(!strcasecmp(dot,".gba"))return "GBA";
 if(!strcasecmp(dot,".sfc")||!strcasecmp(dot,".smc"))return "SNES";
 if(!strcasecmp(dot,".gb"))return "GB";
 if(!strcasecmp(dot,".gbc"))return "GBC";
 if(!strcasecmp(dot,".nes"))return "NES";
 if(!strcasecmp(dot,".md")||!strcasecmp(dot,".gen"))return "MD";
 if(!strcasecmp(dot,".chd")||!strcasecmp(dot,".cue")||!strcasecmp(dot,".iso")||!strcasecmp(dot,".pbp")||!strcasecmp(dot,".m3u"))return "Disc";
 return NULL;
}
static int scan_games(struct mp_runtime *r,const char *root,const char *relative,unsigned depth)
{
 if(depth>4)return 0;
 char path[1024];if(snprintf(path,sizeof path,"%s/%s",root,relative)>=(int)sizeof path)return -1;
 struct dirent **list=NULL;int count=scandir(path,&list,NULL,alphasort);if(count<0)return errno==ENOENT?0:-1;
 int changed=0;
 for(int i=0;i<count;i++){
  const char *name=list[i]->d_name;
  char rel[512],full[1024];struct stat st;
  if(name[0]=='.'||snprintf(rel,sizeof rel,"%s/%s",relative,name)>=(int)sizeof rel||snprintf(full,sizeof full,"%s/%s",root,rel)>=(int)sizeof full||lstat(full,&st))goto next;
  if(S_ISDIR(st.st_mode)){int rc=scan_games(r,root,rel,depth+1);if(rc<0){changed=-1;goto finish;}changed|=rc;goto next;}
  if(!S_ISREG(st.st_mode))goto next;
  const char *console=console_for(name);if(!console)goto next;
  for(unsigned j=0;j<r->count;j++)if(!strcmp(r->games[j].disk.rom,rel))goto next;
  if(r->count>=MP_LIBRARY_MAX)goto next;
  struct runtime_game *games=realloc(r->games,(r->count+1)*sizeof *games);if(!games){changed=-1;goto finish;}r->games=games;
  struct mp_disk_game *g=&r->games[r->count++].disk;memset(&r->games[r->count-1],0,sizeof *games);
  unsigned long long id=14695981039346656037ull;for(const unsigned char *p=(const unsigned char *)rel;*p;p++)id=(id^*p)*1099511628211ull;
  snprintf(g->id,sizeof g->id,"rom-%016llx",id);snprintf(g->rom,sizeof g->rom,"%s",rel);g->flags=1;
  snprintf(g->title,sizeof g->title,"%.*s",(int)sizeof g->title-1,name);char *dot=strrchr(g->title,'.');if(dot)*dot=0;
  snprintf(g->metadata,sizeof g->metadata,"%s",console);changed=1;
 next:;
 }
 finish:for(int i=0;i<count;i++)free(list[i]);free(list);return changed;
}
int mp_runtime_index(const char *root)
{
 if(ensure_library(root))return -1;
 struct mp_runtime r={0};int existed=!load_catalog(&r,root),changed=!existed;
 int was_empty=existed&&r.count==1&&(r.games[0].disk.flags&2);
 if(!existed)clear_catalog(&r);
 for(unsigned i=0;i<r.count;){
  struct mp_disk_game *g=&r.games[i].disk;char path[1024];struct stat st;
  int missing=(g->flags&1)&&(strncmp(g->rom,"Games/",6)||strstr(g->rom,"/../")||snprintf(path,sizeof path,"%s/%s",root,g->rom)>=(int)sizeof path||lstat(path,&st)||!S_ISREG(st.st_mode));
  if((g->flags&2)||missing){memmove(&r.games[i],&r.games[i+1],(--r.count-i)*sizeof *r.games);changed=1;}else i++;
 }
 int scan=scan_games(&r,root,"Games",0);if(scan<0){clear_catalog(&r);return -1;}changed|=scan;
 if(!r.count){
  changed=was_empty?0:1;
  struct runtime_game *games=realloc(r.games,sizeof *games);if(!games){clear_catalog(&r);return -1;}r.games=games;r.count=1;memset(games,0,sizeof *games);
  snprintf(games[0].disk.id,sizeof games[0].disk.id,"library-empty");snprintf(games[0].disk.title,sizeof games[0].disk.title,"No games yet");
  snprintf(games[0].disk.description,sizeof games[0].disk.description,"Add games to the Games folder on your SD card.");games[0].disk.flags=2;
  /* The placeholder is stable, so an empty library does not rewrite each poll. */
  if(was_empty&&scan==0)changed=0;
 }
 int rc=changed?write_catalog(&r,root,(unsigned long long)time(NULL)):0;clear_catalog(&r);return rc;
}
int mp_runtime_publish(const char *root,const char *selected,int scroll,const char *output)
{
 struct mp_runtime r={0};if(load_catalog(&r,root)||!r.count){clear_catalog(&r);return -1;}
 unsigned index=0;for(unsigned i=0;i<r.count;i++)if(!strcmp(selected,r.games[i].disk.id))index=i;
 unsigned first=index>3?index-3:0,last=first+MP_HOME_VISIBLE_MAX;if(last>r.count)last=r.count;
 unsigned original_first=first,original_count=last-first;
 for(unsigned i=first;i<last;i++)(void)art_load(&r.games[i].image,root,r.games[i].disk.asset,"card");
 update_assets(&r);
 struct mp_home_state *prepared=calloc(1,sizeof *prepared);if(!prepared){clear_catalog(&r);return -1;}
 mp_home_view_init(prepared);
 struct mp_home_row rows[]={{"games","Games",r.cards+first,original_count},{"consoles",MP_HOME_SECONDARY_TITLE,&r.console,1}};
 if(mp_home_view_set_rows(prepared,rows,2)){free(prepared);clear_catalog(&r);return -1;}
 int stride=(int)mp_home_layout_default.card_width+(int)mp_home_layout_default.card_gap;
 prepared->index[0]=prepared->previous[0]=index-first;prepared->captions[0].index=index-first;
 prepared->horizontal_px[0]=scroll-(int)first*stride;
 if(prepared->horizontal_px[0]<0)prepared->horizontal_px[0]=0;
 struct mp_home_startup_plan plan;
 if(mp_home_view_startup_plan(prepared,&mp_home_layout_default,960,720,&plan)){free(prepared);clear_catalog(&r);return -1;}
 int adjusted_scroll=prepared->horizontal_px[0]-(int)plan.first*stride;
 free(prepared);
 /* A negative local scroll is legal in MPVIEW2; its absolute catalog scroll
  * remains nonnegative. This preserves the exact sub-card viewport position. */
 first=original_first+plan.first;last=original_first+plan.last;unsigned count=last-first;
 unsigned char *raw=calloc(1,MP_HOME_RAW_MAX);if(!raw){clear_catalog(&r);return -1;}
 struct mp_home_raw_header view={.magic="MPVIEW2",.count=count,.selected=index-first,.scroll=adjusted_scroll,.first=first,.background_bytes=960*720*4,.abi=mp_home_abi()};
 struct mp_home_startup_geometry geometry={plan.row_heading,plan.row_height};
 memcpy(raw,&view,sizeof view);memcpy(raw+sizeof view,&geometry,sizeof geometry);size_t off=sizeof view+sizeof geometry;int rc=-1;
 for(unsigned i=first;i<last;i++){
  struct runtime_game *g=&r.games[i];
  unsigned bytes=plan.visible_mask&(1u<<(i-original_first))?(unsigned)g->image.bytes:0;
  if(off+sizeof g->disk+4+((bytes+3)&~3u)+view.background_bytes>MP_HOME_RAW_MAX)goto done;
  struct mp_disk_game record={0};
  memcpy(record.id,g->disk.id,sizeof record.id);memcpy(record.title,g->disk.title,sizeof record.title);record.flags=g->disk.flags;
  if(i==index){memcpy(record.metadata,g->disk.metadata,sizeof record.metadata);memcpy(record.description,g->disk.description,sizeof record.description);}
  memcpy(raw+off,&record,sizeof record);off+=sizeof record;memcpy(raw+off,&bytes,4);off+=4;
  if(bytes)memcpy(raw+off,g->image.map,bytes);
  off+=(bytes+3)&~3u;
 }
 if(off+view.background_bytes>MP_HOME_RAW_MAX)goto done;
 struct mapped_art *bg=&r.games[index].background;(void)art_load(bg,root,r.games[index].disk.asset,"background");
 uint32_t *pixels=(uint32_t *)(raw+off);const struct mp_home_layout *l=&mp_home_layout_default;
 unsigned long long bg_start=decode_clock(),bg_us=0,compress_start=0,compress_us=0;
 if(bg->art.width){
  struct mp_asset_spec spec={&bg->art,bg->identity,MP_ASSET_CURVED_BACKGROUND,960,720,mp_theme_default.status.box.background,l->mask_clear_left_percent,l->mask_vertical_inset_percent,l->mask_text_shade_percent};char cache[1024];
  if(snprintf(cache,sizeof cache,"%s/Cache/Assets/v1",root)>=(int)sizeof cache)goto done;
  if(mp_asset_read_prepared(cache,&spec,pixels)&&mp_asset_prepare_pixels(&spec,pixels))goto done;
 }
 else for(unsigned i=0;i<960*720;i++)pixels[i]=mp_theme_default.status.box.background;
 bg_us=decode_clock()-bg_start;
 off+=view.background_bytes;
 unsigned char *file=calloc(1,MP_HOME_SLOT_BYTES);if(!file)goto done;
 struct mp_asset_blocks encoded={0};compress_start=decode_clock();
 int compressed=mp_asset_compress(raw,off,file+512,MP_HOME_SLOT_BYTES-512,&encoded);
 /* Capacity/allocation failures can still use the existing one-block format.
  * Worker creation failures already preserve the same four blocks sequentially. */
 if(compressed<0){memset(&encoded,0,sizeof encoded);compressed=LZ4_compress_HC((char *)raw,(char *)file+512,(int)off,MP_HOME_SLOT_BYTES-512,12);}
 compress_us=decode_clock()-compress_start;
 if(compressed>0){
  struct mp_home_disk_header h={.magic="MPHOME1",.version=1,.header_bytes=512,.compressed_bytes=(unsigned)compressed,.raw_bytes=(unsigned)off,.abi=MP_HOME_ABI};
  if(encoded.count==MP_HOME_BLOCKS){
   struct mp_home_blocks blocks={.magic="MPLZ4B1",.count=MP_HOME_BLOCKS};
   for(unsigned i=0;i<MP_HOME_BLOCKS;i++){blocks.block[i].compressed_bytes=encoded.block[i].compressed_bytes;blocks.block[i].raw_bytes=encoded.block[i].raw_bytes;}
   memcpy(h.reserved,&blocks,sizeof blocks);
  }
  h.crc=mp_crc32(file+512,(size_t)compressed);h.header_crc=mp_crc32(&h,sizeof h);memcpy(file,&h,sizeof h);
  rc=atomic_file(output,file,512+(size_t)compressed);
  if(!rc){struct rusage usage;memset(&usage,0,sizeof usage);(void)getrusage(RUSAGE_SELF,&usage);FILE *profile=fopen("/run/minpocket-cache-publish.json","w");if(profile){fprintf(profile,"{\"background_us\":%llu,\"compression_us\":%llu,\"raw_bytes\":%zu,\"compressed_bytes\":%u,\"blocks\":%u,\"background_quality\":60,\"peak_rss_kib\":%ld}\n",bg_us,compress_us,off,(unsigned)compressed,encoded.count?encoded.count:1,(long)usage.ru_maxrss);fclose(profile);}}
 }
 free(file);
 done:free(raw);clear_catalog(&r);return rc;
}
static int exact_io(int fd,void *data,size_t bytes,off_t offset,int writing)
{
 size_t off=0;while(off<bytes){ssize_t n=writing?pwrite(fd,(char *)data+off,bytes-off,offset+(off_t)off):pread(fd,(char *)data+off,bytes-off,offset+(off_t)off);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;off+=(size_t)n;}return 0;
}
int mp_runtime_write_slots(const char *path,int seed)
{
 /* Only the MP1 SD p2 tail is writable. Never search generic mmcblk devices. */
 if(!seed){char cmd[8192];FILE *stream=fopen("/proc/cmdline","r");if(!stream)return -1;int enabled=fgets(cmd,sizeof cmd,stream)&&strstr(cmd," minpocket.startup_handoff=1");fclose(stream);if(!enabled)return -1;}
 glob_t paths={0};const char *pattern="/sys/bus/platform/devices/fe2b0000.mmc/mmc_host/mmc*/mmc*:*/block/mmcblk*/mmcblk*p2";
 if(glob(pattern,0,NULL,&paths)||paths.gl_pathc!=1){globfree(&paths);return -1;}
 char base[512];snprintf(base,sizeof base,"%s",paths.gl_pathv[0]);globfree(&paths);
 char attr[640],device[80],name[80];unsigned long long start=0,sectors=0;FILE *f;
 snprintf(attr,sizeof attr,"%s/start",base);f=fopen(attr,"r");if(f){if(fscanf(f,"%llu",&start)!=1)start=0;fclose(f);}
 snprintf(attr,sizeof attr,"%s/size",base);f=fopen(attr,"r");if(f){if(fscanf(f,"%llu",&sectors)!=1)sectors=0;fclose(f);}
 snprintf(attr,sizeof attr,"%s/uevent",base);f=fopen(attr,"r");int named=0;if(f){while(fgets(name,sizeof name,f))if(!strcmp(name,"PARTNAME=minpocket\n"))named=1;fclose(f);}
 if(start!=32768||sectors<=MP_HOME_RESERVED_BLOCKS||sectors>262144||!named)return -1;
 snprintf(device,sizeof device,"/dev/%s",strrchr(base,'/')+1);
 size_t size;unsigned char *file=read_file(path,MP_HOME_SLOT_BYTES,&size);if(!file)return -1;
 struct mp_home_disk_header h;if(size<512){free(file);return -1;}memcpy(&h,file,512);
 if(!mp_home_header_valid(&h)||size!=512+h.compressed_bytes||mp_crc32(file+512,h.compressed_bytes)!=h.crc){free(file);return -1;}
 int fd=open(device,O_RDWR|O_CLOEXEC|O_NOFOLLOW);struct stat st;
 if(fd<0){free(file);return -1;}int rc=-1;
 if(fstat(fd,&st)||!S_ISBLK(st.st_mode))goto done;
 unsigned char fit[512];if(exact_io(fd,fit,512,0,0)||memcmp(fit,"\xd0\x0d\xfe\xed",4))goto done;
 unsigned fit_size=((unsigned)fit[4]<<24)|((unsigned)fit[5]<<16)|((unsigned)fit[6]<<8)|fit[7];
 off_t offset=(off_t)(sectors-MP_HOME_RESERVED_BLOCKS)*512;
 if(fit_size<40||(unsigned long long)fit_size>(unsigned long long)offset)goto done;
 struct mp_home_disk_header old[2];int valid[2]={0,0};
 for(unsigned i=0;i<2;i++){
  if(exact_io(fd,&old[i],512,offset+(off_t)i*MP_HOME_SLOT_BYTES,0))goto done;
  if(mp_home_header_valid(&old[i])){
   unsigned char *body=malloc(old[i].compressed_bytes);if(!body)goto done;
   valid[i]=!exact_io(fd,body,old[i].compressed_bytes,offset+(off_t)i*MP_HOME_SLOT_BYTES+512,0)&&mp_crc32(body,old[i].compressed_bytes)==old[i].crc;free(body);
  }else {unsigned char zero[512]={0};if(seed&&memcmp(&old[i],zero,512)&&memcmp(old[i].magic,"MPHOME1",8))goto done;}
 }
 unsigned newest=valid[1]&&(!valid[0]||old[1].generation>old[0].generation)?1:0;
 if(valid[newest]&&old[newest].crc==h.crc&&old[newest].compressed_bytes==h.compressed_bytes&&old[newest].raw_bytes==h.raw_bytes){rc=0;goto done;}
 unsigned target=valid[newest]?1-newest:0;
 h.generation=valid[newest]?old[newest].generation+1:1;if(!h.generation)goto done;
 h.header_crc=0;h.header_crc=mp_crc32(&h,sizeof h);memcpy(file,&h,512);
 off_t slot=offset+(off_t)target*MP_HOME_SLOT_BYTES;unsigned char zero[512]={0};
 if(exact_io(fd,zero,512,slot,1)||fsync(fd)||exact_io(fd,file+512,size-512,slot+512,1)||fsync(fd)||exact_io(fd,file,512,slot,1)||fsync(fd))goto done;
 unsigned char *check=malloc(size);if(!check)goto done;rc=exact_io(fd,check,size,slot,0)||memcmp(check,file,size)?-1:0;free(check);
 done:close(fd);free(file);return rc;
}
int main(int argc,char **argv)
{
 if(argc==2&&!strcmp(argv[1],"selftest")){if(mp_crc32("123456789",9)!=0xcbf43926)return 1;puts("IEEE CRC32 passed");return 0;}
 if(argc==3&&!strcmp(argv[1],"seed"))return mp_runtime_write_slots(argv[2],1)?1:0;
 int lock=-1;
 if(argc>=3){char path[1024];if(ensure_library(argv[2]))return 1;snprintf(path,sizeof path,"%s/Library/.index.lock",argv[2]);lock=open(path,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;}
 errno=0;
 if(nice(19)==-1&&errno)perror("lower asset preparation priority");
 if(argc==4&&!strcmp(argv[1],"import"))return import_catalog(argv[2],argv[3])?1:0;
 if(argc==6&&!strcmp(argv[1],"publish"))return mp_runtime_publish(argv[2],argv[3],atoi(argv[4]),argv[5])?1:0;
 if(argc==3&&!strcmp(argv[1],"index"))return mp_runtime_index(argv[2])?1:0;
 if(argc==5&&!strcmp(argv[1],"refresh")){
  if(mp_runtime_index(argv[2]))return 1;
  if(ensure_library(argv[2]))return 1;
  char path[1024],state[256],old[256]={0};snprintf(path,sizeof path,"%s/Library/home.state",argv[2]);
  char catalog_path[1024];snprintf(catalog_path,sizeof catalog_path,"%s/Library/catalog.mpl",argv[2]);
  struct stat st;if(stat(catalog_path,&st))return 1;
  snprintf(state,sizeof state,"%s %s %lld %ld %llu %llu %llu 3\n",argv[3],argv[4],(long long)st.st_mtim.tv_sec,st.st_mtim.tv_nsec,(unsigned long long)st.st_ino,(unsigned long long)st.st_size,mp_home_abi());
  FILE *f=fopen(path,"r");if(f){if(!fgets(old,sizeof old,f))old[0]=0;fclose(f);}if(!strcmp(old,state))return 0;
  char snapshot[1024];snprintf(snapshot,sizeof snapshot,"%s/Library/home.startup",argv[2]);
  if(mp_runtime_publish(argv[2],argv[3],atoi(argv[4]),snapshot)||mp_runtime_write_slots(snapshot,0))return 1;
  return atomic_file(path,state,strlen(state))?1:0;
 }
 fprintf(stderr,"Usage: minpocket-library index ROOT | publish ROOT ID SCROLL OUTPUT | seed FILE | refresh ROOT ID SCROLL\n");return 2;
}
#endif
