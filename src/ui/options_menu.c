// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include "devices/devices.h"
#include "vmc_create.h"
#include "storage.h"
#include <ctype.h>
#include <dirent.h>
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OPTIONS_OPEN_DURATION_MS 360
#define OPTIONS_CLOSE_DURATION_MS 180
#define OPTIONS_GLASS_FADE_DURATION_MS 180
#define OPTIONS_SELECTOR_GLIDE_DURATION_MS 110
#define OPTIONS_SELECTOR_ROW_SCALE 256

typedef enum {
  OPTIONS_PER_GAME,
  OPTIONS_GLOBAL,
  OPTIONS_VIEWS,
  OPTIONS_ORBS
} OptionsPage;

typedef enum {
  GAME_HUB = -1,
  GAME_MEMORY_CARDS,
  GAME_COMPATIBILITY,
  GAME_VIDEO,
  GAME_LAUNCH,
  GAME_SECTION_COUNT,
  GAME_VIDEO_OUT
} GameSection;

typedef struct {
  OptionsPage page;
  int selectedRow;
  int fromRow;
  int toRow;
  uint32_t startMs;
  int initialized;
} OptionsSelector;

typedef struct {
  Target *target;
  char titleHeader[255];
  ArgumentList *titleArguments;
  LunaGameOptions gameOptions;
  int *classicArtOverlap;
  int *ambientOrbsBackgroundSetting;
  int *glassColorSetting;
  int *fontSetting;
  int *ambientEnabled;
  int logoSetting;
  int *orbsThemeSetting;
  int *orbsAppearanceSetting;
  int *orbsColorSetting;
  int *tailsColorSetting;
  uint32_t *enabledViews;
  int pendingOverlap;
  int pendingBackground;
  int pendingGlassColor;
  int pendingFont;
  int pendingAmbient;
  int pendingLogo;
  int pendingOrbsTheme;
  int pendingOrbsAppearance;
  int pendingOrbsColor;
  int pendingTailsColor;
  uint32_t pendingViews;
  int titleArgumentsChanged;
  int saveError;
  const char *saveErrorLabel;
  char vmcStatus[96];
  OptionsPage page;
  OptionsSelector selector;
  GameSection gameSection;
  int selectedGameHubRow;
  int selectedGameRow;
  int videoOutFirstRow;
  int selectedGlobal;
  int selectedView;
  int selectedOrbsRow;
} OptionsMenuState;

static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments);
static int uiVMCPickerLoop(Target *target, ArgumentList *arguments,
                           LunaGameOptions *gameOptions, int slot);

static const char *const gameRowLabels[LUNA_GAME_ROW_COUNT] = {
    "IOP: Fast reads", "IOP: Sync reads",
    "EE: Unhook syscalls", "IOP: Emulate DVD-DL",
    "IOP: Fix game buffer overrun", "Launch arguments",
    "VMC slot 1", "VMC slot 2",
    "Video out", "Field flipping",
    "Show PS2 logo", "Debug colors", "Disable IGR"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Review every launch argument, including global overrides.",
    "Assign an existing card image from this drive's VMC folder.",
    "Assign an existing card image to the second slot.",
    "Choose Neutrino's video output mode.",
    "Choose field flipping for a forced video mode.",
    "Choose Inherit to use the Global PS2 logo setting.",
    "Display debug colors while loading.",
    "Disable Neutrino's in-game return for this title."};

static const char *const gameSectionLabels[GAME_SECTION_COUNT] = {
    "Virtual memory cards", "Compatibility", "Video", "Launch & debug"};
static const char *const gameSectionDescriptions[GAME_SECTION_COUNT] = {
    "Enable virtual cards by assigning one to a slot.",
    "Change this game's compatibility switches.",
    "Adjust this game's video output.",
    "Set the startup logo and review launch options."};
static const LunaGameRow gameSectionRows[GAME_SECTION_COUNT][6] = {
    {LUNA_GAME_VMC_SLOT1, LUNA_GAME_VMC_SLOT2},
    {LUNA_GAME_FAST_READS, LUNA_GAME_SYNC_READS,
     LUNA_GAME_UNHOOK_SYSCALLS, LUNA_GAME_DVD_DL,
     LUNA_GAME_BUFFER_OVERRUN, LUNA_GAME_NEUTRINO_DISABLE_IGR},
    {LUNA_GAME_VIDEO_MODE, LUNA_GAME_FIELD_FLIP},
    {LUNA_GAME_PS2_LOGO, LUNA_GAME_LAUNCH_ARGUMENTS,
     LUNA_GAME_DEBUG_COLORS}};
static const int gameSectionRowCounts[GAME_SECTION_COUNT] = {4, 6, 2, 3};

static int gameSectionRowCount(GameSection section) {
  if (section == GAME_VIDEO_OUT)
    return lunaGameVideoModeCount();
  return gameSectionRowCounts[section];
}

static LunaGameRow gameSectionRow(GameSection section, int row) {
  return gameSectionRows[section][row];
}

#define OPTIONS_VIEW_ART_LAYOUT_ROW UI_VIEW_COUNT
#define OPTIONS_VIEW_ROW_COUNT (UI_VIEW_COUNT + 1)

// Give Options its own scene without carrying library text into the menu.
static void drawOptionsSheet(void) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, 32, 80, width - 32,
                    height - footerHeight + 1, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(30, 78, width - 30, height - footerHeight + 3, 2);
}

static int optionsTransitionProgress(uint32_t elapsed, uint32_t duration) {
  int progress = elapsed >= duration ? 1000 : (int)(elapsed * 1000U / duration);
  return (int)((int64_t)progress * progress *
               (3000 - 2 * progress) / 1000000);
}

static void drawOptionsBlackout(int alpha) {
  if (alpha <= 0)
    return;
  gsGlobal->PrimAlphaEnable = alpha >= 0x80 ? GS_SETTING_OFF : GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0x00, 0x00, 0x00, alpha));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

// Fade the completed Options frame in without sampling a framebuffer that
// double buffering will reuse on the next frame.
static void drawOptionsTransition(int progress, int closing) {
  drawOptionsBlackout((closing ? progress : 1000 - progress) * 0x80 / 1000);
}

static void presentOptionsFrame(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_sync_flip(gsGlobal);
  usleep(1000);
}

static void drawOptionsFooter(int gamePage, int argumentList, int dirty,
                              int placeholder, const char *actionLabel,
                              int gameDetail) {
  const int top = gsGlobal->Height - footerHeight;
  const int width = gsGlobal->Width;
  const int slot = width / 4;
  if (!placeholder) {
    const ButtonPrompt action[] = {
        {ICON_CROSS, argumentList ? "Toggle" : actionLabel ? actionLabel : "Change"}};
    drawPromptBar(8, top, slot, gsGlobal->Height, 0, HeaderTextColor,
                  (PromptBar){NULL, action, 1});
    if (gamePage || argumentList) {
      const ButtonPrompt test[] = {{ICON_SQUARE, "Test"}};
      drawPromptBar(slot, top, 2 * slot, gsGlobal->Height, 0,
                    HeaderTextColor, (PromptBar){NULL, test, 1});
    }
    const ButtonPrompt save[] = {
        {ICON_START, argumentList ? "Save" : "Save tab"}};
    drawPromptBar(2 * slot, top, 3 * slot, gsGlobal->Height, 0,
                  HeaderTextColor, (PromptBar){NULL, save, 1});
  }
  const ButtonPrompt back[] = {
      {ICON_TRIANGLE, argumentList || gameDetail ? "Back" :
                      (dirty ? "Discard" : "Close")}};
  drawPromptBar(3 * slot, top, width - 8, gsGlobal->Height, 0,
                HeaderTextColor, (PromptBar){NULL, back, 1});
}

