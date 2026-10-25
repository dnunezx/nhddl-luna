// Implements titleScanFunc for file-based devices (MMCE, BDM)
// LUNA 2026
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/ui.h"
#include "options.h"
#include "devices/title_id.h"
#ifdef LUNA_ENABLE_PSXCORE
#include "luna_psxcore_library.h"
#include "luna_psxcore_discset.h"
#include "psxcore_vcd.h"
#endif
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  char titleID[12];
  char *fullPath;
} CacheEntry;

typedef struct TitleIDCache {
  int total;           // Total number of elements in cache
  CacheEntry *entries; // Pointer to cache entry array
} TitleIDCache;

int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device,
             const char *path);
int processTitleID(TargetList *result, struct DeviceMapEntry *device, int completeScan);

int storeTitleIDCache(TargetList *list, struct DeviceMapEntry *device);
int loadTitleIDCache(TitleIDCache *cache, struct DeviceMapEntry *device);
char *getCachedTitleID(char *fullPath, TitleIDCache *cache);
void freeTitleCache(TitleIDCache *cache);

// Directories to skip when browsing for ISOs
const char *ignoredDirs[] = {
    "nhddl", "neutrino", "APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "XEBPLUS", "MemoryCards", "bbnl",
};

// Used by _findISO to limit recursion depth
#define MAX_SCAN_DEPTH 6
static int curRecursionLevel = 1;

// Scans given storage device and appends valid launch candidates to TargetList
// Keep usable games when a folder or image cannot be read.
int findISO(TargetList *result, struct DeviceMapEntry *device) {
  DIR *directory;

  if (device->mode == MODE_NONE || device->mountpoint == NULL)
    return -ENODEV;

  curRecursionLevel = 1; // Reset recursion level
  directory = opendir(device->mountpoint);
  // Check if the directory can be opened
  if (directory == NULL) {
    uiSplashLogString(LEVEL_ERROR, "ERROR: Can't open %s\n", device->mountpoint);
    return -EIO;
  }

  int scanResult = _findISO(directory, result, device, device->mountpoint);
  // PS2SDK can return the closed descriptor on success; only negatives fail.
  if (closedir(directory) < 0 && !scanResult)
    scanResult = TITLE_SCAN_PARTIAL;
  if (scanResult < 0)
    return scanResult;

  if (result->total == 0) {
    return scanResult ? scanResult : -ENOENT;
  }

  // Get title IDs for each found title
  int metadataResult = processTitleID(result, device, scanResult == 0);
  if (metadataResult < 0) return metadataResult;
  if (metadataResult == TITLE_SCAN_PARTIAL) scanResult = TITLE_SCAN_PARTIAL;
#ifdef LUNA_ENABLE_PSXCORE
  metadataResult = lunaPsxGroupTargets(result);
  if (metadataResult) return metadataResult;
#endif

  if (result->first == NULL) {
    return scanResult ? scanResult : -ENOENT;
  }

  return scanResult;
}

