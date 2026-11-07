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

int collectionArtReuseRank(const CollectionArtReuseCache *cache, const char *path) {
  if (path != NULL && path[0])
    for (int i = 0; i < cache->demandCount; i++)
      if (strcmp(cache->demand[i], path) == 0) return i;
  return -1;
}

int collectionArtReuseContains(const CollectionArtReuseCache *cache, const char *path) {
  if (path != NULL && path[0])
    for (int i = 0; i < COLLECTION_REUSE_COUNT; i++)
      if (strcmp(cache->entries[i].path, path) == 0) return 1;
  return 0;
}

void collectionArtReusePlan(CollectionArtReuseCache *cache,
                            const char (*paths)[COLLECTION_ART_PATH], int count) {
  uint8_t attempted[COLLECTION_DEMAND_COUNT] = {0};
  size_t requiredBytes[COLLECTION_DEMAND_COUNT] = {0};
  if (count < 0) count = 0;
  if (count > COLLECTION_DEMAND_COUNT) count = COLLECTION_DEMAND_COUNT;
  if (paths != NULL) {
    for (int i = 0; i < count; i++) {
      int previous = collectionArtReuseRank(cache, paths[i]);
      if (previous >= 0) {
        attempted[i] = cache->attempted[previous];
        requiredBytes[i] = cache->requiredBytes[previous];
      }
      for (int j = 0; j < COLLECTION_REUSE_COUNT; j++)
        if (cache->entries[j].path[0] && !strcmp(cache->entries[j].path, paths[i]))
          requiredBytes[i] = cache->entries[j].pixels.bytes;
    }
    memcpy(cache->demand, paths, (size_t)count * sizeof(cache->demand[0]));
    memcpy(cache->attempted, attempted, sizeof(attempted));
    memcpy(cache->requiredBytes, requiredBytes, sizeof(requiredBytes));
    cache->demandCount = count;
    cache->forward = 1;
    // Replanning changes priority, not ownership. Reclaim unused entries only
    // when a requested cover actually needs their space.
  } else {
    cache->demandCount = cache->forward = 0;
    memset(cache->attempted, 0, sizeof(cache->attempted));
    memset(cache->requiredBytes, 0, sizeof(cache->requiredBytes));
    // Preserve the original twelve-entry history behavior for other views.
    for (;;) {
      int used = 0, oldest = -1;
      for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
        if (!cache->entries[i].path[0]) continue;
        used++;
        if (oldest < 0 || cache->entries[i].lastUsed < cache->entries[oldest].lastUsed)
          oldest = i;
      }
      if (used <= COLLECTION_REUSE_HISTORY_COUNT) break;
      discardEntry(cache, oldest);
    }
  }
}

int collectionArtReuseCanPut(const CollectionArtReuseCache *cache, const char *path,
                             size_t bytes) {
  if (!path || !path[0] || bytes == 0 || bytes > COLLECTION_REUSE_BYTES) return 0;
  if (!cache->forward) return 1;
  int rank = collectionArtReuseRank(cache, path), nearer = 0;
  size_t retained = 0;
  if (rank < 0) return 0;
  for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
    const CollectionArtReuseEntry *entry = &cache->entries[i];
    if (!strcmp(entry->path, path)) continue;
    int other = collectionArtReuseRank(cache, entry->path);
    int retain = other >= 0 && other < rank;
    // Do not churn completed predictions for a one-position improvement.
    // Active covers always take precedence; speculative replacements must
    // advance beyond this small margin on the two-sided preparation window.
    if (rank >= PSBBN_COVER_CACHE_COUNT && other >= PSBBN_COVER_CACHE_COUNT &&
        (other - PSBBN_COVER_CACHE_COUNT) / 2 <=
            (rank - PSBBN_COVER_CACHE_COUNT) / 2 + 1)
      retain = 1;
    if (retain) {
      retained += entry->pixels.bytes;
      nearer++;
    }
  }
  return nearer < COLLECTION_REUSE_COUNT && retained <= COLLECTION_REUSE_BYTES - bytes;
}