static int optionsSelectorProgress(const OptionsSelector *selector, uint32_t now) {
  uint32_t elapsed = now - selector->startMs;
  if (elapsed >= OPTIONS_SELECTOR_GLIDE_DURATION_MS)
    return 1000;
  int progress = (int)(elapsed * 1000ULL / OPTIONS_SELECTOR_GLIDE_DURATION_MS);
  return (int)((int64_t)progress * progress * (3000 - 2 * progress) / 1000000);
}

static int optionsSelectorY(OptionsSelector *selector, OptionsPage page,
                            int selectedRow, int selectedY) {
  const uint32_t now = uiNowMs();
  if (!selector->initialized || selector->page != page) {
    selector->page = page;
    selector->selectedRow = selectedRow;
    selector->fromRow = selectedY * OPTIONS_SELECTOR_ROW_SCALE;
    selector->toRow = selector->fromRow;
    selector->startMs = now;
    selector->initialized = 1;
  } else if (selector->selectedRow != selectedRow) {
    int progress = optionsSelectorProgress(selector, now);
    selector->fromRow += (selector->toRow - selector->fromRow) * progress / 1000;
    selector->toRow = selectedY * OPTIONS_SELECTOR_ROW_SCALE;
    selector->selectedRow = selectedRow;
    selector->startMs = now;
  }
  int progress = optionsSelectorProgress(selector, now);
  int row = selector->fromRow +
            (selector->toRow - selector->fromRow) * progress / 1000;
  return row / OPTIONS_SELECTOR_ROW_SCALE;
}


static void drawOptionsTextRow(int x, int y, int right, int selected,
                               int selectorY, const char *label,
                               const char *value) {
  if (selected)
    drawMenuRowSelector(x, selectorY, right);
  int labelWidth = right - x - 30;
  float valueWidth = value ? getLineWidth(value) : 0;
  if (value)
    labelWidth -= (int)valueWidth + 16;
  drawText(x + 18, y, 0, labelWidth, 0,
           selected ? GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80) : HeaderTextColor,
           label);
  if (value)
    drawText(right - valueWidth - 12, y, 0, 0, 0,
             selected ? ColorSelected : FontMainColor, value);
}

static void drawVideoOutRows(OptionsMenuState *state, int baseX, int firstY,
                             int rowStep, int menuBottom) {
  const int rowStart = firstY;
  const int count = lunaGameVideoModeCount();
  const int mode = state->gameOptions.videoMode;
  int visible = (menuBottom - getFontLineHeight() - rowStart) / rowStep + 1;
  if (visible < 1)
    visible = 1;
  int first = state->videoOutFirstRow;
  if (state->selectedGameRow < first)
    first = state->selectedGameRow;
  if (state->selectedGameRow >= first + visible)
    first = state->selectedGameRow - visible + 1;
  if (first != state->videoOutFirstRow)
    state->selector.initialized = 0;
  state->videoOutFirstRow = first;
  int selectorY = optionsSelectorY(&state->selector, state->page,
      state->selectedGameRow, rowStart + (state->selectedGameRow - first) * rowStep);
  for (int row = first; row < count && row < first + visible; row++)
    drawOptionsTextRow(baseX, rowStart + (row - first) * rowStep,
                       gsGlobal->Width - baseX, row == state->selectedGameRow,
                       selectorY, lunaGameVideoModeLabel(row),
                       mode == row ? "On" : "Off");
  char position[20];
  snprintf(position, sizeof(position), "%d/%d", state->selectedGameRow + 1, count);
  drawText(gsGlobal->Width - baseX - getLineWidth(position) - 12,
           firstY, 0, 0, 0, FontMainColor, position);
  drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
                 state->selectedGameRow == 0 ?
                     "Keep the game's original output." :
                     "Select a video output mode for this game.");
}

static int optionsGlobalRowY(int index, int firstY, int rowStep, int lineHeight) {
  return firstY + index * rowStep +
         (index >= 3 ? lineHeight : 0) +
         (index >= 4 ? lineHeight : 0);
}

static int optionsViewRowY(int row, int firstY, int rowStep,
                            int lineHeight) {
  return firstY + row * rowStep +
         (row == OPTIONS_VIEW_ART_LAYOUT_ROW ? lineHeight : 0);
}

static int gameVMCEnabled(ArgumentList *arguments) {
  const char *const names[] = {"mc0", "mc1"};
  for (int slot = 0; slot < 2; slot++) {
    Argument *card = getArgument(arguments, names[slot]);
    if (card != NULL && !card->isDisabled && card->value != NULL &&
        card->value[0] != '\0')
      return 1;
  }
  return 0;
}

static void drawOptionsMusicNote(int centerX, int y, uint64_t color) {
  gsKit_prim_line(gsGlobal, centerX - 1, y + 3,
                  centerX - 1, y + 13, 0, color);
  gsKit_prim_line(gsGlobal, centerX - 1, y + 3,
                  centerX + 5, y + 1, 0, color);
  gsKit_prim_sprite(gsGlobal, centerX - 5, y + 12,
                    centerX - 1, y + 15, 0, color);
}

static void drawOptionsSection(int y, const char *title, int musicNotes) {
  const uint64_t color = HeaderTextColor;
  const int titleWidth = (int)getLineWidth(title);
  const int titleX = (gsGlobal->Width - titleWidth) / 2;
  if (musicNotes)
    drawOptionsMusicNote(titleX - 14, y, color);
  else
    drawGlassDiamond(titleX - 14, y + 8, 6, 0, color);
  drawText(titleX, y, 0, 0, 0, color, title);
  if (musicNotes)
    drawOptionsMusicNote(titleX + titleWidth + 14, y, color);
  else
    drawGlassDiamond(titleX + titleWidth + 14, y + 8, 6, 0, color);
}

static int optionsGlobalDirty(int pendingBackground, int pendingGlassColor,
                              int pendingFont, int pendingAmbient, int pendingLogo,
                              int background, int glassColor, int fontSetting,
                              int ambient, int logo);

static const char *gamePS2LogoValue(const OptionsMenuState *state) {
  Argument *logo = getArgument(state->titleArguments, "logo");
  if (logo == NULL || logo->isGlobal)
    return state->logoSetting ? "Inherit (On)" : "Inherit (Off)";
  return state->gameOptions.ps2Logo ? "Override (On)" : "Override (Off)";
}

static OptionsPage optionsNextPage(OptionsPage page, int direction) {
  return (OptionsPage)((page + direction + OPTIONS_ORBS + 1) %
                       (OPTIONS_ORBS + 1));
}

static int optionsOrbsEditable(const OptionsMenuState *state) {
  return *state->ambientOrbsBackgroundSetting == LIBRARY_BACKGROUND_ORBS;
}

static int optionsOrbsColorsEditable(const OptionsMenuState *state) {
  return state->pendingOrbsAppearance != ORBS_APPEARANCE_PS2_ORIGINAL;
}

