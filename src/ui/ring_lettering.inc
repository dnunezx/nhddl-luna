/* Dedicated title-case English lettering, independent of shared UI fonts. */
#include "ui/ring_lettering_data.h"
typedef char RingLetteringCountCheck[(RING_ITEM_COUNT==RING_LETTERING_COUNT)?1:-1];
/* Keep the atlas indexed: RGBA exceeds the texture pool after the screen,
 * background work pages and shared UI font have reserved their VRAM. */
static unsigned char ringLetteringPixels[sizeof(ringLetteringAlpha4)] __attribute__((aligned(128)));
static u32 ringLetteringClut[64] __attribute__((aligned(128)));
static GSTEXTURE ringLetteringTexture;
static void ringLetteringInit(void) {
  memset(&ringLetteringTexture,0,sizeof(ringLetteringTexture));
  ringLetteringTexture.Width=RING_LETTERING_WIDTH;
  ringLetteringTexture.Height=RING_LETTERING_HEIGHT;
  ringLetteringTexture.PSM=GS_PSM_T4;ringLetteringTexture.Mem=(void *)ringLetteringPixels;
  ringLetteringTexture.Clut=ringLetteringClut;
  ringLetteringTexture.ClutPSM=GS_PSM_CT32;
  ringLetteringTexture.ClutStorageMode=GS_CLUT_STORAGE_CSM1;
  ringLetteringTexture.Filter=GS_FILTER_LINEAR;ringLetteringTexture.Delayed=GS_SETTING_ON;
  for(int i=0;i<16;i++)
    ringLetteringClut[i]=0x00ffffffU|((u32)(i*128/15)<<24);
  /* Packed artwork stores the first pixel in the high nibble; GS PSMT4
   * transfers expect it in the low nibble. Keep every coverage level. */
  for(unsigned int i=0;i<sizeof(ringLetteringPixels);i++) {
    unsigned int packed=ringLetteringAlpha4[i];
    ringLetteringPixels[i]=(unsigned char)((packed<<4)|(packed>>4));
  }
}
static void ringLetteringClose(void) {
  /* ringEffectsClose has already drained the frame and texture transfers. */
  gsKit_TexManager_free(gsGlobal,&ringLetteringTexture);
  memset(&ringLetteringTexture,0,sizeof(ringLetteringTexture));
}
static void ringTitle(int item,float cx,float y,float width,float height,uint64_t color,const char *fallback) {
  if(item<0 || item>=RING_LETTERING_COUNT || !bindTextureSafe(gsGlobal,&ringLetteringTexture)) {
    ringText(cx,y,width,height,color,fallback);return;
  }
  float left=roundf(ringSX(cx-width*.5f)),right=roundf(ringSX(cx+width*.5f));
  float top=roundf(ringSY(y)),bottom=roundf(ringSY(y+height));
  float v=item*RING_LETTERING_ROW;
  gsKit_prim_sprite_texture(gsGlobal,&ringLetteringTexture,
      left,top,.5f,v+.5f,right,bottom,RING_LETTERING_WIDTH-.5f,v+RING_LETTERING_ROW-.5f,4,color);
}
