#include "apps.h"
#include "options.h"
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
static int appRecordMissing(const char *path) { return configFileMissing(path); }
static int appLine(FILE *file,char *line,size_t size) {
  if(!fgets(line,size,file))return 0;
  size_t length=strlen(line);
  if(!length || line[length-1]!='\n')return -1;
  line[--length]=0;
  if(length && line[length-1]=='\r')line[--length]=0;
  return 1;
}
static int readApps(const char *path,LunaApps *apps) {
  FILE *file=fopen(path,"rb");
  if(!file)return appRecordMissing(path)?-ENOENT:-EIO;
  const size_t capacity=PATH_MAX+LUNA_APP_NAME+LUNA_APP_ARGUMENT+12;
  char *line=malloc(capacity);
  if(!line){fclose(file);return -ENOMEM;}
  int error=-EINVAL,count=0;
  if(appLine(file,line,capacity)!=1)goto done;
  const int version=!strcmp(line,"LUNA_APPS_2")?2:!strcmp(line,"LUNA_APPS_1")?1:0;
  if(!version)goto done;
  while(appLine(file,line,capacity)==1) {
    if(!strcmp(line,"END")) {
      if(fgetc(file)==EOF && !ferror(file)){apps->count=count;apps->hasConfig=1;error=0;}
      break;
    }
    if(!line[0] || count>=LUNA_APP_LIMIT)break;
    char *elf=strchr(line,'\t');if(!elf)break;*elf++=0;
    char *argument=version==2?strchr(elf,'\t'):NULL;
    if(version==2 && !argument)break;
    if(argument)*argument++=0;
    if(strlen(line)>LUNA_APP_NAME || strlen(elf)>PATH_MAX ||
       !fieldValid(line,1) || !fieldValid(elf,1) ||
       (argument && (strlen(argument)>LUNA_APP_ARGUMENT || !fieldValid(argument,0))))break;
    LunaApp *app=&apps->items[count++];
    strcpy(app->name,line);strcpy(app->elf,elf);
    if(argument)strcpy(app->argument,argument);
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
  for(int i=0;i<apps->count;i++)if(strnlen(apps->items[i].name,sizeof(apps->items[i].name))>LUNA_APP_NAME ||
      strnlen(apps->items[i].elf,sizeof(apps->items[i].elf))>PATH_MAX ||
      strnlen(apps->items[i].argument,sizeof(apps->items[i].argument))>LUNA_APP_ARGUMENT ||
      !fieldValid(apps->items[i].name,1) || !fieldValid(apps->items[i].elf,1) ||
      !fieldValid(apps->items[i].argument,0))return -EINVAL;
  char path[PATH_MAX+16],stage[PATH_MAX+16];
  int error=configPath(path,sizeof(path),0);if(error)return error;
  error=configPath(stage,sizeof(stage),1);if(error)return error;
  FILE *file=fopen(stage,"wb");if(!file)return -EIO;
  int failed=fputs("LUNA_APPS_2\n",file)<0;
  for(int i=0;i<apps->count && !failed;i++) {
    const LunaApp *app=&apps->items[i];
    failed=fprintf(file,"%s\t%s\t%s\n",app->name,app->elf,app->argument)<0;
  }
  if(!failed)failed=fputs("END\n",file)<0;
  if(fclose(file))failed=1;
  if(failed){remove(stage);return -EIO;}
  return commitConfigFile(stage,path);
}
static char *appTrim(char *text) {
  while(isspace((unsigned char)*text))text++;
  size_t length=strlen(text);
  while(length && isspace((unsigned char)text[length-1]))text[--length]=0;
  return text;
}
static int readAppTitle(const char *directory,LunaApp *app) {
  char path[PATH_MAX+1],line[PATH_MAX+LUNA_APP_NAME+16],boot[PATH_MAX+1]="";
  int length=snprintf(path,sizeof(path),"%s/title.cfg",directory);
  if(length<0 || (size_t)length>=sizeof(path))return 0;
  FILE *file=fopen(path,"rb");if(!file)return 0;
  memset(app,0,sizeof(*app));int valid=1,first=1;
  while(fgets(line,sizeof(line),file)) {
    size_t bytes=strlen(line);
    if(!bytes || (line[bytes-1]!='\n' && !feof(file))){valid=0;break;}
    char *key=line;
    if(first && bytes>=3 && !memcmp(key,"\xef\xbb\xbf",3))key+=3;
    first=0;key=appTrim(key);
    if(!key[0] || key[0]=='#' || key[0]==';')continue;
    char *value=strchr(key,'=');if(!value)continue;*value++=0;
    key=appTrim(key);value=appTrim(value);
    char *out=NULL;size_t capacity=0;
    if(!strcmp(key,"title")){out=app->name;capacity=sizeof(app->name);}
    else if(!strcmp(key,"boot")){out=boot;capacity=sizeof(boot);}
    else if(!strcmp(key,"argv1")){out=app->argument;capacity=sizeof(app->argument);}
    if(out) {
      if(strlen(value)>=capacity || !fieldValid(value,0)){valid=0;break;}
      strcpy(out,value);
    }
  }
  if(ferror(file))valid=0;
  if(fclose(file))valid=0;
  // OPL boot paths are relative to their app folder. Do not escape it.
  if(!valid || !app->name[0] || !boot[0] || strchr(boot,':') ||
      boot[0]=='/' || strchr(boot,'\\'))return 0;
  for(const char *part=boot;part && *part;) {
    const char *slash=strchr(part,'/');size_t n=slash?(size_t)(slash-part):strlen(part);
    if(!n || (n==2 && !memcmp(part,"..",2)))return 0;
    part=slash?slash+1:NULL;
  }
  const char *ext=strrchr(boot,'.');if(!ext || strcasecmp(ext,".elf"))return 0;
  length=snprintf(path,sizeof(path),"%s/%s",directory,boot);
  if(length<0 || (size_t)length>=sizeof(path) ||
      lunaAppsStorePath(path,app->elf,sizeof(app->elf)))return 0;
  return 1;
}
int lunaAppsScan(LunaApps *apps,const char *directory,int memoryCard) {
  if(!directory || !*directory || apps->hasConfig || apps->count>=LUNA_APP_LIMIT)return 0;
  DIR *scan=opendir(directory);if(!scan)return 0;
  LunaApp *candidate=malloc(sizeof(*candidate));
  if(!candidate){closedir(scan);return -ENOMEM;}
  struct dirent *entry;int added=0;
  while(apps->count<LUNA_APP_LIMIT && (entry=readdir(scan))) {
    if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..") ||
        (memoryCard && !strchr(entry->d_name,'_')))continue;
    char path[PATH_MAX+1];
    int length=snprintf(path,sizeof(path),"%s%s%s",directory,
        directory[strlen(directory)-1]=='/'?"":"/",entry->d_name);
    if(length<0 || (size_t)length>=sizeof(path) || !readAppTitle(path,candidate))continue;
    int duplicate=0;
    for(int i=0;i<apps->count;i++)if(!strcasecmp(apps->items[i].elf,candidate->elf))duplicate=1;
    if(!duplicate){apps->items[apps->count++]=*candidate;added++;}
  }
  free(candidate);closedir(scan);return added;
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
