"""Compile the actual loader cache routines against a bounded, in-memory SD.

No device nodes are opened. Check newest selection, CRC fallback, blank slots,
initramfs bounds, and the two fixed CPIO paths emitted from writable UI data.
"""
from pathlib import Path
import os,struct,subprocess,sys,tempfile,zlib
root=Path(__file__).resolve().parents[1]
s=(root/'boot/startup-cache.c').read_text()
s=s[s.index('static ulong startup_bytes'):]
s=s.replace('ALLOC_CACHE_ALIGN_BUFFER(struct mp_home_disk_header,headers,2);','struct mp_home_disk_header headers[2];')
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "home-cache-format.h"
typedef unsigned long ulong;typedef unsigned u32;typedef unsigned long long lbaint_t;typedef unsigned long phys_addr_t;
static unsigned char storage[4*1024*1024] __attribute__((aligned(64)));
static unsigned char combined[24*1024*1024] __attribute__((aligned(64)));
static unsigned char disk_bytes[9*1024*1024];
#define MP_STARTUP_BUFFER ((ulong)storage)
#define MP_COMBINED_BUFFER ((ulong)combined)
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define ALIGN(n,a) (((n)+(a)-1)&~((a)-1))
#define LMB_MEM_ALLOC_ADDR 1
#define LMB_NONE 0
struct blk_desc {unsigned unused;};
struct disk_partition {lbaint_t start,size;};
static unsigned crc32(unsigned start,const void *data,size_t size){assert(!start);const unsigned char *p=data;unsigned c=~0u;while(size--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
static ulong get_timer(ulong base){static ulong now;return ++now-base;}
static int lmb_alloc_mem(int type,int align,phys_addr_t *address,ulong size,int flags){(void)type;(void)align;(void)flags;assert(*address==(ulong)combined);assert(size<=sizeof combined);return 0;}
static lbaint_t blk_dread(struct blk_desc *disk,lbaint_t block,lbaint_t count,void *target){(void)disk;assert(block*512+count*512<=sizeof disk_bytes);memcpy(target,disk_bytes+block*512,count*512);return count;}
'''
main=r'''
int main(int argc,char **argv){
 assert(argc==3);assert(crc32(0,"123456789",9)==0xcbf43926);
 FILE *f=fopen(argv[1],"rb");assert(f);size_t size=fread(disk_bytes+1024,1,MP_HOME_SLOT_BYTES,f);fclose(f);
 assert(size>512&&size<MP_HOME_SLOT_BYTES);
 struct mp_home_disk_header *old=(void *)(disk_bytes+1024),*new=(void *)(disk_bytes+1024+MP_HOME_SLOT_BYTES);
 assert(startup_header(old));old->generation=1;old->header_crc=0;old->header_crc=crc32(0,old,512);
 memcpy(new,old,size);new->generation=2;new->header_crc=0;new->header_crc=crc32(0,new,512);
 struct blk_desc disk={0};struct disk_partition part={1,MP_HOME_RESERVED_BLOCKS+1};
 startup_load(&disk,&part);assert(startup_bytes==size);assert(((struct mp_home_disk_header *)storage)->generation==2);
 ((unsigned char *)new)[600]^=1;startup_load(&disk,&part);assert(startup_bytes==size);assert(((struct mp_home_disk_header *)storage)->generation==1);
 unsigned char base[8]={0x1f,0x8b};ulong start=(ulong)base,end=start+sizeof base;
 assert(!minpocket_extend_initrd(&start,&end));assert(start==(ulong)combined);assert(!memcmp(combined,base,sizeof base));assert(end-start<=size+sizeof base+512);
 f=fopen(argv[2],"wb");assert(f);assert(fwrite(combined+sizeof base,1,end-start-sizeof base,f)==end-start-sizeof base);fclose(f);
 memset(old,0,512);memset(new,0,512);startup_load(&disk,&part);assert(!startup_bytes);
 start=(ulong)base;end=start+sizeof base;assert(!minpocket_extend_initrd(&start,&end));assert(start==(ulong)base);
 puts("U-Boot newest slot, corrupt-body fallback, blank cache, fixed CPIO paths: passed");return 0;
}
'''
out=Path(sys.argv[1]) if len(sys.argv)>1 else root/'build/tests/startup'
out.mkdir(parents=True,exist_ok=True)
# Synthetic framed cache bytes test the loader's CRC and extent checks. This
# fixture is not a renderer snapshot and contains no personal library data.
body=bytes((i*17+3)&255 for i in range(1024))
header=bytearray(struct.pack('<8s6I2Q',b'MPHOME1\0',1,512,len(body),2048,zlib.crc32(body),0,1,1)+bytes(464))
struct.pack_into('<I',header,28,zlib.crc32(header))
(out/'home.startup').write_bytes(header+body)
(out/'loader-test.c').write_text(pre+s+main)
subprocess.run([os.environ.get('HOSTCC','cc'),'-std=c11','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror','-I'+str(root/'native'),str(out/'loader-test.c'),'-o',str(out/'loader-test')],check=True)
subprocess.run([str(out/'loader-test'),str(out/'home.startup'),str(out/'startup.cpio')],check=True)
b=(out/'startup.cpio').read_bytes();off=0;entries=[]
while off<len(b):
 h=b[off:off+110];assert h[:6]==b'070701'
 fields=[int(h[6+i*8:14+i*8],16) for i in range(13)]
 size,namesize=fields[6],fields[11];name=b[off+110:off+110+namesize-1].decode();start=(off+110+namesize+3)&~3
 assert fields[1]==0o100600
 entries.append(name)
 if name=='minpocket-startup':assert b[start+512:start+size]==(out/'home.startup').read_bytes()[512:]
 off=(start+size+3)&~3
assert entries==['minpocket-startup','TRAILER!!!']
