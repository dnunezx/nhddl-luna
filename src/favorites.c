// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "favorites.h"
#include "common.h"
#include "devices/devices.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

static const char favoritesPath[] = "/favorites.txt";
static const char favoritesTempPath[] = "/favorites.txt.tmp";
static const char favoritesComplete[] = "# LUNA_FAVORITES_COMPLETE\n";
#define FAVORITE_SET_PATH_BYTES 64

typedef struct {
  const char *path;
  Target *target;
} FavoriteIndexEntry;

typedef struct {
  TargetList *titles;
  FavoriteIndexEntry *entries;
  size_t count;
  int attempted;
} FavoriteLookup;

static int favoriteFileMissing(const char *path) {
  // BDM reports raw FatFs missing-file/path results instead of POSIX ENOENT.
  // Probe the path so a read error cannot discard a complete recovery file.
  if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
    iox_stat_t info;
    int result = fileXioGetStat(path, &info);
    return result == -4 || result == -5;
  }
  struct stat info;
  return stat(path, &info) < 0 && errno == ENOENT;
}

static struct DeviceMapEntry *favoriteDevice(Target *target) {
  return target->device->metadev ? target->device->metadev : target->device;
}

static const char *relativeTargetPath(Target *target, char setPath[FAVORITE_SET_PATH_BYTES]) {
  // Synthetic identity is stable across starting-disc and playlist-name changes.
  if (target->discs && target->discs->key[0]) {
    snprintf(setPath, FAVORITE_SET_PATH_BYTES, "/@PS1SET/%s", target->discs->key);
    return setPath;
  }
  int relativeIndex = getRelativePathIdx(target->fullPath);
  return target->fullPath + ((relativeIndex < 0) ? 0 : relativeIndex);
}

static int compareFavoriteKey(const FavoriteIndexEntry *entry,
                               struct DeviceMapEntry *device, const char *path) {
  uintptr_t left = (uintptr_t)favoriteDevice(entry->target);
  uintptr_t right = (uintptr_t)device;
  if (left != right) return left < right ? -1 : 1;
  return strcmp(entry->path, path);
}

static int compareFavoriteEntries(const void *left, const void *right) {
  const FavoriteIndexEntry *a = left, *b = right;
  return compareFavoriteKey(a, favoriteDevice(b->target), b->path);
}

static void buildFavoriteLookup(FavoriteLookup *lookup) {
  size_t count = 0, setBytes = 0;
  for (Target *t = lookup->titles->first; t; t = t->next) {
    if (count == SIZE_MAX) return;
    count++;
    if (t->discs && t->discs->key[0]) {
      if (setBytes > SIZE_MAX - FAVORITE_SET_PATH_BYTES) return;
      setBytes += FAVORITE_SET_PATH_BYTES;
    }
  }
  if (!count || count > (SIZE_MAX - setBytes) / sizeof(*lookup->entries)) return;
  FavoriteIndexEntry *entries = malloc(count * sizeof(*entries) + setBytes);
  if (!entries) return; // Preserve the existing loader under memory pressure.
  char *setPath = (char *)(entries + count);
  size_t next = 0;
  for (Target *t = lookup->titles->first; t; t = t->next) {
    entries[next++] = (FavoriteIndexEntry){relativeTargetPath(t, setPath), t};
    if (t->discs && t->discs->key[0]) setPath += FAVORITE_SET_PATH_BYTES;
  }
  qsort(entries, count, sizeof(*entries), compareFavoriteEntries);
  lookup->entries = entries;
  lookup->count = count;
}

