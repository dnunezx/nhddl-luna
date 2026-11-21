#ifndef LUNA_APPS_H
#define LUNA_APPS_H
#include <limits.h>
#include <stddef.h>
#define LUNA_APP_LIMIT 128
#define LUNA_APP_NAME 95
typedef struct {
  char name[LUNA_APP_NAME+1],elf[PATH_MAX+1];
} LunaApp;
typedef struct { int count; LunaApp items[LUNA_APP_LIMIT]; } LunaApps;
void lunaAppsConfigure(const char *directory);
int lunaAppsLoad(LunaApps *apps);
int lunaAppsSave(const LunaApps *apps);
int lunaAppsAdd(LunaApps *apps,const char *path,const char *name);
int lunaAppsResolve(const char *path,char *result,size_t size);
int lunaAppsStorePath(const char *path,char *result,size_t size);
void lunaAppSuggestName(const char *path,char *name,size_t size);
#endif
