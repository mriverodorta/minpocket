// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "asset-cache.h"
#include "asset-codec.h"
#include "artwork-fade.h"
#include "vendor/near-lossless/near-lossless.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/resource.h>
#endif
#define SLOTS 24
#define HEADER 128
#define COMPRESSED_HEADER 192
#define KEY 160
struct entry {struct mp_asset_spec spec;char key[KEY];uint32_t *pixels;size_t bytes;unsigned state,refs;uint64_t used;};
struct mp_asset_cache {
 pthread_mutex_t mutex;pthread_cond_t cond;pthread_t thread;
 char root[512];size_t budget,bytes;int stop;int notify[2];
 int (*prepare_root)(void *);void *context;
 uint64_t clock;struct entry entries[SLOTS];struct mp_asset_stats stats;
};
static uint32_t get32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static uint64_t checksum(const void *data,size_t size)
{const unsigned char *p=data;uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<size;i++)h=(h^p[i])*UINT64_C(1099511628211);return h;}
static size_t bytes(const struct mp_asset_spec *s)
{return (size_t)s->width*s->height*4;}
static int background(const struct mp_asset_spec *s){return s->kind!=MP_ASSET_CARD;}
int mp_asset_key(const struct mp_asset_spec *s,char *out,size_t size)
{
 if(!s||!s->source||!s->source->width||!s->source->height||!s->identity||strlen(s->identity)!=64||
 !s->width||!s->height||s->width>3840||s->height>2160||s->kind<1||s->kind>3||s->fade_left>100||s->fade_bottom>100||s->top_contrast>2160||(s->kind==3&&(s->fade_left>90||s->fade_bottom>40||s->top_contrast>100)))return -1;
 for(unsigned i=0;i<64;i++)if(!((s->identity[i]>='0'&&s->identity[i]<='9')||(s->identity[i]>='a'&&s->identity[i]<='f')))return -1;
 int n=snprintf(out,size,"%s-%s-%u-%ux%u-%06x-%u-%u-%u.mpa",background(s)?"v3n60":"v1",s->identity,s->kind,s->width,s->height,s->background&0xffffff,s->fade_left,s->fade_bottom,s->top_contrast);
 return n>0&&(size_t)n<size?0:-1;
}
/* A single feathered ellipse replaces multiplied rectangular edge fades.
 * A second broad ellipse shades the information area without a box boundary.
 * Q16.16 coordinates keep the mask independent from image sampling. */
