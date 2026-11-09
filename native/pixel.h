// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_PIXEL_H
#define MP_PIXEL_H
#include <stdint.h>
/* Straight-alpha RGB source over an opaque RGB destination. */
static inline uint32_t mp_pixel_blend(uint32_t destination,uint32_t source,unsigned alpha)
{
 if(!alpha)return destination;
 if(alpha>=255)return source;
 uint32_t result=0;
 for(unsigned shift=0;shift<24;shift+=8)
  result|=(((((source>>shift)&255)*alpha+((destination>>shift)&255)*(255-alpha)+127)/255)<<shift);
 return result;
}
#endif
