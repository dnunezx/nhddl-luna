// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "genres.h"
#include "devices/devices.h"
#include "options.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

static const char *const defaults[LUNA_GENRE_DEFAULTS] = {
  "Action", "Adventure", "RPG", "Racing", "Fighting", "Sports",
  "Shooters", "Platformers", "Puzzle", "Strategy", "Rhythm", "Horror"};
static const char signature[] = "LUNA_GENRES_1\n";
static TargetList *library;
static uint32_t *masks;
static uint8_t *readable;

typedef struct Record {
  struct Record *next;
  char name[LUNA_GENRE_NAME_BYTES];
  char key[];
} Record;
typedef struct { Target *target; char *key; } Lookup;

static struct DeviceMapEntry *genreDevice(const Target *target) {
  return target->device->metadev ? target->device->metadev : target->device;
}
static const char *genreKey(const Target *target, char key[64]) {
  if (target->discs && target->discs->key[0]) {
    snprintf(key,64,"/@PS1SET/%s",target->discs->key);
    return key;
  }
  int offset=getRelativePathIdx(target->fullPath);
  return target->fullPath+(offset<0 ? 0 : offset);
}
static int missing(const char *path) {
  iox_stat_t info;
  int result=fileXioGetStat(path,&info);
  if (result==-ENOENT) return 1;
  return !strncmp(path,"mass",4) && result<0 && (result==-4 || result==-5);
}
static uint32_t checksum(uint32_t hash,const char *data,size_t bytes) {
  while(bytes--) hash=(hash^(unsigned char)*data++)*16777619U;
  return hash;
}
static void freeRecords(Record *record) {
  while(record) { Record *next=record->next; free(record); record=next; }
}
static int validName(const char *name) {
  size_t length=strlen(name);
  if(!length || length>=LUNA_GENRE_NAME_BYTES || name[0]==' ' || name[length-1]==' ')return 0;
  for(size_t i=0;i<length;i++)if((unsigned char)name[i]<32 || name[i]=='\t')return 0;
  return 1;
}
// Validate the complete file and checksum before publishing any records.
static int readRecords(const char *path,Record **out) {
  *out=NULL;
  FILE *file=fopen(path,"rb");
  if(!file)return missing(path)?-ENOENT:-EIO;
  if(fseek(file,0,SEEK_END)) { fclose(file);return -EIO; }
  long length=ftell(file);
  if(length<(long)sizeof(signature)-1 || length>8*1024*1024 || fseek(file,0,SEEK_SET)) {
    fclose(file);return -EINVAL;
  }
  char *buffer=malloc((size_t)length+1);
  if(!buffer) { fclose(file);return -ENOMEM; }
  int bad=fread(buffer,1,(size_t)length,file)!=(size_t)length || ferror(file);
  if(fclose(file))bad=1;
  if(bad) { free(buffer);return -EIO; }
  buffer[length]=0;
  if(memchr(buffer,0,(size_t)length) || memcmp(buffer,signature,sizeof(signature)-1)) {
    free(buffer);return -EINVAL;
  }
  char *footer=buffer+length-1;
  if(*footer!='\n') { free(buffer);return -EINVAL; }
  while(footer>buffer && footer[-1]!='\n')footer--;
  unsigned expected; int consumed=0;
  if(sscanf(footer,"#END %8x\n%n",&expected,&consumed)!=1 ||
      footer+consumed!=buffer+length || checksum(2166136261U,buffer,footer-buffer)!=expected) {
    free(buffer);return -EINVAL;
  }
  *footer=0;
  Record *head=NULL,**tail=&head;
  char *line=buffer+sizeof(signature)-1;
  int result=0,count=0;
  while(*line) {
    char *end=strchr(line,'\n'),*tab=strchr(line,'\t');
    if(!end || !tab || tab>end || ++count>65536) { result=-EINVAL;break; }
    *tab=0;*end=0;
    if(!validName(line) || !tab[1] || strlen(tab+1)>PATH_MAX || strchr(tab+1,'\t') || strchr(tab+1,'\r')) {
      result=-EINVAL;break;
    }
    Record *record=malloc(sizeof(*record)+strlen(tab+1)+1);
    if(!record) { result=-ENOMEM;break; }
    record->next=NULL;strcpy(record->name,line);strcpy(record->key,tab+1);
    *tail=record;tail=&record->next;line=end+1;
  }
  free(buffer);
  if(result)freeRecords(head);else *out=head;
  return result;
}
static int paths(struct DeviceMapEntry *device,char primary[PATH_MAX],char staged[PATH_MAX]) {
  return buildConfigFilePath(primary,PATH_MAX,device->mountpoint,"/genres.txt") ||
         buildConfigFilePath(staged,PATH_MAX,device->mountpoint,"/genres.txt.tmp");
}
static int loadRecords(struct DeviceMapEntry *device,Record **out) {
  char primary[PATH_MAX],staged[PATH_MAX];
  if(paths(device,primary,staged))return -ENAMETOOLONG;
  int stage=readRecords(staged,out);
  if(!stage)return 0;
  if(stage!=-ENOENT && stage!=-EINVAL)return stage;
  int result=readRecords(primary,out);
  if(result==-ENOENT && stage==-ENOENT)return 0;
  return result;
}
static int findGenre(const char *name) {
  for(int i=0;i<LUNA_GENRE_DEFAULTS;i++)if(!strcasecmp(defaults[i],name))return i;
  // Preserve explicit assignments saved with the earlier display names.
  if(!strcasecmp(name,"Shooter"))return 6;
  if(!strcasecmp(name,"Platform"))return 7;
  if(!strcasecmp(name,"Rhythm Action") || !strcasecmp(name,"Rhythm & Music"))return 10;
  return -1;
}
int lunaGenreCount(void) { return LUNA_GENRE_DEFAULTS; }
const char *lunaGenreName(int genre) {
  return genre==LUNA_GENRE_UNASSIGNED ? "Unassigned" :
         genre>=0 && genre<LUNA_GENRE_DEFAULTS ? defaults[genre] : "All Games";
}
void lunaGenresClose(void) {
  free(masks);free(readable);masks=NULL;readable=NULL;library=NULL;
}
static int compareLookup(const void *left,const void *right) {
  const Lookup *a=left,*b=right;
  uintptr_t ad=(uintptr_t)genreDevice(a->target),bd=(uintptr_t)genreDevice(b->target);
  if(ad!=bd)return ad<bd?-1:1;
  return strcmp(a->key,b->key);
}
int lunaGenresLoad(TargetList *titles) {
  lunaGenresClose();
  if(!titles || titles->total<0)return -EINVAL;
  library=titles;
  masks=calloc(titles->total ? titles->total : 1,sizeof(*masks));
  readable=calloc(titles->total ? titles->total : 1,1);
  Lookup *lookup=calloc(titles->total ? titles->total : 1,sizeof(*lookup));
  if(!masks || !readable || !lookup) {
    free(lookup);free(masks);free(readable);masks=NULL;readable=NULL;return -ENOMEM;
  }
  int count=0,result=0;
  for(Target *t=titles->first;t;t=t->next) {
    if(count>=titles->total) { result=-EINVAL;break; }
    char key[64]; lookup[count].target=t;lookup[count].key=strdup(genreKey(t,key));
    if(!lookup[count].key) { result=-ENOMEM;break; }count++;
  }
  if(result) {free(masks);free(readable);masks=NULL;readable=NULL;goto done;}
  qsort(lookup,count,sizeof(*lookup),compareLookup);
  for(int first=0;first<count;) {
    struct DeviceMapEntry *device=genreDevice(lookup[first].target);
    int last=first+1;
    while(last<count && genreDevice(lookup[last].target)==device)last++;
    Record *records=NULL;
    int error=loadRecords(device,&records);
    if(error) { if(!result)result=error;first=last;continue; }
    for(Record *r=records;r;r=r->next) {
      int genre=findGenre(r->name);
      if(genre<0)continue; // Unsupported saved names never add browser categories.
      int lo=first,hi=last;
      while(lo<hi) { int mid=lo+(hi-lo)/2;if(strcmp(lookup[mid].key,r->key)<0)lo=mid+1;else hi=mid; }
      for(;lo<last && !strcmp(lookup[lo].key,r->key);lo++)
        if(lookup[lo].target->idx<(unsigned)titles->total)masks[lookup[lo].target->idx]|=1U<<genre;
    }
    if(!error)for(int i=first;i<last;i++)
      if(lookup[i].target->idx<(unsigned)titles->total)readable[lookup[i].target->idx]=1;
    freeRecords(records);first=last;
  }
done:
  for(int i=0;i<count;i++)free(lookup[i].key);
  free(lookup);return result;
}
uint32_t lunaGenreMask(const Target *target) {
  return masks && library && target && target->idx<(unsigned)library->total ? masks[target->idx] : 0;
}
int lunaGenreGameCount(int genre) {
  int count=0;
  if(genre<0 || genre>=LUNA_GENRE_DEFAULTS || !masks || !library)return 0;
  for(int i=0;i<library->total;i++)if(masks[i]&(1U<<genre))count++;
  return count;
}
static int writePart(FILE *file,const char *text,uint32_t *hash) {
  size_t length=strlen(text);
  *hash=checksum(*hash,text,length);
  return fwrite(text,1,length,file)==length?0:-EIO;
}
int lunaGenreSet(Target *target,uint32_t mask) {
  if(!masks || !library || !target || target->idx>=(unsigned)library->total)return -EINVAL;
  if(!readable || !readable[target->idx])return -EIO;
  if(mask>>LUNA_GENRE_DEFAULTS)return -EINVAL;
  struct DeviceMapEntry *device=genreDevice(target);
  Record *records=NULL;
  int result=loadRecords(device,&records);
  if(result)return result; // Never overwrite an unreadable genre database.
  char directory[PATH_MAX],primary[PATH_MAX],staged[PATH_MAX],keyBuffer[64];
  const char *key=genreKey(target,keyBuffer);
  if(strpbrk(key,"\t\r\n") || strlen(key)>PATH_MAX || paths(device,primary,staged) ||
      buildConfigFilePath(directory,sizeof(directory),device->mountpoint,NULL)) {
    freeRecords(records);return -EINVAL;
  }
  struct stat info;
  if(stat(directory,&info) && mkdir(directory,0777)) { freeRecords(records);return -EIO; }
  // Preserve a surviving complete staged database before reusing its name.
  Record *recovery=NULL;
  if(!readRecords(staged,&recovery)) {
    freeRecords(recovery);
    if(commitConfigFile(staged,primary)) { freeRecords(records);return -EIO; }
  }
  FILE *file=fopen(staged,"wb");
  if(!file) { freeRecords(records);return -EIO; }
  uint32_t hash=2166136261U;
  result=writePart(file,signature,&hash);
  char line[PATH_MAX+LUNA_GENRE_NAME_BYTES+4];
  for(Record *r=records;!result && r;r=r->next)if(strcmp(r->key,key) || findGenre(r->name)<0) {
    snprintf(line,sizeof(line),"%s\t%s\n",r->name,r->key);
    result=writePart(file,line,&hash);
  }
  for(int genre=0;!result && genre<LUNA_GENRE_DEFAULTS;genre++)if(mask&(1U<<genre)) {
    snprintf(line,sizeof(line),"%s\t%s\n",defaults[genre],key);
    result=writePart(file,line,&hash);
  }
  if(!result && fprintf(file,"#END %08x\n",(unsigned)hash)<0)result=-EIO;
  if(fflush(file))result=-EIO;
  if(fclose(file))result=-EIO;
  freeRecords(records);
  if(result) { remove(staged);return result; }
  result=readRecords(staged,&recovery);freeRecords(recovery);
  if(result)return result;
  result=commitConfigFile(staged,primary);
  if(!result)for(Target *t=library->first;t;t=t->next) {
    char other[64];
    if(genreDevice(t)==device && !strcmp(genreKey(t,other),key))masks[t->idx]=mask;
  }
  return result;
}
int lunaGenreFilter(TargetList *view,const TargetList *source,TargetFilter platform,
                    const uint8_t *favorites,int genre) {
  int result=filterTargetView(view,source,platform,favorites);
  if(result || genre==-1)return result;
  if(genre!=LUNA_GENRE_UNASSIGNED && (genre<0 || genre>=LUNA_GENRE_DEFAULTS))return -EINVAL;
  int count=0;
  for(int i=0;i<view->total;i++) {
    uint32_t mask=lunaGenreMask(view->byIndex[i]);
    if(genre==LUNA_GENRE_UNASSIGNED ? !mask : (mask&(1U<<genre))!=0)
      view->byIndex[count++]=view->byIndex[i];
  }
  view->total=count;view->first=count?view->byIndex[0]:NULL;view->last=count?view->byIndex[count-1]:NULL;
  return 0;
}
