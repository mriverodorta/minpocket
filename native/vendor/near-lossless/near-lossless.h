/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MP_NEAR_LOSSLESS_H
#define MP_NEAR_LOSSLESS_H
#include <stdint.h>
/* Row-major uint32 ARGB. The alpha channel remains unchanged for XRGB inputs.
 * Quality 60 bounds RGB channel changes to 2. Allocation failure changes nothing.
 * Caller retains the original downloaded image; this operates on a cache copy. */
int mp_near_lossless_inplace(uint32_t *,unsigned width,unsigned height,unsigned quality);
#endif
