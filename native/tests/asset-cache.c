// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "asset-cache.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
static void ready(struct mp_asset_cache *c,const struct mp_asset_spec *s,struct mp_asset_lease *lease)
{
 for(unsigned i=0;i<100;i++){
  if(mp_asset_cache_acquire(c,s,lease))return;
  struct pollfd p={mp_asset_cache_fd(c),POLLIN,0};(void)poll(&p,1,50);char b[64];while(read(p.fd,b,sizeof b)>0){}
 }
 assert(!"Cache preparation timed out");
}
int main(void)
{
 char root[]="/tmp/minpocket-assets-XXXXXX";assert(mkdtemp(root));
 const unsigned char input[]={0,1,2,3};uint32_t palette[256]={0x24283b,0x7dcfff,0x7aa2f7,0xc0caf5};struct mp_game_art art={2,2,input,palette};
 const char *id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
 struct mp_asset_spec s={&art,id,MP_ASSET_CARD,16,16,0,0,0,0};char key[160];assert(!mp_asset_key(&s,key,sizeof key));
 struct mp_asset_spec invalid=s;invalid.identity="../invalid";assert(mp_asset_key(&invalid,key,sizeof key));assert(!mp_asset_key(&s,key,sizeof key));
 uint32_t expected[256];assert(!mp_asset_render(&s,expected));
 for(unsigned x=0;x<16;x++)for(unsigned y=0;y<16;y++)assert(expected[x*16+y]==palette[input[(y*2/16)*2+x*2/16]]);
 assert(!mp_asset_prepare_file(root,&s));
 struct mp_asset_cache *c=mp_asset_cache_open(root,4096,NULL,NULL);assert(c);assert(!mp_asset_cache_request(c,&s));struct mp_asset_lease pinned;ready(c,&s,&pinned);assert(!memcmp(expected,pinned.pixels,sizeof expected));
 struct mp_asset_stats stats;mp_asset_cache_stats(c,&stats);assert(stats.disk_hits==1&&!stats.generated);
 for(unsigned w=17;w<22;w++){
  struct mp_asset_spec other=s;other.width=w;
  int result=mp_asset_cache_request(c,&other);
  if(!result){struct mp_asset_lease lease;ready(c,&other,&lease);mp_asset_cache_release(&lease);}
  mp_asset_cache_stats(c,&stats);assert(stats.bytes<=stats.budget);assert(!memcmp(expected,pinned.pixels,sizeof expected));
 }
 assert(stats.evictions>0);struct mp_asset_spec huge=s;huge.width=3840;assert(mp_asset_cache_request(c,&huge)<0);
 mp_asset_cache_release(&pinned);mp_asset_cache_close(c);
 char file[512];snprintf(file,sizeof file,"%s/%s",root,key);int fd=open(file,O_RDWR);assert(fd>=0);unsigned char corrupt=99;assert(pwrite(fd,&corrupt,1,128)==1);close(fd);
 c=mp_asset_cache_open(root,4096,NULL,NULL);assert(!mp_asset_cache_request(c,&s));ready(c,&s,&pinned);assert(!memcmp(expected,pinned.pixels,sizeof expected));mp_asset_cache_stats(c,&stats);assert(stats.generated==1&&!stats.disk_hits);mp_asset_cache_release(&pinned);mp_asset_cache_close(c);
 fd=open(file,O_WRONLY|O_TRUNC);assert(fd>=0);assert(write(fd,"bad",3)==3);close(fd);
 c=mp_asset_cache_open(root,4096,NULL,NULL);assert(!mp_asset_cache_request(c,&s));ready(c,&s,&pinned);assert(!memcmp(expected,pinned.pixels,sizeof expected));mp_asset_cache_release(&pinned);mp_asset_cache_close(c);
 char target[512];snprintf(target,sizeof target,"%s/original",root);fd=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0&&write(fd,"original",8)==8);close(fd);assert(!unlink(file));assert(!symlink(target,file));assert(!mp_asset_prepare_file(root,&s));
 fd=open(target,O_RDONLY);char original[8];assert(fd>=0&&read(fd,original,8)==8&&!memcmp(original,"original",8));close(fd);
 /* Background-only policy, compressed persistence, exact prepared disk hits,
  * incompatible/corrupt directory rejection and original palette retention. */
 struct mp_asset_spec bg={&art,id,MP_ASSET_CURVED_BACKGROUND,960,720,0x24283b,25,4,0};
 uint32_t *raw=malloc(960*720*4),*prepared=malloc(960*720*4),*restored=malloc(960*720*4);assert(raw&&prepared&&restored);
 assert(!mp_asset_render(&bg,raw)&&!mp_asset_prepare_pixels(&bg,prepared));
 for(unsigned i=0;i<960*720;i++)for(unsigned shift=0;shift<24;shift+=8)assert(abs((int)(raw[i]>>shift&255)-(int)(prepared[i]>>shift&255))<=2);
 assert(!memcmp(palette,(uint32_t[4]){0x24283b,0x7dcfff,0x7aa2f7,0xc0caf5},16));assert(!mp_asset_key(&bg,key,sizeof key)&&!strncmp(key,"v3n60-",6));
 assert(!mp_asset_prepare_file(root,&bg)&&!mp_asset_read_prepared(root,&bg,restored)&&!memcmp(restored,prepared,960*720*4));
 snprintf(file,sizeof file,"%s/%s",root,key);struct stat st;assert(!stat(file,&st)&&st.st_size<960*720*4);
 c=mp_asset_cache_open(root,4*1024*1024,NULL,NULL);assert(c&&!mp_asset_cache_request(c,&bg));ready(c,&bg,&pinned);assert(!memcmp(prepared,pinned.pixels,960*720*4));mp_asset_cache_stats(c,&stats);assert(stats.disk_hits==1&&!stats.generated);mp_asset_cache_release(&pinned);mp_asset_cache_close(c);
 fd=open(file,O_RDWR);assert(fd>=0&&pwrite(fd,"\377\377\377\377",4,128)==4);close(fd);assert(mp_asset_read_prepared(root,&bg,restored)<0);
 c=mp_asset_cache_open(root,4*1024*1024,NULL,NULL);assert(c&&!mp_asset_cache_request(c,&bg));ready(c,&bg,&pinned);assert(!memcmp(prepared,pinned.pixels,960*720*4));mp_asset_cache_stats(c,&stats);assert(stats.generated==1&&!stats.disk_hits);mp_asset_cache_release(&pinned);mp_asset_cache_close(c);
 free(raw);free(prepared);free(restored);
 printf("Asset cache: exact samples, disk hits, corruption/truncation recovery, pinned LRU, hard budget and atomic symlink replacement passed. Root: %s\n",root);
}
