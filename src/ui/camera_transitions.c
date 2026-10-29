#include "ui/camera_transitions.h"
#include "ui/graphics.h"
#include "ui/luna_motion.h"
#include <math.h>
#include <stddef.h>

static LunaMotion menuReturn;
static int menuReturning;
float lunaCameraDrawScale = 1.0f;
float lunaCameraDrawOpacity = 1.0f;
static int layerActive;
static float layerScale=1,layerOpacity=1,layerOffsetX,layerOffsetY;
static uint32_t lastMs, pendingTicks;
static int initialized, armed, applied, savedOffsetX, savedOffsetY;
static LunaMotion viewMotion;
static GSTEXTURE *viewCover;
static float sourceRect[4];
static int viewTarget = -1, viewReady;
static int viewSettled;
static uint32_t turnMs;
static int turnBack;
enum { TURN_HALF_MS = 350 };
static LunaMotion channelMotion;
static uint32_t channelAgeMs;
static int channelTarget = -1;

static void restoreCameraOffset(void) {
  lunaCameraDrawScale = 1.0f;
  lunaCameraDrawOpacity = 1.0f;
  if (applied) {
    gsGlobal->OffsetX = savedOffsetX;
    gsGlobal->OffsetY = savedOffsetY;
    applied = 0;
  }
}

void lunaCameraMenuReturnCancel(void) {
  menuReturning = 0;
  lunaMotionReset(&menuReturn);
  lunaCameraDrawScale = 1.0f;
}

void lunaCameraMenuReturnStart(uint32_t nowMs) {
  lunaCameraMenuReturnCancel();
  menuReturn.position[0] = menuReturn.previous[0] = 0.78f;
  const float destination[3] = {1.0f, 0, 0};
  lunaMotionRetarget(&menuReturn, destination, 750.0f, 1.0f);
  menuReturning = 1;
  // The first revealed frame starts far away, even after a long menu visit.
  lastMs = nowMs;
  pendingTicks = 0;
}

void lunaCameraChannelCancel(void) {
  channelTarget = -1;
  channelAgeMs = 0;
  lunaMotionReset(&channelMotion);
}

void lunaCameraChannelStart(int target) {
  lunaCameraChannelCancel();
  if (!LUNA_CAMERA_ORBIT_TRANSITION || target < 0) return;
  channelTarget = target;
  const float destination[3] = {1, 0, 0};
  lunaMotionRetarget(&channelMotion, destination, 750.0f, 1.0f);
}

void lunaCameraChannelCheck(int isOrbit, int target) {
  if (!isOrbit || target != channelTarget) lunaCameraChannelCancel();
}

int lunaCameraChannelActive(void) { return channelTarget >= 0; }
float lunaCameraChannelProgress(void) {
  return lunaCameraChannelActive()
      ? fminf(1.0f, fmaxf(0, channelMotion.position[0])) : 1;
}
float lunaCameraChannelBlend(void) {
  const float t = lunaCameraChannelActive() ? fminf(1.0f, channelAgeMs / 350.0f) : 1;
  return t * t * (3.0f - 2.0f * t);
}
void lunaCameraChannelPoint(float sourceX, float sourceY, float *x, float *y) {
  const float p = lunaCameraChannelProgress();
  *x = sourceX + (*x - sourceX) * p;
  *y = sourceY + (*y - sourceY) * p;
}

void lunaCameraViewCancel(void) {
  if (viewCover) {
    /* Pixels remain owned by the Classic artwork cache. Only unpin VRAM. */
    gsKit_TexManager_free(gsGlobal, viewCover);
    viewCover->Vram = viewCover->VramClut = 0;
  }
  viewCover = NULL;
  viewTarget = -1;
  viewReady = 0;
  viewSettled = 0;
  turnMs = 0;
  turnBack = 0;
  lunaMotionReset(&viewMotion);
}

void lunaCameraViewStart(GSTEXTURE *cover, int target, const float rect[4]) {
  lunaCameraViewCancel();
  if (!LUNA_CAMERA_LIST_TURN || !cover || !cover->Mem || cover->Width <= 0 || cover->Height <= 0 ||
      rect[2] <= 0 || rect[3] <= 0)
    return;
  viewCover = cover;
  viewTarget = target;
  for (int i = 0; i < 4; ++i) sourceRect[i] = rect[i];
  const float destination[3] = {1.0f, 0, 0};
  lunaMotionRetarget(&viewMotion, destination, 750.0f, 1.0f);
}

void lunaCameraViewCheck(int isCollection, int target) {
  if (viewCover && (!isCollection || target != viewTarget))
    lunaCameraViewCancel();
}