int collectionArtPrefetchAttempted(const CollectionArtReuseCache *cache, const char *path) {
  int rank = collectionArtReuseRank(cache, path);
  return rank >= 0 && cache->attempted[rank];
}

size_t collectionArtPrefetchBytes(const CollectionArtReuseCache *cache, const char *path) {
  int rank = collectionArtReuseRank(cache, path);
  return rank >= 0 ? cache->requiredBytes[rank] : 0;
}

void collectionArtPrefetchAttempt(CollectionArtReuseCache *cache, const char *path, int attempted) {
  int rank = collectionArtReuseRank(cache, path);
  if (rank >= 0) cache->attempted[rank] = attempted != 0;
}

int collectionArtPrefetchIndex(int total, int selected, int direction, int candidate) {
  if (candidate < 0 || candidate >= COLLECTION_PREFETCH_COUNT ||
      total <= PSBBN_COVER_CACHE_COUNT || candidate >= total - PSBBN_COVER_CACHE_COUNT)
    return -1;
  int step = direction < 0 ? -1 : 1;
  if (candidate & 1) step = -step;
  int edge = step > 0 ? PSBBN_COVER_CACHE_COUNT - PSBBN_COVER_CACHE_FOCUS :
                       PSBBN_COVER_CACHE_FOCUS + 1;
  return lunaNavWrap(total, selected + step * (edge + candidate / 2));
}

static int reuseEvictionRank(const CollectionArtReuseCache *cache, const char *path) {
  int rank = collectionArtReuseRank(cache, path);
  return rank < 0 ? COLLECTION_DEMAND_COUNT : rank;
}

int collectionArtReusePut(CollectionArtReuseCache *cache, const char *path,
                          CollectionArtPixels *pixels) {
  int slot = -1;
  if (path == NULL || path[0] == '\0' || strlen(path) >= sizeof(cache->entries[0].path) ||
      pixels->source == NULL || pixels->preview == NULL || pixels->thumbnail == NULL ||
      pixels->bytes == 0 || pixels->bytes > COLLECTION_REUSE_BYTES)
    return 0;
  int rank = collectionArtReuseRank(cache, path);
  if (rank >= 0) cache->requiredBytes[rank] = pixels->bytes;
  if (!collectionArtReuseCanPut(cache, path, pixels->bytes)) return 0;
  for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
    CollectionArtReuseEntry *entry = &cache->entries[i];
    if (entry->path[0] && strcmp(entry->path, path) == 0)
      discardEntry(cache, i);
  }
  for (;;) {
    int oldest = -1, used = 0;
    slot = -1;
    for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
      CollectionArtReuseEntry *entry = &cache->entries[i];
      if (!entry->path[0]) {
        if (slot < 0) slot = i;
      } else {
        used++;
        if (oldest < 0 || (cache->forward ?
            (reuseEvictionRank(cache, entry->path) > reuseEvictionRank(cache, cache->entries[oldest].path) ||
             (reuseEvictionRank(cache, entry->path) == reuseEvictionRank(cache, cache->entries[oldest].path) &&
              entry->lastUsed < cache->entries[oldest].lastUsed)) :
            entry->lastUsed < cache->entries[oldest].lastUsed)) oldest = i;
      }
    }
    int limit = cache->forward ? COLLECTION_REUSE_COUNT : COLLECTION_REUSE_HISTORY_COUNT;
    if (slot >= 0 && used < limit && cache->bytes + pixels->bytes <= COLLECTION_REUSE_BYTES)
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
  cache->demandCount = cache->forward = 0;
  memset(cache->attempted, 0, sizeof(cache->attempted));
  memset(cache->requiredBytes, 0, sizeof(cache->requiredBytes));
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