static void drawOrbsUnavailablePopup(int baseX, int menuTop, int menuBottom) {
  const int left = baseX + 28;
  const int right = gsGlobal->Width - baseX - 28;
  const int middleY = (menuTop + menuBottom) / 2;
  const int top = middleY - 58;
  const int bottom = middleY + 58;
  drawGlassPanel(left, top, right, bottom, 3);
  drawTextWindow(left + 14, top + 14, right - 14, top + 36, 4,
                 ColorSelected, ALIGN_HCENTER, "Ambient Orbs required");
  drawTextWindow(left + 14, top + 43, right - 14, top + 65, 4,
                 FontMainColor, ALIGN_HCENTER,
                 "Shown when Global background is Ambient Orbs.");
  drawTextWindow(left + 14, top + 73, right - 14, bottom - 10, 4,
                 HeaderTextColor, ALIGN_HCENTER,
                 "Set and save Global > Background to Ambient Orbs.");
}

static void drawGameVMCRows(OptionsMenuState *state, int baseX,
                            int firstY, int rowStep, int menuBottom) {
  const int enabled = gameVMCEnabled(state->titleArguments);
  const int visibleRows = enabled ? 4 : 3;
  const char *const labels[] = {
      "VMC slot 1", "VMC slot 2", "Create new card", "Disable virtual cards"};
  const char *const values[] = {
      state->gameOptions.vmcSlotLabel[0],
      state->gameOptions.vmcSlotLabel[1], "8 MB", NULL};
  int selectorY = optionsSelectorY(&state->selector, state->page,
      GAME_SECTION_COUNT + GAME_MEMORY_CARDS * 5 + state->selectedGameRow,
      firstY + state->selectedGameRow * rowStep);
  for (int row = 0; row < visibleRows; row++)
    drawOptionsTextRow(baseX, firstY + row * rowStep,
                       gsGlobal->Width - baseX,
                       row == state->selectedGameRow, selectorY,
                       labels[row], values[row]);
  const char *description = state->vmcStatus[0] ? state->vmcStatus :
      state->selectedGameRow == 2 ?
          "Create an 8 MB card on this game's drive." :
      state->selectedGameRow == 3 ?
          "Use physical cards in both slots for this game." :
          gameRowDescriptions[LUNA_GAME_VMC_SLOT1 + state->selectedGameRow];
  drawTextWindow(baseX + 18, menuBottom,
                 gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0,
                 HeaderTextColor, ALIGN_LEFT, description);
}

