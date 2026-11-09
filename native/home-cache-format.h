/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MP_HOME_CACHE_FORMAT_H
#define MP_HOME_CACHE_FORMAT_H
#define MP_HOME_SLOT_BYTES (4u*1024u*1024u)
#define MP_HOME_RESERVED_BLOCKS 16384u
#define MP_HOME_RAW_MAX (4u*1024u*1024u)
#define MP_HOME_VISIBLE_MAX 8u
#define MP_LIBRARY_MAX 4096u
#define MP_HOME_ABI 1u
#define MP_HOME_BLOCKS 4u
/* Optional header metadata. Zero reserved bytes retain legacy single-block LZ4. */
struct mp_home_blocks {unsigned char magic[8];unsigned count;struct {unsigned compressed_bytes,raw_bytes;} block[MP_HOME_BLOCKS];};
struct mp_home_disk_header {
 unsigned char magic[8];unsigned version,header_bytes,compressed_bytes,raw_bytes,crc,header_crc;
 unsigned long long generation,abi;unsigned char reserved[464];
};
struct mp_disk_game {char id[80],title[256],metadata[160],description[1536],rom[512],asset[80];unsigned flags;};
struct mp_catalog_header {char magic[8];unsigned version,count,bytes,crc;unsigned long long generation;};
struct mp_art_header {char magic[8];unsigned width,height,bytes,crc;char identity[64];unsigned reserved[2];};
/* MPVIEW2 adds these layout dependencies before its visible records. */
struct mp_home_startup_geometry {unsigned row_heading,row_height;};
struct mp_home_raw_header {char magic[8];unsigned count,selected;int scroll;unsigned background_bytes,first,reserved;unsigned long long abi;};
#endif
