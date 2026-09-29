// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include <ctype.h>
#include <dirent.h>
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPTIONS_OPEN_DURATION_MS 360
#define OPTIONS_CLOSE_DURATION_MS 180
#define OPTIONS_SELECTION_GLOW_DURATION_MS 110
#define OPTIONS_GLOW_ROW_SCALE 256

typedef enum {
  OPTIONS_PER_GAME,
  OPTIONS_GLOBAL,
  OPTIONS_VIEWS,
  OPTIONS_ORBS
} OptionsPage;

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
  int pendingOrbsTheme;
  int pendingOrbsAppearance;
  int pendingOrbsColor;
  int pendingTailsColor;
  uint32_t pendingViews;
  int titleArgumentsChanged;
  int saveError;
  OptionsPage page;
  OptionsSelector selector;
  int selectedGameRow;
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
    "Video mode", "Field flipping",
    "Show PS2 logo", "Debug colors"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Review every launch argument, including global overrides.",
    "Assign an existing OPL card image from this drive's VMC folder.",
    "Assign an existing OPL card image to the second slot.",
    "Choose a forced output mode for this game.",
    "Choose field flipping for a forced video mode.",
    "Show the PlayStation 2 startup logo.",
    "Display debug colors while loading."};

static const char *const viewRowLabels[UI_VIEW_ORBS + 1] = {
    "Classic", "Collection", "Grid", "Orbit", "Scroll"};

#define OPTIONS_VIEW_ART_LAYOUT_ROW 1
#define OPTIONS_VIEW_ROW_COUNT (UI_VIEW_ORBS + 2)

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
                              int placeholder) {
  const int top = gsGlobal->Height - footerHeight;
  const int width = gsGlobal->Width;
  const int slot = width / 4;
  if (!placeholder) {
    const ButtonPrompt action[] = {
        {ICON_CROSS, argumentList ? "Toggle" : "Change"}};
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
      {ICON_TRIANGLE, argumentList ? "Back" : (dirty ? "Discard" : "Close")}};
  drawPromptBar(3 * slot, top, width - 8, gsGlobal->Height, 0,
                HeaderTextColor, (PromptBar){NULL, back, 1});
}

static int optionsSelectorProgress(const OptionsSelector *selector, uint32_t now) {
  uint32_t elapsed = now - selector->startMs;
  if (elapsed >= OPTIONS_SELECTION_GLOW_DURATION_MS)
    return 1000;
  int progress = (int)(elapsed * 1000ULL / OPTIONS_SELECTION_GLOW_DURATION_MS);
  return (int)((int64_t)progress * progress * (3000 - 2 * progress) / 1000000);
}

static int optionsSelectorY(OptionsSelector *selector, OptionsPage page,
                            int selectedRow, int selectedY) {
  const uint32_t now = uiNowMs();
  if (!selector->initialized || selector->page != page) {
    selector->page = page;
    selector->selectedRow = selectedRow;
    selector->fromRow = selectedY * OPTIONS_GLOW_ROW_SCALE;
    selector->toRow = selector->fromRow;
    selector->startMs = now;
    selector->initialized = 1;
  } else if (selector->selectedRow != selectedRow) {
    int progress = optionsSelectorProgress(selector, now);
    selector->fromRow += (selector->toRow - selector->fromRow) * progress / 1000;
    selector->toRow = selectedY * OPTIONS_GLOW_ROW_SCALE;
    selector->selectedRow = selectedRow;
    selector->startMs = now;
  }
  int progress = optionsSelectorProgress(selector, now);
  int row = selector->fromRow +
            (selector->toRow - selector->fromRow) * progress / 1000;
  return row / OPTIONS_GLOW_ROW_SCALE;
}