static void matchFavorite(FavoriteLookup *lookup, struct DeviceMapEntry *device,
                           const char *path, uint8_t *flags, size_t flagCount) {
  // Empty files need no index; failed allocations should not retry per record.
  if (!lookup->attempted) {
    lookup->attempted = 1;
    buildFavoriteLookup(lookup);
  }
  if (lookup->entries) {
    size_t low = 0, high = lookup->count;
    while (low < high) {
      size_t middle = low + (high - low) / 2;
      if (compareFavoriteKey(&lookup->entries[middle], device, path) < 0)
        low = middle + 1;
      else
        high = middle;
    }
    // Preserve all matches when multiple library entries share the same key.
    for (; low < lookup->count &&
           !compareFavoriteKey(&lookup->entries[low], device, path); ++low) {
      Target *target = lookup->entries[low].target;
      if (target->idx < flagCount) flags[target->idx] = 1;
    }
    return;
  }
  char setPath[FAVORITE_SET_PATH_BYTES];
  for (Target *target = lookup->titles->first; target; target = target->next)
    if (target->idx < flagCount && favoriteDevice(target) == device) {
      if (!strcmp(path, relativeTargetPath(target, setPath))) flags[target->idx] = 1;
    }
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

// Only a complete staged record may override the primary file. A save interrupted
// while writing its list must not hide the previous selection with a valid prefix.
static int readFavoriteFile(const char *path, int staged, FavoriteLookup *lookup,
                             struct DeviceMapEntry *device, uint8_t *flags,
                             size_t flagCount) {
  FILE *file = fopen(path, "r");
  if (!file)
    return favoriteFileMissing(path) ? -ENOENT : -EIO;
  char favorite[PATH_MAX + 2];
  int complete = 0, result = 0;
  while (fgets(favorite, sizeof(favorite), file)) {
    if (!strcmp(favorite, favoritesComplete)) {
      complete = 1;
      if (fgetc(file) != EOF)
        result = -EINVAL;
      break;
    }
    size_t length = strcspn(favorite, "\r\n");
    if (length == sizeof(favorite) - 1 ||
        (!strchr(favorite, '\n') && !feof(file))) {
      result = -EINVAL;
      break;
    }
    favorite[length] = '\0';
    if (!length || !lookup || !flags)
      continue;
    matchFavorite(lookup, device, favorite, flags, flagCount);
  }
  if (ferror(file))
    result = -EIO;
  if (fclose(file))
    result = -EIO;
  if (!result && staged && !complete)
    result = -EINVAL;
  return result;
}

int loadFavoriteFlags(TargetList *titles, uint8_t *flags, size_t flagCount) {
  int loaded = 0;

  if (titles == NULL || flags == NULL || flagCount < (size_t)titles->total)
    return -EINVAL;
  memset(flags, 0, flagCount);
  uint8_t *candidate = calloc(flagCount ? flagCount : 1, 1);
  if (!candidate)
    return -ENOMEM;

  FavoriteLookup lookup = {.titles = titles};
  // Metadata drives are bounded in production; retain the full-list fallback
  // for callers supplying more devices than the normal device map can hold.
  struct DeviceMapEntry *visited[MAX_DEVICES];
  size_t visitedCount = 0;
  for (Target *deviceTarget = titles->first; deviceTarget; deviceTarget = deviceTarget->next) {
    struct DeviceMapEntry *device = favoriteDevice(deviceTarget);
    char path[PATH_MAX];
    size_t i = 0;
    while (i < visitedCount && visited[i] != device) i++;
    if (i < visitedCount || (visitedCount == MAX_DEVICES &&
        deviceWasVisited(titles, deviceTarget, device))) continue;
    if (visitedCount < MAX_DEVICES) visited[visitedCount++] = device;
    const char *paths[] = {favoritesTempPath, favoritesPath};
    for (int attempt = 0; attempt < 2; attempt++) {
      memset(candidate, 0, flagCount);
      if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[attempt]) ||
          readFavoriteFile(path, attempt == 0, &lookup, device, candidate, flagCount))
        continue;
      for (size_t i = 0; i < flagCount; i++)
        if (candidate[i] && !flags[i]) { flags[i] = 1; loaded++; }
      break;
    }
  }
  free(lookup.entries);
  free(candidate);
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
    return -EIO;
  }
  // Do not truncate the only complete copy left by an earlier failed commit.
  int recoveryResult = readFavoriteFile(tempPath, 1, NULL, NULL, NULL, 0);
  if ((recoveryResult && recoveryResult != -ENOENT && recoveryResult != -EINVAL) ||
      (!recoveryResult && commitConfigFile(tempPath, path)))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL) {
    return -EIO;
  }

  char setPath[FAVORITE_SET_PATH_BYTES];
  for (target = titles->first; target != NULL; target = target->next) {
    if (target->idx < flagCount && flags[target->idx] && favoriteDevice(target) == device &&
        fprintf(file, "%s\n", relativeTargetPath(target, setPath)) < 0) {
      fclose(file);
      remove(tempPath);
      return -EIO;
    }
  }
  int completeResult = fputs(favoritesComplete, file);
  if (fclose(file) || completeResult == EOF) {
    remove(tempPath);
    return -EIO;
  }

  if (commitConfigFile(tempPath, path)) {
    return -EIO;
  }
  return 0;
}

// The source library owns games; a filtered view owns only its pointer array.
TargetList *buildFavoriteTargetList(TargetList *titles, const uint8_t *flags,
                                    size_t flagCount) {
  if (!titles || !flags || flagCount < (size_t)titles->total) return NULL;
  TargetList *favorites = calloc(1, sizeof(*favorites));
  if (!favorites) return NULL;
  favorites->borrowed = 1;
  favorites->indexCapacity = titles->total;
  if (titles->total) {
    favorites->byIndex = malloc((size_t)titles->total * sizeof(*favorites->byIndex));
    if (!favorites->byIndex) { freeTargetList(favorites); return NULL; }
  }
  for (Target *source = titles->first; source; source = source->next)
    if (flags[source->idx]) favorites->byIndex[favorites->total++] = source;
  favorites->first = favorites->total ? favorites->byIndex[0] : NULL;
  favorites->last = favorites->total ? favorites->byIndex[favorites->total - 1] : NULL;
  return favorites;
}

static int favoriteInsertionRank(const TargetList *favorites, int originalIndex) {
  int low = 0, high = favorites->total;
  while (low < high) {
    int middle = low + (high - low) / 2;
    if (favorites->byIndex[middle]->idx < originalIndex) low = middle + 1;
    else high = middle;
  }
  return low;
}

int setFavoriteTarget(TargetList *favorites, Target *source, int enabled) {
  if (!favorites || !favorites->borrowed || !source) return -EINVAL;
  int rank = favoriteInsertionRank(favorites, source->idx);
  int present = rank < favorites->total && favorites->byIndex[rank] == source;
  if (!!enabled == present) return 0;
  if (enabled) {
    if (favorites->total >= favorites->indexCapacity) return -ENOMEM;
    memmove(favorites->byIndex + rank + 1, favorites->byIndex + rank,
            (size_t)(favorites->total - rank) * sizeof(*favorites->byIndex));
    favorites->byIndex[rank] = source;
    favorites->total++;
  } else {
    memmove(favorites->byIndex + rank, favorites->byIndex + rank + 1,
            (size_t)(favorites->total - rank - 1) * sizeof(*favorites->byIndex));
    favorites->byIndex[--favorites->total] = NULL;
  }
  favorites->first = favorites->total ? favorites->byIndex[0] : NULL;
  favorites->last = favorites->total ? favorites->byIndex[favorites->total - 1] : NULL;
  return 0;
}
