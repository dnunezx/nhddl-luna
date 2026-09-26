// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "favorites.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char favoritesPath[] = "/favorites.txt";
static const char favoritesTempPath[] = "/favorites.txt.tmp";

static struct DeviceMapEntry *favoriteDevice(Target *target) {
  return target->device->metadev ? target->device->metadev : target->device;
}

static const char *relativeTargetPath(Target *target) {
  int relativeIndex = getRelativePathIdx(target->fullPath);
  return target->fullPath + ((relativeIndex < 0) ? 0 : relativeIndex);
}

static int deviceWasVisited(TargetList *titles, Target *stop, struct DeviceMapEntry *device) {
  Target *target = titles->first;
  while (target != NULL && target != stop) {
    if (favoriteDevice(target) == device)
      return 1;
    target = target->next;
  }
  return 0;
}

int loadFavoriteFlags(TargetList *titles, uint8_t *flags, size_t flagCount) {
  Target *deviceTarget;
  int loaded = 0;

  if (titles == NULL || flags == NULL || flagCount < (size_t)titles->total)
    return -EINVAL;
  memset(flags, 0, flagCount);

  for (deviceTarget = titles->first; deviceTarget != NULL; deviceTarget = deviceTarget->next) {
    struct DeviceMapEntry *device = favoriteDevice(deviceTarget);
    char path[PATH_MAX];
    char favorite[PATH_MAX + 2];
    FILE *file;

    if (deviceWasVisited(titles, deviceTarget, device) ||
        buildConfigFilePath(path, sizeof(path), device->mountpoint, favoritesPath))
      continue;
    file = fopen(path, "r");
    if (file == NULL)
      continue;

    while (fgets(favorite, sizeof(favorite), file) != NULL) {
      Target *target;
      size_t length = strcspn(favorite, "\r\n");
      favorite[length] = '\0';
      if (length == 0)
        continue;
      for (target = titles->first; target != NULL; target = target->next) {
        if (target->idx < flagCount && favoriteDevice(target) == device &&
            !strcmp(favorite, relativeTargetPath(target))) {
          flags[target->idx] = 1;
          loaded++;
        }
      }
    }
    fclose(file);
  }
  return loaded;
}

int saveFavoriteFlags(TargetList *titles, const uint8_t *flags, size_t flagCount,
                      Target *selectedTitle) {
  struct DeviceMapEntry *device;
  Target *target;
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  FILE *file;
  struct stat st;

  if (titles == NULL || flags == NULL || selectedTitle == NULL ||
      flagCount < (size_t)titles->total)
    return -EINVAL;
  device = favoriteDevice(selectedTitle);
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, favoritesPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, favoritesTempPath))
    return -ENAMETOOLONG;

  if (stat(directory, &st) == -1 && mkdir(directory, 0777)) {
    DPRINTF("ERROR: Failed to create favorites directory: %d\n", errno);
    return -EIO;
  }
  file = fopen(tempPath, "w");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open favorites file: %d\n", errno);
    return -EIO;
  }

  for (target = titles->first; target != NULL; target = target->next) {
    if (target->idx < flagCount && flags[target->idx] && favoriteDevice(target) == device &&
        fprintf(file, "%s\n", relativeTargetPath(target)) < 0) {
      fclose(file);
      remove(tempPath);
      return -EIO;
    }
  }
  if (fclose(file)) {
    remove(tempPath);
    return -EIO;
  }

  remove(path);
  if (rename(tempPath, path)) {
    DPRINTF("ERROR: Failed to commit favorites file: %d\n", errno);
    return -EIO;
  }
  return 0;
}

TargetList *buildFavoriteTargetList(TargetList *titles, const uint8_t *flags,
                                    size_t flagCount) {
  TargetList *favorites;
  Target *source;

  if (titles == NULL || flags == NULL || flagCount < (size_t)titles->total)
    return NULL;
  favorites = calloc(1, sizeof(*favorites));
  if (favorites == NULL)
    return NULL;

  for (source = titles->first; source != NULL; source = source->next) {
    Target *copy;
    if (!flags[source->idx])
      continue;
    copy = copyTarget(source);
    if (copy == NULL) {
      freeTargetList(favorites);
      return NULL;
    }
    copy->idx = favorites->total++;
    copy->prev = favorites->last;
    copy->next = NULL;
    if (favorites->last != NULL)
      favorites->last->next = copy;
    else
      favorites->first = copy;
    favorites->last = copy;
  }
  return favorites;
}
