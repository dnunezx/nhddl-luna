/* Clear-ice cube renderer with procedural LUNA material maps. */
#include <gsInline.h>
#include "cube_geometry.inc"

typedef struct { float x,y,depth; } CubePoint;
typedef struct { CubeVec sine,cosine; } CubeRotation;
typedef struct { CubeVec v[4],normal; float front; } CubeGlassFace;
typedef struct {
  CubeGlassFace face[6];
  CubeVec position;
  CubeRotation rotation;
  float left,top,width,height,depth,fade,highlight;
  unsigned visible;
} CubeGlass;
static GSTEXTURE cubeMaterial[4],cubeWork[2],cubeScreen;
/* Clear-ice styling shared with the accepted local preview. */
static u32 cubeIcePixels[4][4096] __attribute__((aligned(128)));
#define CUBE_ICE_REFRACTION_BACK .035f
#define CUBE_ICE_REFRACTION_FRONT .060f
#define CUBE_ICE_REFLECTION .22f
#define CUBE_ICE_GLOSS .24f
static int cubeWorkReady,cubeRenderTarget=-1;
static const CubeGlass *cubeRenderScene;
static float cubeTextureScaleX=1,cubeTextureScaleY=1;
static float cubeSceneScale=1,cubeSceneOffsetX,cubeSceneOffsetY;
/* Borrow the existing background pages AFTER drawing that background. */
extern int uiBorrowBackgroundWorkPages(GSTEXTURE *pages);
static float cubeSX(float x){return x*gsGlobal->Width/640;}
static float cubeSY(float y){return y*gsGlobal->Height/480;}
static uint64_t cubeColor(int r,int g,int b,float alpha) {
  return GS_SETREG_RGBA(r,g,b,(int)(128*cubeClamp(alpha)));
}
static CubeRotation cubePrepareRotation(CubeVec angles) {
  return (CubeRotation){
    {sinf(angles.x),sinf(angles.y),sinf(angles.z)},
    {cosf(angles.x),cosf(angles.y),cosf(angles.z)}};
}
static CubeVec cubeRotate(CubeVec p,const CubeRotation *rotation) {
  const CubeVec s=rotation->sine,c=rotation->cosine;
  CubeVec q={p.x,p.y*c.x-p.z*s.x,p.y*s.x+p.z*c.x};
  p=(CubeVec){q.x*c.y+q.z*s.y,q.y,-q.x*s.y+q.z*c.y};
  return (CubeVec){p.x*c.z-p.y*s.z,p.x*s.z+p.y*c.z,p.z};
}
static CubePoint cubeProject(CubeVec p,CubeVec camera) {
  float depth=500+p.z-camera.z,scale=500/fmaxf(16,depth);
  return (CubePoint){(320+(-p.x-camera.x)*scale)*cubeSceneScale+cubeSceneOffsetX,
      (240-(p.y-camera.y)*scale)*cubeSceneScale+cubeSceneOffsetY,depth};
}
static u64 *cubePacket(int count) {
  u64 *p=gsKit_heap_alloc(gsGlobal,count,count*16,GIF_AD);
  p[0]=GIF_TAG_AD(count);p[1]=GIF_AD;return p+2;
}
static u32 cubeFloatBits(float f){u32 v;memcpy(&v,&f,sizeof(v));return v;}
static void cubeMaterialInit(void) {
  memset(cubeMaterial,0,sizeof(cubeMaterial));
  /* Authored neutral environment reflections and gloss, independent of the
   * local reference-texture experiment. These maps are generated once. */
  for(int y=0;y<64;y++)for(int x=0;x<64;x++) {
    float u=(x+.5f)/64,v=(y+.5f)/64;
    float a=(u+.35f*v-.42f)*5,b=(u-.22f*v-.77f)*9;
    float band=expf(-a*a),secondary=expf(-b*b);
    float dx=u-.28f,dy=v-.22f,soft=expf(-(dx*dx+dy*dy)*12);
    int light[2]={(int)(255*cubeClamp(.06f+.48f*band+.19f*secondary+.15f*soft)),
                  (int)(255*cubeClamp(.04f+.34f*band+.26f*secondary+.18f*soft))};
    for(int i=0;i<2;i++)cubeIcePixels[i][y*64+x]=GS_SETREG_RGBA(
        light[i]*236/255,light[i]*248/255,light[i],128);
    int gloss=(int)(128*cubeClamp(.12f+.65f*band+.18f*soft));
    cubeIcePixels[2][y*64+x]=GS_SETREG_RGBA(255,255,255,gloss);
  }
  /* A continuous edge-to-outside falloff for the joined selection contour. */
  for(int y=0;y<64;y++)for(int x=0;x<64;x++) {
    float fade=1-y/63.0f;
    /* A rounded crest joins the inner and outer halo without a sharp ridge. */
    float halo=gsGlobal->Interlace==GS_INTERLACED?
        fade*fade*(3-2*fade):fade*sqrtf(fade);
    cubeIcePixels[3][y*64+x]=GS_SETREG_RGBA(220,242,255,(int)(128*halo));
  }
  for(int i=0;i<4;i++) {
    cubeMaterial[i].Width=cubeMaterial[i].Height=64;
    cubeMaterial[i].PSM=GS_PSM_CT32;cubeMaterial[i].Mem=cubeIcePixels[i];
    cubeMaterial[i].Filter=GS_FILTER_LINEAR;cubeMaterial[i].Delayed=GS_SETTING_ON;
  }
  cubeWorkReady=uiBorrowBackgroundWorkPages(cubeWork);
  cubeRenderTarget=-1;
}
static void cubeMaterialClose(void) {
  gsKit_queue_exec(gsGlobal);gsKit_finish();dmaKit_wait_fast();
  for(int i=0;i<4;i++)gsKit_TexManager_free(gsGlobal,&cubeMaterial[i]);
  memset(cubeMaterial,0,sizeof(cubeMaterial));
  /* Work pages belong to the GS context, not to this menu. */
  cubeWorkReady=0;
}
static void cubeTarget(int target) {
  cubeRenderTarget=target;
  int w=target<0?gsGlobal->Width:cubeWork[target].Width;
  int h=target<0?gsGlobal->Height:cubeWork[target].Height;
  u32 address=target<0?gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer]:cubeWork[target].Vram;
  u64 *p=cubePacket(5);
  p[0]=GS_SETREG_FRAME(address/8192,w/64,target<0?gsGlobal->PSM:GS_PSM_CT32,0);
  p[1]=gsGlobal->PrimContext?GS_FRAME_2:GS_FRAME_1;
  p[2]=GS_SETREG_SCISSOR(0,w-1,0,h-1);
  p[3]=gsGlobal->PrimContext?GS_SCISSOR_2:GS_SCISSOR_1;
  /* Disable UI alpha rejection and all depth writes for these 2D passes. */
  p[4]=GS_SETREG_TEST(0,0,0,0,0,0,1,1);
  p[5]=gsGlobal->PrimContext?GS_TEST_2:GS_TEST_1;
  p[6]=0;p[7]=GS_TEXFLUSH;
  p[8]=GS_SETREG_ZBUF(gsGlobal->ZBuffer/8192,gsGlobal->PSMZ&15,1);
  p[9]=gsGlobal->PrimContext?GS_ZBUF_2:GS_ZBUF_1;
}
static void cubeBind(GSTEXTURE *t,int managed,int repeat) {
  if(managed)bindTextureSafe(gsGlobal,t);
  int tw=0,th=0;
  while((1<<tw)<t->Width)tw++;
  while((1<<th)<t->Height)th++;
  cubeTextureScaleX=t->Width/(float)(1<<tw);
  cubeTextureScaleY=t->Height/(float)(1<<th);
  u64 *p=cubePacket(5);
  p[0]=0;p[1]=GS_TEXFLUSH;
  p[2]=GS_SETREG_TEX0(t->Vram/256,t->TBW,t->PSM,tw,th,1,0,0,0,0,0,0);
  p[3]=gsGlobal->PrimContext?GS_TEX0_2:GS_TEX0_1;
  p[4]=GS_SETREG_TEX1(1,0,1,1,0,0,0);
  p[5]=gsGlobal->PrimContext?GS_TEX1_2:GS_TEX1_1;
  /* Framebuffers are not powers of two. Region clamp prevents refracted UVs
   * sampling adjacent VRAM beyond their actual width/height. */
  int region=t->Width!=(1<<tw)||t->Height!=(1<<th);
  p[6]=GS_SETREG_CLAMP(region?2:repeat?0:1,region?2:repeat?0:1,
      0,region?t->Width-1:0,0,region?t->Height-1:0);
  p[7]=gsGlobal->PrimContext?GS_CLAMP_2:GS_CLAMP_1;
  /* CT24 main-screen samples have full alpha. */
  p[8]=GS_SETREG_TEXA(0x80,0,0x80);p[9]=GS_TEXA;
}
static void cubeBlend(int mode) {
  /* Native mode 0 -> 0x0a, 1 -> 0x44, 2 -> 0x48, 7 -> 0x49.
   * 0x202 adds the source with FIX=128; 7 multiplies DEST by 1+As. */
  static const unsigned char table[8]={0x0a,0x44,0x48,0x42,0x4a,0x89,0x46,0x49};
  unsigned v=table[mode&7]|((mode>>4)&0x30);
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(v&3,(v>>2)&3,(v>>4)&3,(v>>6)&3,128),0);
}
static void cubeClearWork(void) {
  int alpha=gsGlobal->PrimAlphaEnable;gsGlobal->PrimAlphaEnable=GS_SETTING_OFF;
  gsKit_prim_sprite(gsGlobal,0,0,cubeWork[cubeRenderTarget].Width,
      cubeWork[cubeRenderTarget].Height,0,GS_SETREG_RGBA(0,0,0,0));
  gsGlobal->PrimAlphaEnable=alpha;
}
static void cubeEmit(const CubePoint *p,const float uv[][2],int count,u64 color,int blend) {
  u64 *q=cubePacket(1+count*3);
  q[0]=4|16|(blend?64:0)|(gsGlobal->PrimContext<<9);q[1]=GS_PRIM;
  for(int i=0;i<count;i++) {
    float x=p[i].x,y=p[i].y;
    if(cubeRenderTarget<0) {x=cubeSX(x);y=cubeSY(y);}
    else {
      x=(x-cubeRenderScene->left)*cubeWork[cubeRenderTarget].Width/cubeRenderScene->width;
      y=(y-cubeRenderScene->top)*cubeWork[cubeRenderTarget].Height/cubeRenderScene->height;
    }
    float reciprocal=1/fmaxf(16,p[i].depth);
    int k=2+i*6;
    q[k]=(u64)cubeFloatBits(uv[i][0]*cubeTextureScaleX*reciprocal)|
      ((u64)cubeFloatBits(uv[i][1]*cubeTextureScaleY*reciprocal)<<32);
    q[k+1]=GS_ST;
    q[k+2]=(color&0xffffffffULL)|((u64)cubeFloatBits(reciprocal)<<32);q[k+3]=GS_RGBAQ;
    q[k+4]=GS_SETREG_XYZ2(gsKit_float_to_int_x(gsGlobal,x),gsKit_float_to_int_y(gsGlobal,y),3);
    q[k+5]=GS_XYZ2;
  }
}
static void cubeBlit(GSTEXTURE *texture,float u0,float v0,float u1,float v1,float alpha,int mode) {
  const CubeGlass *s=cubeRenderScene;
  CubePoint p[4]={{s->left,s->top,500},{s->left,s->top+s->height,500},
    {s->left+s->width,s->top,500},{s->left+s->width,s->top+s->height,500}};
  float uv[4][2]={{u0,v0},{u0,v1},{u1,v0},{u1,v1}};
  cubeBind(texture,0,0);cubeBlend(mode);
  cubeEmit(p,uv,4,cubeColor(128,128,128,alpha),mode!=0);
}
static void cubeGlassBuild(CubeGlass *s,int index,const CubeMenu *m,CubeVec camera,float fade) {
  memset(s,0,sizeof(*s));s->position=cubePosition(index,m->phase[index]);
  CubeVec angles=cubeRotation(index,m->phase[index]);
  CubeVec size={CUBE_SIDE,CUBE_SIDE,CUBE_SIDE};
  int portal=m->submenuOpen&&index==m->selected%CUBE_COUNT;
  if(portal) {
    float t=cubeSmooth(m->submenuAge/CUBE_SUBMENU_MS);
    /* The selected crystal unfolds into the options pane at (20,97)-(520,385).
     * Keep its glass and contour throughout the reversible 1440 ms morph. */
    s->position.x+=(50-s->position.x)*t;
    s->position.y+=(-1-s->position.y)*t;
    s->position.z*=1-t;
    angles.x=remainderf(angles.x,2*CUBE_PI)*(1-t);
    angles.y=remainderf(angles.y,2*CUBE_PI)*(1-t);
    angles.z=remainderf(angles.z,2*CUBE_PI)*(1-t);
    size=(CubeVec){CUBE_SIDE+(500-CUBE_SIDE)*t,
        CUBE_SIDE+(288-CUBE_SIDE)*t,CUBE_SIDE+(2-CUBE_SIDE)*t};
  }
  s->depth=500+s->position.z-camera.z;s->highlight=m->light[index];
  /* Native GetFadeFactor: ((distance-NEAR)/bounding_radius)-1, clamped. */
  const float radius=CUBE_SIDE*.8660254038f;
  s->fade=fade*cubeClamp((s->depth-16)/radius-1);
  CubePoint center=cubeProject(s->position,camera);
  float scale=cubeSceneScale*500/fmaxf(16,s->depth-radius);
  /* A sphere-enclosing crop is independent of rotation and never clips the
   * unit cube. Crop extent is a LUNA projection adaptation, not an art knob. */
  s->width=s->height=2*radius*scale;
  s->left=center.x-s->width*.5f;s->top=center.y-s->height*.5f;
  float right=fminf(640,s->left+s->width),bottom=fminf(480,s->top+s->height);
  s->left=fmaxf(0,s->left);s->top=fmaxf(0,s->top);
  s->width=right-s->left;s->height=bottom-s->top;
  if(!portal&&(s->width<=0||s->height<=0)){s->fade=0;return;}
  // Every face, corner and reflection offset shares this crystal's rotation.
  s->rotation=cubePrepareRotation(angles);
  for(int f=0;f<6;f++) {
    CubeGlassFace *face=&s->face[f];
    const float *normal=cubeUnitMesh[f][0]+3;
    face->normal=cubeRotate((CubeVec){normal[0],normal[1],normal[2]},&s->rotation);
    CubeVec centerFace={0,0,0};
    for(int j=0;j<4;j++) {
      const float *v=cubeUnitMesh[f][j];
      CubeVec q=cubeRotate((CubeVec){size.x*v[0],size.y*v[1],size.z*v[2]},&s->rotation);
      face->v[j]=(CubeVec){q.x+s->position.x,q.y+s->position.y,q.z+s->position.z};
      centerFace.x+=face->v[j].x*.25f;centerFace.y+=face->v[j].y*.25f;centerFace.z+=face->v[j].z*.25f;
    }
    face->front=face->normal.x*(-camera.x-centerFace.x)+face->normal.y*(camera.y-centerFace.y)+
      face->normal.z*(camera.z-500-centerFace.z);
    if(face->front>0)s->visible|=1U<<f;
  }
  if(portal) {
    /* Crop the expanded geometry itself so its faces and rim stay intact. */
    float left=640,top=480,right=0,bottom=0;
    for(int f=0;f<6;f++)for(int j=0;j<4;j++) {
      CubePoint p=cubeProject(s->face[f].v[j],camera);
      left=fminf(left,p.x);top=fminf(top,p.y);
      right=fmaxf(right,p.x);bottom=fmaxf(bottom,p.y);
    }
    s->left=fmaxf(0,left-2);s->top=fmaxf(0,top-2);
    s->width=fminf(640,right+2)-s->left;
    s->height=fminf(480,bottom+2)-s->top;
    if(s->width<=0||s->height<=0)s->fade=0;
  }
}
enum {CUBE_REFRACTION_BACK,CUBE_REFRACTION_FRONT,CUBE_REFLECTION_BACK,CUBE_REFLECTION_FRONT,CUBE_GLOSS};
static void cubeGlassFaces(const CubeGlass *s,CubeVec camera,int pass,CubeVec offset,float brightness) {
  int front=pass==CUBE_REFRACTION_FRONT||pass==CUBE_REFLECTION_FRONT||pass==CUBE_GLOSS;
  int reflection=pass==CUBE_REFLECTION_BACK||pass==CUBE_REFLECTION_FRONT;
  if(reflection)brightness*=CUBE_ICE_REFLECTION;
  if(pass==CUBE_REFRACTION_BACK)cubeBind(&cubeScreen,0,1);
  else if(pass==CUBE_REFRACTION_FRONT)cubeBind(&cubeWork[0],0,1);
  else cubeBind(&cubeMaterial[pass==CUBE_GLOSS?2:pass==CUBE_REFLECTION_FRONT?1:0],1,1);
  cubeBlend(pass==CUBE_GLOSS?7:1);
  for(int f=0;f<6;f++) {
    const CubeGlassFace *face=&s->face[f];
    if((face->front>0)!=front)continue;
    CubePoint p[4];float uv[4][2];int clipped=0;
    for(int j=0;j<4;j++) {
      CubeVec v={face->v[j].x+offset.x,face->v[j].y+offset.y,face->v[j].z+offset.z};
      p[j]=cubeProject(v,camera);
      if(p[j].depth<16)clipped=1;
      if(pass==CUBE_REFRACTION_BACK) {
        uv[j][0]=p[j].x/640-CUBE_ICE_REFRACTION_BACK*face->normal.x;
        uv[j][1]=p[j].y/480-CUBE_ICE_REFRACTION_BACK*face->normal.y;
      } else if(pass==CUBE_REFRACTION_FRONT) {
        uv[j][0]=(p[j].x-s->left)/s->width+CUBE_ICE_REFRACTION_FRONT*face->normal.x;
        uv[j][1]=(p[j].y-s->top)/s->height-CUBE_ICE_REFRACTION_FRONT*face->normal.y;
      } else if(reflection) {
        /* 0x71fb4c: rotation-only matrix followed by (-z/2+1/2,
         * -y/2+1/2). Camera faces -Z; X/Z are reversed in this projection. */
        uv[j][0]=.5f+.5f*face->normal.z;
        uv[j][1]=.5f-.5f*face->normal.y;
      } else {
        uv[j][0]=cubeUnitMesh[f][j][6];uv[j][1]=cubeUnitMesh[f][j][7];
      }
    }
    if(!clipped)cubeEmit(p,uv,4,cubeColor(128*brightness,128*brightness,128*brightness,
        pass==CUBE_GLOSS?s->fade*CUBE_ICE_GLOSS:1),1);
  }
}
static float cubeGlassGlowWidth(const CubeGlass *s) {
  float width=fmaxf(3,fminf(7,2100/fmaxf(16,s->depth)));
  return width*(gsGlobal->Interlace==GS_INTERLACED?1.4f:1);
}
static int cubeGlassOutline(const CubeGlass *s,CubeVec camera,CubePoint *inner,CubePoint *outer) {
  CubePoint corners[8];
  for(int f=0;f<6;f++)for(int j=0;j<4;j++) {
    corners[cubeUnitCorner[f][j]]=cubeProject(s->face[f].v[j],camera);
  }
  /* Join the XOR silhouette edges into one closed contour. This uses the
   * exact projected glass vertices, not an independently sized selection box. */
  unsigned mask=0;
  for(int e=0;e<12;e++) {
    const unsigned char *edge=cubeUnitEdges[e];
    if(((s->visible>>edge[0])^(s->visible>>edge[1]))&1)mask|=1U<<e;
  }
  if(!mask)return 0;
  int firstEdge=0;
  while(!(mask&(1U<<firstEdge)))firstEdge++;
  int first=cubeUnitEdges[firstEdge][2],vertex=cubeUnitEdges[firstEdge][3],count=1;
  inner[0]=corners[first];mask&=~(1U<<firstEdge);
  while(vertex!=first&&count<8) {
    inner[count++]=corners[vertex];int next=-1;
    for(int e=0;e<12;e++)if(mask&(1U<<e)) {
      const unsigned char *edge=cubeUnitEdges[e];
      if(edge[2]==vertex)next=edge[3];
      else if(edge[3]==vertex)next=edge[2];
      else continue;
      mask&=~(1U<<e);break;
    }
    if(next<0)return 0;
    vertex=next;
  }
  if(vertex!=first||mask||count<3)return 0;
  CubePoint center=cubeProject(s->position,camera);
  float nx[8],ny[8];
  for(int i=0;i<count;i++) {
    CubePoint a=inner[i],b=inner[(i+1)%count];
    if(a.depth<16||b.depth<16)return 0;
    float x=a.y-b.y,y=b.x-a.x,len=sqrtf(x*x+y*y);
    if(len<.001f)return 0;
    x/=len;y/=len;
    if(x*((a.x+b.x)*.5f-center.x)+y*((a.y+b.y)*.5f-center.y)<0){x=-x;y=-y;}
    nx[i]=x;ny[i]=y;
  }
  /* Shared miter vertices keep the halo attached through edges and corners. */
  float glow=cubeGlassGlowWidth(s);
  for(int i=0;i<count;i++) {
    int previous=(i+count-1)%count;
    float x=nx[previous]+nx[i],y=ny[previous]+ny[i];
    float denominator=x*nx[i]+y*ny[i];
    if(denominator<.25f)denominator=.25f;
    float dx=x*glow/denominator,dy=y*glow/denominator;
    float length=sqrtf(dx*dx+dy*dy),limit=glow*2;
    if(length>limit){dx*=limit/length;dy*=limit/length;}
    outer[i]=(CubePoint){inner[i].x+dx,inner[i].y+dy,inner[i].depth};
  }
  return count;
}
static void cubeGlowQuad(const CubePoint *p,const float uv[][2],float alpha) {
  /* Four quarter-pixel samples soften raster stair steps without blurring the
   * glass or background. Glow is a screen-space overlay, so use affine UVs. */
  static const float jitter[4][2]={{-.25f,-.25f},{.25f,-.25f},{-.25f,.25f},{.25f,.25f}};
  /* Interlaced output also needs a vertical [1 2 1]/4 filter: subpixel AA
   * alone leaves the bright crest concentrated on alternating scanlines.
   * Filter only this additive contour, in output pixels, and retain the
   * original sampling and brightness for progressive output. */
  static const float fieldWeight[3]={.25f,.5f,.25f};
  int interlaced=gsGlobal->Interlace==GS_INTERLACED;
  int taps=interlaced?3:1;
  for(int tap=0;tap<taps;tap++)for(int sample=0;sample<4;sample++) {
    float offsetY=interlaced?tap-1:0;
    float weight=interlaced?fieldWeight[tap]*.85f:1;
    CubePoint shifted[4];
    for(int j=0;j<4;j++)shifted[j]=(CubePoint){
      p[j].x+jitter[sample][0]*640/gsGlobal->Width,
      p[j].y+(jitter[sample][1]+offsetY)*480/gsGlobal->Height,500};
    cubeEmit(shifted,uv,4,cubeColor(128,128,128,alpha*.25f*weight),1);
  }
}
static void cubeGlassHighlight(const CubeGlass *s,CubeVec camera) {
  if(s->highlight<=0)return;
  CubePoint inner[8],outer[8];int count=cubeGlassOutline(s,camera,inner,outer);
  if(!count)return;
  cubeBind(&cubeMaterial[3],1,0);cubeBlend(2);
  float glow=cubeGlassGlowWidth(s);
  CubePoint feather[8];
  for(int i=0;i<count;i++) {
    /* The CRT halo extends into the glass as well as outside it, spreading
     * the bright crest over several output pixels instead of a thin rim. */
    float dx=(outer[i].x-inner[i].x)/glow,dy=(outer[i].y-inner[i].y)/glow;
    float physical=sqrtf(dx*dx*gsGlobal->Width*gsGlobal->Width/(640.0f*640)+
        dy*dy*gsGlobal->Height*gsGlobal->Height/(480.0f*480));
    float width=(gsGlobal->Interlace==GS_INTERLACED?2.5f:1.25f)/fmaxf(.001f,physical);
    feather[i]=(CubePoint){inner[i].x-dx*width,inner[i].y-dy*width,500};
  }
  for(int i=0;i<count;i++) {
    int next=(i+1)%count;
    CubePoint p[4]={inner[i],inner[next],outer[i],outer[next]};
    /* Texel centers keep the brightest part attached to the geometric edge. */
    float uv[4][2]={{.5f,.5f/64},{.5f,.5f/64},{.5f,63.5f/64},{.5f,63.5f/64}};
    cubeGlowQuad(p,uv,s->fade*s->highlight);
    CubePoint rim[4]={feather[i],feather[next],inner[i],inner[next]};
    float rimUV[4][2]={{.5f,63.5f/64},{.5f,63.5f/64},{.5f,.5f/64},{.5f,.5f/64}};
    cubeGlowQuad(rim,rimUV,s->fade*s->highlight);
  }
}
static void cubeGlassDraw(const CubeGlass *s,CubeVec camera) {
  if(!cubeWorkReady||s->fade<=0||s->left>640||s->top>480||s->left+s->width<0||s->top+s->height<0)return;
  cubeRenderScene=s;
  cubeScreen=(GSTEXTURE){0};cubeScreen.Width=gsGlobal->Width;cubeScreen.Height=gsGlobal->Height;
  cubeScreen.PSM=gsGlobal->PSM;cubeScreen.TBW=gsGlobal->Width/64;
  cubeScreen.Vram=gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer];
  cubeTarget(0);cubeClearWork();
  cubeGlassFaces(s,camera,CUBE_REFRACTION_BACK,(CubeVec){0,0,0},1);
  cubeTarget(1);cubeClearWork();
  cubeGlassFaces(s,camera,CUBE_REFLECTION_BACK,(CubeVec){0,0,0},1);
  /* AddColorBuffer(graph, destination, source): destination is buffer 0. */
  cubeTarget(0);cubeBlit(&cubeWork[1],0,0,1,1,1,0x202);
  cubeTarget(1);
  for(int f=0;f<6;f++)if(s->visible&(1U<<f)) {
    const float *n=cubeUnitMesh[f][0]+3;
    CubeVec offset=cubeRotate((CubeVec){n[0]*80,n[1]*80,n[2]*80},&s->rotation);
    float intensity=cubeClamp(1-fabsf(s->face[f].normal.z*2));
    /* DefineShape(face+1) translates the entire unit mesh by 2*normal. */
    cubeGlassFaces(s,camera,CUBE_REFLECTION_BACK,offset,intensity);
  }
  cubeTarget(0);cubeBlit(&cubeWork[1],0,0,1,1,1,0x202);
  if(s->fade<.99f) {
    cubeTarget(1);cubeClearWork();
    cubeBlit(&cubeScreen,s->left/640,s->top/480,(s->left+s->width)/640,(s->top+s->height)/480,1,0);
  }
  cubeTarget(-1);cubeGlassFaces(s,camera,CUBE_REFRACTION_FRONT,(CubeVec){0,0,0},1);
  if(s->fade<.99f)cubeBlit(&cubeWork[1],0,0,1,1,1-s->fade,1);
  cubeTarget(1);cubeClearWork();
  cubeGlassFaces(s,camera,CUBE_REFLECTION_FRONT,(CubeVec){0,0,0},1);
  cubeTarget(-1);cubeBlit(&cubeWork[1],0,0,1,1,s->fade,2);
  cubeGlassFaces(s,camera,CUBE_GLOSS,(CubeVec){0,0,0},1);
  cubeGlassHighlight(s,camera);
}
static void cubeRestoreGlassState(void) {
  cubeTarget(-1);
  const GSTEST *t=gsGlobal->Test;const GSCLAMP *c=gsGlobal->Clamp;
  u64 *p=cubePacket(4);
  p[0]=GS_SETREG_TEST(t->ATE,t->ATST,t->AREF,t->AFAIL,t->DATE,t->DATM,t->ZTE,t->ZTST);
  p[1]=gsGlobal->PrimContext?GS_TEST_2:GS_TEST_1;
  p[2]=GS_SETREG_CLAMP(c->WMS,c->WMT,c->MINU,c->MAXU,c->MINV,c->MAXV);
  p[3]=gsGlobal->PrimContext?GS_CLAMP_2:GS_CLAMP_1;
  p[4]=GS_SETREG_TEXA(0,0,0x80);p[5]=GS_TEXA;
  p[6]=GS_SETREG_ZBUF(gsGlobal->ZBuffer/8192,gsGlobal->PSMZ&15,!gsGlobal->ZBuffering);
  p[7]=gsGlobal->PrimContext?GS_ZBUF_2:GS_ZBUF_1;
  cubeBlend(1);
}
