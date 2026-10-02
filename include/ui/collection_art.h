// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_COLLECTION_ART_H
#define LUNA_COLLECTION_ART_H

#include "ui/navigation.h"
#include <stddef.h>

#define COLLECTION_REUSE_COUNT 12
#define COLLECTION_REUSE_BYTES (2U * 1024U * 1024U)

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
} CollectionArtReuseCache;

// Successful puts/takes transfer ownership and clear the previous owner.
int collectionArtReusePut(CollectionArtReuseCache *cache, const char *path,
                          CollectionArtPixels *pixels);
int collectionArtReuseTake(CollectionArtReuseCache *cache, const char *path,
                           CollectionArtPixels *pixels);
void collectionArtReuseClear(CollectionArtReuseCache *cache);
// Preserve focal detail with UI headroom; other cover textures are evictable.
size_t collectionArtSourceBudget(size_t capacity);
// Returns 0 unchanged, 1 resized, or -1 if no resolution fits. Never upscales.
int collectionArtFitResolution(int *width, int *height, size_t budget,
                               size_t (*textureBytes)(int, int));
// Both workers use this order when visible/upcoming artwork needs attention.
void collectionArtPriority(uint8_t *priority, int direction, int fastScrolling,
                           int flowOffset);

#endif
