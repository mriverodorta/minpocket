// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "asset-platform.h"
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int mp_asset_data_ready(void *context)
{
 const char *target_path=context?(const char *)context:"/media/Data";
 char status[32]={0};FILE *f=fopen("/run/minpocket-data.status","r");if(!f)return 0;
 int ok=fgets(status,sizeof status,f)!=NULL;fclose(f);
 if(!ok||(strcmp(status,"existing\n")&&strcmp(status,"initialized\n")&&strcmp(status,"existing")&&strcmp(status,"initialized")))return 0;
 glob_t paths={0};
 const char *pattern="/sys/bus/platform/devices/fe2b0000.mmc/mmc_host/mmc*/mmc*:*/block/mmcblk*/mmcblk*p[34]";
 if(glob(pattern,0,NULL,&paths)){globfree(&paths);return 0;}
 char path[640],device[80]={0};unsigned matches=0;
 for(size_t i=0;i<paths.gl_pathc;i++){
  unsigned long long start=0;snprintf(path,sizeof path,"%s/start",paths.gl_pathv[i]);f=fopen(path,"r");if(f){if(fscanf(f,"%llu",&start)!=1)start=0;fclose(f);}
  if(start!=294912)continue;
  char line[256];int named=0;snprintf(path,sizeof path,"%s/uevent",paths.gl_pathv[i]);f=fopen(path,"r");
  if(f){while(fgets(line,sizeof line,f))if(!strcmp(line,"PARTNAME=Data\n"))named=1;fclose(f);}
  if(named){snprintf(device,sizeof device,"/dev/%s",strrchr(paths.gl_pathv[i],'/')+1);matches++;}
 }
 globfree(&paths);if(matches!=1)return 0;
 if(mkdir(target_path,0755)&&errno!=EEXIST)return 0;
 if(!mount(device,target_path,"exfat",MS_NOSUID|MS_NODEV|MS_NOEXEC,"uid=0,gid=0,umask=0077"))return 1;
 if(errno!=EBUSY)return 0;
 f=fopen("/proc/mounts","r");if(!f)return 0;
 char line[1024],source[256],target[256],type[64];ok=0;
 while(fgets(line,sizeof line,f))if(sscanf(line,"%255s %255s %63s",source,target,type)==3&&!strcmp(source,device)&&!strcmp(target,target_path)&&!strcmp(type,"exfat")){ok=1;break;}
 fclose(f);return ok;
}
