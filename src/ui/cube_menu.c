/* Shared category menus with authored crystal rendering and portal effects. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "ui/cube_menu.h"
#define CUBE_PI 3.14159265359f
#define CUBE_ITEMS 6
#define CUBE_COUNT CUBE_ITEMS
#define CUBE_SIDE 56.0f
#define CUBE_SIZE_RATIO (CUBE_SIDE/40.0f)
typedef struct { float x,y,z; } CubeVec;
typedef struct {
  float entry,exit,time,moveAge,rowFrom,row;
  float light[CUBE_COUNT],lightFrom[CUBE_COUNT],lightTarget[CUBE_COUNT];
  float lightAge[CUBE_COUNT],lightDuration[CUBE_COUNT];
  float phase[CUBE_COUNT],spinProgress[CUBE_COUNT];
  float motionRemainder,motionFps;
  unsigned spinning;
  CubeVec cameraTarget;
  uint32_t lastMs;
  int selected,closing,entering;
  int itemCount;
  float submenuAge;
  int submenuOpen,submenuDirection;
} CubeMenu;
enum { CUBE_SUBMENU_MS = 1440 };
static float cubeClamp(float v) { return fmaxf(0,fminf(1,v)); }
static float cubeSmooth(float t) {t=cubeClamp(t);return t*t*(3-2*t);}
static float cubeSin(float degrees) { return sinf(degrees*CUBE_PI/180); }
static CubeVec cubePosition(int i,float seconds) {
  float t=seconds*30,x=0,y=0,z=0;
  /* Six independent PSBBN-derived paths, spaced for larger LUNA crystals. */
  switch(i) {
    case 0:x=5.8f;y=3;z=-5.4f+1.5f*cubeSin(30+.1f*t);break;
    case 1:x=4+cubeSin(135+.2f*t);y=5;z=6.5f+cubeSin(60+.2f*t);break;
    case 2:x=3+cubeSin(-.1f*t);y=1.8f;z=2+cubeSin(90-.1f*t);break;
    case 3:x=11+2*cubeSin(.1f*t-130);y=.3f;z=18+2*cubeSin(30-.08f*t);break;
    case 4:x=4+cubeSin(.1f*t);y=-5.5f;z=13+3*cubeSin(.1f*t+90);break;
    case 5:x=9+1.2f*cubeSin(.1f*t+180);y=-5.7f-.6f*cubeSin(-.1f*t-30);z=6+3*cubeSin(.1f*t+270);break;
  }
  return (CubeVec){-25*x,25*y,25*z};
}
static CubeVec cubeRotation(int i,float seconds) {
  float t=seconds*30,x=0,y=0,z=0;
  switch(i) {
    case 0:x=.05f*t;y=-.15f*t;z=.1f*t;break;
    case 1:x=.05f*t;y=-.1f*t;z=45+.1f*t;break;
    case 2:x=45-.1f*t;y=.15f*t;z=.1f*t;break;
    case 3:x=.1f*t;y=45+.15f*t;z=-.05f*t;break;
    case 4:x=30+.1f*t;y=-.05f*t;z=.1f*t;break;
    case 5:x=.1f*t;y=.142f*t;z=.05f*t;break;
  }
  return (CubeVec){x*CUBE_PI/180,y*CUBE_PI/180,z*CUBE_PI/180};
}
/* top_list 0x883c/0xb708/0xb804: selection advances the existing motion
 * timer by 1000 times the change in native mode-6 progress. Evaluate its
 * stateful recurrence at the video cadence, retaining fractional ticks. */
