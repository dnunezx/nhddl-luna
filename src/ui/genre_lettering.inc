/* Genre headings share the Start menu's typeface, with separate texture ownership. */
#include "ui/genre_lettering_data.h"
#include <dmaKit.h>
typedef char GenreLetteringCountCheck[(LUNA_GENRE_DEFAULTS==GENRE_LETTERING_COUNT)?1:-1];
static GSTEXTURE genreLetteringTextures[GENRE_LETTERING_COUNT];
static u32 genreLetteringClut[64] __attribute__((aligned(128)));
static int genreLetteringReady;

static void genreLetteringOpen(void) {
  if(genreLetteringReady || lunaLanguage()!=LUNA_LANGUAGE_ENGLISH)return;
  for(int i=0;i<16;i++)genreLetteringClut[i]=0x00ffffffU|((u32)(i*128/15)<<24);
  for(int i=0;i<GENRE_LETTERING_COUNT;i++) {
    GSTEXTURE *texture=&genreLetteringTextures[i];
    memset(texture,0,sizeof(*texture));
    texture->Width=GENRE_LETTERING_WIDTH;texture->Height=GENRE_LETTERING_ROW;
    /* Each aligned row is already packed in GS nibble order. */
    texture->PSM=GS_PSM_T4;
    texture->Mem=(u32 *)(genreLetteringAlpha4+i*GENRE_LETTERING_ROW_BYTES);
    texture->Clut=genreLetteringClut;texture->ClutPSM=GS_PSM_CT32;
    texture->ClutStorageMode=GS_CLUT_STORAGE_CSM1;
    texture->Filter=GS_FILTER_LINEAR;texture->Delayed=GS_SETTING_ON;
  }
  genreLetteringReady=1;
}
static void genreLetteringClose(void) {
  if(!genreLetteringReady)return;
  gsKit_queue_exec(gsGlobal);gsKit_finish();dmaKit_wait_fast();
  for(int i=0;i<GENRE_LETTERING_COUNT;i++)
    gsKit_TexManager_free(gsGlobal,&genreLetteringTextures[i]);
  memset(genreLetteringTextures,0,sizeof(genreLetteringTextures));
  genreLetteringReady=0;
}
static int genreLetteringDraw(int genre,float cx,float cy,float size,uint64_t color) {
  if(!genreLetteringReady || lunaLanguage()!=LUNA_LANGUAGE_ENGLISH ||
     genre<0 || genre>=GENRE_LETTERING_COUNT)return 0;
  GSTEXTURE *texture=&genreLetteringTextures[genre];
  float sx=gsGlobal->Width/640.0f,sy=gsGlobal->Height/480.0f;
  float unit=size/GENRE_LETTERING_INK_HEIGHT;
  float width=GENRE_LETTERING_WIDTH*unit*sx,height=GENRE_LETTERING_ROW*unit*sy;
  float left=roundf(cx*sx-width*.5f),right=roundf(cx*sx+width*.5f);
  float top=roundf(cy*sy-height*.5f),bottom=roundf(cy*sy+height*.5f);
  if(right<=0 || left>=gsGlobal->Width || bottom<=0 || top>=gsGlobal->Height)return 1;
  if(!bindTextureSafe(gsGlobal,texture))return 0;
  GSTEST savedTest=*gsGlobal->Test;
  uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  gsGlobal->Test->ATST=6;gsGlobal->Test->AREF=0;gsGlobal->Test->AFAIL=0;
  gsGlobal->Test->DATE=0;gsGlobal->Test->DATM=0;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_ON);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  gsKit_prim_sprite_texture(gsGlobal,texture,left,top,.5f,.5f,right,bottom,
      GENRE_LETTERING_WIDTH-.5f,GENRE_LETTERING_ROW-.5f,6,color);
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest;
  gsKit_set_test(gsGlobal,savedTest.ATE?GS_ATEST_ON:GS_ATEST_OFF);
  return 1;
}
