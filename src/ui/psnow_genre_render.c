#include "ui/animation.h"
/* PS Now RingMenuScript's textured scene plane, using its recovered texture,
 * scale (2400 / 2000), transform, blend and 1000 ms linear alpha manager. */
#include "ui/psnow_genre_render.h"
#include "ui/graphics.h"
#include <dmaKit.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
extern const unsigned char lunaPsnowRingData[];
extern const unsigned int lunaPsnowRingSize;
extern const unsigned char lunaPsnowSmallRingData[];
extern const unsigned int lunaPsnowSmallRingSize;
static GSTEXTURE ring,smallRing;
#include "../../assets/psnow-brackets.inc"
static GSTEXTURE brackets[2];
static unsigned char tiles[16][16],smallTiles[16][16];
int psnowGameCoverDraw(GSTEXTURE *texture,float left,float top,float right,float bottom,
                        float shade,float alpha) {
  if(!texture || alpha<=0 || right<=left || bottom<=top || !bindTextureSafe(gsGlobal,texture))return 0;
  GSTEST saved=*gsGlobal->Test;uint64_t blend=gsGlobal->PrimAlpha;
  int enabled=gsGlobal->PrimAlphaEnable,pabe=gsGlobal->PABE;
  // GREATER zero accepts opaque centers (alpha=128) as well as soft edges.
  gsGlobal->Test->ATST=6;gsGlobal->Test->AREF=0;gsGlobal->Test->AFAIL=0;
  gsGlobal->Test->DATE=0;gsGlobal->Test->DATM=0;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_ON);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  int light=(int)(128*fminf(1,fmaxf(0,shade)));
  gsKit_prim_sprite_texture(gsGlobal,texture,left,top,0,0,right,bottom,
      texture->Width-1,texture->Height-1,4,
      GS_SETREG_RGBA(light,light,light,(int)(128*fminf(1,alpha))));
  gsKit_set_primalpha(gsGlobal,blend,pabe);gsGlobal->PrimAlphaEnable=enabled;
  *gsGlobal->Test=saved;
  gsKit_set_test(gsGlobal,saved.ATE?GS_ATEST_ON:GS_ATEST_OFF);
  return 1;
}
static int loadPlane(GSTEXTURE *texture,unsigned char occupied[16][16],
                     const unsigned char *data,unsigned int dataSize) {
  unsigned long size=320*320+1024,decoded=size;
  unsigned char *pixels=memalign(128,size);
  if(!pixels)return -1;
  if(uncompress(pixels,&decoded,data,dataSize)!=Z_OK || decoded!=size) {
    free(pixels);return -1;
  }
  memset(texture,0,sizeof(*texture));memset(occupied,0,16*16);
  texture->Width=texture->Height=320;texture->PSM=GS_PSM_T8;texture->Mem=(u32 *)pixels;
  texture->Clut=(u32 *)(pixels+320*320);texture->ClutPSM=GS_PSM_CT32;
  texture->ClutStorageMode=GS_CLUT_STORAGE_CSM1;texture->Filter=GS_FILTER_LINEAR;
  texture->Delayed=GS_SETTING_ON;
  for(int y=0;y<320;y++)for(int x=0;x<320;x++) {
    unsigned i=pixels[y*320+x];i=(i&~24)|((i&8)<<1)|((i&16)>>1);
    if((texture->Clut[i]>>24)&255)occupied[y/20][x/20]=1;
  }
  return 0;
}
int psnowRingOpen(void) {
  if(ring.Mem)return 0;
  if(loadPlane(&ring,tiles,lunaPsnowRingData,lunaPsnowRingSize))return -1;
  if(loadPlane(&smallRing,smallTiles,lunaPsnowSmallRingData,lunaPsnowSmallRingSize)) {
    free(ring.Mem);memset(&ring,0,sizeof(ring));return -1;
  }
  for(int i=0;i<2;i++) {
    memset(&brackets[i],0,sizeof(brackets[i]));
    brackets[i].Width=brackets[i].Height=32;brackets[i].PSM=GS_PSM_CT32;
    brackets[i].Mem=i?psnowBracketRight:psnowBracketLeft;
    brackets[i].Filter=GS_FILTER_LINEAR;brackets[i].Delayed=GS_SETTING_ON;
  }
  return 0;
}
void psnowRingClose(void) {
  if(!ring.Mem)return;
  gsKit_queue_exec(gsGlobal);gsKit_finish();dmaKit_wait_fast();
  gsKit_TexManager_free(gsGlobal,&ring);free(ring.Mem);memset(&ring,0,sizeof(ring));
  gsKit_TexManager_free(gsGlobal,&smallRing);free(smallRing.Mem);memset(&smallRing,0,sizeof(smallRing));
  for(int i=0;i<2;i++){gsKit_TexManager_free(gsGlobal,&brackets[i]);memset(&brackets[i],0,sizeof(brackets[i]));}
}
typedef struct { float x,y; int visible; } RingScreen;
static RingScreen project(const PsnowGenreModel *model,PsnowVec p) {
  float depth=600-p.z,scale=500/fmaxf(64,depth);
  scale*=psnowGenreProjectionScale(model);
  return (RingScreen){(320+p.x*scale)*gsGlobal->Width/640,
      (240-p.y*scale)*gsGlobal->Height/480,depth>=64};
}
static void scenePlane(const PsnowGenreModel *model,GSTEXTURE *texture,
                        unsigned char occupied[16][16],PsnowVec center,
                        PsnowVec u,PsnowVec v,float opacity) {
  if(opacity<=0 || !texture->Mem || !bindTextureSafe(gsGlobal,texture))return;
  RingScreen vertices[17][17];
  for(int y=0;y<=16;y++)for(int x=0;x<=16;x++) {
    float a=x/16.0f-.5f,b=.5f-y/16.0f;
    vertices[y][x]=project(model,(PsnowVec){center.x+u.x*a+v.x*b,
        center.y+u.y*a+v.y*b,center.z+u.z*a+v.z*b});
  }
  GSTEST saved=*gsGlobal->Test;uint64_t blend=gsGlobal->PrimAlpha;
  int enabled=gsGlobal->PrimAlphaEnable,pabe=gsGlobal->PABE;
  gsGlobal->Test->DATE=0;gsGlobal->Test->DATM=0;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,2,0,1,0),0);
  uint64_t color=GS_SETREG_RGBA(128,128,128,(int)(128*fminf(1,opacity)));
  for(int y=0;y<16;y++)for(int x=0;x<16;x++)if(occupied[y][x]) {
    RingScreen a=vertices[y][x],b=vertices[y][x+1],c=vertices[y+1][x],d=vertices[y+1][x+1];
    if(!a.visible||!b.visible||!c.visible||!d.visible)continue;
    if(fmaxf(fmaxf(a.x,b.x),fmaxf(c.x,d.x))<0 ||
       fminf(fminf(a.x,b.x),fminf(c.x,d.x))>gsGlobal->Width ||
       fmaxf(fmaxf(a.y,b.y),fmaxf(c.y,d.y))<0 ||
       fminf(fminf(a.y,b.y),fminf(c.y,d.y))>gsGlobal->Height)continue;
    gsKit_prim_quad_goraud_texture_3d(gsGlobal,texture,
        a.x,a.y,2,x*20.0f,y*20.0f,b.x,b.y,2,(x+1)*20.0f,y*20.0f,
        c.x,c.y,2,x*20.0f,(y+1)*20.0f,d.x,d.y,2,(x+1)*20.0f,(y+1)*20.0f,
        color,color,color,color);
  }
  gsKit_set_primalpha(gsGlobal,blend,pabe);gsGlobal->PrimAlphaEnable=enabled;
  *gsGlobal->Test=saved;
  gsKit_set_test(gsGlobal,saved.ATE?GS_ATEST_ON:GS_ATEST_OFF);
}
static void ringPlane(const PsnowGenreModel *model,float opacity) {
  float diameter=model->profile==2?2000:2400;
  PsnowVec center=psnowGenreTransform(model,(PsnowVec){0},1);
  PsnowVec u=psnowGenreTransform(model,(PsnowVec){diameter,0,0},1);
  PsnowVec v=psnowGenreTransform(model,(PsnowVec){0,diameter,0},1);
  u.x-=center.x;u.y-=center.y;u.z-=center.z;
  v.x-=center.x;v.y-=center.y;v.z-=center.z;
  scenePlane(model,&ring,tiles,center,u,v,opacity);
}
void psnowRingDraw(const PsnowGenreModel *model) {
  ringPlane(model,fminf(1,model->entryAge/1000));
  if(lunaAnimationEffects()>0 && model->profile==1)for(int i=0;i<3;i++) {
    float age=psnowSmallWaveAge(model,i);
    if(age<0)continue;
    // nowhot 0x7f14 -> EffectType3 -> 0x4fc8: Scene(0) planes,
    // world (0,50,0), no SetRot; ringSmallTexId, not the base ring.
    float diameter=100*psnowSmallWaveScale(age);
    scenePlane(model,&smallRing,smallTiles,(PsnowVec){0,50,0},
        (PsnowVec){diameter,0,0},(PsnowVec){0,diameter,0},(1-age/1000)*lunaAnimationEffects());
  }
}
void psnowDescriptionDraw(const PsnowGenreModel *model,float cx,float cy,float halfWidth,float age) {
  (void)model;
  if(age<0 || !brackets[0].Mem)return;
  GSTEST saved=*gsGlobal->Test;uint64_t blend=gsGlobal->PrimAlpha;
  int enabled=gsGlobal->PrimAlphaEnable,pabe=gsGlobal->PABE;
  gsGlobal->Test->DATE=0;gsGlobal->Test->DATM=0;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  float sx=gsGlobal->Width/640.0f,sy=gsGlobal->Height/480.0f;
  for(int i=0;i<2;i++)if(bindTextureSafe(gsGlobal,&brackets[i])) {
    float x=cx+halfWidth*(i?psnowDescriptionRight(age):-1);
    gsKit_prim_sprite_texture(gsGlobal,&brackets[i],(x-6.5f)*sx,(cy-15)*sy,0,0,
        (x+6.5f)*sx,(cy+15)*sy,13,27,6,GS_SETREG_RGBA(128,128,128,128));
  }
  gsKit_set_primalpha(gsGlobal,blend,pabe);gsGlobal->PrimAlphaEnable=enabled;
  *gsGlobal->Test=saved;
  gsKit_set_test(gsGlobal,saved.ATE?GS_ATEST_ON:GS_ATEST_OFF);
}
