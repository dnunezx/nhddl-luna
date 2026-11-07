// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_COLLECTION_ART_H
#define LUNA_COLLECTION_ART_H

#include "ui/navigation.h"
#include <stddef.h>

#define COLLECTION_REUSE_COUNT 16
#define COLLECTION_REUSE_HISTORY_COUNT 12
#define COLLECTION_REUSE_BYTES (4U * 1024U * 1024U)
#define COLLECTION_PREFETCH_COUNT 16
#define COLLECTION_DEMAND_COUNT (PSBBN_COVER_CACHE_COUNT + COLLECTION_PREFETCH_COUNT)
#define COLLECTION_ART_PATH 255
#define COLLECTION_ENTRY_COVER_COUNT (PSBBN_COVER_CACHE_COUNT - PSBBN_COVER_CACHE_FOCUS)

typedef struct {
  void *source;
  void *preview;
  void *thumbnail;
  int width;
  int height;
  size_t bytes;
} CollectionArtPixels;

typedef struct {
  char path[255];
  CollectionArtPixels pixels;
  uint64_t lastUsed;
} CollectionArtReuseEntry;

typedef struct {
  CollectionArtReuseEntry entries[COLLECTION_REUSE_COUNT];
  size_t bytes;
  uint64_t clock;
  // UI-owned, nearest-first demand. Disabled outside visible Collections.
  char demand[COLLECTION_DEMAND_COUNT][COLLECTION_ART_PATH];
  uint8_t attempted[COLLECTION_DEMAND_COUNT];
  size_t requiredBytes[COLLECTION_DEMAND_COUNT];
  int demandCount;
  int forward;
} CollectionArtReuseCache;

// Successful puts/takes transfer ownership and clear the previous owner.
int collectionArtReusePut(CollectionArtReuseCache *cache, const char *path,
                          CollectionArtPixels *pixels);
int collectionArtReuseTake(CollectionArtReuseCache *cache, const char *path,
                           CollectionArtPixels *pixels);
void collectionArtReuseClear(CollectionArtReuseCache *cache);
// NULL restores the original history policy. Planning preserves attempts by path.
void collectionArtReusePlan(CollectionArtReuseCache *cache,
                            const char (*paths)[COLLECTION_ART_PATH], int count);
int collectionArtReuseRank(const CollectionArtReuseCache *cache, const char *path);
int collectionArtReuseContains(const CollectionArtReuseCache *cache, const char *path);
int collectionArtReuseCanPut(const CollectionArtReuseCache *cache, const char *path,
                             size_t bytes);
int collectionArtPrefetchAttempted(const CollectionArtReuseCache *cache, const char *path);
size_t collectionArtPrefetchBytes(const CollectionArtReuseCache *cache, const char *path);
void collectionArtPrefetchAttempt(CollectionArtReuseCache *cache, const char *path, int attempted);
// Alternating sides beyond the active window, direction first; -1 when exhausted.
int collectionArtPrefetchIndex(int total, int selected, int direction, int candidate);
// Preserve focal detail with UI headroom; other cover textures are evictable.
size_t collectionArtSourceBudget(size_t capacity);
// Returns 0 unchanged, 1 resized, or -1 if no resolution fits. Never upscales.
int collectionArtFitResolution(int *width, int *height, size_t budget,
                               size_t (*textureBytes)(int, int));
// Both workers use this order when visible/upcoming artwork needs attention.
void collectionArtPriority(uint8_t *priority, int direction, int fastScrolling,
                           int flowOffset);
// Required distinct games first, using the same nearest slots as the renderer.
// Fills all cache slots; returns the number required for current + six upcoming.
int collectionArtEntryPriority(uint8_t *priority, int total, int selectedTitleIdx);
int collectionArtEntryReady(int total, int selectedTitleIdx, uint16_t resolvedMask);

#endif
