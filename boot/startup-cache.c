/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by the MP1 loader. Writable cache data never supplies CPIO paths. */
#include <u-boot/crc.h>
#include <lmb.h>
#include <memalign.h>
#include "minpocket-home-cache-format.h"
#define MP_STARTUP_BUFFER 0x12000000UL
#define MP_COMBINED_BUFFER 0x10000000UL
static ulong startup_bytes, startup_ms;
static bool startup_appended;
static bool startup_header(const struct mp_home_disk_header *header)
{
 struct mp_home_disk_header h=*header;
 u32 expected=h.header_crc;h.header_crc=0;
 return !memcmp(h.magic,"MPHOME1",8)&&h.version==1&&h.header_bytes==512&&
  h.compressed_bytes&&h.compressed_bytes<=MP_HOME_SLOT_BYTES-512&&
  h.raw_bytes&&h.raw_bytes<=MP_HOME_RAW_MAX&&h.abi==MP_HOME_ABI&&
  crc32(0,(void *)&h,sizeof h)==expected;
}
static void startup_load(struct blk_desc *disk,const struct disk_partition *partition)
{
 ALLOC_CACHE_ALIGN_BUFFER(struct mp_home_disk_header,headers,2);
 bool valid[2]={false,false};ulong begin=get_timer(0);
 startup_bytes=0;startup_ms=0;startup_appended=false;
 if(partition->size<=MP_HOME_RESERVED_BLOCKS)return;
 lbaint_t base=partition->start+partition->size-MP_HOME_RESERVED_BLOCKS;
 for(unsigned i=0;i<2;i++){
  lbaint_t read=blk_dread(disk,base+i*(MP_HOME_SLOT_BYTES/512),1,&headers[i]);
  valid[i]=read==1&&startup_header(&headers[i]);
 }
 unsigned newest=valid[1]&&(!valid[0]||headers[1].generation>headers[0].generation)?1:0;
 for(unsigned attempt=0;attempt<2;attempt++){
  unsigned i=attempt?1-newest:newest;if(!valid[i])continue;
  ulong bytes=512+headers[i].compressed_bytes,blocks=DIV_ROUND_UP(bytes,512);
  lbaint_t read=blk_dread(disk,base+i*(MP_HOME_SLOT_BYTES/512),blocks,(void *)MP_STARTUP_BUFFER);
  if(read!=blocks||memcmp((void *)MP_STARTUP_BUFFER,&headers[i],512))continue;
  if(crc32(0,(void *)(MP_STARTUP_BUFFER+512),headers[i].compressed_bytes)!=headers[i].crc)continue;
  startup_bytes=bytes;break;
 }
 startup_ms=get_timer(begin);
}
static char *startup_cpio(char *out,const char *name,const void *data,ulong size)
{
 char header[111];unsigned namesize=strlen(name)+1;
 snprintf(header,sizeof header,"070701%08x%08x%08x%08x%08x%08x%08lx%08x%08x%08x%08x%08x%08x",
  0,0100600,0,0,1,0,size,0,0,0,0,namesize,0);
 memcpy(out,header,110);out+=110;memcpy(out,name,namesize);out+=namesize;
 while((ulong)out&3)*out++=0;
 if(size){memcpy(out,data,size);out+=size;}
 while((ulong)out&3)*out++=0;
 return out;
}
int minpocket_extend_initrd(ulong *start,ulong *end)
{
 if(!startup_bytes||startup_appended)return 0;
 if(*end<=*start||*end-*start>16*1024*1024)return -EINVAL;
 ulong base_bytes=*end-*start,total=ALIGN(base_bytes,4)+startup_bytes+512;
 phys_addr_t address=MP_COMBINED_BUFFER;
 if(lmb_alloc_mem(LMB_MEM_ALLOC_ADDR,0,&address,total,LMB_NONE))return -ENOMEM;
 char *out=(char *)MP_COMBINED_BUFFER;
 memcpy(out,(void *)*start,base_bytes);out+=base_bytes;
 while((ulong)out&3)*out++=0;
 out=startup_cpio(out,"minpocket-startup",(void *)MP_STARTUP_BUFFER,startup_bytes);
 out=startup_cpio(out,"TRAILER!!!",NULL,0);
 *start=MP_COMBINED_BUFFER;*end=(ulong)out;startup_appended=true;
 return 0;
}