static void drawTitleOptionsFrame(OptionsMenuState *state,
                                  int transitionProgress, int transitionMode) {
  const int showDirty = transitionMode != 2;
  const int gameDirty = showDirty && state->titleArgumentsChanged;
  const int systemDirty = showDirty && optionsGlobalDirty(
      state->pendingBackground, state->pendingGlassColor,
      state->pendingFont, state->pendingAmbient, state->pendingLogo,
      *state->ambientOrbsBackgroundSetting, *state->glassColorSetting,
      *state->fontSetting, *state->ambientEnabled, state->logoSetting);
  const int viewsDirty = showDirty &&
                         (state->pendingViews != *state->enabledViews ||
                          state->pendingOverlap != *state->classicArtOverlap);
  const int orbsDirty = showDirty &&
                        (state->pendingOrbsTheme != *state->orbsThemeSetting ||
                         state->pendingOrbsAppearance != *state->orbsAppearanceSetting ||
                         state->pendingOrbsColor != *state->orbsColorSetting ||
                         state->pendingTailsColor != *state->tailsColorSetting);
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();

  drawTextWindow(baseX, headerHeight - lineHeight,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, "Options");

  const int tabY = headerHeight + lineHeight / 4;
  const int middle = gsGlobal->Width / 2;
  static const int tabOffsets[] = {-175, -75, 30, 135};
  static const char *const tabLabels[] = {"Game", "Global", "Views", "Orbs"};
  static const char *const dirtyTabLabels[] = {"Game *", "Global *", "Views *", "Orbs *"};
  const int tabDirty[] = {gameDirty, systemDirty, viewsDirty, orbsDirty};
  drawIconWindow(middle - 230, tabY, middle - 200, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_L1);
  for (int tab = OPTIONS_PER_GAME; tab <= OPTIONS_ORBS; tab++) {
    drawText(middle + tabOffsets[tab], tabY, 0, 0, 0,
             state->page == tab ? ColorSelected : HeaderTextColor,
             tabDirty[tab] ? dirtyTabLabels[tab] : tabLabels[tab]);
  }
  drawIconWindow(middle + 215, tabY, middle + 245, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_R1);
  const int menuTop = headerHeight + 2 * lineHeight + 8;
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (state->page == OPTIONS_GLOBAL) {
    static const char *const backgroundLabels[LIBRARY_BACKGROUND_COUNT] = {
        "Stars & cubes", "Ambient Orbs", "Red Clouds", "Midnight Cubes",
        "System Configuration"};
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedGlobal,
                                     optionsGlobalRowY(state->selectedGlobal, firstY, rowStep, lineHeight));
    drawOptionsSection(menuTop, "Appearance", 0);
    drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                       state->selectedGlobal == 0, selectorY, "Background (Experimental)",
                       backgroundLabels[state->pendingBackground]);
    static const char *const glassColorLabels[GLASS_COLOR_COUNT] = {
        "Original", "Luminous", "Cosmic"};
    drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                       state->selectedGlobal == 1, selectorY, "Glass color",
                       glassColorLabels[state->pendingGlassColor]);
    drawOptionsTextRow(baseX, firstY + 2 * rowStep, gsGlobal->Width - baseX,
                       state->selectedGlobal == 2, selectorY, "Font",
                       state->pendingFont == UI_FONT_PSBBN ? "PSBBN" : "DejaVu Sans");
    drawOptionsSection(firstY + 3 * rowStep, "Audio", 1);
    drawOptionsTextRow(baseX, optionsGlobalRowY(3, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, state->selectedGlobal == 3, selectorY,
                       "Ambient sound", state->pendingAmbient ? "On" : "Off");
    drawOptionsSection(firstY + 4 * rowStep + lineHeight,
                       "Game defaults", 0);
    drawOptionsTextRow(baseX, optionsGlobalRowY(4, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, state->selectedGlobal == 4, selectorY,
                       "PlayStation 2 logo", state->pendingLogo ? "On" : "Off");
    static const char *const descriptions[] = {
        "Choose a library background; customize Ambient Orbs in Orbs.",
        "Change the tint of the glass interface.",
        "Use LUNA's font or the PSBBN keyboard lettering.",
        "Play ambient music while browsing.",
        "Default PS2 startup logo setting for every game."};
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT, descriptions[state->selectedGlobal]);
  } else if (state->page == OPTIONS_VIEWS) {
    const int firstY = menuTop + lineHeight + 4;
    // Keep all view rows and the List layout control above the footer.
    const int rowStep = (menuBottom - firstY - 3 * lineHeight) / UI_VIEW_COUNT;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedView,
                                     optionsViewRowY(state->selectedView,
                                                     firstY, rowStep, lineHeight));
    drawOptionsSection(menuTop, "Enabled views", 0);
    for (int row = 0; row < UI_VIEW_COUNT; row++) {
      const UILibraryView view = lunaViewCycleOrder[row];
      const char *label = view == UI_VIEW_ORBS ? "Scroll (Experimental)" :
                          lunaNavViewLabel(view);
      drawOptionsTextRow(baseX, firstY + row * rowStep,
                         gsGlobal->Width - baseX, state->selectedView == row,
                         selectorY, label,
                         state->pendingViews & (1U << view) ? "On" : "Off");
    }
    drawOptionsSection(firstY + OPTIONS_VIEW_ART_LAYOUT_ROW * rowStep,
                       "List view", 0);
    drawOptionsTextRow(baseX + 18,
                       optionsViewRowY(OPTIONS_VIEW_ART_LAYOUT_ROW,
                                       firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX,
                       state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW, selectorY,
                       "List art layout",
                       state->pendingOverlap ? "Overlap" : "Separate");
    if (state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW)
      drawTextWindow(baseX + 18, menuBottom - lineHeight,
                     gsGlobal->Width - baseX, menuBottom, 0,
                     HeaderTextColor, ALIGN_LEFT,
                     "Choose how cover art sits in List view.");
    else {
      const ButtonPrompt cycle[] = {
          {ICON_CIRCLE, "Cycle views; keep at least one on"}};
      drawPromptBar(baseX + 18, menuBottom - lineHeight,
                    gsGlobal->Width - baseX, menuBottom, 0,
                    HeaderTextColor, (PromptBar){NULL, cycle, 1});
    }
  } else if (state->page == OPTIONS_ORBS) {
    drawOptionsSection(menuTop, "Orbs", 0);
    if (!optionsOrbsEditable(state)) {
      drawOrbsUnavailablePopup(baseX, menuTop, menuBottom);
    } else {
      static const char *const colorLabels[ORBS_COLOR_COUNT] = {
          "Original", "Cyan", "Violet", "Rose", "Green", "Gold", "White"};
      const int firstY = menuTop + lineHeight + 4;
      const int selectorY = optionsSelectorY(&state->selector, state->page,
                                             state->selectedOrbsRow,
                                             firstY + state->selectedOrbsRow * rowStep);
      drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                         state->selectedOrbsRow == 0, selectorY,
                         "Behavior", state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ?
                         "PS2 original" : "LUNA");
      drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                         state->selectedOrbsRow == 1, selectorY,
                         "Appearance", state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                         "PS2 original" : "LUNA");
      if (optionsOrbsColorsEditable(state)) {
        drawOptionsTextRow(baseX, firstY + 2 * rowStep, gsGlobal->Width - baseX,
                           state->selectedOrbsRow == 2, selectorY,
                           "Orb color", colorLabels[state->pendingOrbsColor]);
        drawOptionsTextRow(baseX, firstY + 3 * rowStep, gsGlobal->Width - baseX,
                           state->selectedOrbsRow == 3, selectorY,
                           "Tail color", colorLabels[state->pendingTailsColor]);
      }
      drawTextWindow(baseX, menuBottom - 2 * lineHeight,
                     gsGlobal->Width - baseX, menuBottom, 0,
                     HeaderTextColor, ALIGN_HCENTER,
                     !optionsOrbsColorsEditable(state) ?
                         "PS2 original appearance uses its original colors." :
                     state->selectedOrbsRow >= 2 ?
                         "Color choices affect only the shared orb background." :
                     state->selectedOrbsRow == 0 ?
                         (state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ?
                          "Seven clock-driven lights with long trails." :
                          "Animated LUNA formations and title reactions.") :
                         (state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                          "Original halo and core masks from the PS2 ROM." :
                          "LUNA's soft glass lights and bright cores."));
    }
  } else {
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    const int contentTop = menuTop + lineHeight + 5;
    const int gameStep = lineHeight + lineHeight / 2;
    const int firstY = contentTop + lineHeight + 4;
    if (state->gameSection == GAME_HUB) {
      char compatSummary[24];
      int compatEnabled = 0;
      for (int bit = 0; bit < LUNA_GAME_COMPAT_COUNT; bit++)
        compatEnabled += (state->gameOptions.compat & (1U << bit)) != 0;
      compatEnabled += state->gameOptions.neutrinoIgrDisabled;
      snprintf(compatSummary, sizeof(compatSummary), "%d enabled", compatEnabled);
      const char *const summaries[GAME_SECTION_COUNT] = {
          gameVMCEnabled(state->titleArguments) ? "Enabled" : "Disabled",
          compatSummary,
          lunaGameOptionsValue(&state->gameOptions,
              LUNA_GAME_VIDEO_MODE),
          "3 options"};
      drawOptionsSection(contentTop, "Game settings", 0);
      int selectorY = optionsSelectorY(&state->selector, state->page,
          state->selectedGameHubRow, firstY + state->selectedGameHubRow * gameStep);
      for (int row = 0; row < GAME_SECTION_COUNT; row++)
        drawOptionsTextRow(baseX, firstY + row * gameStep,
                           gsGlobal->Width - baseX,
                           row == state->selectedGameHubRow, selectorY,
                           gameSectionLabels[row], summaries[row]);
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                     ALIGN_LEFT, gameSectionDescriptions[state->selectedGameHubRow]);
    } else {
      const GameSection section = state->gameSection;
      if (state->selectedGameRow >= gameSectionRowCount(section))
        state->selectedGameRow = 0;
      drawOptionsSection(contentTop, section == GAME_VIDEO_OUT ? "Video out" :
                                                          gameSectionLabels[section], 0);
      if (section == GAME_VIDEO_OUT) {
        drawVideoOutRows(state, baseX, firstY, gameStep, menuBottom);
      } else if (section == GAME_MEMORY_CARDS) {
        drawGameVMCRows(state, baseX, firstY, gameStep, menuBottom);
      } else {
        int rowStart = firstY;
        const LunaGameRow selectedRow =
            gameSectionRow(section, state->selectedGameRow);
        int selectorY = optionsSelectorY(&state->selector, state->page,
            GAME_SECTION_COUNT + section * 6 + state->selectedGameRow,
            rowStart + state->selectedGameRow * gameStep);
        for (int row = 0; row < gameSectionRowCount(section); row++) {
          LunaGameRow option = gameSectionRow(section, row);
          drawOptionsTextRow(baseX, rowStart + row * gameStep,
                             gsGlobal->Width - baseX,
                             row == state->selectedGameRow, selectorY,
                             gameRowLabels[option],
                             option == LUNA_GAME_PS2_LOGO ?
                                 gamePS2LogoValue(state) :
                                 lunaGameOptionsValue(&state->gameOptions, option));
        }
        drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                       gsGlobal->Height - footerHeight, 0,
                       HeaderTextColor, ALIGN_LEFT,
                       gameRowDescriptions[selectedRow]);
      }
    }
  }
  drawOptionsFooter(state->page == OPTIONS_PER_GAME, 0,
                    gameDirty || systemDirty || viewsDirty || orbsDirty,
                    state->page == OPTIONS_ORBS && !optionsOrbsEditable(state),
                    state->page == OPTIONS_PER_GAME && state->gameSection == GAME_HUB ?
                        (state->selectedGameHubRow == GAME_MEMORY_CARDS ?
                            (gameVMCEnabled(state->titleArguments) ? "Manage" : "Enable") :
                            "Open") :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_VIDEO_OUT ? "Select" :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_VIDEO && state->selectedGameRow == 0 ?
                        "Open" :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_MEMORY_CARDS ?
                        (state->selectedGameRow == 2 ? "Create" :
                         state->selectedGameRow == 3 ? "Disable" : "Assign") : NULL,
                    (state->page == OPTIONS_PER_GAME &&
                     state->gameSection != GAME_HUB) ||
                    (state->page == OPTIONS_ORBS &&
                     !optionsOrbsEditable(state)));
  if (showDirty && state->saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   state->saveErrorLabel ? state->saveErrorLabel :
                   state->page == OPTIONS_GLOBAL ? "Could not save global settings" :
                   state->page == OPTIONS_VIEWS ? "Could not save views" :
                   state->page == OPTIONS_ORBS ? "Could not save orb settings" :
                                           "Could not save game settings");
  if (transitionMode != 0)
    drawOptionsTransition(transitionProgress, transitionMode == 2);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