static void cubeMotionStep(CubeMenu *m,float milliseconds) {
  float tick=1000/m->motionFps,seconds=1/m->motionFps;
  float coefficient=1/((3*m->motionFps-5)*.333333f+2.2f);
  m->motionRemainder+=milliseconds;
  while(m->motionRemainder>=tick) {
    m->motionRemainder-=tick;
    for(int i=0;i<CUBE_COUNT;i++) {
      float boost=0;
      if(m->spinning&(1U<<i)) {
        float previous=m->spinProgress[i];
        /* Native 3000 ms exponent startup clamps its ramp factor to one. */
        m->spinProgress[i]+=(1-previous)*coefficient;
        boost=(m->spinProgress[i]-previous)*1000;
        if(boost<.5f) {m->spinning&=~(1U<<i);boost=0;}
      }
      m->phase[i]+=seconds*(1+boost);
    }
  }
}
static void cubeSpin(CubeMenu *m) {
  m->spinProgress[m->selected]=0;
  m->spinning|=1U<<m->selected;
}
static void cubeInit(CubeMenu *m,uint32_t now,int selected,float fps) {
  memset(m,0,sizeof(*m));m->lastMs=now;m->selected=selected;
  m->itemCount=CUBE_ITEMS;
  m->motionFps=fps;
  m->row=m->rowFrom=selected;m->moveAge=1000;
  for(int i=0;i<CUBE_COUNT;i++) {m->lightTarget[i]=i==selected;m->lightDuration[i]=150;}
  cubeSpin(m);
}
static void cubeMove(CubeMenu *m,int dir) {
  if(m->closing)return;
  m->selected=(m->selected+dir+m->itemCount)%m->itemCount;
  m->rowFrom=m->row;m->moveAge=0;
  cubeSpin(m);
  for(int i=0;i<CUBE_COUNT;i++) {
    m->lightFrom[i]=m->light[i];m->lightTarget[i]=i==m->selected%CUBE_COUNT;
    m->lightAge[i]=0;m->lightDuration[i]=i==m->selected?150:200;
  }
}
static void cubeClose(CubeMenu *m,int entering) {
  m->closing=1;m->entering=entering;m->exit=0;
  CubeVec p=cubePosition(m->selected,m->phase[m->selected]);
  /* camera.bnx: lateral offset 96, vertical -4, near destination z+260.
   * Camera/root handedness is expressed in our projection coordinates. */
  m->cameraTarget=(CubeVec){-p.x-96*CUBE_SIZE_RATIO,p.y-4*CUBE_SIZE_RATIO,
      500+p.z-260*CUBE_SIZE_RATIO};
}
static void cubeStep(CubeMenu *m,uint32_t now) {
  uint32_t dt=now-m->lastMs;m->lastMs=now;
  if(dt>250)return;
  m->entry=fminf(500,m->entry+dt);
  if(m->closing)m->exit+=dt;
  else if(!m->submenuOpen) {
    m->time+=dt*.001f;
    cubeMotionStep(m,dt);
  }
  if(m->submenuDirection) {
    m->submenuAge=fmaxf(0,fminf(CUBE_SUBMENU_MS,
        m->submenuAge+(float)dt*m->submenuDirection));
    if(m->submenuAge==0||m->submenuAge==CUBE_SUBMENU_MS) {
      m->submenuDirection=0;
      if(m->submenuAge==0)m->submenuOpen=0;
    }
  }
  m->moveAge=fminf(150,m->moveAge+dt);
  float u=cubeClamp(m->moveAge/150);
  m->row=m->rowFrom+(m->selected-m->rowFrom)*u;
  for(int i=0;i<CUBE_COUNT;i++) {
    m->lightAge[i]+=dt;
    float a=cubeClamp(m->lightAge[i]/m->lightDuration[i]);
    m->light[i]=m->lightFrom[i]+(m->lightTarget[i]-m->lightFrom[i])*a;
  }
}
#include "ui/view_internal.h"
#include "ui/pad.h"
#include "ui/language.h"
#include <libpad.h>
#include <dmaKit.h>
#include <unistd.h>
static int cubeBackdropOnly;
static int cubePageSceneOnly;
#include "cube_draw.inc"
static CubeMenu *activeCubeMenu;
static const LunaCubeHub *activeCubeHub;
static const LunaCubeSubmenu *activeCubeSubmenu;
static const char *activeCubeStatus;
static int activeCubeConfirm;
#include "cube_quick_glass.inc"

void uiCubeHubDrawBackdrop(void) {
  int saved=cubeBackdropOnly;
  cubeBackdropOnly=1;
  if(activeCubeMenu)cubeDraw(activeCubeMenu,activeCubeStatus,activeCubeConfirm,
                             activeCubeHub,activeCubeSubmenu);
  cubeBackdropOnly=saved;
}

void uiCubeHubPageDraw(const LunaCubePage *page,int selected,LunaCubePageFocus *focus) {
  int saved=cubePageSceneOnly;
  cubePageSceneOnly=1;
  uiCubeHubDrawBackdrop();
  cubePageSceneOnly=saved;
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  uiCubeSubmenuPageDraw(page,selected,focus,1);
}

static void cubeHubClose(void) {
  activeCubeMenu=NULL;activeCubeHub=NULL;activeCubeSubmenu=NULL;
  cubeMaterialClose();
}

