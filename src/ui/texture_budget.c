// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/texture_budget.h"
#include "ui/collection_art.h"
#include "dprintf.h"
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

size_t texturePoolCapacity(const GSGLOBAL *gs) {
  const size_t vramBytes = 4U * 1024U * 1024U;
  return gs != NULL && gs->CurrentPointer < vramBytes ?
      vramBytes - gs->CurrentPointer : 0;
}

static size_t rgbaBytes(int width, int height) {
  return gsKit_texture_size(width, height, GS_PSM_CT32);
}

int fitTextureToVram(const GSGLOBAL *gs, GSTEXTURE *texture) {
  if (texture == NULL || texture->Mem == NULL || texture->Clut != NULL ||
      texture->Vram != 0 || texture->PSM != GS_PSM_CT32 ||
      texture->Width <= 0 || texture->Height <= 0 ||
      texture->Width > 1024 || texture->Height > 1024)
    return -1;
  int width = texture->Width, height = texture->Height;
  const size_t budget = collectionArtSourceBudget(texturePoolCapacity(gs));
  int result = collectionArtFitResolution(&width, &height, budget, rgbaBytes);
  if (result <= 0)
    return result;
  const size_t bytes = rgbaBytes(width, height);
  uint8_t *scaled = memalign(128, bytes);
  if (scaled == NULL)
    return -1;
  memset(scaled, 0, bytes);
  const uint8_t *source = (const uint8_t *)texture->Mem;
  // Area averaging preserves detail and the already-normalized alpha channel.
  for (int y = 0; y < height; y++) {
    const int top = y * texture->Height / height;
    const int bottom = (y + 1) * texture->Height / height;
    for (int x = 0; x < width; x++) {
      const int left = x * texture->Width / width;
      const int right = (x + 1) * texture->Width / width;
      uint32_t sum[4] = {0};
      const unsigned samples = (bottom - top) * (right - left);
      for (int sy = top; sy < bottom; sy++)
        for (int sx = left; sx < right; sx++)
          for (int channel = 0; channel < 4; channel++)
            sum[channel] += source[((size_t)sy * texture->Width + sx) * 4 + channel];
      for (int channel = 0; channel < 4; channel++)
        scaled[((size_t)y * width + x) * 4 + channel] = sum[channel] / samples;
    }
  }
  DPRINTF("Artwork resized: %dx%d -> %dx%d (budget=%uKiB)\n",
          texture->Width, texture->Height, width, height, (unsigned)(budget / 1024));
  free(texture->Mem);
  texture->Mem = (void *)scaled;
  texture->Width = width;
  texture->Height = height;
  texture->TBW = 0;
  return 1;
}

int bindTextureSafe(GSGLOBAL *gs, GSTEXTURE *texture) {
  const size_t capacity = texturePoolCapacity(gs);
  if (texture == NULL || texture->Mem == NULL ||
      texture->Width <= 0 || texture->Height <= 0 ||
      texture->Width > 1024 || texture->Height > 1024)
    return 0;
  const size_t size = gsKit_texture_size(texture->Width, texture->Height, texture->PSM);
  const size_t clutSize = texture->Clut != NULL ?
      gsKit_texture_size(texture->PSM == GS_PSM_T8 ? 16 : 8,
                         texture->PSM == GS_PSM_T8 ? 16 : 2, texture->ClutPSM) : 0;
  // Subtract only after checking size to avoid overflow/underflow.
  if (size == 0 || size > capacity || clutSize > capacity - size) {
    DPRINTF("Artwork upload skipped: %dx%d exceeds texture pool (%uKiB)\n",
            texture->Width, texture->Height, (unsigned)(capacity / 1024));
    return 0;
  }
  gsKit_TexManager_bind(gs, texture);
  return 1;
}