static void drawOptionsTextRow(int x, int y, int right, int selected,
                               int selectorY, const char *label,
                               const char *value) {
  if (selected) {
    drawPSBBNFocusGlow(x, selectorY, right, right - 12);
  }
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

static int optionsGlobalRowY(int index, int firstY, int rowStep, int lineHeight) {
  return firstY + index * rowStep + (index == 3 ? lineHeight : 0);
}

static int optionsViewRowForLibraryView(int view) {
  return view == UI_VIEW_CLASSIC ? 0 : view + 1;
}

static int optionsGameRowY(int index, int firstY, int rowStep) {
  if (index <= LUNA_GAME_LAUNCH_ARGUMENTS)
    return firstY + (index + 1) * rowStep;
  if (index <= LUNA_GAME_VMC_SLOT2)
    return firstY + (index + 2) * rowStep;
  if (index <= LUNA_GAME_FIELD_FLIP)
    return firstY + (index + 3) * rowStep;
  return firstY + (index + 4) * rowStep;
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
                              int pendingFont, int pendingAmbient,
                              int background, int glassColor, int fontSetting,
                              int ambient);

static void drawTitleOptionsFrame(OptionsMenuState *state,
                                  int transitionProgress, int transitionMode) {
  const int showDirty = transitionMode != 2;
  const int gameDirty = showDirty && state->titleArgumentsChanged;
  const int systemDirty = showDirty && optionsGlobalDirty(
      state->pendingBackground, state->pendingGlassColor,
      state->pendingFont, state->pendingAmbient,
      *state->ambientOrbsBackgroundSetting, *state->glassColorSetting,
      *state->fontSetting, *state->ambientEnabled);
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
  static const char *const tabLabels[] = {"Game", "System", "Views", "Orbs"};
  static const char *const dirtyTabLabels[] = {"Game *", "System *", "Views *", "Orbs *"};
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
        "Stars & cubes", "Ambient Orbs", "Red Clouds", "Midnight Cubes"};
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
    static const char *const descriptions[] = {
        "Choose stars, orbs, or the moving red clouds.",
        "Change the tint of the glass interface.",
        "Use LUNA's font or the PSBBN keyboard lettering.",
        "Play ambient music while browsing."};
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT, descriptions[state->selectedGlobal]);
  } else if (state->page == OPTIONS_VIEWS) {
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedView,
                                     firstY + state->selectedView * rowStep);
    drawOptionsSection(menuTop, "Library views", 0);
    for (int row = UI_VIEW_CLASSIC; row <= UI_VIEW_ORBS; row++) {
      const int optionRow = optionsViewRowForLibraryView(row);
      drawOptionsTextRow(baseX, firstY + optionRow * rowStep,
                         gsGlobal->Width - baseX, state->selectedView == optionRow,
                         selectorY, viewRowLabels[row],
                         state->pendingViews & (1U << row) ? "On" : "Off");
    }
    drawOptionsTextRow(baseX + 18,
                       firstY + OPTIONS_VIEW_ART_LAYOUT_ROW * rowStep,
                       gsGlobal->Width - baseX,
                       state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW, selectorY,
                       "Classic art layout",
                       state->pendingOverlap ? "Overlap" : "Separate");
    if (state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW)
      drawTextWindow(baseX + 18, menuBottom - lineHeight,
                     gsGlobal->Width - baseX, menuBottom, 0,
                     HeaderTextColor, ALIGN_LEFT,
                     "Choose how cover art sits in Classic view.");
    else {
      const ButtonPrompt cycle[] = {
          {ICON_CIRCLE, "Cycle views; keep at least one on"}};
      drawPromptBar(baseX + 18, menuBottom - lineHeight,
                    gsGlobal->Width - baseX, menuBottom, 0,
                    HeaderTextColor, (PromptBar){NULL, cycle, 1});
    }
  } else if (state->page == OPTIONS_ORBS) {
    static const char *const colorLabels[ORBS_COLOR_COUNT] = {
        "Original", "Cyan", "Violet", "Rose", "Green", "Gold", "White"};
    drawOptionsSection(menuTop, "Orbs", 0);
    const int firstY = menuTop + lineHeight + 4;
    const int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedOrbsRow,
                                           firstY + state->selectedOrbsRow * rowStep);
    drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                       state->selectedOrbsRow == 0, selectorY,
                       "Behavior", state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ?
                       "PS2 original" : "LUNA");
    drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                       state->selectedOrbsRow == 1, selectorY,
                       "Appearance", state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                       "PS2 original" : "LUNA");
    drawOptionsTextRow(baseX, firstY + 2 * rowStep, gsGlobal->Width - baseX,
                       state->selectedOrbsRow == 2, selectorY,
                       "Orb color", colorLabels[state->pendingOrbsColor]);
    drawOptionsTextRow(baseX, firstY + 3 * rowStep, gsGlobal->Width - baseX,
                       state->selectedOrbsRow == 3, selectorY,
                       "Tail color", colorLabels[state->pendingTailsColor]);
    drawTextWindow(baseX, menuBottom - 2 * lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_HCENTER,
                   state->selectedOrbsRow >= 2 ?
                       "Color choices affect only the shared orb background." :
                   state->selectedOrbsRow == 0 ?
                       (state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ?
                        "Seven clock-driven lights with long trails." :
                        "Animated LUNA formations and title reactions.") :
                       (state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                        "Original halo and core masks from the PS2 ROM." :
                        "LUNA's soft glass lights and bright cores."));
  } else {
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    const int contentTop = menuTop + lineHeight + 5;
    const int gameStep = lineHeight + lineHeight / 3;
    CardArtType selectedCardArt = state->selectedGameRow == LUNA_GAME_VMC_SLOT1 ?
                                  CARD_ART_SLOT_1 :
                                  state->selectedGameRow == LUNA_GAME_VMC_SLOT2 ?
                                  CARD_ART_SLOT_2 : CARD_ART_NONE;
    int gameRight = gsGlobal->Width - baseX -
                    (selectedCardArt == CARD_ART_NONE ? 0 : 128);
    int selectedY = optionsGameRowY(state->selectedGameRow, contentTop, gameStep);
    int scrollOffset = selectedY + lineHeight > menuBottom
                           ? selectedY + lineHeight - menuBottom : 0;
    int focusY = selectedY - scrollOffset;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedGameRow, focusY);
    const int headingRows[] = {LUNA_GAME_FAST_READS, LUNA_GAME_VMC_SLOT1,
                               LUNA_GAME_VIDEO_MODE,
                               LUNA_GAME_PS2_LOGO};
    const char *const headings[] = {"Compatibility", "Memory cards", "Video", "Launch"};
    for (int section = 0; section < 4; section++) {
      int y = optionsGameRowY(headingRows[section], contentTop, gameStep) -
              gameStep - scrollOffset;
      if (y >= contentTop && y + lineHeight <= menuBottom)
        drawOptionsSection(y, headings[section], 0);
    }
    for (int row = 0; row < LUNA_GAME_ROW_COUNT; row++) {
      int y = optionsGameRowY(row, contentTop, gameStep) - scrollOffset;
      if (y < contentTop || y + lineHeight > menuBottom)
        continue;
      drawOptionsTextRow(baseX, y, gameRight,
                         row == state->selectedGameRow, selectorY,
                         gameRowLabels[row],
                         lunaGameOptionsValue(&state->gameOptions, (LunaGameRow)row));
    }
    if (selectedCardArt != CARD_ART_NONE)
      drawCardArt(selectedCardArt, gsGlobal->Width - baseX - 116,
                  contentTop + 8, 108);
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, gameRowDescriptions[state->selectedGameRow]);
  }
  drawOptionsFooter(state->page == OPTIONS_PER_GAME, 0,
                    gameDirty || systemDirty || viewsDirty || orbsDirty, 0);
  if (showDirty && state->saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   state->page == OPTIONS_GLOBAL ? "Could not save global settings" :
                   state->page == OPTIONS_VIEWS ? "Could not save views" :
                   state->page == OPTIONS_ORBS ? "Could not save orb settings" :
                                           "Could not save game settings");
  if (transitionMode != 0)
    drawOptionsTransition(transitionProgress, transitionMode == 2);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