// Searches rootpath and adds discovered ISOs to TargetList
int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device,
             const char *path) {
  if (directory == NULL)
    return -EIO;

  // Read directory entries
  struct dirent *entry;
  char *fileext;
  char titlePath[PATH_MAX + 1];
  int cwdLen = snprintf(titlePath, sizeof(titlePath), "%s", path);
  if (cwdLen <= 0 || (size_t)cwdLen >= sizeof(titlePath))
    return -ENAMETOOLONG;
  if (titlePath[cwdLen - 1] != '/') { // Add path separator if cwd doesn't have one
    if ((size_t)cwdLen >= sizeof(titlePath) - 1)
      return -ENAMETOOLONG;
    strcat(titlePath, "/");
    cwdLen++;
  }

  curRecursionLevel++;
  if (curRecursionLevel == MAX_SCAN_DEPTH)
    DPRINTF("Max recursion limit reached, all directories in %s will be ignored\n", titlePath);

  int scanResult = 0;
  while (1) {
    errno = 0;
    entry = readdir(directory);
    if (!entry) {
      if (errno) scanResult = TITLE_SCAN_PARTIAL;
      break;
    }
    // Reset titlePath by ending string on base path
    titlePath[cwdLen] = '\0';
    if (strlen(entry->d_name) > sizeof(titlePath)-1-(size_t)cwdLen) {
      scanResult = TITLE_SCAN_PARTIAL;
      continue;
    }

    // Ignore .files and directories
    if (entry->d_name[0] == '.')
      continue;

    // Some network/removable drivers do not provide d_type.
    int type = entry->d_type;
    if (type == DT_UNKNOWN) {
      struct stat info;
      snprintf(titlePath + cwdLen, sizeof(titlePath) - cwdLen, "%s", entry->d_name);
      if (stat(titlePath, &info) < 0) { scanResult = TITLE_SCAN_PARTIAL; continue; }
      type = S_ISDIR(info.st_mode) ? DT_DIR : DT_REG;
      titlePath[cwdLen] = '\0';
    }
    switch (type) {
    case DT_DIR:
      // Ignore directories if max scan depth is reached
      if (curRecursionLevel == MAX_SCAN_DEPTH)
        continue;

      // Ignore special and invalid directories (non-ASCII paths seem to return '?' and cause crashes when used with opendir)
      if ((entry->d_name[0] == '$') || (entry->d_name[0] == '?'))
        continue;

      for (int i = 0; i < sizeof(ignoredDirs) / sizeof(char *); i++) {
        if (!strcmp(ignoredDirs[i], entry->d_name))
          goto next;
      }

      // Recurse using full paths, independently of the launcher's working directory.
      strcat(titlePath, entry->d_name);
      DIR *d = opendir(titlePath);
      if (d == NULL) {
        DPRINTF("Failed to open %s for scanning\n", entry->d_name);
        scanResult = TITLE_SCAN_PARTIAL;
        continue;
      }
      // Process inner directory recursively
      int childResult = _findISO(d, result, device, titlePath);
      if (closedir(d) < 0 && childResult >= 0)
        childResult = TITLE_SCAN_PARTIAL;
      if (childResult == -ENOMEM) {
        scanResult = childResult;
        goto done;
      }
      if (childResult) scanResult = TITLE_SCAN_PARTIAL;

    next:
      break;
    default:
      // Make sure file has .iso extension
      fileext = strrchr(entry->d_name, '.');
      int isVcd = 0;
      int isPlaylist = 0;
#ifdef LUNA_ENABLE_PSXCORE
      int ps1Device = (device->mode == MODE_ATA && device->index == 0) ||
                      (device->mode == MODE_USB && device->index <= 1);
      isVcd = ps1Device && np_vcd_filename(entry->d_name);
      isPlaylist = ps1Device && lunaPsxPlaylistFilename(entry->d_name);
#endif
      if (isVcd || isPlaylist || (fileext && !strcasecmp(fileext, ".iso"))) {
        // Generate full path
        strcat(titlePath, entry->d_name);

        // Initialize target
        Target *title = calloc(sizeof(Target), 1);
        if (!title) { scanResult = -ENOMEM; goto done; }
        title->prev = NULL;
        title->next = NULL;
        title->fullPath = strdup(titlePath);
        title->device = device;
        title->platform = isVcd || isPlaylist ? TARGET_PS1 : TARGET_PS2;
        if (isPlaylist) title->discs = calloc(1, sizeof(*title->discs));

        // Get file name without the extension
        int nameLength = (int)(fileext - entry->d_name);
        title->name = calloc(sizeof(char), nameLength + 1);
        if (!title->fullPath || !title->name || (isPlaylist && !title->discs)) {
          free(title->fullPath); free(title->name); free(title->discs); free(title);
          scanResult = -ENOMEM; goto done;
        }
        strncpy(title->name, entry->d_name, nameLength);

        appendTarget(result, title);
      }
    }
  }
done:
  curRecursionLevel--;

  return scanResult;
}

