#include "ui/animation.h"
#include <string.h>

#define CINEMATIC_PROFILE {{4,4,4,4,4,4,4,4,4,4,4},2,2,1}
#define INSTANT_PROFILE {{1,1,1,1,1,1,LUNA_ANIMATION_ORIGINAL,1, \
                          LUNA_ANIMATION_ORIGINAL,1,LUNA_ANIMATION_ORIGINAL},2,3,1}
static const LunaAnimations defaults = {0,{CINEMATIC_PROFILE,INSTANT_PROFILE}};
static LunaAnimations active = {0,{CINEMATIC_PROFILE,INSTANT_PROFILE}};
static const unsigned percentages[] = {0,25,50,75,100,150,200};
static struct { uint32_t real, visual, remainder; int initialized; } clocks[LUNA_ANIM_CLOCK_COUNT];

void lunaAnimationDefaults(LunaAnimations *settings) { *settings=defaults; }
void lunaAnimationResetProfile(LunaAnimations *settings,unsigned profile) {
  if(profile<LUNA_ANIMATION_PROFILE_COUNT)settings->profiles[profile]=defaults.profiles[profile];
}
static const LunaAnimationProfile *profile(void) { return &active.profiles[active.mode]; }
unsigned lunaAnimationPercent(LunaAnimation animation) {
  // Older Appearance records still contain overrides for these timings.
  if ((unsigned)animation >= LUNA_ANIM_COUNT || animation == LUNA_ANIM_LIBRARY_MOVE ||
      animation == LUNA_ANIM_ARTWORK || animation == LUNA_ANIM_STARTUP)
    return 100;
  return percentages[profile()->timing[animation]];
}
int lunaAnimationInstant(LunaAnimation animation) { return !lunaAnimationPercent(animation); }
uint32_t lunaAnimationDuration(LunaAnimation animation,uint32_t original) {
  unsigned percent=lunaAnimationPercent(animation);
  if(!percent||!original)return 0;
  uint64_t duration=(uint64_t)original*percent/100;
  return duration>UINT32_MAX?UINT32_MAX:duration?duration:1;
}
uint32_t lunaAnimationElapsed(LunaAnimation animation,uint32_t elapsed) {
  unsigned percent=lunaAnimationPercent(animation);
  if(!percent)return 0x3fffffffU;
  uint64_t scaled=(uint64_t)elapsed*100/percent;
  return scaled>0x3fffffffU?0x3fffffffU:(uint32_t)scaled;
}
float lunaAnimationStep(LunaAnimation animation,float elapsed,float endpoint) {
  unsigned percent=lunaAnimationPercent(animation);
  if(percent==100)return elapsed;
  return percent?elapsed*100/percent:endpoint;
}
uint32_t lunaAnimationNow(LunaAnimationClock clock,uint32_t now) {
  if (clock == LUNA_ANIM_CLOCK_BACKGROUND || (unsigned)clock >= LUNA_ANIM_CLOCK_COUNT)
    return now;
  unsigned rate=100;
  if(clock==LUNA_ANIM_CLOCK_EFFECT)rate=profile()->effects==3?25:profile()->effects?100:0;
  else {
    unsigned duration=lunaAnimationPercent(LUNA_ANIM_ARTWORK);
    rate=duration?10000/duration:0;
  }
  if(!clocks[clock].initialized) {
    clocks[clock].real=clocks[clock].visual=now;
    clocks[clock].initialized=1;
  }
  uint64_t advance=(uint64_t)(now-clocks[clock].real)*rate+clocks[clock].remainder;
  clocks[clock].visual+=(uint32_t)(advance/100);
  clocks[clock].remainder=(uint32_t)(advance%100);
  clocks[clock].real=now;
  return clocks[clock].visual;
}
float lunaAnimationEffects(void) { return profile()->effects==3?.25f:profile()->effects==2?1:profile()->effects==1?.35f:0; }
int lunaAnimationBlink(void) { return profile()->blink!=0; }
void lunaAnimationApply(const LunaAnimations *settings,uint32_t now) {
  if(!settings||settings->mode>=LUNA_ANIMATION_PROFILE_COUNT)return;
  for(unsigned p=0;p<LUNA_ANIMATION_PROFILE_COUNT;p++) {
    if(settings->profiles[p].background>3||settings->profiles[p].effects>3||settings->profiles[p].blink>1)return;
    for(unsigned i=0;i<LUNA_ANIM_COUNT;i++)if(settings->profiles[p].timing[i]>=LUNA_ANIMATION_TIMING_COUNT)return;
  }
  if(!memcmp(&active,settings,sizeof(active)))return;
  for(int i=0;i<LUNA_ANIM_CLOCK_COUNT;i++)lunaAnimationNow((LunaAnimationClock)i,now);
  active=*settings;
}
