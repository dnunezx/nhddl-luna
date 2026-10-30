/* Root HEAD: radius=250, pos=(140,10,-160), rot=(-1.15,-.9,0).
 * Camera(0)=(0,0,600); native Camera virtual screen=640x480, distance=500.
 * The native artwork is recreated with filtered light bands and LUNA glyphs. */
typedef struct { float x,y,z,scale; int visible; } RingPoint;
static float ringOpacity;
static float ringViewOffsetX,ringViewOffsetY;
static float ringSX(float x) { return x*gsGlobal->Width/640.0f; }
static float ringSY(float y) { return y*gsGlobal->Height/448.0f; }
static uint64_t ringColor(int r,int g,int b,int a) {
  return GS_SETREG_RGBA(r,g,b,(int)(a*ringClamp(ringOpacity)));
}
static void ringLine(float x1,float y1,float x2,float y2,uint64_t color) {
  if(x1<-640||x1>1280||x2<-640||x2>1280||y1<-448||y1>896||y2<-448||y2>896) return;
  gsKit_prim_line(gsGlobal,ringSX(x1),ringSY(y1),ringSX(x2),ringSY(y2),3,color);
}
static RingPoint ringWorld(float x,float y,float z) {
  float depth=600-z;
  float scale=500/fmaxf(64,depth);
  return (RingPoint){320+x*scale+ringViewOffsetX,224-y*scale*(448.0f/480)+ringViewOffsetY,z,scale,depth>64};
}
static RingPoint ringProject(float angle,float radius,const RingMenu *menu,int decoration,float visualZ) {
  /* A rotating item pivot owns (0,radius,0); the visible plane is placed
   * at its world position and remains front-facing, as in 0x1e098. */
  RingVec p=ringTransform(-radius*sinf(angle),radius*cosf(angle),visualZ,menu,decoration);
  return ringWorld(p.x,p.y,p.z);
}
static void ringText(float cx,float y,float width,float height,uint64_t color,const char *text) {
  float nativeWidth=getLineWidth(text);
  if(nativeWidth<=0) return;
  /* Width is a ceiling, never a target that stretches shorter words. Keep
   * one scale for both axes and align the line to the physical pixel grid. */
  float linePixels=roundf(ringSY(height));
  float scale=fminf(linePixels/getFontLineHeight(),ringSX(width)/nativeWidth);
  int left=(int)roundf(ringSX(cx)-nativeWidth*scale*.5f);
  drawTextLineScaledCrisp(left,(int)roundf(ringSY(y)),(int)ringSX(638),4,
      scale,color,text);
}
#include "ring_lettering.inc"
static void ringBracket(float x,float y,float width,float height,int side) {
  float edge=x+side*width*.5f,cap=x-side*width*.5f;
  uint64_t color=ringColor(191,191,191,105);
  /* Authored outline of the inspected 14x28 waku planes, including both caps. */
  ringLine(cap,y-height*.5f,edge-side*1.5f,y-height*.5f,color);
  ringLine(edge-side*1.5f,y-height*.5f,edge,y-height*.5f+1.5f,color);
  ringLine(edge,y-height*.5f+1.5f,edge,y+height*.5f-1.5f,color);
  ringLine(edge,y+height*.5f-1.5f,edge-side*1.5f,y+height*.5f,color);
  ringLine(edge-side*1.5f,y+height*.5f,cap,y+height*.5f,color);
}
#include "ring_ribbon.inc"
#include "ring_effect_draw.inc"
static void ringLayoutInit(const RingMenu *menu) {
  /* Center the projected ring, including its perspective tilt. A common
   * screen translation keeps every label, orb and effect attached without
   * changing their world positions, sizes, movement or animation clocks. */
  ringViewOffsetX=ringViewOffsetY=0;
  float left=100000,right=-100000,top=100000,bottom=-100000;
  int visible=0;
  for(int j=0;j<RING_RIBBON_SEGMENTS;j++) {
    float angle=j*RING_TAU/RING_RIBBON_SEGMENTS;
    RingVec world=ringTransform(-238.5f*sinf(angle),238.5f*cosf(angle),0,menu,1);
    RingPoint point=ringWorld(world.x,world.y,world.z);
    if(!point.visible)continue;
    left=fminf(left,point.x);right=fmaxf(right,point.x);
    top=fminf(top,point.y);bottom=fmaxf(bottom,point.y);visible++;
  }
  if(visible) {
    ringViewOffsetX=320-(left+right)*.5f;
    ringViewOffsetY=224-(top+bottom)*.5f;
  }
}
static float ringTitleHeight(float size) {
  if(lunaLanguage()!=LUNA_LANGUAGE_ENGLISH)return fmaxf(16,20*size*448/480);
  return fmaxf(24,36*size*448/480);
}
static void ringDraw(RingMenu *menu,int hasLibrary,const char *status) {
  const int previousTextLayout=setAuthoredTextLayout(1);
  static const char *labels[]={"Game Library","File Browser","Appearance","Settings"};
  static const char *labelKeys[]={"Return to Library","File Explorer","Appearance","System Configuration"};
  static const char *details[]={"Your games, ready to play.","Explore connected devices.","Customize your library.","Make LUNA your own."};
  static const float widths[]={280,261,220,220};
  static const float slotAlpha[]={1,.4f,.3f,.3f};
  gsKit_clear(gsGlobal,BGColor); gsKit_TexManager_nextFrame(gsGlobal);
  GSTEST savedTest=*gsGlobal->Test;
  uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF); gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  ringOpacity=1;
  gsKit_prim_sprite(gsGlobal,0,0,gsGlobal->Width,gsGlobal->Height,1,ringColor(0,2,18,75));
  /* Modest branding rather than the previous dashboard heading/cards. */
  drawLunaLogo(ringSX(320),ringSY(15),ringSX(78),4,ringColor(179,198,216,128));
  if(status) ringText(320,43,400,18,ringColor(215,229,244,128),status);
  float ringAlpha=ringClamp(menu->entry/1000)*(1-ringClamp((menu->exit-1000)/1000));
  RingVec center=ringTransform(0,0,0,menu,1);
  RingVec u=ringSubtract(ringTransform(1,0,0,menu,1),center);
  RingVec v=ringSubtract(ringTransform(0,1,0,menu,1),center);
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,2,0,1,0),0);
  ringRibbon(center,u,v,500,ringAlpha,1);
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  ringDrawEffects(menu);
  RingPoint positions[RING_ITEM_COUNT]; int order[RING_ITEM_COUNT];
  for(int i=0;i<RING_ITEM_COUNT;i++) {
    order[i]=i;positions[i]=ringProject(menu->angle[i],250,menu,0,0);
    float size=positions[i].scale*(i==menu->selected?1.1f:1);
    positions[i].y+=.5f*sinf(menu->phase+i)*size;
  }
  for(int i=0;i<RING_ITEM_COUNT-1;i++) for(int j=i+1;j<RING_ITEM_COUNT;j++) if(positions[order[i]].z>positions[order[j]].z){int t=order[i];order[i]=order[j];order[j]=t;}
  for(int pass=0;pass<RING_ITEM_COUNT;pass++) {
    int i=order[pass],slot=(i-menu->selected+RING_ITEM_COUNT)%RING_ITEM_COUNT,active=slot==0;
    RingPoint p=positions[i]; if(!p.visible) continue;
    float appearance=ringClamp((menu->entry-1500)/1000)*(1-ringClamp(menu->exit/600));
    /* Show the new title during the effects instead of waiting for their
     * 1800 ms decorative hold: fade back in from 500 to 700 ms. */
    if(menu->moveAge<700) appearance*=menu->moveAge<200?1-menu->moveAge/200:ringClamp((menu->moveAge-500)/200);
    ringOpacity=appearance*slotAlpha[slot]*(ringCanOpen(i,hasLibrary)?1:.4f);
    float size=p.scale*(active?1.1f:1);
    float width=widths[slot]*size,height=ringTitleHeight(size);
    const char *label=lunaText(labelKeys[i]);
    int titleItem=lunaLanguage()==LUNA_LANGUAGE_ENGLISH?i:-1;
    if(titleItem>=0) label=labels[i];
    /* Front-facing floating text, yellow at the focal slot; no cards/icons. */
    ringTitle(titleItem,p.x+1,p.y+1,width,height,ringColor(0,0,16,100),label);
    ringTitle(titleItem,p.x,p.y,width,height,active?ringColor(255,255,0,128):ringColor(235,239,248,128),label);
  }
  /* Use the same projected/bobbing point as the selected title, so its
   * description follows it throughout navigation instead of the screen. */
  RingPoint info=positions[menu->selected];
  float titleHeight=ringTitleHeight(info.scale*1.1f);
  int captionTop=(int)roundf(ringSY(info.y+titleHeight+10));
  float captionCenter=roundf(ringSX(info.x));
  const char *description=lunaText(!ringCanOpen(menu->selected,hasLibrary)?"No games found":details[menu->selected]);
  float captionWidth=getLineWidth(description);
  float captionHeight=getFontLineHeight();
  /* Keep this supporting line at its native atlas size: the old 165x14
   * box shrank whole sentences, losing strokes and unevenly sampling them. */
  float bracketHalfWidth=(captionWidth*.5f+14)/ringSX(1);
  float bracketY=(captionTop+captionHeight*.5f)/ringSY(1);
  float bracketHeight=(captionHeight+8)/ringSY(1);
  float infoAge=fminf(menu->effectAge-2550,menu->moveAge-1800);
  float infoFade=(1-ringClamp(menu->exit/500))*(info.visible?1:0);
  float blink=fmodf(fmaxf(0,infoAge-600),80)/40;
  float textAlpha=infoAge<600?0:infoAge<1000?(blink<1?1-blink:blink-1):1;
  ringOpacity=textAlpha*infoFade;
  if(textAlpha*infoFade>0)
    drawTextLineScaledCrisp((int)roundf(captionCenter-captionWidth*.5f),captionTop,
        (int)ringSX(638),4,1,ringColor(235,242,255,128),description);
  if(infoAge>=0 && infoFade>0) {
    static const float target[]={1,-.9f,-.2f,-.9f,-.2f,-.9f,1};
    float right=1;
    if(infoAge<600) {
      int stage=(int)(infoAge/100);
      right=target[stage]+(target[stage+1]-target[stage])*(infoAge-stage*100)/100;
    }
    ringOpacity=infoFade;
    ringBracket(info.x-bracketHalfWidth,bracketY,8,bracketHeight,-1);
    ringBracket(info.x+bracketHalfWidth*right,bracketY,8,bracketHeight,1);
  }
  ringOpacity=1;
  static MenuPromptTransition footerTransition;
  ButtonPrompt prompts[]={{ICON_DPAD,lunaText("Navigate")},{ICON_CROSS,lunaText("Open")},{ICON_CIRCLE,lunaText("Back")}};
  drawMenuPromptBar(&footerTransition,(int)ringSX(100),(int)ringSY(413),(int)ringSX(540),(int)ringSY(442),4,
      HeaderTextColor,(PromptBar){NULL,prompts,3},menu->closing?1-ringClamp(menu->exit/2000):ringClamp(menu->entry/600));
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe); gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest; gsKit_set_test(gsGlobal,GS_ZTEST_ON);
  setAuthoredTextLayout(previousTextLayout);
  gsKit_queue_exec(gsGlobal); gsKit_finish(); gsKit_sync_flip(gsGlobal); usleep(1000);
}