static int optionsGlobalDirty(int pendingBackground, int pendingGlassColor,
                              int pendingFont, int pendingAmbient,
                              int background, int glassColor, int fontSetting,
                              int ambient) {
  return pendingBackground != background || pendingGlassColor != glassColor ||
         pendingFont != fontSetting || pendingAmbient != ambient;
}

#define VMC_PICKER_MAX_FILES 128

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
  const char *mountpoint = target->device->mountpoint;
  size_t mountLength = strlen(mountpoint);
  if (snprintf(directoryPath, sizeof(directoryPath), "%s%sVMC", mountpoint,
               mountLength > 0 && mountpoint[mountLength - 1] == '/' ? "" : "/")
      >= sizeof(directoryPath))
    return 0;
  DIR *directory = opendir(directoryPath);
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
    const int showCardPreview = selected == 0;
    const int rowRight = gsGlobal->Width - baseX -
                         (showCardPreview ? 128 : 0);
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
    if (showCardPreview)
      drawCardArt(slot == 0 ? CARD_ART_SLOT_1 : CARD_ART_SLOT_2,
                  gsGlobal->Width - baseX - 116, menuTop + 8, 108);
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, count ? "Select a card from /VMC on this drive."
                                     : "Create a card from Virtual Memory Cards in the main menu.");
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
  if (input & PAD_UP) {
    state->selectedOrbsRow = (state->selectedOrbsRow + 3) % 4;
  } else if (input & PAD_DOWN) {
    state->selectedOrbsRow = (state->selectedOrbsRow + 1) % 4;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    const int direction = input & PAD_LEFT ? -1 : 1;
    if (state->selectedOrbsRow == 0)
      state->pendingOrbsTheme = state->pendingOrbsTheme == ORBS_THEME_LUNA ?
                         ORBS_THEME_PS2_ORIGINAL : ORBS_THEME_LUNA;
    else if (state->selectedOrbsRow == 1)
      state->pendingOrbsAppearance = state->pendingOrbsAppearance == ORBS_APPEARANCE_LUNA ?
                              ORBS_APPEARANCE_PS2_ORIGINAL : ORBS_APPEARANCE_LUNA;
    else if (state->selectedOrbsRow == 2)
      state->pendingOrbsColor = (state->pendingOrbsColor + direction + ORBS_COLOR_COUNT) %
                         ORBS_COLOR_COUNT;
    else
      state->pendingTailsColor = (state->pendingTailsColor + direction + ORBS_COLOR_COUNT) %
                          ORBS_COLOR_COUNT;
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingOrbsTheme != *state->orbsThemeSetting) {
      state->saveError = saveAmbientOrbsTheme(state->target,
                        (AmbientOrbsTheme)state->pendingOrbsTheme);
      if (!state->saveError) {
        *state->orbsThemeSetting = state->pendingOrbsTheme;
        setAmbientOrbsTheme((AmbientOrbsTheme)state->pendingOrbsTheme, uiNowMs());
      }
    }
    if (!state->saveError && state->pendingOrbsAppearance != *state->orbsAppearanceSetting) {
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
    if (!state->saveError && state->pendingOrbsColor != *state->orbsColorSetting) {
      state->saveError = saveAmbientOrbsColor(state->target, ORBS_COLOR_PART_ORBS,
                                      (AmbientOrbsColor)state->pendingOrbsColor);
      if (!state->saveError) {
        *state->orbsColorSetting = state->pendingOrbsColor;
        setAmbientOrbsColor(ORBS_COLOR_PART_ORBS,
                            (AmbientOrbsColor)state->pendingOrbsColor);
      }
    }
    if (!state->saveError && state->pendingTailsColor != *state->tailsColorSetting) {
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
      int view = state->selectedView == UI_VIEW_CLASSIC
                     ? UI_VIEW_CLASSIC : state->selectedView - 1;
      uint32_t bit = 1U << view;
      if (state->pendingViews != bit)
        state->pendingViews ^= bit;
    }
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingOverlap != *state->classicArtOverlap) {
      state->saveError = saveClassicArtOverlap(state->target, state->pendingOverlap);
      if (!state->saveError) {
        *state->classicArtOverlap = state->pendingOverlap;
        setClassicArtOverlap(state->pendingOverlap);
      }
    }
    if (!state->saveError && state->pendingViews != *state->enabledViews) {
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
    state->selectedGlobal = (state->selectedGlobal + 3) % 4;
  } else if (input & PAD_DOWN) {
    state->selectedGlobal = (state->selectedGlobal + 1) % 4;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    const int direction = (input & PAD_LEFT) ? -1 : 1;
    if (state->selectedGlobal == 0)
      state->pendingBackground = (state->pendingBackground +
                                  direction + LIBRARY_BACKGROUND_COUNT) %
                                 LIBRARY_BACKGROUND_COUNT;
    else if (state->selectedGlobal == 1)
      state->pendingGlassColor = (state->pendingGlassColor + direction + GLASS_COLOR_COUNT) % GLASS_COLOR_COUNT;
    else if (state->selectedGlobal == 2)
      state->pendingFont = (state->pendingFont + direction + UI_FONT_COUNT) % UI_FONT_COUNT;
    else
      state->pendingAmbient = !state->pendingAmbient;
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingBackground != *state->ambientOrbsBackgroundSetting) {
      if (setLibraryBackground((LibraryBackground)state->pendingBackground)) {
        state->saveError = -1;
      } else {
        state->saveError = saveLibraryBackground(state->target,
                                                 (LibraryBackground)state->pendingBackground);
        if (!state->saveError)
          *state->ambientOrbsBackgroundSetting = state->pendingBackground;
        else
          setLibraryBackground((LibraryBackground)*state->ambientOrbsBackgroundSetting);
      }
    }
    if (!state->saveError && state->pendingGlassColor != *state->glassColorSetting) {
      state->saveError = saveGlassColorPreset(state->target, (GlassColorPreset)state->pendingGlassColor);
      if (!state->saveError)
        *state->glassColorSetting = state->pendingGlassColor;
    }
    if (!state->saveError && state->pendingFont != *state->fontSetting) {
      if (setUIFont((UIFont)state->pendingFont)) {
        state->saveError = -1;
      } else {
        state->saveError = saveUIFont(state->target, (UIFont)state->pendingFont);
        if (!state->saveError)
          *state->fontSetting = state->pendingFont;
        else
          setUIFont((UIFont)*state->fontSetting);
      }
    }
    if (!state->saveError && state->pendingAmbient != *state->ambientEnabled) {
      state->saveError = saveAmbientSoundEnabled(state->target, state->pendingAmbient);
      if (!state->saveError) {
        *state->ambientEnabled = state->pendingAmbient;
        ambientSetEnabled(state->pendingAmbient);
      }
    }
  } else if (input & PAD_TRIANGLE) {
    return 1;
  }
  return 0;
}