// Fills in title ID for every entry in the list
int processTitleID(TargetList *result, struct DeviceMapEntry *device, int completeScan) {
  if (result->total == 0)
    return 0;

  // Load title cache
  TitleIDCache *cache = calloc(1, sizeof(TitleIDCache));
  if (!cache) return -ENOMEM;
  int ps2Count = 0;
  for (Target *t=result->first;t;t=t->next)
    if (t->device==device && t->platform==TARGET_PS2) ++ps2Count;
  int isCacheUpdateNeeded = 0;
  int cacheResult = loadTitleIDCache(cache, device);
  if (cacheResult == -ENOMEM) { freeTitleCache(cache); return cacheResult; }
  if (cacheResult) {
    uiSplashLogString(LEVEL_WARN, "Failed to load title ID cache, all ISOs will be rescanned\n");
    free(cache);
    cache = NULL;
  } else if (cache->total != ps2Count) {
    // Set flag if number of entries is different
    isCacheUpdateNeeded = 1;
  }

  // For every entry in target list, try to get title ID from cache
  // If cache doesn't have title ID for the path,
  // get it from ISO
  int cacheMisses = 0, scanResult = 0;
  char *titleID = NULL;
  Target *curTarget = result->first;
  while (curTarget != NULL) {
    // Ignore targets not belonging to the current device
    if (curTarget->device != device) {
      curTarget = curTarget->next;
      continue;
    }
#ifdef LUNA_ENABLE_PSXCORE
    if (curTarget->platform == TARGET_PS1) {
      if (curTarget->discs) { curTarget=curTarget->next; continue; }
      char id[12];
      const char *error = lunaPsxDiscInfo(curTarget->fullPath,id);
      if (error && (!strcmp(error, "disc-open") || !strcmp(error, "disc-read"))) {
        scanResult = TITLE_SCAN_PARTIAL;
      }
      if (error) {
        DPRINTF("PS1 inventory rejected: %s (%s)\n",curTarget->fullPath,error?error:"memory");
        curTarget=freeTarget(result,curTarget); continue;
      }
      curTarget->id = strdup(id);
      if (!curTarget->id) { freeTitleCache(cache); return -ENOMEM; }
      DPRINTF("PS1 library disc: %s id=%s\n",curTarget->fullPath,curTarget->id);
      curTarget=curTarget->next; continue;
    }
#endif

    // Try to get title ID from cache
    if (cache != NULL) {
      titleID = getCachedTitleID(curTarget->fullPath, cache);
    }

    if (titleID != NULL) {
      curTarget->id = strdup(titleID);
      if (!curTarget->id) { freeTitleCache(cache); return -ENOMEM; }
    } else { // Get title ID from ISO
      cacheMisses++;
      DPRINTF("Cache miss for %s\n", curTarget->fullPath);
      curTarget->id = getTitleID(curTarget->fullPath);
      if (curTarget->id == NULL) {
        // A bad image must not hide other readable games on the device.
        int error = errno;
        if (error == ENOMEM) {
          freeTitleCache(cache);
          return -ENOMEM;
        }
        if (error != EINVAL) scanResult = TITLE_SCAN_PARTIAL;
        uiSplashLogString(LEVEL_WARN, "Failed to scan\n%s\n", curTarget->fullPath);
        curTarget = freeTarget(result, curTarget);
        continue;
      }
    }

    curTarget = curTarget->next;
  }
  freeTitleCache(cache);

  // Never replace a complete cache with an incomplete scan.
  if (completeScan && !scanResult && ((cacheMisses > 0) || isCacheUpdateNeeded)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "Updating title ID cache...\n");
    if (storeTitleIDCache(result, device)) {
      uiSplashLogString(LEVEL_WARN, "Failed to save title ID cache\n");
    }
  }
  return scanResult;
}

//
// Title cache
//

#define CACHE_MAGIC "NIDC"
#define CACHE_VERSION 2

const char titleIDCacheFile[] = "/cache.bin";
static const char titleIDCacheTempFile[] = "/cache.bin.tmp";

// Structs used to read and write cache file contents
typedef struct {
  char titleID[12];
  size_t pathLength; // Includes null-terminator
} CacheEntryHeader;

typedef struct {
  char magic[4];   // Must be always equal to CACHE_MAGIC
  uint8_t version; // Cache version
  int total;       // Total number of elements in cache file
} CacheMetadata;

