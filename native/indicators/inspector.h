// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_INSPECTOR_INDICATOR_H
#define MP_INSPECTOR_INDICATOR_H
#include "../status-indicator.h"
struct mp_inspector_indicator {
 char key[3];unsigned font,top,bottom,modifiers;int has_font,visible;
 uint64_t revision,visibility_ms,cached_revision;
 unsigned width;uint8_t pixels[224*36];
};
void mp_inspector_update(struct mp_inspector_indicator *,const char *key,int has_font,unsigned font,unsigned top,unsigned bottom,unsigned modifiers,int visible,uint64_t now);
extern const struct mp_status_indicator_ops mp_inspector_indicator_ops;
#endif
