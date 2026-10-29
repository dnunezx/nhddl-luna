/* Authored, filtered blue light band. The inspected ring_07/ring_small assets
 * are texture planes, not pixel-width line loops. No original pixels used. */
#define RING_RIBBON_SEGMENTS 192
static u32 ringRibbonPixels[64*2] __attribute__((aligned(128)));
static GSTEXTURE ringRibbonTexture;
static u32 ringEffectsRibbonPixels[64*2] __attribute__((aligned(128)));
static GSTEXTURE ringEffectsRibbonTexture;
static float ringRibbonCos[RING_RIBBON_SEGMENTS+1],ringRibbonSin[RING_RIBBON_SEGMENTS+1];
static void ringRibbonTextureInit(GSTEXTURE *texture,u32 *pixels,LunaRingColor preset) {
  memset(texture,0,sizeof(*texture));
  texture->Width=64; texture->Height=2;
  texture->PSM=GS_PSM_CT32; texture->Mem=pixels;
  texture->Filter=GS_FILTER_LINEAR; texture->Delayed=GS_SETTING_ON;
  for(int x=0;x<64;x++) {
    float d=fabsf((x-31.5f)/31.5f);
    float core=expf(-d*d*18),glow=expf(-d*d*6);
    float edge=ringClamp((1-d)*8);
    int alpha=(int)(128*ringClamp(.72f*core+.28f*glow)*edge);
    int r=(int)(24+121*core),g=(int)(114+106*core),b=255;
    if(preset!=LUNA_RING_ORIGINAL) {
      static const int palette[LUNA_RING_COLOR_COUNT][3]={
        {0,0,0},{45,232,255},{55,123,255},{176,110,255},
        {255,105,174},{90,242,135},{245,250,255}};
      const int *color=palette[preset];
      float light=.5f+.5f*core;
      r=(int)(color[0]*light+(255-color[0])*core*.15f);
      g=(int)(color[1]*light+(255-color[1])*core*.15f);
      b=(int)(color[2]*light+(255-color[2])*core*.15f);
    }
    u32 pixel=(u32)r|((u32)g<<8)|((u32)b<<16)|((u32)alpha<<24);
    pixels[x]=pixels[x+64]=pixel;
  }
}
static void ringRibbonInit(void) {
  for(int j=0;j<=RING_RIBBON_SEGMENTS;j++) {
    ringRibbonCos[j]=cosf(j*RING_TAU/RING_RIBBON_SEGMENTS);
    ringRibbonSin[j]=sinf(j*RING_TAU/RING_RIBBON_SEGMENTS);
  }
  /* Exact seam closure avoids a bright/dark join from sin(2*pi) rounding. */
  ringRibbonCos[RING_RIBBON_SEGMENTS]=1; ringRibbonSin[RING_RIBBON_SEGMENTS]=0;
  ringRibbonTextureInit(&ringRibbonTexture,ringRibbonPixels,uiRingGetColor());
  if(ringEffectsColorPreset!=LUNA_RING_ORIGINAL)
    ringRibbonTextureInit(&ringEffectsRibbonTexture,ringEffectsRibbonPixels,ringEffectsColorPreset);
}
static void ringRibbonClose(void) {
  gsKit_TexManager_free(gsGlobal,&ringRibbonTexture);
  memset(&ringRibbonTexture,0,sizeof(ringRibbonTexture));
  if(ringEffectsRibbonTexture.Mem)gsKit_TexManager_free(gsGlobal,&ringEffectsRibbonTexture);
  memset(&ringEffectsRibbonTexture,0,sizeof(ringEffectsRibbonTexture));
}
typedef struct { RingPoint inner,outer; float coverage; int visible; } RingRibbonVertex;
static RingRibbonVertex ringRibbonVertex(RingVec center,RingVec u,RingVec v,float radius,float halfWidth,int j) {
  float c=ringRibbonCos[j],s=ringRibbonSin[j];
  RingVec radial={u.x*c+v.x*s,u.y*c+v.y*s,u.z*c+v.z*s};
  RingVec tangent={-u.x*s+v.x*c,-u.y*s+v.y*c,-u.z*s+v.z*c};
  RingVec world={center.x+radial.x*radius,center.y+radial.y*radius,center.z+radial.z*radius};
  RingPoint p=ringWorld(world.x,world.y,world.z);
  RingPoint along=ringWorld(world.x+tangent.x,world.y+tangent.y,world.z+tangent.z);
  RingPoint outside=ringWorld(world.x+radial.x*halfWidth,world.y+radial.y*halfWidth,world.z+radial.z*halfWidth);
  float tx=along.x-p.x,ty=along.y-p.y,length=sqrtf(tx*tx+ty*ty);
  if(!p.visible || !along.visible || !outside.visible || length<.00001f)
    return (RingRibbonVertex){.visible=0};
  float nx=-ty/length,ny=tx/length;
  float projected=fabsf((outside.x-p.x)*nx+(outside.y-p.y)*ny);
  /* Keep a bilinear-filter footprint across a foreshortened edge. Coverage
   * compensates its brightness, so distant edges do not become thick rails. */
  float minimum=1.25f*640/gsGlobal->Width;
  float width=fmaxf(minimum,projected),coverage=fminf(1,projected/minimum);
  RingPoint inner=p,outer=p;
  inner.x-=nx*width; inner.y-=ny*width; outer.x+=nx*width; outer.y+=ny*width;
  return (RingRibbonVertex){inner,outer,coverage,1};
}
static void ringRibbonDraw(RingVec center,RingVec u,RingVec v,float diameter,float opacity,int bright,GSTEXTURE *texture) {
  if(opacity<=0 || diameter<=0 || !bindTextureSafe(gsGlobal,texture)) return;
  /* The source textures have a narrow transparent margin around their rim. */
  float radius=diameter*(bright?.477f:.471f);
  float halfWidth=diameter*(bright?.028f:.030f);
  RingRibbonVertex previous=ringRibbonVertex(center,u,v,radius,halfWidth,0);
  for(int j=1;j<=RING_RIBBON_SEGMENTS;j++) {
    RingRibbonVertex p=ringRibbonVertex(center,u,v,radius,halfWidth,j);
    int safe=1;
    RingPoint points[]={previous.inner,previous.outer,p.inner,p.outer};
    for(int k=0;k<4;k++) if(points[k].x<-1024 || points[k].x>1664 || points[k].y<-896 || points[k].y>1344) safe=0;
    if(p.visible && previous.visible && safe) {
      ringOpacity=opacity*previous.coverage;
      uint64_t before=bright?ringColor(128,128,128,128):ringColor(76,80,88,100);
      ringOpacity=opacity*p.coverage;
      uint64_t after=bright?ringColor(128,128,128,128):ringColor(76,80,88,100);
      gsKit_prim_quad_goraud_texture_3d(gsGlobal,texture,
          ringSX(previous.inner.x),ringSY(previous.inner.y),3,.5f,.5f,
          ringSX(previous.outer.x),ringSY(previous.outer.y),3,63.5f,.5f,
          ringSX(p.inner.x),ringSY(p.inner.y),3,.5f,1.5f,
          ringSX(p.outer.x),ringSY(p.outer.y),3,63.5f,1.5f,
          before,before,after,after);
    }
    previous=p;
  }
}
static void ringRibbon(RingVec center,RingVec u,RingVec v,float diameter,float opacity,int bright) {
  ringRibbonDraw(center,u,v,diameter,opacity,bright,&ringRibbonTexture);
}