// Saves TargetList into title ID cache on given storage device
int storeTitleIDCache(TargetList *list, struct DeviceMapEntry *device) {
  if (list->total == 0) {
    return 0;
  }

  // Get total number of valid cache entries
  int total = 0;
  Target *curTitle = list->first;
  while (curTitle != NULL) {
    if (curTitle->device == device && curTitle->platform == TARGET_PS2 &&
        curTitle->id && strlen(curTitle->id) == 11) {
      total++;
    }
    curTitle = curTitle->next;
  }
  if (total == 0) {
    DPRINTF("WARN: No valid cache entries found\n");
    return 0;
  }

  // Make sure path exists
  if (device->mode == MODE_NONE || device->mountpoint == NULL)
    return -ENODEV;

  // Prepare paths and header
  char cachePath[PATH_MAX];
  char tempPath[PATH_MAX];
  char dirPath[PATH_MAX];
  CacheEntryHeader header = {0};
  CacheMetadata meta = {.magic = CACHE_MAGIC, .version = CACHE_VERSION, .total = total};

  if (buildConfigFilePath(dirPath, sizeof(dirPath), device->mountpoint, NULL) ||
      buildConfigFilePath(cachePath, sizeof(cachePath), device->mountpoint, titleIDCacheFile) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, titleIDCacheTempFile))
    return -ENAMETOOLONG;

  // Get path to config directory and make sure it exists
  struct stat st;
  if (stat(dirPath, &st) == -1) {
    DPRINTF("Creating config directory: %s\n", dirPath);
    if (mkdir(dirPath, 0777)) {
      DPRINTF("ERROR: Failed to create directory\n");
      return -EIO;
    }
  }

  // Open cache file for writing
  FILE *file = fopen(tempPath, "wb");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open cache file for writing\n");
    return -EIO;
  }

  int result;
  // Write cache file header
  result = fwrite(&meta, sizeof(CacheMetadata), 1, file);
  if (!result) {
    DPRINTF("ERROR: Failed to write metadata: %d\n", errno);
    fclose(file);
    remove(tempPath);
    return -EIO;
  }

  // Write each entry
  curTitle = list->first;
  int mountpointLen = -1;
  while (curTitle != NULL) {
    // Ignore empty entries or entries not belonging to the current device
    if (!curTitle->id || (strlen(curTitle->id) < 11) || (curTitle->device != device) ||
        curTitle->platform != TARGET_PS2) {
      curTitle = curTitle->next;
      continue;
    }

    // Compare paths without the mountpoint
    mountpointLen = getRelativePathIdx(curTitle->fullPath);
    if (mountpointLen == -1) {
      DPRINTF("WARN: Failed to get device mountpoint for %s\n", curTitle->name);
      curTitle = curTitle->next;
      continue;
    }

    // Write entry header
    memcpy(header.titleID, curTitle->id, sizeof(header.titleID));
    header.titleID[11] = '\0';
    header.pathLength = strlen(curTitle->fullPath) - mountpointLen + 1;
    result = fwrite(&header, sizeof(CacheEntryHeader), 1, file);
    if (!result) {
      DPRINTF("ERROR: %s: Failed to write header: %d\n", curTitle->name, errno);
      fclose(file);
      remove(tempPath);
      return -EIO;
    }
    // Write full ISO path without the mountpoint
    result = fwrite(curTitle->fullPath + mountpointLen, header.pathLength, 1, file);
    if (!result) {
      DPRINTF("ERROR: %s: Failed to write full path: %d\n", curTitle->name, errno);
      fclose(file);
      remove(tempPath);
      return -EIO;
    }
    curTitle = curTitle->next;
  }
  int failed = fflush(file) != 0;
  if (fclose(file)) failed = 1;
  if (failed) {
    remove(tempPath);
    return -EIO;
  }

  remove(cachePath);
  if (rename(tempPath, cachePath)) {
    DPRINTF("ERROR: Failed to commit title ID cache: %d\n", errno);
    return -EIO;
  }

  return 0;
}

// Loads title ID cache from storage into cache
static int compareCachePaths(const void *left, const void *right) {
  const CacheEntry *a = left, *b = right;
  return strcmp(a->fullPath, b->fullPath);
}