int lunaCameraViewActive(void) { return viewCover != NULL; }
float lunaCameraViewProgress(void) {
  if (viewSettled) return 1.0f;
  return viewCover ? fminf(1.0f, fmaxf(0, viewMotion.position[0])) : 1.0f;
}
int lunaCameraViewTurnBack(void) { return turnBack; }
float lunaCameraViewTurnWidth(void) {
  if (!viewCover) return 1;
  const float t = fminf(1.0f, turnMs / (float)TURN_HALF_MS);
  const float eased = t * t * (3.0f - 2.0f * t);
  return fmaxf(0, turnBack ? sinf(eased * 1.57079632679f) : cosf(eased * 1.57079632679f));
}
GSTEXTURE *lunaCameraViewSource(void) { return viewCover; }
void lunaCameraViewReady(int ready) { viewReady = ready != 0; }

void lunaCameraViewCoverRect(float x, float y, float size, float rect[4]) {
  const float destination[4] = {x, y, size, size};
  const float progress = lunaCameraViewProgress();
  for (int i = 0; i < 4; ++i)
    rect[i] = sourceRect[i] + (destination[i] - sourceRect[i]) * progress;
}

void lunaCameraViewNeighbor(float anchorX, float anchorY,
                           float *x, float *y, float *size) {
  if (!viewCover) return;
  const float progress = lunaCameraViewProgress();
  const float zoom = 1.0f + 0.35f * (1.0f - progress);
  const float sourceX = sourceRect[0] + sourceRect[2] * 0.5f;
  const float sourceY = sourceRect[1] + sourceRect[3] * 0.5f;
  const float centerX = sourceX + (anchorX - sourceX) * progress;
  const float centerY = sourceY + (anchorY - sourceY) * progress;
  const float xCenter = centerX + (*x + *size * 0.5f - anchorX) * zoom;
  const float yCenter = centerY + (*y + *size * 0.5f - anchorY) * zoom;
  *size *= zoom;
  *x = xCenter - *size * 0.5f;
  *y = yCenter - *size * 0.5f;
}

void lunaCameraSceneBeforeBackground(void) {
  restoreCameraOffset();
}

void lunaCameraSceneBegin(int open, uint32_t nowMs) {
  restoreCameraOffset();
  if (!initialized) {
    lastMs = nowMs;
    initialized = 1;
  }
  uint32_t elapsed = nowMs - lastMs;
  lastMs = nowMs;
  /* Long modal/storage gaps pause the camera without jumping its position. */
  if (elapsed > 250U) {
    pendingTicks = elapsed = 0;
  }
  pendingTicks += elapsed * 60U;
  while (pendingTicks >= 1000U) {
    if (menuReturning) lunaMotionTick(&menuReturn);
    if (viewCover && !viewSettled) lunaMotionTick(&viewMotion);
    if (lunaCameraChannelActive()) lunaMotionTick(&channelMotion);
    pendingTicks -= 1000U;
  }
  if (menuReturning && menuReturn.position[0] >= 0.999f)
    lunaCameraMenuReturnCancel();
  if (viewCover) {
    turnMs += elapsed;
    if (turnMs >= TURN_HALF_MS) {
      turnMs = TURN_HALF_MS;
      if (!turnBack && viewReady) {
        /* Swap only at zero projected width. Late artwork waits on the edge. */
        turnBack = 1;
        turnMs = 0;
      }
    }
    if (lunaCameraViewProgress() >= 0.998f) viewSettled = 1;
    if (open || (viewSettled && turnBack && turnMs >= TURN_HALF_MS))
      lunaCameraViewCancel();
  }
  if (lunaCameraChannelActive()) {
    channelAgeMs += elapsed;
    if (open || (channelAgeMs >= 350U && lunaCameraChannelProgress() >= 0.998f))
      lunaCameraChannelCancel();
  }
  armed = 1;
}

void lunaCameraSceneAfterBackground(void) {
  if ((!armed && !layerActive) || applied)
    return;
  savedOffsetX = gsGlobal->OffsetX;
  savedOffsetY = gsGlobal->OffsetY;
  if (layerActive) {
    lunaCameraDrawScale=layerScale;
    lunaCameraDrawOpacity=layerOpacity;
    gsGlobal->OffsetX+=(int)lroundf(layerOffsetX*16.0f);
    gsGlobal->OffsetY+=(int)lroundf(layerOffsetY*16.0f);
  } else {
    lunaCameraDrawScale = menuReturning ? menuReturn.position[0] : 1.0f;
  }
  applied = 1;
}

void lunaCameraSceneEnd(void) {
  restoreCameraOffset();
  armed = 0;
}

void lunaCameraLayerBegin(float scale, float opacity, float offsetX, float offsetY) {
  restoreCameraOffset();
  layerActive=1;
  layerScale=fmaxf(.01f,scale);
  layerOpacity=fminf(1,fmaxf(0,opacity));
  layerOffsetX=offsetX;
  layerOffsetY=offsetY;
}

void lunaCameraLayerEnd(void) {
  restoreCameraOffset();
  layerActive=0;
}