// Returns 1 for Back, -1 if a test launch unexpectedly returns, or 0 to stay.
static int handleGameInput(OptionsMenuState *state, int input) {
  if ((state->selectedGameRow == LUNA_GAME_VMC_SLOT1 ||
       state->selectedGameRow == LUNA_GAME_VMC_SLOT2) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    int slot = state->selectedGameRow - LUNA_GAME_VMC_SLOT1;
    if (uiVMCPickerLoop(state->target, state->titleArguments, &state->gameOptions, slot))
      state->titleArgumentsChanged = 1;
  } else if (state->selectedGameRow == LUNA_GAME_LAUNCH_ARGUMENTS &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    // Open the full argument list from its visible Game row.
    int result = uiArgumentListLoop(state->target, state->titleArguments);
    if (result < 0)
      return -1;
    if (result == 2) {
      state->titleArgumentsChanged = 0;
    } else if (result == 1) {
      state->titleArgumentsChanged = 1;
    }
    lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
  } else if (input & PAD_SQUARE) {
    // Launch title without saving arguments
    uiLaunchTitle(state->target, state->titleArguments, NULL);
    return -1;
  } else if (input & PAD_START) {
    state->saveError = updateTitleLaunchArguments(state->target, state->titleArguments);
    if (!state->saveError)
      state->titleArgumentsChanged = 0;
  } else if (input & PAD_TRIANGLE) {
    return 1;
  } else if (input & PAD_UP) {
    state->selectedGameRow = (state->selectedGameRow + LUNA_GAME_ROW_COUNT - 1) %
                      LUNA_GAME_ROW_COUNT;
  } else if (input & PAD_DOWN) {
    state->selectedGameRow = (state->selectedGameRow + 1) % LUNA_GAME_ROW_COUNT;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    int direction = (input & PAD_LEFT) ? -1 : 1;
    if (lunaGameOptionsChange(&state->gameOptions, state->titleArguments,
                              (LunaGameRow)state->selectedGameRow, direction))
      state->titleArgumentsChanged = 1;
  }
  return 0;
}

