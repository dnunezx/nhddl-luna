#include "apps.h"
#include "options.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

static char appDirectory[PATH_MAX+1];
static int fieldValid(const char *text,int required) {
  if(!text || (required && !*text))return 0;
  for(const unsigned char *p=(const unsigned char *)text;*p;p++)if(*p<32 || *p==127)return 0;
  return 1;
}
void lunaAppsConfigure(const char *directory) {
  appDirectory[0]=0;
  if(!directory || !*directory || strlen(directory)>PATH_MAX)return;
  snprintf(appDirectory,sizeof(appDirectory),"%s",directory);
  size_t length=strlen(appDirectory);
  while(length>1 && appDirectory[length-1]=='/')appDirectory[--length]=0;
}
int lunaAppsResolve(const char *path,char *result,size_t size) {
  if(!path || !*path)return -EINVAL;
  int length=strchr(path,':')?snprintf(result,size,"%s",path):
      appDirectory[0]?snprintf(result,size,"%s/%s",appDirectory,path):-1;
  return length<0 || (size_t)length>=size?-ENAMETOOLONG:0;
}
int lunaAppsStorePath(const char *path,char *result,size_t size) {
  if(!fieldValid(path,1))return -EINVAL;
  size_t length=strlen(appDirectory);
  if(length && !strncmp(path,appDirectory,length) && path[length]=='/')path+=length+1;
  int written=snprintf(result,size,"%s",path);
  return written<0 || (size_t)written>=size?-ENAMETOOLONG:0;
}
static int configPath(char *path,size_t size,int temporary) {
  if(!appDirectory[0])return -EROFS;
  int length=snprintf(path,size,"%s/apps.cfg%s",appDirectory,temporary?".tmp":"");
  return length<0 || (size_t)length>=size?-ENAMETOOLONG:0;
}
static int appRecordMissing(const char *path) {
  // BDM exposes FatFs results, where 4/5 mean missing and 2 is an I/O error.
  if(!strncmp(path,"mass",4) && path[4]>='0' && path[4]<='9' && path[5]==':') {
    iox_stat_t info;int result=fileXioGetStat(path,&info);
    return result==-4 || result==-5;
  }
  // PCSX2 HostFS open can report generic I/O failure for a missing file;
  // its stat operation still identifies that missing file correctly.
  struct stat info;
  return stat(path,&info)<0 && errno==ENOENT;
}
static int readApps(const char *path,LunaApps *apps) {
  FILE *file=fopen(path,"rb");
  if(!file)return appRecordMissing(path)?-ENOENT:-EIO;
  char *line=malloc(PATH_MAX+LUNA_APP_NAME+8);
  if(!line){fclose(file);return -ENOMEM;}
  int error=-EINVAL,count=0;
  if(!fgets(line,PATH_MAX+LUNA_APP_NAME+8,file) || strcmp(line,"LUNA_APPS_1\n"))goto done;
  while(fgets(line,PATH_MAX+LUNA_APP_NAME+8,file)) {
    if(!strcmp(line,"END\n")) {
      if(fgetc(file)==EOF && !ferror(file)){apps->count=count;error=0;}
      break;
    }
    size_t length=strlen(line);
    if(!length || line[length-1]!='\n' || count>=LUNA_APP_LIMIT)break;
    line[length-1]=0;
    char *elf=strchr(line,'\t');if(!elf)break;*elf++=0;
    if(strlen(line)>LUNA_APP_NAME || strlen(elf)>PATH_MAX ||
       !fieldValid(line,1) || !fieldValid(elf,1))break;
    LunaApp *app=&apps->items[count++];
    strcpy(app->name,line);strcpy(app->elf,elf);
  }
done:
  free(line);if(fclose(file) && !error)error=-EIO;
  return error;
}
int lunaAppsLoad(LunaApps *apps) {
  memset(apps,0,sizeof(*apps));
  char path[PATH_MAX+16];int error=configPath(path,sizeof(path),1);
  if(error)return error;
  int temporary=readApps(path,apps);
  if(!temporary)return 0;
  memset(apps,0,sizeof(*apps));configPath(path,sizeof(path),0);
  error=readApps(path,apps);
  if(error){memset(apps,0,sizeof(*apps));if(error==-ENOENT && temporary==-ENOENT)return 0;}
  return error;
}
int lunaAppsSave(const LunaApps *apps) {
  if(apps->count<0 || apps->count>LUNA_APP_LIMIT)return -EINVAL;
  for(int i=0;i<apps->count;i++)if(!fieldValid(apps->items[i].name,1) ||
      !fieldValid(apps->items[i].elf,1))return -EINVAL;
  char path[PATH_MAX+16],stage[PATH_MAX+16];
  int error=configPath(path,sizeof(path),0);if(error)return error;
  error=configPath(stage,sizeof(stage),1);if(error)return error;
  FILE *file=fopen(stage,"wb");if(!file)return -EIO;
  int failed=fputs("LUNA_APPS_1\n",file)<0;
  for(int i=0;i<apps->count && !failed;i++) {
    const LunaApp *app=&apps->items[i];
    failed=fprintf(file,"%s\t%s\n",app->name,app->elf)<0;
  }
  if(!failed)failed=fputs("END\n",file)<0;
  if(fclose(file))failed=1;
  if(failed){remove(stage);return -EIO;}
  return commitConfigFile(stage,path);
}
void lunaAppSuggestName(const char *path,char *name,size_t size) {
  const char *base=strrchr(path,'/');base=base?base+1:path;
  size_t length=strlen(base);if(length>4 && !strcasecmp(base+length-4,".elf"))length-=4;
  if((length==4 && !strncasecmp(base,"BOOT",4)) || (length==3 && !strncasecmp(base,"APP",3))) {
    if(base>path) {
      const char *end=base-1,*start=end;
      while(start>path && start[-1]!='/' && start[-1]!=':')start--;
      if(end>start){base=start;length=(size_t)(end-start);}
    }
  }
  if(length>=size)length=size-1;
  // Do not split a UTF-8 code point at the end of a suggested name.
  while(length && ((unsigned char)base[length]&0xc0)==0x80)length--;
  memcpy(name,base,length);name[length]=0;
}
int lunaAppsAdd(LunaApps *apps,const char *path,const char *name) {
  if(!fieldValid(name,1) || strlen(name)>LUNA_APP_NAME)return -EINVAL;
  char stored[PATH_MAX+1];int error=lunaAppsStorePath(path,stored,sizeof(stored));if(error)return error;
  for(int i=0;i<apps->count;i++)if(!strcmp(apps->items[i].elf,stored))return -EEXIST;
  if(apps->count>=LUNA_APP_LIMIT)return -ENOSPC;
  LunaApp *app=&apps->items[apps->count];memset(app,0,sizeof(*app));
  strcpy(app->name,name);strcpy(app->elf,stored);apps->count++;
  error=lunaAppsSave(apps);if(error){apps->count--;memset(app,0,sizeof(*app));}
  return error;
}
