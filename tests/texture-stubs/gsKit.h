#ifndef LUNA_TEST_GSKIT_H
#define LUNA_TEST_GSKIT_H
#include <stdint.h>
#define GS_PSM_CT32 0
#define GS_PSM_T8 19
typedef struct { uint32_t CurrentPointer; } GSGLOBAL;
typedef struct {
  int Width, Height, PSM, ClutPSM, TBW;
  uint32_t Vram;
  void *Mem, *Clut;
} GSTEXTURE;
uint32_t gsKit_texture_size(int width, int height, int psm);
unsigned gsKit_TexManager_bind(GSGLOBAL *gs, GSTEXTURE *texture);
#endif
