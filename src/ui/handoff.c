// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/handoff.h"
#include "ui/graphics.h"
#include "ui/view_internal.h"

#define LAUNCH_LIBRARY_FADE_MS 200
#define LAUNCH_BACKGROUND_IN_MS 180
#define LAUNCH_BACKGROUND_HOLD_MS 300
#define LAUNCH_FADE_MS 240

static void presentFrame(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_sync_flip(gsGlobal);
}

static void drawBlack(int alpha) {
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsGlobal->PrimAlphaEnable = alpha < 0x80 ? GS_SETTING_ON : GS_SETTING_OFF;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0, 0, 0, alpha));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
}

static void presentBlack(int waitForVSync) {
  gsKit_TexManager_nextFrame(gsGlobal);
  drawBlack(0x80);
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  if (waitForVSync)
    gsKit_sync_flip(gsGlobal);
  else
    gsKit_setactive(gsGlobal);
}

void uiPlayLaunchTransition(void) {
  uint32_t start = uiNowMs();
  uint32_t elapsed;
  int bufferFade[2] = {0, 0};

  // Fade the still library frame in each display buffer. Track its existing
  // opacity so repeated overlays stay linear instead of darkening too fast.
  do {
    elapsed = uiNowMs() - start;
    if (elapsed > LAUNCH_LIBRARY_FADE_MS)
      elapsed = LAUNCH_LIBRARY_FADE_MS;
    const int fade = (int)(elapsed * 1000U / LAUNCH_LIBRARY_FADE_MS);
    const int buffer = gsGlobal->ActiveBuffer;
    const int previous = bufferFade[buffer];
    const int alpha = (fade >= 1000) ? 0x80 :
        ((fade - previous) * 0x80) / (1000 - previous);
    gsKit_TexManager_nextFrame(gsGlobal);
    if (alpha > 0)
      drawBlack(alpha);
    bufferFade[buffer] = fade;
    presentFrame();
  } while (elapsed < LAUNCH_LIBRARY_FADE_MS);

  // Bring the moving background through black after the library disappears.
  start = uiNowMs();
  do {
    elapsed = uiNowMs() - start;
    if (elapsed > LAUNCH_BACKGROUND_IN_MS)
      elapsed = LAUNCH_BACKGROUND_IN_MS;
    gsKit_TexManager_nextFrame(gsGlobal);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawSharedLibraryBackground(uiNowMs());
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    drawBlack((int)((LAUNCH_BACKGROUND_IN_MS - elapsed) * 0x80U /
                    LAUNCH_BACKGROUND_IN_MS));
    presentFrame();
  } while (elapsed < LAUNCH_BACKGROUND_IN_MS);

  // Leave the background playing alone before it fades away.
  start = uiNowMs();
  do {
    elapsed = uiNowMs() - start;
    gsKit_TexManager_nextFrame(gsGlobal);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawSharedLibraryBackground(uiNowMs());
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentFrame();
  } while (elapsed < LAUNCH_BACKGROUND_HOLD_MS);

  start = uiNowMs();
  do {
    elapsed = uiNowMs() - start;
    if (elapsed > LAUNCH_FADE_MS)
      elapsed = LAUNCH_FADE_MS;
    gsKit_TexManager_nextFrame(gsGlobal);
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawSharedLibraryBackground(uiNowMs());
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    drawBlack((int)(elapsed * 0x80U / LAUNCH_FADE_MS));
    presentFrame();
  } while (elapsed < LAUNCH_FADE_MS);

  // Fill both display buffers before the loader takes control.
  presentBlack(1);
  presentBlack(1);
}

void uiLaunchHandoffProgress(LaunchStage stage, void *userdata) {
  (void)stage;
  (void)userdata;
  presentBlack(0);
}
