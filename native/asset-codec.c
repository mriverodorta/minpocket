// SPDX-License-Identifier: GPL-2.0-only
#include "asset-codec.h"
#include "vendor/lz4/lz4.h"
#include "vendor/lz4/lz4hc.h"
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct job {const char *source;char *output;unsigned source_bytes,output_bytes;int result;};
static void *compress(void *context)
{struct job *j=context;j->result=LZ4_compress_HC(j->source,j->output,(int)j->source_bytes,(int)j->output_bytes,12);return NULL;}
static void *decompress(void *context)
{struct job *j=context;j->result=LZ4_decompress_safe(j->source,j->output,(int)j->source_bytes,(int)j->output_bytes);return NULL;}
static void run(struct job jobs[4],unsigned count,void *(*fn)(void *))
{
 pthread_t threads[4];unsigned active[4]={0};
 for(unsigned i=1;i<count;i++)active[i]=!pthread_create(&threads[i],NULL,fn,&jobs[i]);
 fn(&jobs[0]);
 for(unsigned i=1;i<count;i++){if(active[i])pthread_join(threads[i],NULL);else fn(&jobs[i]);}
}
int mp_asset_compress(const void *source,size_t size,void *destination,size_t capacity,struct mp_asset_blocks *blocks)
{
 if(!source||!destination||!blocks||!size||size>LZ4_MAX_INPUT_SIZE||capacity>INT_MAX)return -1;
 memset(blocks,0,sizeof *blocks);unsigned count=size>=1024*1024?4:1;struct job jobs[4]={0};size_t used=0;int valid=1;
 for(unsigned i=0;i<count;i++){
  size_t begin=size*i/count,end=size*(i+1)/count;int bound=LZ4_compressBound((int)(end-begin));
  jobs[i]=(struct job){(const char *)source+begin,malloc((size_t)bound),(unsigned)(end-begin),(unsigned)bound,0};
  if(!jobs[i].output){valid=0;break;}
 }
 if(valid){
  run(jobs,count,compress);
  for(unsigned i=0;i<count;i++)if(jobs[i].result<=0||(size_t)jobs[i].result>capacity-used){valid=0;break;}else{
   memcpy((char *)destination+used,jobs[i].output,(size_t)jobs[i].result);
   blocks->block[i].compressed_bytes=(unsigned)jobs[i].result;blocks->block[i].raw_bytes=jobs[i].source_bytes;used+=(unsigned)jobs[i].result;
  }
 }
 for(unsigned i=0;i<count;i++)free(jobs[i].output);
 if(!valid){memset(blocks,0,sizeof *blocks);return -1;}blocks->count=count;return (int)used;
}
int mp_asset_decompress(const void *source,size_t size,void *destination,size_t output_size,const struct mp_asset_blocks *blocks)
{
 if(!source||!destination||!blocks||!size||!output_size||size>INT_MAX||output_size>INT_MAX||(blocks->count!=1&&blocks->count!=4))return -1;
 for(unsigned i=blocks->count;i<4;i++)if(blocks->block[i].compressed_bytes||blocks->block[i].raw_bytes)return -1;
 struct job jobs[4];size_t compressed=0,raw=0;
 for(unsigned i=0;i<blocks->count;i++){
  unsigned c=blocks->block[i].compressed_bytes,r=blocks->block[i].raw_bytes;
  if(!c||!r||c>size-compressed||r>output_size-raw||c>INT_MAX||r>INT_MAX)return -1;
  jobs[i]=(struct job){(const char *)source+compressed,(char *)destination+raw,c,r,-1};compressed+=c;raw+=r;
 }
 if(compressed!=size||raw!=output_size)return -1;
 run(jobs,blocks->count,decompress);
 for(unsigned i=0;i<blocks->count;i++)if(jobs[i].result!=(int)jobs[i].output_bytes)return -1;
 return (int)raw;
}
