// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_ASSET_CACHE_H
#define MP_ASSET_CACHE_H
#include "game-art.h"
#include <stdint.h>
#include <stddef.h>
enum mp_asset_kind {MP_ASSET_CARD=1,MP_ASSET_BACKGROUND=2,MP_ASSET_CURVED_BACKGROUND=3};
#define MP_ASSET_PREPROCESS_ABI UINT64_C(0x4e4c363000000001)
struct mp_asset_spec {
 const struct mp_game_art *source;const char *identity;
 unsigned kind,width,height;uint32_t background;
 /* Kind 2: legacy edge fades. Kind 3: clear-left %, vertical inset %, text shade %. */
 unsigned fade_left,fade_bottom,top_contrast;
};
struct mp_asset_cache;
struct mp_asset_lease {struct mp_asset_cache *cache;unsigned slot;const uint32_t *pixels;unsigned width,height;};
struct mp_asset_stats {size_t bytes,budget;unsigned ready,pending,hits,misses,disk_hits,generated,rejected,evictions;};
/* Call only after the second confirmed refresh. prepare_root runs on worker.
 * Sources are immutable and must outlive the cache. Release leases before close. */
struct mp_asset_cache *mp_asset_cache_open(const char *root,size_t budget,int (*prepare_root)(void *),void *context);
void mp_asset_cache_close(struct mp_asset_cache *);
int mp_asset_cache_fd(struct mp_asset_cache *);
int mp_asset_cache_request(struct mp_asset_cache *,const struct mp_asset_spec *);
int mp_asset_cache_acquire(struct mp_asset_cache *,const struct mp_asset_spec *,struct mp_asset_lease *);
void mp_asset_cache_release(struct mp_asset_lease *);
void mp_asset_cache_stats(struct mp_asset_cache *,struct mp_asset_stats *);
/* Shared native preparation path, used by the client tool and cache worker. */
int mp_asset_key(const struct mp_asset_spec *,char *out,size_t size);
int mp_asset_render(const struct mp_asset_spec *,uint32_t *pixels);
/* Deferred preparation: backgrounds use bounded near-lossless quality 60.
 * Cards remain exact. Raw rendering above stays available for uncached drawing. */
int mp_asset_prepare_pixels(const struct mp_asset_spec *,uint32_t *pixels);
/* Render a clipped region directly into either framebuffer stride, without allocation. */
int mp_asset_render_region(const struct mp_asset_spec *,unsigned x,unsigned y,unsigned width,unsigned height,uint32_t *,ptrdiff_t x_stride,ptrdiff_t y_stride);
unsigned mp_asset_curved_alpha(const struct mp_asset_spec *,unsigned x,unsigned y);
int mp_asset_prepare_file(const char *root,const struct mp_asset_spec *);
/* Reuse an already prepared persistent asset without changing its source. */
int mp_asset_read_prepared(const char *root,const struct mp_asset_spec *,uint32_t *pixels);
#endif
