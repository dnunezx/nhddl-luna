/* Circular Start menu with authored lettering and effects. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "ui/ring_menu.h"

#define RING_PI 3.14159265359f
#define RING_TAU (2.0f * RING_PI)
static LunaRingColor ringColorPreset=LUNA_RING_ORIGINAL;
void uiRingSetColor(LunaRingColor color) {
  ringColorPreset=color>=LUNA_RING_ORIGINAL&&color<LUNA_RING_COLOR_COUNT?color:LUNA_RING_ORIGINAL;
}
LunaRingColor uiRingGetColor(void) {return ringColorPreset;}
#define RING_ITEM_COUNT 4
/* Preserve the three recovered positions; the fourth is a LUNA extension. */
static const float ringSlots[RING_ITEM_COUNT] = {-2.28f, 0, 1.3f, 2.4f};
static const int ringActions[RING_ITEM_COUNT] = {
  LUNA_RING_LIBRARY,LUNA_RING_BROWSER,LUNA_MENU_APPEARANCE,LUNA_RING_SETTINGS
};
static int ringCanOpen(int selected,int hasLibrary) {
  return hasLibrary || ringActions[selected]!=LUNA_RING_LIBRARY;
}
#define RING_PARTICLE_COUNT 256
#define RING_WAVE_COUNT 64
typedef struct { float x,y,z; } RingVec;
typedef struct { RingVec position,velocity,tint; float age,life,size,alpha; int active; } RingParticle;
typedef struct { float size,alpha; RingVec tint; } RingOrbLight;
typedef struct { RingVec center,u,v; float age,life,size,factor,alpha; int active; } RingWave;
typedef struct {
  float angle[RING_ITEM_COUNT], from[RING_ITEM_COUNT], target[RING_ITEM_COUNT], age[RING_ITEM_COUNT], duration[RING_ITEM_COUNT];
  float progress[RING_ITEM_COUNT], finalTarget[RING_ITEM_COUNT];
  int stage[RING_ITEM_COUNT];
  float entry, exit, phase, ticks;
  float effectAge, moveAge, pulseAge;
  float chargeProgress[3];
  RingVec chargePosition[RING_ITEM_COUNT][4];
  RingParticle particles[RING_PARTICLE_COUNT];
  RingWave waves[RING_WAVE_COUNT];
  RingVec bulletFrom,bulletPosition,bulletTo;
  float bulletAge,bulletTrail,bulletSinceHit,bulletPulseAge,bulletDuration;
  int waveCursor,entryWaves,moveWaves,bulletStarted,bulletTarget,bulletPulsing,bulletDying,bulletDeathWaves;
  uint32_t random;
  int particleCursor, burstDone, moveEffect;
  uint32_t lastMs;
  int selected, closing;
} RingMenu;

