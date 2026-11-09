// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_ASSET_CODEC_H
#define MP_ASSET_CODEC_H
#include <stddef.h>
struct mp_asset_blocks {
 unsigned count;
 struct {unsigned compressed_bytes,raw_bytes;} block[4];
};
/* Preparation only: four independent HC12 dictionaries for >=1 MiB, otherwise
 * one. Thread limits fall back to the same deterministic blocks sequentially. */
int mp_asset_compress(const void *,size_t,void *,size_t,struct mp_asset_blocks *);
/* Validate every extent before allocating workers or accessing a block. */
int mp_asset_decompress(const void *,size_t,void *,size_t,const struct mp_asset_blocks *);
#endif
