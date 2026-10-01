// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/collection_art.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CollectionArtPixels artwork(size_t bytes) {
  CollectionArtPixels pixels = {malloc(32), malloc(16), malloc(8), 256, 256, bytes};
  assert(pixels.source && pixels.preview && pixels.thumbnail);
  return pixels;
}

static void discardPixels(CollectionArtPixels *pixels) {
  free(pixels->source);
  free(pixels->preview);
  free(pixels->thumbnail);
  memset(pixels, 0, sizeof(*pixels));
}

static void testOwnershipAndIdentity(void) {
  CollectionArtReuseCache cache = {0};
  CollectionArtPixels pixels = artwork(1024), restored = {0};
  void *original = pixels.source;
  assert(collectionArtReusePut(&cache, "mass0:/ART/PSBBN/A.png", &pixels));
  assert(!pixels.source && !pixels.preview && !pixels.thumbnail);
  assert(cache.bytes == 1024);
  assert(!collectionArtReuseTake(&cache, "mass1:/ART/PSBBN/A.png", &restored));
  assert(!restored.source && cache.bytes == 1024);
  // Collection and Favorites find the same physical artwork without a list index.
  assert(collectionArtReuseTake(&cache, "mass0:/ART/PSBBN/A.png", &restored));
  assert(restored.source == original && restored.width == 256 && restored.height == 256);
  assert(restored.preview && restored.thumbnail && cache.bytes == 0);
  assert(!collectionArtReuseTake(&cache, "mass0:/ART/PSBBN/A.png", &pixels));
  discardPixels(&restored);
  collectionArtReuseClear(&cache);
}

static void testBudgetAndReplacement(void) {
  CollectionArtReuseCache cache = {0};
  CollectionArtPixels pixels = artwork(COLLECTION_REUSE_BYTES / 2), restored = {0};
  assert(collectionArtReusePut(&cache, "A", &pixels));
  pixels = artwork(COLLECTION_REUSE_BYTES / 2);
  assert(collectionArtReusePut(&cache, "B", &pixels));
  assert(cache.bytes == COLLECTION_REUSE_BYTES);
  // Updating A makes B the least recently retained entry.
  pixels = artwork(COLLECTION_REUSE_BYTES / 2);
  assert(collectionArtReusePut(&cache, "A", &pixels));
  pixels = artwork(COLLECTION_REUSE_BYTES / 2);
  assert(collectionArtReusePut(&cache, "C", &pixels));
  assert(cache.bytes == COLLECTION_REUSE_BYTES);
  assert(!collectionArtReuseTake(&cache, "B", &restored));
  assert(collectionArtReuseTake(&cache, "A", &restored));
  discardPixels(&restored);
  pixels = artwork(COLLECTION_REUSE_BYTES + 1U);
  assert(!collectionArtReusePut(&cache, "oversize", &pixels));
  assert(pixels.source && cache.bytes == COLLECTION_REUSE_BYTES / 2);
  discardPixels(&pixels);
  collectionArtReuseClear(&cache);
  assert(cache.bytes == 0);
  collectionArtReuseClear(&cache);
}

static void testEntryLimitAndReversal(void) {
  CollectionArtReuseCache cache = {0};
  CollectionArtPixels pixels, restored = {0};
  char path[32];
  for (int i = 0; i <= COLLECTION_REUSE_COUNT; i++) {
    snprintf(path, sizeof(path), "cover%d", i);
    pixels = artwork(1024);
    assert(collectionArtReusePut(&cache, path, &pixels));
  }
  assert(cache.bytes == COLLECTION_REUSE_COUNT * 1024);
  assert(!collectionArtReuseTake(&cache, "cover0", &restored));
  // Walking backward reuses the recent stream without allocating new pixels.
  for (int i = COLLECTION_REUSE_COUNT; i > 0; i--) {
    snprintf(path, sizeof(path), "cover%d", i);
    assert(collectionArtReuseTake(&cache, path, &restored));
    discardPixels(&restored);
  }
  assert(cache.bytes == 0);
  collectionArtReuseClear(&cache);
}

static void testPriority(void) {
  uint8_t priority[PSBBN_COVER_CACHE_COUNT];
  const uint8_t forward[] = {3, 4, 5, 6, 7, 8, 9, 2, 1, 0};
  const uint8_t reverse[] = {3, 2, 1, 0, 4, 5, 6, 7, 8, 9};
  collectionArtPriority(priority, 1, 1, 0);
  assert(memcmp(priority, forward, sizeof(forward)) == 0);
  collectionArtPriority(priority, -1, 1, 0);
  assert(memcmp(priority, reverse, sizeof(reverse)) == 0);
  collectionArtPriority(priority, 1, 1, 1000);
  assert(priority[0] == 2 && priority[1] == 3);
  collectionArtPriority(priority, -1, 1, -1000);
  assert(priority[0] == 4 && priority[1] == 3);
  // Interrupted glides and offsets outside the window must still schedule each slot once.
  for (int offset = -12000; offset <= 12000; offset += 125) {
    for (int direction = -1; direction <= 1; direction++) {
      for (int fast = 0; fast <= 1; fast++) {
        unsigned seen = 0;
        collectionArtPriority(priority, direction, fast, offset);
        for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
          assert(priority[i] < PSBBN_COVER_CACHE_COUNT);
          assert(!(seen & (1U << priority[i])));
          seen |= 1U << priority[i];
        }
        assert(seen == (1U << PSBBN_COVER_CACHE_COUNT) - 1);
      }
    }
  }
}

static void testChurn(void) {
  CollectionArtReuseCache cache = {0};
  char path[32];
  for (int step = 0; step < 2000; step++) {
    int id = (step < 1000 ? step : 2000 - step) % 40;
    CollectionArtPixels pixels = {0};
    snprintf(path, sizeof(path), "mass0:/cover%d", id);
    if (collectionArtReuseTake(&cache, path, &pixels)) {
      assert(*(int *)pixels.source == id);
    } else {
      pixels = artwork(336 * 1024);
      *(int *)pixels.source = id;
    }
    assert(collectionArtReusePut(&cache, path, &pixels));
    size_t accounted = 0;
    for (int i = 0; i < COLLECTION_REUSE_COUNT; i++) {
      accounted += cache.entries[i].pixels.bytes;
      for (int j = i + 1; j < COLLECTION_REUSE_COUNT; j++)
        assert(!cache.entries[i].path[0] || !cache.entries[j].path[0] ||
               strcmp(cache.entries[i].path, cache.entries[j].path) != 0);
    }
    assert(accounted == cache.bytes && cache.bytes <= COLLECTION_REUSE_BYTES);
  }
  collectionArtReuseClear(&cache);
  assert(cache.bytes == 0);
}

int main(void) {
  testOwnershipAndIdentity();
  testBudgetAndReplacement();
  testEntryLimitAndReversal();
  testPriority();
  testChurn();
  puts("Collection artwork tests passed");
  return 0;
}