static float ringClamp(float x) { return fminf(1, fmaxf(0, x)); }
static float ringEase(float x) { x = ringClamp(x); return 1 - (1-x)*(1-x)*(1-x); }
#include "ring_effect_model.inc"
static void ringInit(RingMenu *menu, uint32_t now, int selected) {
  memset(menu, 0, sizeof(*menu));
  menu->selected = selected;
  menu->lastMs = now;
  menu->random = 0x4c554e41U;
  menu->moveAge = 10000;
  menu->pulseAge = 10000;
  menu->moveWaves = 3;
  menu->bulletDuration = 2000;
  for (int i=0; i<RING_ITEM_COUNT; ++i) menu->angle[i] = menu->from[i] = menu->target[i] = ringSlots[(i-selected+RING_ITEM_COUNT)%RING_ITEM_COUNT];
}
static void ringStep(RingMenu *menu, uint32_t now) {
  uint32_t elapsed = now-menu->lastMs;
  menu->lastMs = now;
  if (elapsed>250) return;
  menu->entry = fminf(5000, menu->entry+elapsed);
  menu->phase += elapsed * 0.0012f;
  if (menu->phase>RING_TAU) menu->phase-=RING_TAU;
  if (menu->closing) menu->exit = fminf(2000, menu->exit+elapsed);
  ringEffectsStep(menu,elapsed);
  menu->ticks += elapsed * 0.06f;
  while(menu->ticks>=1) {
    menu->ticks-=1;
    for (int i=0; i<RING_ITEM_COUNT; ++i) {
      if(menu->duration[i]<=0) continue;
      menu->age[i]+=1000.0f/60;
      if(menu->stage[i]==1) menu->progress[i]=ringClamp(menu->age[i]/menu->duration[i]);
      else {
        /* Stateful exponent progress; angle interpolation is linear in that
         * progress, unlike the separate positional Hermite helper. */
        float seconds=menu->duration[i]*.001f;
        float frames=menu->duration[i]*.06f;
        float coefficient=1.0f/((frames-5)*.33333298564f+2.20000004768f);
        float time=menu->age[i]/menu->duration[i];
        float threshold=1.0f/(seconds*3+1);
        float base=fminf(seconds*.5f,1);
        float phase=time<threshold?base+time*(1-base)/threshold:1;
        menu->progress[i]+=(1-menu->progress[i])*coefficient*phase;
        if(menu->progress[i]>.998f) menu->progress[i]=1;
      }
      menu->angle[i]=menu->from[i]+(menu->target[i]-menu->from[i])*menu->progress[i];
      if(menu->progress[i]>=1 && menu->stage[i]==1) {
        menu->from[i]=menu->angle[i]; menu->target[i]=menu->finalTarget[i];
        menu->duration[i]=200; menu->age[i]=menu->progress[i]=0; menu->stage[i]=2;
      }
    }
  }
}
static void ringMove(RingMenu *menu, int direction) {
  if (menu->closing) return;
  menu->selected = (menu->selected+direction+RING_ITEM_COUNT)%RING_ITEM_COUNT;
  menu->moveAge = menu->pulseAge = 0;
  menu->moveEffect = 1;
  menu->moveWaves = 0;
  ringWave(menu,ringTransform(0,0,0,menu,0),1,500,2,300,1,0);
  for (int i=0; i<RING_ITEM_COUNT; ++i) {
    float delta = ringSlots[(i-menu->selected+RING_ITEM_COUNT)%RING_ITEM_COUNT]-menu->angle[i];
    if(direction>0) { while(delta>0) delta-=RING_TAU; while(delta<-RING_TAU) delta+=RING_TAU; }
    else { while(delta<0) delta+=RING_TAU; while(delta>RING_TAU) delta-=RING_TAU; }
    /* Retarget from the displayed position, including rapid reversals. */
    menu->from[i] = menu->angle[i];
    menu->finalTarget[i]=menu->angle[i]+delta;
    menu->stage[i]=fabsf(delta)>RING_PI?1:0;
    menu->target[i]=menu->angle[i]+delta*(menu->stage[i]?0.5f:1);
    menu->duration[i]=menu->stage[i]?250:500;
    menu->age[i]=menu->progress[i]=0;
  }
}

#include "ui/view_internal.h"
#include "ui/pad.h"
#include "ui/language.h"
#include <libpad.h>
#include <dmaKit.h>
#include <unistd.h>

#include "ring_scene.inc"
int uiRingMenuLoop(int hasLibrary, const char *status) {
  /* Bounded effect pool stays off the nested library/menu thread's stack. */
  static RingMenu menu;
  ringInit(&menu,uiNowMs(),hasLibrary?0:1);
  ringLayoutInit(&menu);
  ringLetteringInit();
  ringEffectsInit();
  int previous=pollInput(), destination=LUNA_RING_LIBRARY;
  for(;;) {
    ringStep(&menu,uiNowMs());
    ringDraw(&menu,hasLibrary,status);
    int held=pollInput(),input=held&~previous;
    previous=held;
    if(menu.closing) {
      if(menu.exit>=2000) { ringEffectsClose(); ringLetteringClose(); return destination; }
      continue;
    }
    if((input&(PAD_CIRCLE|PAD_TRIANGLE|PAD_START))&&hasLibrary) {
      destination=LUNA_RING_LIBRARY; menu.closing=1;
    } else if(input&(PAD_LEFT|PAD_UP|PAD_L1)) ringMove(&menu,-1);
    else if(input&(PAD_RIGHT|PAD_DOWN|PAD_R1)) ringMove(&menu,1);
    else if((input&PAD_CROSS)&&ringCanOpen(menu.selected,hasLibrary)) {
      destination=ringActions[menu.selected]; menu.closing=1;
    }
  }
}