// Runs the Game, System, Views, and Orbs settings tabs.
// Returns -1 if a test launch unexpectedly returns.
int uiTitleOptionsLoop(Target *target, int *classicArtOverlap,
                       int *ambientOrbsBackgroundSetting, int *glassColorSetting,
                       int *fontSetting, int *ambientEnabled, int *orbsThemeSetting,
                       int *orbsAppearanceSetting, int *orbsColorSetting,
                       int *tailsColorSetting,
                        uint32_t *enabledViews) {
  int res = 0;
  OptionsMenuState state = {
      .target = target,
      .classicArtOverlap = classicArtOverlap,
      .ambientOrbsBackgroundSetting = ambientOrbsBackgroundSetting,
      .glassColorSetting = glassColorSetting,
      .fontSetting = fontSetting,
      .ambientEnabled = ambientEnabled,
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
      .pendingOrbsTheme = *orbsThemeSetting,
      .pendingOrbsAppearance = *orbsAppearanceSetting,
      .pendingOrbsColor = *orbsColorSetting,
      .pendingTailsColor = *tailsColorSetting,
      .pendingViews = *enabledViews,
      .page = OPTIONS_PER_GAME,
      .selectedGameRow = LUNA_GAME_FAST_READS,
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
      state.page = (OptionsPage)((state.page + OPTIONS_ORBS) % (OPTIONS_ORBS + 1));
      state.saveError = 0;
      continue;
    }
    if (input & PAD_R1) {
      state.page = (OptionsPage)((state.page + 1) % (OPTIONS_ORBS + 1));
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
    drawOptionsFooter(1, 1, changed, 0);
    if (saveError)
      drawTextWindow(baseX, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, ErrorTextColor, ALIGN_HCENTER,
                     "Could not save game settings");
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    int input = waitForInput(-1);
    if (input & PAD_SQUARE) {
      uiLaunchTitle(target, titleArguments, NULL);
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
