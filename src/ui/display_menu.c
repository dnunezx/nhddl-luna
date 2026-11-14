#include "ui/display_menu.h"
#include "display.h"
#include "ui/graphics.h"
#include "ui/language.h"
#include "ui/pad.h"
#include "ui/ui.h"
#include <libpad.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
  int selected;
  LunaCubePageFocus focus;
} DisplayEditor;
static DisplayEditor editor;
static int requestedMode = -1;

static void displayBegin(void *context) {
  DisplayEditor *state = context;
  state->selected = lunaDisplayMode();
  state->focus = (LunaCubePageFocus){.selected=-1,.glideMs=110};
}
static const char *displayLabel(void *context, int row) {
  (void)context;
  return lunaDisplayLabel((LunaDisplayMode)row);
}
static const char *displayValue(void *context, int row) {
  (void)context;
  return row == (int)lunaDisplayMode() ? "Selected" : "";
}
static const char *displayDescription(void *context, int row) {
  (void)context;
  return row == LUNA_DISPLAY_480P ?
      "Menu output only. Requires a 480p-compatible cable and TV or HDMI adapter." :
      "Menu output only. Game video settings remain separate. Changes are tested before saving.";
}
static void displayDraw(void *context, float opacity) {
  DisplayEditor *state = context;
  LunaCubePage page = {.title="Menu video output",.sectionTitle="Display",
      .count=LUNA_DISPLAY_COUNT,.label=displayLabel,.value=displayValue,
      .description=displayDescription,.context=state};
  uiCubeSubmenuPageDraw(&page,state->selected,&state->focus,opacity);
}
static int displayInput(void *context, int input) {
  DisplayEditor *state = context;
  if (input & (PAD_CIRCLE | PAD_TRIANGLE | PAD_START)) return LUNA_CUBE_SUBMENU_BACK;
  if (input & PAD_UP) state->selected = (state->selected + LUNA_DISPLAY_COUNT - 1) % LUNA_DISPLAY_COUNT;
  else if (input & PAD_DOWN) state->selected = (state->selected + 1) % LUNA_DISPLAY_COUNT;
  else if (input & PAD_CROSS) {
    requestedMode = state->selected;
    return LUNA_CUBE_SUBMENU_DONE;
  }
  return LUNA_CUBE_SUBMENU_STAY;
}
LunaCubeSubmenu uiDisplaySubmenu(void) {
  return (LunaCubeSubmenu){.context=&editor,.begin=displayBegin,
      .draw=displayDraw,.input=displayInput};
}

static void displayMessage(const char *title, const char *detail,
                           const char *status, int confirmation) {
  gsKit_TexManager_nextFrame(gsGlobal);
  gsKit_clear(gsGlobal,BGColor);
  gsKit_set_test(gsGlobal,GS_ZTEST_OFF);
  int line = getFontLineHeight(), top = gsGlobal->Height / 4;
  drawTextWindow(32,top,gsGlobal->Width-32,top+line,6,FontMainColor,
                 ALIGN_HCENTER,lunaText(title));
  drawTextWindow(32,top+line*2,gsGlobal->Width-32,top+line*4,6,HeaderTextColor,
                 ALIGN_HCENTER,lunaText(detail));
  drawTextWindow(32,top+line*5,gsGlobal->Width-32,top+line*6,6,HeaderTextColor,
                 ALIGN_HCENTER,status);
  const ButtonPrompt prompts[] = {{ICON_CROSS,lunaText(confirmation ? "Keep" : "Continue")},
      {ICON_CIRCLE,lunaText("Revert")}};
  drawPromptBar(32,gsGlobal->Height-line*3,gsGlobal->Width-32,gsGlobal->Height,6,
                FontMainColor,(PromptBar){NULL,prompts,confirmation ? 2 : 1});
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_sync_flip(gsGlobal);
}

int uiDisplayApplyRequest(void) {
  if (requestedMode < 0 || requestedMode >= LUNA_DISPLAY_COUNT) return -1;
  LunaDisplayMode next = (LunaDisplayMode)requestedMode;
  requestedMode = -1;
  VModeType previous = LAUNCHER_OPTIONS.vmode;
  LAUNCHER_OPTIONS.vmode = lunaDisplayVMode(next);
  const char *error = NULL;
  if (uiInit()) error = "Could not initialize this display mode.";
  else {
    initPad();
    int previousButtons = pollInput(), confirmed = 0;
    uint32_t start = uiNowMs();
    for (;;) {
      uint32_t elapsed = uiNowMs() - start;
      if (elapsed >= 15000) break;
      char countdown[128];
      snprintf(countdown,sizeof(countdown),lunaText("Reverting in %d seconds."),
               (int)((15000-elapsed+999)/1000));
      displayMessage("Keep this display mode?",lunaDisplayLabel(next),countdown,1);
      int held = pollInput(), input = held & ~previousButtons;
      previousButtons = held;
      // A slow frame must not accept a confirmation after the deadline.
      if (uiNowMs() - start >= 15000) break;
      if (input & (PAD_CIRCLE | PAD_TRIANGLE | PAD_START)) break;
      if (input & PAD_CROSS) { confirmed = 1; break; }
      usleep(1000);
    }
    closePad();
    if (confirmed) {
      if (!lunaDisplaySave(next)) return 0;
      error = "Could not save display mode. Previous mode restored.";
    }
  }
  LAUNCHER_OPTIONS.vmode = previous;
  if (uiInit()) return -1;
  if (error) {
    initPad();
    int previousButtons = pollInput();
    for (;;) {
      displayMessage("Display",error,"",0);
      int held = pollInput(), input = held & ~previousButtons;
      previousButtons = held;
      if (input & (PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE | PAD_START)) break;
      usleep(1000);
    }
    closePad();
  }
  return 0;
}
