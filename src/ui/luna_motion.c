#include "ui/animation.h"
#include "ui/luna_motion.h"
#include <math.h>
#include <string.h>

static const float referenceTick = 1.0f / 60.0f;

void lunaMotionReset(LunaMotion *motion) {
  memset(motion, 0, sizeof(*motion));
}

void lunaMotionRetarget(LunaMotion *motion, const float target[3],
                           float durationMs, float historyScale) {
  durationMs=(float)lunaAnimationDuration(LUNA_ANIM_VIEW_CHANGE,(uint32_t)fmaxf(0,durationMs));
  if(!durationMs) {
    for(int axis=0;axis<3;axis++)motion->position[axis]=motion->previous[axis]=motion->start[axis]=motion->target[axis]=target[axis];
    motion->active=0;motion->progress=1;return;
  }
  const float requested = fmaxf(0.0f, durationMs) * 0.001f;
  int frames = (int)(requested * 60.0f);
  static const float shortCoefficients[] = {
      1.0f, 0.9615384936332703f, 0.78125f,
      0.6369426250457764f, 0.5291005373001099f};
  motion->duration = fmaxf(referenceTick,
      (float)((double)((float)frames * referenceTick) * 0.999999));
  motion->coefficient = frames < 5 ? shortCoefficients[frames] :
      1.0f / ((float)(frames - 5) * 0.33333298563957214f + 2.200000047683716f);
  motion->elapsed = motion->progress = 0.0f;
  motion->active = 1;
  for (int axis = 0; axis < 3; ++axis) {
    motion->start[axis] = motion->position[axis];
    motion->tangent[axis] = (motion->position[axis] - motion->previous[axis]) * historyScale;
    motion->target[axis] = target[axis];
  }
}

void lunaMotionTick(LunaMotion *motion) {
  if (!motion->active)
    return;
  motion->elapsed += referenceTick;
  const float time = motion->elapsed / motion->duration;
  const float threshold = 1.0f / (motion->duration * 3.0f + 1.0f);
  float phase = 1.0f;
  if (time < threshold) {
    const float base = fminf(motion->duration * 0.5f, 1.0f);
    phase = base + time * ((1.0f - base) / threshold);
  }
  motion->progress += (1.0f - motion->progress) * (motion->coefficient * phase);
  if (motion->progress > 1.0f)
    motion->progress = 1.0f;
  const float u = motion->progress, u2 = u * u, u3 = u2 * u;
  const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
  const float h01 = -2.0f * u3 + 3.0f * u2;
  const float h10 = u3 - 2.0f * u2 + u;
  for (int axis = 0; axis < 3; ++axis) {
    motion->previous[axis] = motion->position[axis];
    motion->position[axis] = h00 * motion->start[axis] +
        h01 * motion->target[axis] + h10 * motion->tangent[axis];
  }
  /* Retain the native-style asymptotic tail. Rendering rounds to GS subpixels. */
}