static unsigned square_ratio(int64_t delta,unsigned radius)
{
 if(!radius)return 262144;
 int64_t q=delta*65536/(int64_t)radius;
 if(q>131072||q< -131072)return 262144;
 return (unsigned)(q*q>>16);
}
static unsigned smooth(unsigned t)
{if(t>=65536)return 65536;return (unsigned)((((uint64_t)t*t)>>16)*(196608u-2*t)>>16);}
static unsigned curve(unsigned qx,unsigned qy,unsigned text_x,unsigned text_y,unsigned shade)
{
 unsigned q=qx+qy;if(q>=65536)return 0;
 unsigned alpha=smooth(65536-q);if(!shade)return alpha;
 unsigned text=text_x+text_y;
 unsigned feather=text<=53084?65536:text>=65536?0:smooth((unsigned)((uint64_t)(65536-text)*65536/(65536-53084)));
 unsigned dim=(unsigned)((uint64_t)feather*shade/100);
 return (unsigned)((uint64_t)alpha*(65536-dim)>>16);
}
unsigned mp_asset_curved_alpha(const struct mp_asset_spec *s,unsigned x,unsigned y)
{
 unsigned rx=s->width*(100-s->fade_left)/100,ry=s->height*(50-s->fade_bottom)/100;
 return curve(square_ratio((int64_t)s->width-x,rx),square_ratio((int64_t)y-s->height/2,ry),
  square_ratio((int64_t)x-s->width/5,s->width*64/100),square_ratio((int64_t)y-s->height*28/100,s->height*62/100),s->top_contrast);
}
int mp_asset_render_region(const struct mp_asset_spec *s,unsigned x0,unsigned y0,unsigned width,unsigned height,uint32_t *pixels,ptrdiff_t xs,ptrdiff_t ys)
{
 struct mp_asset_spec checked;if(!s)return -1;checked=*s;if(!checked.identity)checked.identity="0000000000000000000000000000000000000000000000000000000000000000";
 char key[KEY];if(!pixels||mp_asset_key(&checked,key,sizeof key)||s->kind!=MP_ASSET_CURVED_BACKGROUND||x0>s->width||y0>s->height||width>s->width-x0||height>s->height-y0)return -1;
 const struct mp_game_art *a=s->source;unsigned w=s->width,h=s->height;
 unsigned sw=w,sh=h;if((uint64_t)w*a->height<(uint64_t)h*a->width)sw=(unsigned)((uint64_t)h*a->width/a->height);else sh=(unsigned)((uint64_t)w*a->height/a->width);
 unsigned cx=(sw-w)/2,cy=(sh-h)/2,rx=w*(100-s->fade_left)/100,ry=h*(50-s->fade_bottom)/100;
 unsigned row[2160],vertical[2160],text_y[2160];
 for(unsigned y=y0;y<y0+height;y++){
  row[y]=(unsigned)((uint64_t)(y+cy)*a->height/sh)*a->width;
  vertical[y]=square_ratio((int64_t)y-h/2,ry);text_y[y]=square_ratio((int64_t)y-h*28/100,h*62/100);
 }
 static const unsigned char bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 unsigned br=s->background>>16&255,bg=s->background>>8&255,bb=s->background&255;
 for(unsigned x=x0;x<x0+width;x++){
  unsigned sx=(unsigned)((uint64_t)(x+cx)*a->width/sw),horizontal=square_ratio((int64_t)w-x,rx),text_x=square_ratio((int64_t)x-w/5,w*64/100);
  uint32_t *dst=pixels+(ptrdiff_t)(x-x0)*xs;
  for(unsigned y=y0;y<y0+height;y++,dst+=ys){
   unsigned alpha=curve(horizontal,vertical[y],text_x,text_y[y],s->top_contrast);
   /* Distribute sub-step opacity errors in two dimensions, rather than
    * introducing constant-color vertical strips. No temporally changing noise. */
   unsigned value=alpha*255,opacity=value>>16;
   if((value&65535)>(unsigned)bayer[y&3][x&3]*4096+2048&&opacity<255)++opacity;
   if(!opacity){*dst=s->background;continue;}
   uint32_t src=a->palette[a->pixels[row[y]+sx]];unsigned inv=255-opacity;
   *dst=((((src>>16&255)*opacity+br*inv+127)/255)<<16)|((((src>>8&255)*opacity+bg*inv+127)/255)<<8)|((src&255)*opacity+bb*inv+127)/255;
  }
 }
 return 0;
}
int mp_asset_render(const struct mp_asset_spec *s,uint32_t *pixels)
{
 char key[KEY];if(!pixels||mp_asset_key(s,key,sizeof key))return -1;
 if(s->kind==MP_ASSET_CURVED_BACKGROUND)return mp_asset_render_region(s,0,0,s->width,s->height,pixels,(ptrdiff_t)s->height,1);
 const struct mp_game_art *a=s->source;unsigned w=s->width,h=s->height;
 if(s->kind==MP_ASSET_CARD){
  for(unsigned x=0;x<w;x++){unsigned sx=(unsigned)((uint64_t)x*a->width/w);
   for(unsigned y=0;y<h;y++)pixels[x*h+y]=a->palette[a->pixels[(unsigned)((uint64_t)y*a->height/h)*a->width+sx]];}
  return 0;
 }
 uint32_t fade[32][256];
 for(unsigned step=0;step<32;step++)for(unsigned i=0;i<256;i++){
  uint32_t src=a->palette[i],dst=0;unsigned alpha=step*255/31;
  for(unsigned shift=0;shift<24;shift+=8){unsigned c=(src>>shift)&255,b=(s->background>>shift)&255;dst|=((c*alpha+b*(255-alpha)+127)/255)<<shift;}fade[step][i]=dst;
 }
 unsigned sw=w,sh=h;if((uint64_t)w*a->height<(uint64_t)h*a->width)sw=(unsigned)((uint64_t)h*a->width/a->height);else sh=(unsigned)((uint64_t)w*a->height/a->width);
 unsigned cx=(sw-w)/2,cy=(sh-h)/2,fw=w*s->fade_left/100,fh=h*s->fade_bottom/100;if(!fw)fw=1;if(!fh)fh=1;
 unsigned row[2160];unsigned char vertical[2160];
 for(unsigned y=0;y<h;y++){
  unsigned remain=h-y,v=remain>=fh?255:remain*255/fh,t=s->top_contrast?y*255/s->top_contrast:255;if(t>255)t=255;
  vertical[y]=(unsigned char)((v*v/255)*(t*t/255)/255);row[y]=(unsigned)((uint64_t)(y+cy)*a->height/sh)*a->width;
 }
 for(unsigned x=0;x<w;x++){
  unsigned horizontal=x>=fw?255:x*255/fw;horizontal=horizontal*horizontal/255;unsigned sx=(unsigned)((uint64_t)(x+cx)*a->width/sw);
  for(unsigned y=0;y<h;y++)pixels[x*h+y]=fade[mp_art_fade_steps[horizontal][vertical[y]]][a->pixels[row[y]+sx]];
 }
 return 0;
}
int mp_asset_prepare_pixels(const struct mp_asset_spec *s,uint32_t *pixels)
{
 if(mp_asset_render(s,pixels))return -1;
 /* The renderer writes x-major pixels. Swapping logical dimensions avoids
  * two full-buffer transposes while retaining exactly the upstream result. */
 return background(s)?mp_near_lossless_inplace(pixels,s->height,s->width,60):0;
}
static void header(unsigned char h[COMPRESSED_HEADER],const struct mp_asset_spec *s,uint64_t hash,unsigned compressed,const struct mp_asset_blocks *blocks)
{
 int bg=background(s);memset(h,0,COMPRESSED_HEADER);memcpy(h,bg?"MPACv2\r\n":"MPACv1\r\n",8);put32(h+8,bg?2:1);put32(h+12,s->kind);put32(h+16,s->width);put32(h+20,s->height);put32(h+24,s->background);put32(h+28,s->fade_left);put32(h+32,s->fade_bottom);put32(h+36,s->top_contrast);put32(h+40,(uint32_t)bytes(s));put32(h+44,bg?60:0);put32(h+48,(uint32_t)hash);put32(h+52,(uint32_t)(hash>>32));memcpy(h+56,s->identity,64);
 if(bg){put32(h+120,compressed);put32(h+124,blocks->count);for(unsigned i=0;i<4;i++){put32(h+128+i*8,blocks->block[i].compressed_bytes);put32(h+132+i*8,blocks->block[i].raw_bytes);}}
}
static int full(int fd,void *data,size_t count,int writing)
{
 unsigned char *p=data;while(count){ssize_t n=writing?write(fd,p,count):read(fd,p,count);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;count-=(size_t)n;}return 0;
}
static int read_file(int root,const char *key,const struct mp_asset_spec *s,uint32_t *pixels)
{
 int fd=openat(root,key,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
 unsigned char h[COMPRESSED_HEADER],expected[COMPRESSED_HEADER];struct stat st;int result=-1;unsigned hs=background(s)?COMPRESSED_HEADER:HEADER;
 if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&(uint64_t)st.st_size>=hs&&!full(fd,h,hs,0)){
  uint64_t sum=(uint64_t)get32(h+48)|((uint64_t)get32(h+52)<<32);struct mp_asset_blocks blocks={0};unsigned compressed=0;
  if(background(s)){compressed=get32(h+120);blocks.count=get32(h+124);for(unsigned i=0;i<4;i++){blocks.block[i].compressed_bytes=get32(h+128+i*8);blocks.block[i].raw_bytes=get32(h+132+i*8);}}
  header(expected,s,sum,compressed,&blocks);
  if(!memcmp(h,expected,hs)){
   if(!background(s)){if((uint64_t)st.st_size==HEADER+bytes(s)&&!full(fd,pixels,bytes(s),0))result=0;}
   else if(compressed&&compressed<=bytes(s)+bytes(s)/255+128&&(uint64_t)st.st_size==COMPRESSED_HEADER+(uint64_t)compressed){
    void *encoded=malloc(compressed);if(encoded){if(!full(fd,encoded,compressed,0)&&mp_asset_decompress(encoded,compressed,pixels,bytes(s),&blocks)==(int)bytes(s))result=0;free(encoded);}
   }
   if(!result&&checksum(pixels,bytes(s))!=sum)result=-1;
  }
 }
 close(fd);return result;
}
static int write_file(int root,const char *key,const struct mp_asset_spec *s,uint32_t *pixels)
{
 struct mp_asset_blocks blocks={0};unsigned char *encoded=NULL;const void *body=pixels;size_t body_size=bytes(s);unsigned hs=HEADER;
 if(background(s)){
  size_t capacity=body_size+body_size/255+128;encoded=malloc(capacity);if(!encoded)return -1;
  int n=mp_asset_compress(pixels,body_size,encoded,capacity,&blocks);if(n<0){free(encoded);return -1;}body=encoded;body_size=(size_t)n;hs=COMPRESSED_HEADER;
 }
 char temp[96];struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);snprintf(temp,sizeof temp,".asset-%ld-%ld-%ld.tmp",(long)getpid(),(long)now.tv_sec,now.tv_nsec);
 int fd=openat(root,temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0){free(encoded);return -1;}
 unsigned char h[COMPRESSED_HEADER];header(h,s,checksum(pixels,bytes(s)),background(s)?(unsigned)body_size:0,&blocks);int result=full(fd,h,hs,1)||full(fd,(void *)body,body_size,1)||fsync(fd);if(close(fd))result=-1;free(encoded);
 if(!result)result=renameat(root,temp,root,key);
 if(result)unlinkat(root,temp,0);else (void)fsync(root);
 return result? -1:0;
}
static int open_root(const char *root)
{
 char path[512];if(strlen(root)>=sizeof path)return -1;strcpy(path,root);
 for(char *p=path+1;*p;p++)if(*p=='/'){*p=0;if(mkdir(path,0700)&&errno!=EEXIST)return -1;*p='/';}
 if(mkdir(path,0700)&&errno!=EEXIST)return -1;
 return open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
}
int mp_asset_prepare_file(const char *root,const struct mp_asset_spec *s)
{
 char key[KEY];if(mp_asset_key(s,key,sizeof key))return -1;int fd=open_root(root);if(fd<0)return -1;
 uint32_t *pixels=malloc(bytes(s));int result=-1;
 if(pixels){result=read_file(fd,key,s,pixels);if(result){result=mp_asset_prepare_pixels(s,pixels);if(!result)result=write_file(fd,key,s,pixels);}free(pixels);}close(fd);return result;
}
int mp_asset_read_prepared(const char *root,const struct mp_asset_spec *s,uint32_t *pixels)
{
 char key[KEY];if(!root||!pixels||mp_asset_key(s,key,sizeof key))return -1;
 int fd=open(root,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return -1;
 int result=read_file(fd,key,s,pixels);close(fd);return result;
}
static int victim(struct mp_asset_cache *c,int allow_empty)
{
 int slot=-1;for(unsigned i=0;i<SLOTS;i++){struct entry *e=&c->entries[i];if(!e->state&&allow_empty)return (int)i;if(e->state==3&&!e->refs&&(slot<0||e->used<c->entries[slot].used))slot=(int)i;}return slot;
}
static void evict(struct mp_asset_cache *c,unsigned slot)
{struct entry *e=&c->entries[slot];if(e->state){free(e->pixels);c->bytes-=e->bytes;++c->stats.evictions;}memset(e,0,sizeof *e);}
static void *worker(void *context)
{
#ifdef __linux__
 /* Linux niceness is per thread. Compression workers inherit this priority. */
 (void)setpriority(PRIO_PROCESS,0,19);
#endif
 struct mp_asset_cache *c=context;int root=-1;
 pthread_mutex_lock(&c->mutex);
 while(!c->stop){
  int slot=-1;for(unsigned i=0;i<SLOTS;i++)if(c->entries[i].state==1){slot=(int)i;break;}
  if(slot<0){pthread_cond_wait(&c->cond,&c->mutex);continue;}
  if(root<0){
   pthread_mutex_unlock(&c->mutex);int prepared=!c->prepare_root||c->prepare_root(c->context);if(prepared)root=open_root(c->root);pthread_mutex_lock(&c->mutex);
   if(root<0){pthread_mutex_unlock(&c->mutex);struct timespec delay={1,0};nanosleep(&delay,NULL);pthread_mutex_lock(&c->mutex);continue;}
   if(c->stop)break;
  }
  struct entry *e=&c->entries[slot];e->state=2;struct mp_asset_spec spec=e->spec;char key[KEY];strcpy(key,e->key);size_t size=e->bytes;
  pthread_mutex_unlock(&c->mutex);
  uint32_t *pixels=malloc(size);int disk=-1,result=-1;
  if(pixels){disk=read_file(root,key,&spec,pixels);if(!disk)result=0;else if(!mp_asset_prepare_pixels(&spec,pixels)){result=0;(void)write_file(root,key,&spec,pixels);}}
  pthread_mutex_lock(&c->mutex);e=&c->entries[slot];
  if(result){free(pixels);c->bytes-=e->bytes;memset(e,0,sizeof *e);++c->stats.rejected;}
  else{e->pixels=pixels;e->state=3;e->used=++c->clock;if(!disk)++c->stats.disk_hits;else ++c->stats.generated;(void)write(c->notify[1],"r",1);}
 }
 pthread_mutex_unlock(&c->mutex);if(root>=0)close(root);return NULL;
}
struct mp_asset_cache *mp_asset_cache_open(const char *root,size_t budget,int (*prepare)(void *),void *context)
{
 if(!root||strlen(root)>=512||budget<4096)return NULL;
 struct mp_asset_cache *c=calloc(1,sizeof *c);if(!c)return NULL;
 strcpy(c->root,root);c->budget=budget;c->prepare_root=prepare;c->context=context;
 if(pthread_mutex_init(&c->mutex,NULL)){free(c);return NULL;}
 if(pthread_cond_init(&c->cond,NULL)){pthread_mutex_destroy(&c->mutex);free(c);return NULL;}
 if(pipe(c->notify)){pthread_cond_destroy(&c->cond);pthread_mutex_destroy(&c->mutex);free(c);return NULL;}
 for(unsigned i=0;i<2;i++){fcntl(c->notify[i],F_SETFL,O_NONBLOCK);fcntl(c->notify[i],F_SETFD,FD_CLOEXEC);}
 if(pthread_create(&c->thread,NULL,worker,c)){close(c->notify[0]);close(c->notify[1]);pthread_cond_destroy(&c->cond);pthread_mutex_destroy(&c->mutex);free(c);return NULL;}return c;
}
void mp_asset_cache_close(struct mp_asset_cache *c)
{
 if(!c)return;
 pthread_mutex_lock(&c->mutex);c->stop=1;pthread_cond_signal(&c->cond);pthread_mutex_unlock(&c->mutex);pthread_join(c->thread,NULL);
 close(c->notify[0]);close(c->notify[1]);
 for(unsigned i=0;i<SLOTS;i++)free(c->entries[i].pixels);
 pthread_cond_destroy(&c->cond);pthread_mutex_destroy(&c->mutex);free(c);
}
int mp_asset_cache_fd(struct mp_asset_cache *c){return c?c->notify[0]:-1;}
int mp_asset_cache_request(struct mp_asset_cache *c,const struct mp_asset_spec *s)
{
 if(!c)return -1;
 char key[KEY];if(mp_asset_key(s,key,sizeof key))return -1;size_t size=bytes(s);pthread_mutex_lock(&c->mutex);
 if(size>c->budget){++c->stats.rejected;pthread_mutex_unlock(&c->mutex);return -1;}
 for(unsigned i=0;i<SLOTS;i++)if(c->entries[i].state&&!strcmp(c->entries[i].key,key)){c->entries[i].used=++c->clock;pthread_mutex_unlock(&c->mutex);return 0;}
 while(c->bytes+size>c->budget){int slot=victim(c,0);if(slot<0){pthread_mutex_unlock(&c->mutex);return -1;}evict(c,(unsigned)slot);}
 int slot=victim(c,1);if(slot<0){pthread_mutex_unlock(&c->mutex);return -1;}evict(c,(unsigned)slot);struct entry *e=&c->entries[slot];e->spec=*s;strcpy(e->key,key);e->state=1;e->bytes=size;e->used=++c->clock;c->bytes+=size;pthread_cond_signal(&c->cond);pthread_mutex_unlock(&c->mutex);return 0;
}
int mp_asset_cache_acquire(struct mp_asset_cache *c,const struct mp_asset_spec *s,struct mp_asset_lease *lease)
{
 memset(lease,0,sizeof *lease);if(!c)return 0;char key[KEY];if(mp_asset_key(s,key,sizeof key))return 0;pthread_mutex_lock(&c->mutex);
 for(unsigned i=0;i<SLOTS;i++){struct entry *e=&c->entries[i];if(e->state==3&&!strcmp(e->key,key)){++e->refs;e->used=++c->clock;++c->stats.hits;*lease=(struct mp_asset_lease){c,i,e->pixels,s->width,s->height};pthread_mutex_unlock(&c->mutex);return 1;}}
 ++c->stats.misses;pthread_mutex_unlock(&c->mutex);return 0;
}
void mp_asset_cache_release(struct mp_asset_lease *lease)
{if(!lease->cache)return;struct mp_asset_cache *c=lease->cache;pthread_mutex_lock(&c->mutex);if(c->entries[lease->slot].refs)--c->entries[lease->slot].refs;pthread_mutex_unlock(&c->mutex);memset(lease,0,sizeof *lease);}
void mp_asset_cache_stats(struct mp_asset_cache *c,struct mp_asset_stats *stats)
{
 memset(stats,0,sizeof *stats);if(!c)return;
 pthread_mutex_lock(&c->mutex);*stats=c->stats;stats->bytes=c->bytes;stats->budget=c->budget;
 for(unsigned i=0;i<SLOTS;i++){if(c->entries[i].state==3)++stats->ready;else if(c->entries[i].state)++stats->pending;}pthread_mutex_unlock(&c->mutex);
}
