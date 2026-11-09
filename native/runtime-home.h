// SPDX-License-Identifier: GPL-2.0-only
#ifndef MP_RUNTIME_HOME_H
#define MP_RUNTIME_HOME_H
#include "home-view.h"
#include "home-cache-format.h"
struct mp_runtime;
const struct mp_game_assets *mp_runtime_assets(size_t *count);
unsigned mp_crc32(const void *,size_t);
unsigned long long mp_home_abi(void);
int mp_home_header_valid(const struct mp_home_disk_header *);
struct mp_runtime *mp_runtime_boot(struct mp_home_state *);
void mp_runtime_report(const struct mp_runtime *);
int mp_runtime_poll(struct mp_runtime *,struct mp_home_state *,unsigned long long now_ms);
int mp_runtime_index(const char *root);
int mp_runtime_publish(const char *root,const char *selected,int scroll,const char *output);
int mp_runtime_write_slots(const char *path,int seed);
#endif