int loadTitleIDCache(TitleIDCache *cache, struct DeviceMapEntry *device) {
  cache->total = 0;
  cache->entries = NULL;
  // Make sure path exists
  if (device->mode == MODE_NONE || device->mountpoint == NULL)
    return -ENODEV;

  // Open cache file for reading
  char cachePath[PATH_MAX];
  if (buildConfigFilePath(cachePath, sizeof(cachePath), device->mountpoint, titleIDCacheFile))
    return -ENAMETOOLONG;

  FILE *file = fopen(cachePath, "rb");
  if (file == NULL) {
    if (!buildConfigFilePath(cachePath, sizeof(cachePath), device->mountpoint,
                             titleIDCacheTempFile))
      file = fopen(cachePath, "rb");
  }
  if (file == NULL) {
    if (!buildLegacyConfigFilePath(cachePath, sizeof(cachePath), device->mountpoint,
                                   titleIDCacheFile))
      file = fopen(cachePath, "rb");
  }
  if (file == NULL)
    return -ENOENT;

  int result = -EINVAL;
  int readIndex = 0;
  CacheEntry *entries = NULL;

  // Read cache file header
  CacheMetadata meta;
  if (fread(&meta, sizeof(CacheMetadata), 1, file) != 1) {
    DPRINTF("ERROR: Failed to read cache metadata\n");
    result = -EIO;
    goto fail;
  }

  // Make sure header is valid
  if (memcmp(meta.magic, CACHE_MAGIC, sizeof(meta.magic)) != 0) {
    DPRINTF("ERROR: Cache magic doesn't match, refusing to load\n");
    goto fail;
  }
  if (meta.version != CACHE_VERSION) {
    DPRINTF("ERROR: Unsupported cache version %d\n", meta.version);
    goto fail;
  }

  long entriesStart = ftell(file);
  if (meta.total < 0 || fseek(file, 0, SEEK_END) != 0) {
    goto fail;
  }
  long fileSize = ftell(file);
  if (entriesStart < 0 || fileSize < entriesStart ||
      meta.total > (fileSize - entriesStart) / (long)(sizeof(CacheEntryHeader) + 2) ||
      (size_t)meta.total > SIZE_MAX / sizeof(CacheEntry) ||
      fseek(file, entriesStart, SEEK_SET) != 0) {
    DPRINTF("ERROR: Invalid cache entry count %d\n", meta.total);
    goto fail;
  }

  // Allocate memory for cache entries based on total entry count from header metadata
  if (meta.total > 0)
    entries = malloc(sizeof(CacheEntry) * (size_t)meta.total);
  if (meta.total > 0 && entries == NULL) {
    DPRINTF("ERROR: Can't allocate enough memory\n");
    result = -ENOMEM;
    goto fail;
  }

  // Read each entry into cache
  CacheEntryHeader header;
  char pathBuf[PATH_MAX + 1];
  while (readIndex < meta.total) {
    // Read cache entry header
    pathBuf[0] = '\0';
    if (fread(&header, sizeof(CacheEntryHeader), 1, file) != 1) {
      DPRINTF("WARN: Incomplete title ID cache header\n");
      result = -EIO;
      goto fail;
    }
    if (header.pathLength < 2 || header.pathLength > PATH_MAX ||
        header.titleID[11] != '\0' || memchr(header.titleID, '\0', 11)) {
      DPRINTF("WARN: Invalid cached path length or title ID\n");
      goto fail;
    }
    // Read ISO path
    if (fread(pathBuf, header.pathLength, 1, file) != 1) {
      DPRINTF("WARN: Read less than expected, title ID cache might be incomplete\n");
      result = -EIO;
      goto fail;
    }
    if (pathBuf[header.pathLength - 1] != '\0' ||
        memchr(pathBuf, '\0', header.pathLength - 1)) {
      DPRINTF("WARN: Invalid cached path terminator\n");
      goto fail;
    }

    // Store entry in cache
    CacheEntry entry;
    memcpy(entry.titleID, header.titleID, sizeof(entry.titleID));
    entry.titleID[11] = '\0';
    entry.fullPath = strdup(pathBuf);
    if (!entry.fullPath) {
      result = -ENOMEM;
      goto fail;
    }
    entries[readIndex] = entry;
    readIndex++;
  }
  if (fclose(file)) {
    file = NULL;
    result = -EIO;
    goto fail;
  }

  cache->entries = entries;
  cache->total = readIndex;
  // Directory discovery order may differ from both old and new cache files.
  if (cache->total > 1)
    qsort(cache->entries, (size_t)cache->total, sizeof(*cache->entries), compareCachePaths);
  return 0;
fail:
  if (file)
    fclose(file);
  for (int i = 0; i < readIndex; i++)
    free(entries[i].fullPath);
  free(entries);
  return result;
}

// Returns a pointer to title ID or NULL if fullPath is not found in the cache
char *getCachedTitleID(char *fullPath, TitleIDCache *cache) {
  int mountpointLen = getRelativePathIdx(fullPath);
  if (mountpointLen == -1) {
    DPRINTF("WARN: Failed to get device mountpoint for %s\n", fullPath);
    return NULL;
  }

  int low = 0, high = cache->total;
  while (low < high) {
    int middle = low + (high - low) / 2;
    int difference = strcmp(cache->entries[middle].fullPath, fullPath + mountpointLen);
    if (!difference) return cache->entries[middle].titleID;
    if (difference < 0) low = middle + 1;
    else high = middle;
  }
  return NULL;
}

// Frees memory used by title ID cache
// All pointers to cache entries (including title IDs) will be invalid
void freeTitleCache(TitleIDCache *cache) {
  if (cache == NULL)
    return;

  for (int i = 0; i < cache->total; i++) {
    free(cache->entries[i].fullPath);
  }

  free(cache->entries);
  free(cache);
}