static void changeOptionsGlassColor(OptionsMenuState *state, int color) {
  uint32_t transitionStart = uiNowMs();
  int progress;
  do {
    progress = optionsTransitionProgress(
        uiNowMs() - transitionStart, OPTIONS_GLASS_FADE_DURATION_MS);
    drawTitleOptionsFrame(state, progress, 2);
    pollInput();
  } while (progress < 1000);

  state->pendingGlassColor = color;
  setGlassColorPreset((GlassColorPreset)color);

  transitionStart = uiNowMs();
  do {
    progress = optionsTransitionProgress(
        uiNowMs() - transitionStart, OPTIONS_GLASS_FADE_DURATION_MS);
    drawTitleOptionsFrame(state, progress, 1);
    pollInput();
  } while (progress < 1000);
}

static int optionsGlobalDirty(int pendingBackground, int pendingGlassColor,
                              int pendingFont, int pendingAmbient, int pendingLogo,
                              int background, int glassColor, int fontSetting,
                              int ambient, int logo) {
  return pendingBackground != background || pendingGlassColor != glassColor ||
         pendingFont != fontSetting || pendingAmbient != ambient ||
         pendingLogo != logo;
}

#define VMC_PICKER_MAX_FILES 128

static int gameVMCDirectory(Target *target, char *path, size_t pathSize) {
  const char *mountpoint = storageVMCRoot(target->device);
  if (!mountpoint || !mountpoint[0]) return 0;
  size_t mountLength = strlen(mountpoint);
  int written = snprintf(path, pathSize, "%s%sVMC", mountpoint,
                         mountLength > 0 && mountpoint[mountLength - 1] == '/' ?
                             "" : "/");
  return written >= 0 && (size_t)written < pathSize;
}

static void drawGameVMCProgress(int percent, void *context) {
  char message[64];
  snprintf(message, sizeof(message), "Creating card: %d%%", percent);
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();
  const int baseX = keepoutArea + 10;
  drawTextWindow(baseX, headerHeight - getFontLineHeight(),
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, "Options");
  drawOptionsSection(headerHeight + getFontLineHeight() / 4,
                     "Virtual memory cards", 0);
  drawTextWindow(baseX, gsGlobal->Height / 2,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, message);
  drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, "Please wait until creation finishes");
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
  (void)context;
}

static int createNextGameVMC(OptionsMenuState *state, char *created,
                             size_t createdSize) {
  char directory[PATH_MAX + 1];
  struct stat info;
  if (!gameVMCDirectory(state->target, directory, sizeof(directory)))
    return 0;
  if (stat(directory, &info) != 0 && mkdir(directory, 0777) != 0)
    return 0;
  for (int index = 1; index <= 999; index++) {
    char name[24];
    snprintf(name, sizeof(name), "LUNA_%03d.bin", index);
    int written = snprintf(created, createdSize, "%s/%s", directory, name);
    if (written < 0 || (size_t)written >= createdSize)
      return 0;
    if (stat(created, &info) == 0)
      continue;
    drawGameVMCProgress(0, state);
    return lunaCreateVMC8(created, drawGameVMCProgress, state) == 0;
  }
  return 0;
}

static int vmcFileCompare(const void *left, const void *right) {
  return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static int vmcImageFilename(const char *name) {
  const char *extension = strrchr(name, '.');
  return extension != NULL && strlen(extension) == 4 &&
         tolower((unsigned char)extension[1]) == 'b' &&
         tolower((unsigned char)extension[2]) == 'i' &&
         tolower((unsigned char)extension[3]) == 'n' &&
         extension[4] == '\0';
}

static int uiVMCPickerLoop(Target *target, ArgumentList *arguments,
                           LunaGameOptions *gameOptions, int slot) {
  char directoryPath[PATH_MAX + 1];
  char *files[VMC_PICKER_MAX_FILES];
  int count = 0;
  int selected = 0;
  int changed = 0;
  int supported = gameVMCDirectory(target, directoryPath, sizeof(directoryPath));
  DIR *directory = supported ? opendir(directoryPath) : NULL;
  if (directory != NULL) {
    struct dirent *entry;
    while (count < VMC_PICKER_MAX_FILES && (entry = readdir(directory)) != NULL) {
      if (entry->d_type == DT_DIR || !vmcImageFilename(entry->d_name))
        continue;
      files[count] = strdup(entry->d_name);
      if (files[count] == NULL)
        break;
      count++;
    }
    closedir(directory);
    qsort(files, count, sizeof(files[0]), vmcFileCompare);
  }

  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet();
    const int baseX = keepoutArea + 10;
    const int lineHeight = getFontLineHeight();
    const int menuTop = headerHeight + 2 * lineHeight + 8;
    const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
    const int rowStep = lineHeight + lineHeight / 3;
    const int rowRight = gsGlobal->Width - baseX;
    const int focusY = menuTop + selected * rowStep;
    const int scrollOffset = focusY + lineHeight > menuBottom
                                 ? focusY + lineHeight - menuBottom : 0;
    drawTextWindow(baseX, headerHeight - lineHeight, gsGlobal->Width - baseX,
                   0, 0, HeaderTextColor, ALIGN_HCENTER, "Options");
    drawOptionsSection(headerHeight + lineHeight / 4,
                       slot == 0 ? "VMC slot 1" : "VMC slot 2", 0);
    drawTextWindow(baseX, menuTop - lineHeight, gsGlobal->Width - baseX,
                   menuTop, 0, HeaderTextColor, ALIGN_HCENTER,
                   gameOptions->vmcSlotLabel[slot]);
    for (int index = 0; index <= count; index++) {
      int y = menuTop + index * rowStep - scrollOffset;
      if (y < menuTop || y + lineHeight > menuBottom)
        continue;
      drawOptionsTextRow(baseX, y, rowRight,
                         index == selected, y,
                         index == 0 ? "Physical card" : files[index - 1], NULL);
    }
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, !supported ? "File cards need enabled local storage. Use a physical card."
                               : count ? "Select a card from /VMC on this drive."
                                     : "Create a card from the Virtual memory cards page.");
    const ButtonPrompt assign[] = {
        {ICON_CROSS, "Assign"}, {ICON_TRIANGLE, "Back"}};
    drawPromptBar(baseX + 18, gsGlobal->Height - footerHeight,
                  gsGlobal->Width - baseX, gsGlobal->Height, 0,
                  HeaderTextColor, (PromptBar){NULL, assign, 2});
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    int input = waitForInput(-1);
    if (input & PAD_TRIANGLE)
      break;
    if (input & PAD_UP)
      selected = (selected + count) % (count + 1);
    else if (input & PAD_DOWN)
      selected = (selected + 1) % (count + 1);
    else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      char path[PATH_MAX + 1];
      if (selected == 0) {
        path[0] = '\0';
      } else if (snprintf(path, sizeof(path), "%s/%s", directoryPath,
                          files[selected - 1]) >= sizeof(path)) {
        continue;
      }
      changed = lunaGameOptionsSetVMC(gameOptions, arguments, slot, path);
      break;
    }
  }
  for (int index = 0; index < count; index++)
    free(files[index]);
  return changed;
}

