#include "ui/texture_budget.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int binds;

uint32_t gsKit_texture_size(int width, int height, int psm) {
  if (psm != GS_PSM_CT32 && psm != GS_PSM_T8)
    return UINT32_MAX;
  int block = psm == GS_PSM_T8 ? 16 : 8;
  int w = (width + block - 1) / block;
  int h = (height + block - 1) / block;
  int wa = 8, ha = 4;
  if (w <= 2 && h <= 1) wa = ha = 1;
  else if (w <= 4 && h <= 2) wa = ha = 2;
  else if (w <= 8 && h <= 4) wa = ha = 4;
  return ((w + wa - 1) / wa * wa) * ((h + ha - 1) / ha * ha) * 256U;
}

unsigned gsKit_TexManager_bind(GSGLOBAL *gs, GSTEXTURE *texture) {
  assert(gs && texture);
  binds++;
  return 0; // Already-resident uploads still count as successful safe binds.
}

static GSTEXTURE artwork(int width, int height) {
  GSTEXTURE texture = {.Width = width, .Height = height, .PSM = GS_PSM_CT32};
  texture.Mem = malloc(gsKit_texture_size(width, height, texture.PSM));
  assert(texture.Mem);
  for (int i = 0; i < width * height; i++) {
    unsigned char *p = (unsigned char *)texture.Mem + i * 4;
    p[0] = 17; p[1] = 39; p[2] = 201; p[3] = 64;
  }
  return texture;
}

int main(void) {
  GSGLOBAL gs = {4U * 1024U * 1024U - 512U * 1024U};
  GSTEXTURE texture = artwork(128, 128);
  void *original = texture.Mem;
  assert(fitTextureToVram(&gs, &texture) == 0 && texture.Mem == original);
  assert(bindTextureSafe(&gs, &texture) == 1 && binds == 1);
  free(texture.Mem);

  texture = artwork(1024, 512);
  assert(!bindTextureSafe(&gs, &texture) && binds == 1);
  assert(fitTextureToVram(&gs, &texture) == 1);
  assert(texture.Width <= 1024 && texture.Height <= 512);
  assert(abs(texture.Width - 2 * texture.Height) <= 1);
  assert(gsKit_texture_size(texture.Width, texture.Height, texture.PSM) <=
         texturePoolCapacity(&gs) - 65536U);
  for (int i = 0; i < texture.Width * texture.Height; i++) {
    unsigned char *p = (unsigned char *)texture.Mem + i * 4;
    assert(p[0] == 17 && p[1] == 39 && p[2] == 201 && p[3] == 64);
  }
  assert(bindTextureSafe(&gs, &texture) && binds == 2);
  free(texture.Mem);

  gs.CurrentPointer = 4U * 1024U * 1024U - 8192U;
  texture = artwork(1024, 1);
  assert(fitTextureToVram(&gs, &texture) == 1 && texture.Height == 1);
  assert(bindTextureSafe(&gs, &texture));
  free(texture.Mem);

  texture = artwork(1, 1);
  original = texture.Mem;
  gs.CurrentPointer = 4U * 1024U * 1024U;
  assert(fitTextureToVram(&gs, &texture) == -1 && texture.Mem == original);
  assert(!bindTextureSafe(&gs, &texture));
  gs.CurrentPointer++;
  assert(texturePoolCapacity(&gs) == 0 && !bindTextureSafe(&gs, &texture));
  assert(texturePoolCapacity(NULL) == 0 && !bindTextureSafe(NULL, &texture));

  // Pixel bytes alone fit, but palette/block padding must also be accounted for.
  gs.CurrentPointer = 4U * 1024U * 1024U - 256U;
  texture.Clut = original;
  texture.ClutPSM = GS_PSM_CT32;
  assert(!bindTextureSafe(&gs, &texture));
  texture.Clut = NULL;
  assert(bindTextureSafe(&gs, &texture));
  texture.PSM = 999;
  assert(!bindTextureSafe(&gs, &texture));
  free(texture.Mem);
  puts("Texture fit, alpha, padding, palette and exhausted-pool tests passed");
  return 0;
}
