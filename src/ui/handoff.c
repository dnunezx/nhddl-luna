// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/handoff.h"
#include "ui/graphics.h"
#include "ui/view_internal.h"

void drawGameID(const char *gameID);

static const char *launchStageName(LaunchStage stage) {
  switch (stage) {
  case LAUNCH_STAGE_SYNCING:
    return "SYNCING DRIVE";
  case LAUNCH_STAGE_STARTING:
    return "STARTING GAME";
  case LAUNCH_STAGE_PREPARING:
  default:
    return "PREPARING GAME";
  }
}

static void drawHandoffCover(GSTEXTURE *cover, float x, float y, float maxWidth,
                             float maxHeight) {
  float width;
  float height;
  int previousAlphaTest;
  int previousAlphaReference;
  int previousAlphaFail;

  if (cover == NULL || cover->Width <= 0 || cover->Height <= 0) {
    drawGlassDiamond((int)(x + maxWidth / 2), (int)(y + maxHeight / 2), 28, 6,
                     HeaderTextColor);
    drawTextWindow((int)x, (int)(y + maxHeight / 2 + 38),
                   (int)(x + maxWidth), (int)(y + maxHeight), 7,
                   HeaderTextColor, ALIGN_HCENTER, "ART UNAVAILABLE");
    return;
  }

  width = maxWidth;
  height = width * cover->Height / cover->Width;
  if (height > maxHeight) {
    height = maxHeight;
    width = height * cover->Width / cover->Height;
  }
  x += (maxWidth - width) / 2;
  y += (maxHeight - height) / 2;

  previousAlphaTest = gsGlobal->Test->ATST;
  previousAlphaReference = gsGlobal->Test->AREF;
  previousAlphaFail = gsGlobal->Test->AFAIL;
  gsKit_TexManager_bind(gsGlobal, cover);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_prim_sprite_texture(gsGlobal, cover, x, y, 0.0f, 0.0f, x + width,
                            y + height, cover->Width - 1, cover->Height - 1, 6,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawStageRail(LaunchStage stage, int left, int right, int y) {
  static const char *labels[] = {"PREPARE", "SYNC", "START"};
  int spacing = (right - left) / 2;

  gsKit_prim_sprite(gsGlobal, left, y, right, y + 1, 5, ColorPanelEdge);
  for (int i = 0; i < 3; i++) {
    int x = left + i * spacing;
    uint64_t color = (i <= (int)stage) ? ColorSelected : HeaderTextColor;
    drawGlassDiamond(x, y, (i == (int)stage) ? 6 : 4, 6, color);
    drawTextWindow(x - 44, y + 12, x + 44, y + 36, 6, color,
                   ALIGN_HCENTER, labels[i]);
  }
}

static void presentLaunchHandoff(Target *target, GSTEXTURE *cover,
                                 LaunchStage stage, int waitForVSync) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int panelTop = 64;
  const int panelBottom = height - 56;
  const int coverTop = panelTop + 28;
  const int coverHeight = panelBottom - coverTop - 30;

  gsKit_clear(gsGlobal, BGColor);
  gsKit_TexManager_nextFrame(gsGlobal);
  drawSharedLibraryBackground(uiNowMs());
  drawGlassPanel(42, panelTop, width - 42, panelBottom, 2);
  drawTextWindow(0, 22, width, 0, 5, FontMainColor, ALIGN_HCENTER,
                 "L  U  N  A");

  drawHandoffCover(cover, 66, coverTop, 176, coverHeight);
  drawTextWindow(278, panelTop + 42, width - 68, panelTop + 112, 6,
                 FontMainColor, ALIGN_VCENTER, target->name);
  drawTextWindow(278, panelTop + 112, width - 68, panelTop + 142, 6,
                 HeaderTextColor, ALIGN_LEFT, target->id);
  drawTextWindow(278, panelTop + 145, width - 68, panelTop + 180, 6,
                 FontMainColor, ALIGN_HCENTER, launchStageName(stage));
  drawStageRail(stage, 302, width - 84, panelTop + 196);

  // Retain the exact signal encoding used by the previous launch screen.
  drawGameID(target->id);
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  if (waitForVSync)
    gsKit_sync_flip(gsGlobal);
  else
    gsKit_setactive(gsGlobal);
}

void uiPresentLaunchHandoff(Target *target, GSTEXTURE *cover, LaunchStage stage) {
  // This replaces the single synchronized frame used by the previous launch
  // screen; it does not add another timed transition.
  presentLaunchHandoff(target, cover, stage, 1);
}

void uiLaunchHandoffProgress(LaunchStage stage, void *userdata) {
  UILaunchHandoff *handoff = (UILaunchHandoff *)userdata;
  if (handoff == NULL || handoff->target == NULL)
    return;
  // Later stage changes flip directly after their real work boundary. Waiting
  // for another VSync here would extend the launch solely for presentation.
  presentLaunchHandoff(handoff->target, handoff->cover, stage, 0);
}