int uiCubeHubLoop(int *selected,const char *status,const LunaCubeHub *hub) {
  static CubeMenu menu;
  if(!hub||!hub->labels||hub->count<1||hub->count>CUBE_COUNT)return -1;
  uiCubePanelGlassClose();
  int initial=*selected;
  if(initial<0||initial>=hub->count)initial=0;
  cubeInit(&menu,uiNowMs(),initial,gsGlobal->Mode==GS_MODE_PAL?50:60);
  menu.itemCount=hub->count;*selected=initial;
  cubeMaterialInit();
  int previous=pollInput(),destination=-1,confirm=0;
  const LunaCubeSubmenu *submenu=NULL;
  for(;;) {
    int wasSubmenuOpen=menu.submenuOpen;
    cubeStep(&menu,uiNowMs());
    if(wasSubmenuOpen&&!menu.submenuOpen) {
      if(submenu->end)submenu->end(submenu->context);
      submenu=NULL;
    }
    activeCubeMenu=&menu;activeCubeHub=hub;activeCubeSubmenu=submenu;
    activeCubeStatus=status;activeCubeConfirm=confirm;
    int sceneReady=cubeDraw(&menu,status,confirm,hub,submenu);
    gsKit_queue_exec(gsGlobal);gsKit_finish();gsKit_sync_flip(gsGlobal);usleep(1000);
    int held=pollInput(),input=held&~previous;previous=held;
    if(menu.submenuOpen) {
      if(menu.submenuDirection)continue;
      int result=submenu->input(submenu->context,input);
      /* Resample after actions that may open a blocking dialog. Idle frames
       * keep their original sample so a new press cannot be swallowed. */
      if(input)previous=pollInput();
      if(result==LUNA_CUBE_SUBMENU_BACK)menu.submenuDirection=-1;
      else if(result==LUNA_CUBE_SUBMENU_DONE) {
        if(submenu->end)submenu->end(submenu->context);
        cubeHubClose();return menu.selected;
      }
      continue;
    }
    if(menu.closing) {
      if(menu.exit>=(menu.entering?1440:500)&&sceneReady) {cubeHubClose();return destination;}
      continue;
    }
    if(hub->input) {
      int result=hub->input(hub->context,input);
      if(input)previous=pollInput();
      if(result==LUNA_CUBE_SUBMENU_DONE) {cubeHubClose();return menu.selected;}
    }
    if(confirm) {
      if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))confirm=0;
      else if(input&PAD_CROSS) {destination=menu.selected;cubeClose(&menu,1);}
    } else if(input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START)) {
      destination=-1;cubeClose(&menu,0);
    } else if(input&(PAD_UP|PAD_LEFT|PAD_L1))cubeMove(&menu,-1);
    else if(input&(PAD_DOWN|PAD_RIGHT|PAD_R1))cubeMove(&menu,1);
    else if(input&PAD_CROSS) {
      if(menu.selected==hub->confirmRow)confirm=1;
      else if(menu.selected<hub->submenuCount&&hub->submenus&&
              hub->submenus[menu.selected].draw&&hub->submenus[menu.selected].input) {
        submenu=&hub->submenus[menu.selected];
        if(submenu->begin)submenu->begin(submenu->context);
        menu.submenuOpen=1;menu.submenuDirection=1;menu.submenuAge=0;
      }
      else {destination=menu.selected;cubeClose(&menu,1);}
    }
    *selected=menu.selected;
  }
}

int uiCubeSettingsLoop(int *selected,const char *status,
                       const LunaCubeSubmenu submenus[4]) {
  static const char *const labels[]={"Memory Cards","Storage",
    "Language","Defaults","Exit LUNA","Power Off"};
  static const int actions[]={LUNA_CUBE_VMC,LUNA_CUBE_STORAGE,LUNA_CUBE_LANGUAGE,
                              LUNA_CUBE_DEFAULTS,LUNA_CUBE_EXIT,LUNA_CUBE_SHUTDOWN};
  const LunaCubeHub hub={.title="System Configuration",.labels=labels,.count=CUBE_ITEMS,
    .submenus=submenus,.submenuCount=4,.confirmRow=5,.confirmText="Power off the console?"};
  int row=uiCubeHubLoop(selected,status,&hub);
  return row<0?LUNA_CUBE_BACK:actions[row];
}