static int handleOrbsInput(OptionsMenuState *state, int input) {
  if (!optionsOrbsEditable(state)) {
    if (input & PAD_TRIANGLE) {
      state->page = OPTIONS_VIEWS;
      state->selector.initialized = 0;
      state->saveError = 0;
    }
    return 0;
  }
  const int rowCount = optionsOrbsColorsEditable(state) ? 4 : 2;
  if (state->selectedOrbsRow >= rowCount)
    state->selectedOrbsRow = 1;
  if (input & PAD_UP) {
    state->selectedOrbsRow = (state->selectedOrbsRow + rowCount - 1) % rowCount;
  } else if (input & PAD_DOWN) {
    state->selectedOrbsRow = (state->selectedOrbsRow + 1) % rowCount;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    const int direction = input & PAD_LEFT ? -1 : 1;
    if (state->selectedOrbsRow == 0)
      state->pendingOrbsTheme = state->pendingOrbsTheme == ORBS_THEME_LUNA ?
                         ORBS_THEME_PS2_ORIGINAL : ORBS_THEME_LUNA;
    else if (state->selectedOrbsRow == 1) {
      state->pendingOrbsAppearance = state->pendingOrbsAppearance == ORBS_APPEARANCE_LUNA ?
                              ORBS_APPEARANCE_PS2_ORIGINAL : ORBS_APPEARANCE_LUNA;
      if (!optionsOrbsColorsEditable(state)) {
        state->pendingOrbsColor = *state->orbsColorSetting;
        state->pendingTailsColor = *state->tailsColorSetting;
      }
    } else if (state->selectedOrbsRow == 2 && optionsOrbsColorsEditable(state))
      state->pendingOrbsColor = (state->pendingOrbsColor + direction + ORBS_COLOR_COUNT) %
                         ORBS_COLOR_COUNT;
    else if (state->selectedOrbsRow == 3 && optionsOrbsColorsEditable(state))
      state->pendingTailsColor = (state->pendingTailsColor + direction + ORBS_COLOR_COUNT) %
                          ORBS_COLOR_COUNT;
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingOrbsTheme != *state->orbsThemeSetting) {
      state->saveErrorLabel = "Could not save orb behavior";
      state->saveError = saveAmbientOrbsTheme(state->target,
                        (AmbientOrbsTheme)state->pendingOrbsTheme);
      if (!state->saveError) {
        *state->orbsThemeSetting = state->pendingOrbsTheme;
        setAmbientOrbsTheme((AmbientOrbsTheme)state->pendingOrbsTheme, uiNowMs());
      }
    }
    if (!state->saveError && state->pendingOrbsAppearance != *state->orbsAppearanceSetting) {
      state->saveErrorLabel = "Could not save orb appearance";
      if (setAmbientOrbsAppearance((AmbientOrbsAppearance)state->pendingOrbsAppearance)) {
        state->saveError = -1;
      } else {
        state->saveError = saveAmbientOrbsAppearance(state->target,
                         (AmbientOrbsAppearance)state->pendingOrbsAppearance);
        if (!state->saveError)
          *state->orbsAppearanceSetting = state->pendingOrbsAppearance;
        else
          setAmbientOrbsAppearance((AmbientOrbsAppearance)*state->orbsAppearanceSetting);
      }
    }
    if (!state->saveError && optionsOrbsColorsEditable(state) &&
        state->pendingOrbsColor != *state->orbsColorSetting) {
      state->saveErrorLabel = "Could not save orb color";
      state->saveError = saveAmbientOrbsColor(state->target, ORBS_COLOR_PART_ORBS,
                                      (AmbientOrbsColor)state->pendingOrbsColor);
      if (!state->saveError) {
        *state->orbsColorSetting = state->pendingOrbsColor;
        setAmbientOrbsColor(ORBS_COLOR_PART_ORBS,
                            (AmbientOrbsColor)state->pendingOrbsColor);
      }
    }
    if (!state->saveError && optionsOrbsColorsEditable(state) &&
        state->pendingTailsColor != *state->tailsColorSetting) {
      state->saveErrorLabel = "Could not save tail color";
      state->saveError = saveAmbientOrbsColor(state->target, ORBS_COLOR_PART_TAILS,
                                      (AmbientOrbsColor)state->pendingTailsColor);
      if (!state->saveError) {
        *state->tailsColorSetting = state->pendingTailsColor;
        setAmbientOrbsColor(ORBS_COLOR_PART_TAILS,
                            (AmbientOrbsColor)state->pendingTailsColor);
      }
    }
  } else if (input & PAD_TRIANGLE) {
    return 1;
  }
  return 0;
}

static int handleViewsInput(OptionsMenuState *state, int input) {
  if (input & PAD_UP) {
    state->selectedView = (state->selectedView + OPTIONS_VIEW_ROW_COUNT - 1) %
                   OPTIONS_VIEW_ROW_COUNT;
  } else if (input & PAD_DOWN) {
    state->selectedView = (state->selectedView + 1) % OPTIONS_VIEW_ROW_COUNT;
  } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
    if (state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW) {
      state->pendingOverlap = !state->pendingOverlap;
    } else {
      uint32_t bit = 1U << lunaViewCycleOrder[state->selectedView];
      if (state->pendingViews != bit)
        state->pendingViews ^= bit;
    }
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingOverlap != *state->classicArtOverlap) {
      state->saveErrorLabel = "Could not save List art layout";
      state->saveError = saveClassicArtOverlap(state->target, state->pendingOverlap);
      if (!state->saveError) {
        *state->classicArtOverlap = state->pendingOverlap;
        setClassicArtOverlap(state->pendingOverlap);
      }
    }
    if (!state->saveError && state->pendingViews != *state->enabledViews) {
      state->saveErrorLabel = "Could not save enabled views";
      state->saveError = saveEnabledLibraryViews(state->target, state->pendingViews);
      if (!state->saveError)
        *state->enabledViews = state->pendingViews;
    }
  } else if (input & PAD_TRIANGLE) {
    return 1;
  }
  return 0;
}

static int handleSystemInput(OptionsMenuState *state, int input) {
  if (input & PAD_UP) {
    state->selectedGlobal = (state->selectedGlobal + 4) % 5;
  } else if (input & PAD_DOWN) {
    state->selectedGlobal = (state->selectedGlobal + 1) % 5;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    const int direction = (input & PAD_LEFT) ? -1 : 1;
    if (state->selectedGlobal == 0)
      state->pendingBackground = (state->pendingBackground +
                                  direction + LIBRARY_BACKGROUND_COUNT) %
                                 LIBRARY_BACKGROUND_COUNT;
    else if (state->selectedGlobal == 1)
      changeOptionsGlassColor(state,
          (state->pendingGlassColor + direction + GLASS_COLOR_COUNT) % GLASS_COLOR_COUNT);
    else if (state->selectedGlobal == 2)
      state->pendingFont = (state->pendingFont + direction + UI_FONT_COUNT) % UI_FONT_COUNT;
    else if (state->selectedGlobal == 3)
      state->pendingAmbient = !state->pendingAmbient;
    else
      state->pendingLogo = !state->pendingLogo;
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingBackground != *state->ambientOrbsBackgroundSetting) {
      if (setLibraryBackground((LibraryBackground)state->pendingBackground)) {
        state->saveError = -1;
        state->saveErrorLabel = "Could not load background";
      } else {
        int result = saveLibraryBackground(state->target,
                                           (LibraryBackground)state->pendingBackground);
        if (!result)
          *state->ambientOrbsBackgroundSetting = state->pendingBackground;
        else {
          setLibraryBackground((LibraryBackground)*state->ambientOrbsBackgroundSetting);
          state->saveError = result;
          state->saveErrorLabel = "Could not save background";
        }
      }
    }
    if (state->pendingGlassColor != *state->glassColorSetting) {
      int result = saveGlassColorPreset(state->target,
                                       (GlassColorPreset)state->pendingGlassColor);
      if (!result)
        *state->glassColorSetting = state->pendingGlassColor;
      else if (!state->saveError) {
        state->saveError = result;
        state->saveErrorLabel = "Could not save glass color";
      }
    }
    if (state->pendingFont != *state->fontSetting) {
      if (setUIFont((UIFont)state->pendingFont)) {
        if (!state->saveError) {
          state->saveError = -1;
          state->saveErrorLabel = "Could not load font";
        }
      } else {
        int result = saveUIFont(state->target, (UIFont)state->pendingFont);
        if (!result)
          *state->fontSetting = state->pendingFont;
        else {
          setUIFont((UIFont)*state->fontSetting);
          if (!state->saveError) {
            state->saveError = result;
            state->saveErrorLabel = "Could not save font";
          }
        }
      }
    }
    if (state->pendingAmbient != *state->ambientEnabled) {
      int result = saveAmbientSoundEnabled(state->target, state->pendingAmbient);
      if (!result) {
        *state->ambientEnabled = state->pendingAmbient;
        ambientSetEnabled(state->pendingAmbient);
      } else if (!state->saveError) {
        state->saveError = result;
        state->saveErrorLabel = "Could not save ambient sound";
      }
    }
    if (state->pendingLogo != state->logoSetting) {
      int result = savePS2LogoEnabled(state->target, state->pendingLogo);
      if (!result) {
        state->logoSetting = state->pendingLogo;
        lunaApplyGlobalPS2Logo(state->titleArguments, state->logoSetting);
        lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
      } else if (!state->saveError) {
        state->saveError = result;
        state->saveErrorLabel = "Could not save PS2 logo setting";
      }
    }
  } else if (input & PAD_TRIANGLE) {
    return 1;
  }
  return 0;
}

