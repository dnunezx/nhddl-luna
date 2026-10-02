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

// Deliberate allocation padding: pixel bytes alone must not decide a fit.
static size_t paddedTextureBytes(int width, int height) {
  return (size_t)((width + 63) / 64) * 64 *
         ((height + 31) / 32) * 32 * 4;
}

static void testResolutionBudget(void) {
  const size_t preview = 128U * 128U * 4U;
  const size_t thumbnail = 64U * 64U * 4U;
  assert(collectionArtSourceBudget(64U * 1024U + 512U * 512U * 4U) ==
         512U * 512U * 4U);
  assert(collectionArtSourceBudget(0) == 0);
  assert(collectionArtSourceBudget(preview) == preview);
  assert(collectionArtSourceBudget(thumbnail) == thumbnail);
  assert(collectionArtSourceBudget(320U * 1024U) == 256U * 1024U);
  int width = 512, height = 512;
  assert(collectionArtFitResolution(&width, &height, 512U * 512U * 4U,
                                    paddedTextureBytes) == 0);
  assert(width == 512 && height == 512); // Preserve detail when it fits.
  width = height = 256;
  assert(collectionArtFitResolution(&width, &height, 512U * 512U * 4U,
                                    paddedTextureBytes) == 0);
  assert(width == 256 && height == 256); // Never upscale.
  width = height = 1024;
  assert(collectionArtFitResolution(&width, &height, 256U * 256U * 4U,
                                    paddedTextureBytes) == 1);
  assert(width == 256 && height == 256);
  width = 1024; height = 512;
  assert(collectionArtFitResolution(&width, &height, 256U * 128U * 4U,
                                    paddedTextureBytes) == 1);
  assert(width == 256 && height == 128);
  width = 512; height = 1024;
  assert(collectionArtFitResolution(&width, &height, 128U * 256U * 4U,
                                    paddedTextureBytes) == 1);
  assert(width == 128 && height == 256);
  width = height = 1024;
  assert(collectionArtFitResolution(&width, &height, 0,
                                    paddedTextureBytes) == -1);
  assert(width == 1024 && height == 1024); // Failure leaves ownership intact.
  width = 0; height = 256;
  assert(collectionArtFitResolution(&width, &height, preview,
                                    paddedTextureBytes) == -1);
  // Check capacity boundaries and the largest fitting size across aspect ratios.
  const int dimensions[][2] = {{1024, 1024}, {1024, 513}, {513, 1024},
                               {1, 1024}, {1024, 1}, {257, 129}};
  for (unsigned i = 0; i < sizeof(dimensions) / sizeof(dimensions[0]); i++) {
    int longest = dimensions[i][0] > dimensions[i][1] ? dimensions[i][0] : dimensions[i][1];
    for (size_t budget = 0; budget <= 512U * 1024U; budget += 4093) {
      width = dimensions[i][0]; height = dimensions[i][1];
      int result = collectionArtFitResolution(&width, &height, budget, paddedTextureBytes);
      if (result < 0) {
        assert(paddedTextureBytes(1, 1) > budget);
        continue;
      }
      assert(width >= 1 && height >= 1);
      assert(width <= dimensions[i][0] && height <= dimensions[i][1]);
      assert(paddedTextureBytes(width, height) <= budget);
      if (result == 0) continue;
      int next = (width > height ? width : height) + 1;
      assert(next <= longest);
      int nextWidth = (int)((int64_t)dimensions[i][0] * next / longest);
      int nextHeight = (int)((int64_t)dimensions[i][1] * next / longest);
      if (nextWidth < 1) nextWidth = 1;
      if (nextHeight < 1) nextHeight = 1;
      assert(paddedTextureBytes(nextWidth, nextHeight) > budget);
    }
  }
}

int main(void) {
  testOwnershipAndIdentity();
  testBudgetAndReplacement();
  testEntryLimitAndReversal();
  testPriority();
  testChurn();
  testResolutionBudget();
  puts("Collection artwork tests passed");
  return 0;
}
