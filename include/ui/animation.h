#ifndef LUNA_UI_ANIMATION_H
#define LUNA_UI_ANIMATION_H

#include <stdint.h>

typedef enum { LUNA_ANIMATION_CINEMATIC, LUNA_ANIMATION_INSTANT,
               LUNA_ANIMATION_PROFILE_COUNT } LunaAnimationMode;
typedef enum {
  LUNA_ANIM_START_ENTRY, LUNA_ANIM_START_EXIT, LUNA_ANIM_START_MOVE,
  LUNA_ANIM_MENU_ENTRY, LUNA_ANIM_MENU_EXIT, LUNA_ANIM_MENU_MOVE,
  LUNA_ANIM_LIBRARY_MOVE, LUNA_ANIM_VIEW_CHANGE, LUNA_ANIM_ARTWORK,
  LUNA_ANIM_DIALOG, LUNA_ANIM_STARTUP, LUNA_ANIM_COUNT
} LunaAnimation;
enum { LUNA_ANIMATION_TIMING_COUNT = 7, LUNA_ANIMATION_ORIGINAL = 4 };
typedef struct {
  uint32_t timing[LUNA_ANIM_COUNT];
  uint32_t background, effects, blink;
} LunaAnimationProfile;
typedef struct {
  uint32_t mode;
  LunaAnimationProfile profiles[LUNA_ANIMATION_PROFILE_COUNT];
} LunaAnimations;
typedef enum { LUNA_ANIM_CLOCK_BACKGROUND, LUNA_ANIM_CLOCK_EFFECT,
               LUNA_ANIM_CLOCK_ARTWORK, LUNA_ANIM_CLOCK_COUNT } LunaAnimationClock;

void lunaAnimationDefaults(LunaAnimations *settings);
void lunaAnimationResetProfile(LunaAnimations *settings, unsigned profile);
void lunaAnimationApply(const LunaAnimations *settings, uint32_t now);
unsigned lunaAnimationPercent(LunaAnimation animation);
int lunaAnimationInstant(LunaAnimation animation);
uint32_t lunaAnimationDuration(LunaAnimation animation, uint32_t original);
uint32_t lunaAnimationElapsed(LunaAnimation animation, uint32_t elapsed);
float lunaAnimationStep(LunaAnimation animation, float elapsed, float endpoint);
uint32_t lunaAnimationNow(LunaAnimationClock clock, uint32_t now);
float lunaAnimationEffects(void);
int lunaAnimationBlink(void);

#endif