// Returns 1 for Back, -1 if a test launch unexpectedly returns, or 0 to stay.
static int handleGameInput(OptionsMenuState *state, int input) {
  if (input & PAD_SQUARE) {
    // Launch title without saving arguments
    uiLaunchTitle(state->target, state->titleArguments);
    return -1;
  }
  if (input & PAD_START) {
    state->saveErrorLabel = "Could not save game settings";
    state->saveError = updateTitleLaunchArguments(state->target, state->titleArguments);
    if (!state->saveError)
      state->titleArgumentsChanged = 0;
    return 0;
  }
  if (state->gameSection == GAME_HUB) {
    if (input & PAD_TRIANGLE)
      return 1;
    if (input & PAD_UP)
      state->selectedGameHubRow = (state->selectedGameHubRow +
                                   GAME_SECTION_COUNT - 1) % GAME_SECTION_COUNT;
    else if (input & PAD_DOWN)
      state->selectedGameHubRow = (state->selectedGameHubRow + 1) % GAME_SECTION_COUNT;
    else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      state->gameSection = (GameSection)state->selectedGameHubRow;
      state->selectedGameRow = 0;
      state->vmcStatus[0] = '\0';
      state->selector.initialized = 0;
    }
    return 0;
  }
  if (input & PAD_TRIANGLE) {
    state->gameSection = state->gameSection == GAME_VIDEO_OUT ? GAME_VIDEO : GAME_HUB;
    state->selectedGameRow = 0;
    state->selector.initialized = 0;
    return 0;
  }
  int count = state->gameSection == GAME_MEMORY_CARDS &&
              !gameVMCEnabled(state->titleArguments) ? 3 :
              gameSectionRowCount(state->gameSection);
  if (input & PAD_UP) {
    state->selectedGameRow = (state->selectedGameRow + count - 1) % count;
    return 0;
  }
  if (input & PAD_DOWN) {
    state->selectedGameRow = (state->selectedGameRow + 1) % count;
    return 0;
  }
  if (state->gameSection == GAME_VIDEO_OUT) {
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int current = state->gameOptions.videoMode;
      int mode = current == state->selectedGameRow ? 0 : state->selectedGameRow;
      if (lunaGameOptionsSetVideoMode(&state->gameOptions, state->titleArguments,
                                     mode))
        state->titleArgumentsChanged = 1;
    }
    return 0;
  }
  if (state->gameSection == GAME_MEMORY_CARDS &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    if (state->selectedGameRow == 2) {
      char created[PATH_MAX + 1];
      if (!storageVMCRoot(state->target->device)) {
        snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                 "File cards need enabled local storage. MMCE switches cards automatically when enabled.");
        return 0;
      }
      if (createNextGameVMC(state, created, sizeof(created)))
        snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                 "Created %.24s. Select a slot to assign it.",
                 strrchr(created, '/') + 1);
      else
        snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                 "Could not create card. Check drive and free space.");
      return 0;
    }
    if (state->selectedGameRow == 3) {
      if (gameVMCEnabled(state->titleArguments)) {
        if (lunaGameOptionsSetVMC(&state->gameOptions,
                                  state->titleArguments, 0, "") &&
            lunaGameOptionsSetVMC(&state->gameOptions,
                                  state->titleArguments, 1, "")) {
          state->titleArgumentsChanged = 1;
          state->selectedGameRow = 0;
          state->selector.initialized = 0;
          snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                   "Virtual cards disabled. Press Start to save.");
        } else {
          snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                   "Could not disable virtual cards.");
        }
      }
      return 0;
    }
  }
  if (state->gameSection == GAME_MEMORY_CARDS && state->selectedGameRow >= 2)
    return 0;
  if (state->gameSection == GAME_MEMORY_CARDS &&
      (input & (PAD_LEFT | PAD_RIGHT)))
    return 0;
  LunaGameRow selectedRow = gameSectionRow(state->gameSection,
                                            state->selectedGameRow);
  if (selectedRow == LUNA_GAME_VIDEO_MODE &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    state->gameSection = GAME_VIDEO_OUT;
    int mode = state->gameOptions.videoMode;
    state->selectedGameRow = mode > 0 ? mode : 0;
    state->videoOutFirstRow = 0;
    state->selector.initialized = 0;
  } else if ((selectedRow == LUNA_GAME_VMC_SLOT1 || selectedRow == LUNA_GAME_VMC_SLOT2) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    int slot = selectedRow - LUNA_GAME_VMC_SLOT1;
    if (uiVMCPickerLoop(state->target, state->titleArguments, &state->gameOptions, slot)) {
      state->titleArgumentsChanged = 1;
      snprintf(state->vmcStatus, sizeof(state->vmcStatus),
               "Card selection changed. Press Start to save.");
    }
  } else if (selectedRow == LUNA_GAME_LAUNCH_ARGUMENTS &&
             (input & (PAD_CROSS | PAD_CIRCLE))) {
    int result = uiArgumentListLoop(state->target, state->titleArguments);
    if (result < 0)
      return -1;
    if (result == 2)
      state->titleArgumentsChanged = 0;
    else if (result == 1)
      state->titleArgumentsChanged = 1;
    lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    int direction = (input & PAD_LEFT) ? -1 : 1;
    int changed = selectedRow == LUNA_GAME_PS2_LOGO ?
        lunaGameOptionsCyclePS2Logo(&state->gameOptions,
                                    state->titleArguments,
                                    state->logoSetting, direction) :
        lunaGameOptionsChange(&state->gameOptions, state->titleArguments,
                              selectedRow, direction);
    if (changed)
      state->titleArgumentsChanged = 1;
  }
  return 0;
}

