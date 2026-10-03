// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/collection_art.h"
#include <stdlib.h>
#include <string.h>

size_t collectionArtSourceBudget(size_t capacity) {
  const size_t headroom = 64U * 1024U;
  // gsKit can evict and re-upload the moving partner and distant covers.
  // Dividing this pool across both focal covers would reduce selected detail.
  return capacity > headroom ? capacity - headroom : capacity;
}

int collectionArtFitResolution(int *width, int *height, size_t budget,
                               size_t (*textureBytes)(int, int)) {
  int originalWidth = *width, originalHeight = *height;
  if (originalWidth <= 0 || originalHeight <= 0)
    return -1;
  if (textureBytes(originalWidth, originalHeight) <= budget)
    return 0;
  int longest = originalWidth > originalHeight ? originalWidth : originalHeight;
  int low = 1, high = longest - 1, bestWidth = 0, bestHeight = 0;
  while (low <= high) {
    int size = low + (high - low) / 2;
    int w = (int)((int64_t)originalWidth * size / longest);
    int h = (int)((int64_t)originalHeight * size / longest);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (textureBytes(w, h) <= budget) {
      bestWidth = w;
      bestHeight = h;
      low = size + 1;
    } else {
      high = size - 1;
    }
  }
  if (bestWidth == 0)
    return -1;
  *width = bestWidth;
  *height = bestHeight;
  return 1;
}

static void discardEntry(CollectionArtReuseCache *cache, int index) {
  CollectionArtReuseEntry *entry = &cache->entries[index];
  cache->bytes -= entry->pixels.bytes;
  free(entry->pixels.source);
  free(entry->pixels.preview);
  free(entry->pixels.thumbnail);
  memset(entry, 0, sizeof(*entry));
}

int collectionArtReusePut(CollectionArtReuseCache *cache, const char *path,
                          CollectionArtPixels *pixels) {
  int slot = -1;
  if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(cache->entries[0].path) ||
      pixels->source == NULL || pixels->preview == NULL || pixels->thumbnail == NULL ||
      pixels->bytes == 0 || pixels->bytes > COLLECTION_REUSE_BYTES)
    return 0;
  for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
    CollectionArtReuseEntry *entry = &cache->entries[i];
    if (entry->path[0] && strcmp(entry->path, path) == 0)
      discardEntry(cache, i);
  }
  for (;;) {
    int oldest = -1;
    slot = -1;
    for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
      CollectionArtReuseEntry *entry = &cache->entries[i];
      if (!entry->path[0]) {
        if (slot < 0) slot = i;
      } else if (oldest < 0 || entry->lastUsed < cache->entries[oldest].lastUsed) {
        oldest = i;
      }
    }
    if (slot >= 0 && cache->bytes + pixels->bytes <= COLLECTION_REUSE_BYTES)
      break;
    discardEntry(cache, oldest);
  }
  CollectionArtReuseEntry *entry = &cache->entries[slot];
  strcpy(entry->path, path);
  entry->pixels = *pixels;
  entry->lastUsed = ++cache->clock;
  cache->bytes += pixels->bytes;
  memset(pixels, 0, sizeof(*pixels));
  return 1;
}

int collectionArtReuseTake(CollectionArtReuseCache *cache, const char *path,
                           CollectionArtPixels *pixels) {
  for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
    CollectionArtReuseEntry *entry = &cache->entries[i];
    if (entry->path[0] && strcmp(entry->path, path) == 0) {
      *pixels = entry->pixels;
      cache->bytes -= pixels->bytes;
      memset(entry, 0, sizeof(*entry));
      return 1;
    }
  }
  return 0;
}

void collectionArtReuseClear(CollectionArtReuseCache *cache) {
  for (int i = 0; i < COLLECTION_REUSE_COUNT; i++)
    discardEntry(cache, i);
  cache->clock = 0;
}

int collectionArtEntryPriority(uint8_t *priority, int total, int selectedTitleIdx) {
  uint16_t used = 0;
  int count = 0;
  for (int ahead = 0; total > 0 && ahead < COLLECTION_ENTRY_COVER_COUNT; ahead++) {
    int target = lunaNavWrap(total, selectedTitleIdx + ahead);
    int closest = -1, closestDistance = PSBBN_COVER_CACHE_COUNT;
    for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
      int offset = i - PSBBN_COVER_CACHE_FOCUS;
      int distance = offset < 0 ? -offset : offset;
      if (lunaNavWrap(total, selectedTitleIdx + offset) == target && distance < closestDistance) {
        closest = i;
        closestDistance = distance;
      }
    }
    if (closest >= 0 && !(used & (1U << closest))) {
      priority[count++] = (uint8_t)closest;
      used |= 1U << closest;
    }
  }
  int required = count;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++)
    if (!(used & (1U << i))) priority[count++] = (uint8_t)i;
  return required;
}

int collectionArtEntryReady(int total, int selectedTitleIdx, uint16_t resolvedMask) {
  uint8_t priority[PSBBN_COVER_CACHE_COUNT];
  int required = collectionArtEntryPriority(priority, total, selectedTitleIdx);
  for (int p = 0; p < required; p++)
    if (!(resolvedMask & (1U << priority[p]))) return 0;
  return 1;
}

void collectionArtPriority(uint8_t *priority, int direction, int fastScrolling,
                           int flowOffset) {
  int focus = PSBBN_COVER_CACHE_FOCUS;
  int closestDistance = 0x7fffffff;
  int count = 0;
  int step = direction < 0 ? -1 : 1;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    int position = (i - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    if (distance < closestDistance) {
      focus = i;
      closestDistance = distance;
    }
  }
  priority[count++] = (uint8_t)focus;
  for (int distance = 1; distance < PSBBN_COVER_CACHE_COUNT; distance++) {
    int ahead = focus + step * distance;
    int behind = focus - step * distance;
    if (ahead >= 0 && ahead < PSBBN_COVER_CACHE_COUNT)
      priority[count++] = (uint8_t)ahead;
    if (!fastScrolling && behind >= 0 && behind < PSBBN_COVER_CACHE_COUNT)
      priority[count++] = (uint8_t)behind;
  }
  if (fastScrolling) {
    for (int distance = 1; distance < PSBBN_COVER_CACHE_COUNT; distance++) {
      int behind = focus - step * distance;
      if (behind >= 0 && behind < PSBBN_COVER_CACHE_COUNT)
        priority[count++] = (uint8_t)behind;
    }
  }
}
