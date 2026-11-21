#ifndef LUNA_APPS_H
#define LUNA_APPS_H
#include <limits.h>
#include <stddef.h>
#define LUNA_APP_LIMIT 128
#define LUNA_APP_NAME 95
#define LUNA_APP_ARGUMENT 128
typedef struct {
  char name[LUNA_APP_NAME+1],elf[PATH_MAX+1],argument[LUNA_APP_ARGUMENT+1];
} LunaApp;
typedef struct { int count,hasConfig; LunaApp items[LUNA_APP_LIMIT]; } LunaApps;
void lunaAppsConfigure(const char *directory);
int lunaAppsLoad(LunaApps *apps);
int lunaAppsSave(const LunaApps *apps);
// Discover OPL title.cfg files only for a first-run list; never modify them.
int lunaAppsScan(LunaApps *apps,const char *directory,int memoryCard);
int lunaAppsAdd(LunaApps *apps,const char *path,const char *name);
int lunaAppsResolve(const char *path,char *result,size_t size);
int lunaAppsStorePath(const char *path,char *result,size_t size);
void lunaAppSuggestName(const char *path,char *name,size_t size);
#endif