// Runs the Game, Global, Views, and Orbs settings tabs.
// Returns -1 if a test launch unexpectedly returns.
int uiTitleOptionsLoop(Target *target, int *classicArtOverlap,
                       int *ambientOrbsBackgroundSetting, int *glassColorSetting,
                       int *fontSetting, int *ambientEnabled, int *orbsThemeSetting,
                       int *orbsAppearanceSetting, int *orbsColorSetting,
                       int *tailsColorSetting,
                        uint32_t *enabledViews) {
  int res = 0;
  int logoSetting = loadPS2LogoEnabled(target);
  OptionsMenuState state = {
      .target = target,
      .classicArtOverlap = classicArtOverlap,
      .ambientOrbsBackgroundSetting = ambientOrbsBackgroundSetting,
      .glassColorSetting = glassColorSetting,
      .fontSetting = fontSetting,
      .ambientEnabled = ambientEnabled,
      .logoSetting = logoSetting,
      .orbsThemeSetting = orbsThemeSetting,
      .orbsAppearanceSetting = orbsAppearanceSetting,
      .orbsColorSetting = orbsColorSetting,
      .tailsColorSetting = tailsColorSetting,
      .enabledViews = enabledViews,
      .pendingOverlap = *classicArtOverlap,
      .pendingBackground = *ambientOrbsBackgroundSetting,
      .pendingGlassColor = *glassColorSetting,
      .pendingFont = *fontSetting,
      .pendingAmbient = *ambientEnabled,
      .pendingLogo = logoSetting,
      .pendingOrbsTheme = *orbsThemeSetting,
      .pendingOrbsAppearance = *orbsAppearanceSetting,
      .pendingOrbsColor = *orbsColorSetting,
      .pendingTailsColor = *tailsColorSetting,
      .pendingViews = *enabledViews,
      .page = OPTIONS_PER_GAME,
      .gameSection = GAME_HUB,
      .selectedView = UI_VIEW_CLASSIC};
  snprintf(state.titleHeader, sizeof(state.titleHeader), "%.38s  (%s)",
           target->name, target->id);

  // Load arguments from config files
  state.titleArguments = loadLaunchArgumentLists(state.target);
  lunaGameOptionsRead(&state.gameOptions, state.titleArguments);
  int input = 0;

  uint32_t transitionStart = uiNowMs();
  int transitionProgress;
  do {
    transitionProgress = optionsTransitionProgress(
        uiNowMs() - transitionStart, OPTIONS_OPEN_DURATION_MS);
    drawTitleOptionsFrame(&state, transitionProgress, 1);
    pollInput();
  } while (transitionProgress < 1000);

  while (1) {
    drawTitleOptionsFrame(&state, 0, 0);

    // Process user inputs
    input = readInput();
    if (input & PAD_L1) {
      state.page = optionsNextPage(state.page, -1);
      state.selector.initialized = 0;
      state.saveError = 0;
      continue;
    }
    if (input & PAD_R1) {
      state.page = optionsNextPage(state.page, 1);
      state.selector.initialized = 0;
      state.saveError = 0;
      continue;
    }
    if (state.page == OPTIONS_ORBS) {
      if (handleOrbsInput(&state, input))
        goto exit;
      continue;
    }
    if (state.page == OPTIONS_VIEWS) {
      if (handleViewsInput(&state, input))
        goto exit;
      continue;
    }
    if (state.page == OPTIONS_GLOBAL) {
      if (handleSystemInput(&state, input))
        goto exit;
      continue;
    }
    int action = handleGameInput(&state, input);
    if (action != 0) {
      res = action < 0 ? -1 : 0;
      goto exit;
    }
  }
exit:
  if (res >= 0) {
    state.pendingOrbsTheme = *state.orbsThemeSetting;
    state.pendingOrbsAppearance = *state.orbsAppearanceSetting;
    state.pendingOrbsColor = *state.orbsColorSetting;
    state.pendingTailsColor = *state.tailsColorSetting;
    transitionStart = uiNowMs();
    do {
      transitionProgress = optionsTransitionProgress(
          uiNowMs() - transitionStart, OPTIONS_CLOSE_DURATION_MS);
      drawTitleOptionsFrame(&state, transitionProgress, 2);
    } while (transitionProgress < 1000);
  }
  // Restore the saved color while the closing frame is fully black.
  setGlassColorPreset((GlassColorPreset)*state.glassColorSetting);
  freeArgumentList(state.titleArguments);
  return res;
}

// LUNA's advanced view of the merged game and global launch arguments.
// Returns -1 after a failed launch, 0 on Back, 1 after edits, or 2 after Save.
static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments) {
  int selectedArgIdx = 0;
  int saveError = 0;
  int changed = 0;
  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet();
    const int baseX = keepoutArea + 10;
    const int lineHeight = getFontLineHeight();
    const int menuTop = headerHeight + 2 * lineHeight + 8;
    const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
    const int rowStep = lineHeight + lineHeight / 3;
    const int focusY = menuTop + selectedArgIdx * rowStep;
    const int scrollOffset = focusY + lineHeight > menuBottom
                                 ? focusY + lineHeight - menuBottom : 0;

    drawTextWindow(baseX, headerHeight - lineHeight, gsGlobal->Width - baseX,
                   0, 0, HeaderTextColor, ALIGN_HCENTER, "Options");
    drawOptionsSection(headerHeight + lineHeight / 4,
                       "Launch arguments", 0);
    snprintf(lineBuffer, sizeof(lineBuffer), "%.38s  (%s)", target->name, target->id);
    drawTextWindow(baseX, menuTop - lineHeight, gsGlobal->Width - baseX,
                   menuTop, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    if (titleArguments->total == 0)
      drawTextWindow(baseX + 18, menuTop, gsGlobal->Width - baseX, 0, 0,
                     FontMainColor, ALIGN_LEFT, "No launch arguments set");

    Argument *argument = titleArguments->first;
    for (int index = 0; argument != NULL; index++, argument = argument->next) {
      int y = menuTop + index * rowStep - scrollOffset;
      if (y < menuTop || y + lineHeight > menuBottom)
        continue;
      const char *value = argument->value != NULL ? argument->value : "";
      snprintf(lineBuffer, sizeof(lineBuffer), "%s%s%s%s",
               argument->isGlobal ? "[G] " : "",
               argument->arg, value[0] ? ": " : "", value);
      drawOptionsTextRow(baseX, y, gsGlobal->Width - baseX,
                         index == selectedArgIdx, y, lineBuffer,
                         argument->isDisabled ? "Off" : "On");
    }
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, "[G] Inherited from global settings");
    drawOptionsFooter(1, 1, changed, 0, NULL, 0);
    if (saveError)
      drawTextWindow(baseX, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, ErrorTextColor, ALIGN_HCENTER,
                     "Could not save game settings");
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    int input = waitForInput(-1);
    if (input & PAD_SQUARE) {
      uiLaunchTitle(target, titleArguments);
      return -1;
    } else if (input & PAD_START) {
      saveError = updateTitleLaunchArguments(target, titleArguments);
      if (!saveError)
        return 2;
    } else if (input & PAD_TRIANGLE) {
      return changed;
    }
    if (!curArgument)
      continue;
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      curArgument->isDisabled = !curArgument->isDisabled;
      changed = 1;
      if (curArgument->isDisabled)
        curArgument->isGlobal = 0;
    } else if (input & PAD_UP) {
      selectedArgIdx = (selectedArgIdx - 1 + titleArguments->total) % titleArguments->total;
      curArgument = (curArgument->prev) ? curArgument->prev : titleArguments->last;
    } else if (input & PAD_DOWN) {
      selectedArgIdx = (selectedArgIdx + 1) % titleArguments->total;
      curArgument = (curArgument->next) ? curArgument->next : titleArguments->first;
    }
  }
}
