#ifndef LUNA_OPTIONS_REVEAL_H
#define LUNA_OPTIONS_REVEAL_H

#include <stdint.h>
#include <math.h>
#ifdef LUNA_CAMERA_ANIMATIONS
#include "ui/luna_motion.h"
#endif
/* Retreat the library with the Game Options camera while Options appear.
 * Closing reverses the library motion from its current pose. */
#define LUNA_OPTIONS_PAN_MS 750U
typedef struct {
  float opacity, shade, fromOpacity, fromShade;
  float position, fromPosition;
  uint32_t startedMs, previousMs, remainder;
  int opening;
#ifdef LUNA_CAMERA_ANIMATIONS
  LunaMotion camera;
#endif
} LunaOptionsReveal;

static inline void lunaOptionsRevealStart(LunaOptionsReveal *r, int opening, uint32_t now) {
  r->opening=opening!=0;
  r->fromOpacity=r->opacity;
  r->fromShade=r->shade;
  r->fromPosition=r->position;
  r->startedMs=now;
  r->previousMs=now;
  r->remainder=0;
#ifdef LUNA_CAMERA_ANIMATIONS
  const float target[3]={0,r->opening ? 1 : 0,0};
  lunaMotionRetarget(&r->camera,target,LUNA_OPTIONS_PAN_MS,1);
#endif
}

static inline float lunaOptionsRevealEase(float t) {
  return t*t*(3-2*t);
}

static inline int lunaOptionsRevealTick(LunaOptionsReveal *r, uint32_t now) {
  uint32_t elapsed=now-r->startedMs;
  uint32_t delta=now-r->previousMs;
  r->previousMs=now;
  float destination=r->opening ? 1 : 0;
  float fade=fminf(1,elapsed/400.0f);
  uint32_t dimDuration=r->opening ? 500 : 300;
  float dim=fminf(1,(float)elapsed/dimDuration);
  r->opacity=r->fromOpacity+(destination-r->fromOpacity)*fade;
  r->shade=r->fromShade+(.3f*destination-r->fromShade)*dim;
#ifdef LUNA_CAMERA_ANIMATIONS
  // Preserve the Game Options camera's reference ticks and pause after long modal gaps.
  if (delta>250U) r->remainder=delta=0;
  r->remainder+=delta*60U;
  while (r->remainder>=1000U) {
    lunaMotionTick(&r->camera);
    r->remainder-=1000U;
  }
  r->position=r->camera.position[1];
#else
  (void)delta;
  float pan=lunaOptionsRevealEase(fminf(1,(float)elapsed/LUNA_OPTIONS_PAN_MS));
  r->position=r->fromPosition+(destination-r->fromPosition)*pan;
#endif
  if (elapsed>=LUNA_OPTIONS_PAN_MS && fabsf(destination-r->position)<.0005f) {
    r->opacity=destination;
    r->shade=.3f*destination;
    r->position=destination;
#ifdef LUNA_CAMERA_ANIMATIONS
    r->camera.position[1]=r->camera.previous[1]=destination;
    r->camera.active=0;
#endif
    return 1;
  }
  // Allow menu input once revealed while the camera keeps settling.
  return r->opening && elapsed>=500U;
}
#endif
