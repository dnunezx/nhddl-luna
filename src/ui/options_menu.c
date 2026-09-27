// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "ui/orbs_coming_soon.h"
#include "options.h"
#include <libpad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OPTIONS_PAN_DURATION_MS 300
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

static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments);

static const char *const gameRowLabels[LUNA_GAME_ROW_COUNT] = {
    "IOP: Fast reads", "IOP: Sync reads",
    "EE: Unhook syscalls", "IOP: Emulate DVD-DL",
    "IOP: Fix game buffer overrun", "Launch arguments",
    "Video mode", "Field flipping",
    "Show PS2 logo", "Debug colors"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Review every launch argument, including global overrides.",
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

// The library frame stays in the other screen buffer. Moving it down reveals
// Options from the top in Collection; other views reveal it from the left.
static void drawOptionsPanCover(const GSTEXTURE *libraryFrame, int offset,
                                int panToTop) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  if (offset >= (panToTop ? height : width))
    return;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, libraryFrame,
                            panToTop ? 0 : offset, panToTop ? offset : 0,
                            0, 0, width, height,
                            width - (panToTop ? 0 : offset),
                            height - (panToTop ? offset : 0), 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

static int optionsPanOffset(uint32_t elapsed, int opening, int panToTop) {
  int progress = elapsed >= OPTIONS_PAN_DURATION_MS
                     ? 1000 : (int)(elapsed * 1000U / OPTIONS_PAN_DURATION_MS);
  int eased = (int)((int64_t)progress * progress *
                    (3000 - 2 * progress) / 1000000);
  int span = panToTop ? gsGlobal->Height : gsGlobal->Width;
  int offset = span * (opening ? eased : 1000 - eased) / 1000;
  return panToTop ? psbbnFieldStableY(offset) : offset;
}

static void presentOptionsFrame(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_vsync_wait();
  // Keep ActiveBuffer fixed while redrawing the options screen.
  gsKit_display_buffer(gsGlobal);
  usleep(1000);
}

static void drawOptionsFooter(int gamePage, int argumentList, int dirty,
                              int placeholder) {
  const int top = gsGlobal->Height - footerHeight;
  const int width = gsGlobal->Width;
  const int slot = width / 4;
  const char *action = argumentList ? "Toggle" : "Change";
  const char *back = argumentList ? "Back" : (dirty ? "Discard" : "Close");
  const int iconX = 24;
  if (!placeholder) {
    drawIconWindow(iconX, top, iconX + 26, gsGlobal->Height, 0,
                   FontMainColor, ALIGN_VCENTER, ICON_CROSS);
    drawTextWindow(iconX + 31, top, slot, gsGlobal->Height, 0,
                   HeaderTextColor, ALIGN_VCENTER, action);
    if (gamePage || argumentList) {
      drawIconWindow(slot + 8, top, slot + 34, gsGlobal->Height, 0,
                     FontMainColor, ALIGN_VCENTER, ICON_SQUARE);
      drawTextWindow(slot + 39, top, 2 * slot, gsGlobal->Height, 0,
                     HeaderTextColor, ALIGN_VCENTER, "Test");
    }
    drawIconWindow(2 * slot + 8, top, 2 * slot + 30, gsGlobal->Height, 0,
                   FontMainColor, ALIGN_VCENTER, ICON_START);
    drawTextWindow(2 * slot + 35, top, 3 * slot, gsGlobal->Height, 0,
                   HeaderTextColor, ALIGN_VCENTER,
                   argumentList ? "Save" : "Save tab");
  }
  drawIconWindow(3 * slot + 8, top, 3 * slot + 34, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_TRIANGLE);
  drawTextWindow(3 * slot + 39, top, width - 12, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, back);
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
  if (value)
    labelWidth -= (int)getLineWidth(value) + 16;
  drawText(x + 18, y, 0, labelWidth, 0,
           selected ? GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80) : HeaderTextColor,
           label);
  if (value)
    drawText(right - getLineWidth(value) - 12, y, 0, 0, 0,
             selected ? ColorSelected : FontMainColor, value);
}

static int optionsGlobalRowY(int index, int firstY, int rowStep, int lineHeight) {
  return firstY + index * rowStep + (index == 2 ? lineHeight : 0);
}

static int optionsViewRowForLibraryView(int view) {
  return view == UI_VIEW_CLASSIC ? 0 : view + 1;
}

static int optionsGameRowY(int index, int firstY, int rowStep) {
  if (index <= LUNA_GAME_LAUNCH_ARGUMENTS)
    return firstY + (index + 1) * rowStep;
  if (index <= LUNA_GAME_FIELD_FLIP)
    return firstY + (index + 2) * rowStep;
  return firstY + (index + 3) * rowStep;
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

static void drawOrbsComingSoon(GSTEXTURE *texture, int centerX, int centerY,
                              int size, uint32_t now) {
  const float half = size * 0.5f;
  const float angle = 6.28318530718f * (now % 16000U) / 16000.0f;
  const float sine = sinf(angle);
  const float cosine = cosf(angle);
  const float upperLeftX = centerX + half * (sine - cosine);
  const float upperLeftY = centerY - half * (sine + cosine);
  const float upperRightX = centerX + half * (cosine + sine);
  const float upperRightY = centerY + half * (sine - cosine);
  const float lowerLeftX = centerX - half * (cosine + sine);
  const float lowerLeftY = centerY + half * (cosine - sine);
  const float lowerRightX = centerX + half * (cosine - sine);
  const float lowerRightY = centerY + half * (sine + cosine);
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  gsKit_TexManager_bind(gsGlobal, texture);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_prim_quad_texture(gsGlobal, texture,
                          upperLeftX, upperLeftY, 0.0f, 0.0f,
                          upperRightX, upperRightY, texture->Width - 1, 0.0f,
                          lowerLeftX, lowerLeftY, 0.0f, texture->Height - 1,
                          lowerRightX, lowerRightY,
                          texture->Width - 1, texture->Height - 1,
                          0, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawTitleOptionsFrame(Target *target,
                                  OptionsPage page, int selectedGameRow,
                                  int selectedGlobal, int selectedView,
                                  int pendingOverlap, uint32_t pendingViews,
                                  int pendingBackground,
                                  int pendingGlassColor, int pendingAmbient,
                                  GSTEXTURE *orbsPreview,
                                  const LunaGameOptions *gameOptions, int gameDirty,
                                  int systemDirty, int viewsDirty,
                                  int saveError,
                                  const GSTEXTURE *libraryFrame, int panOffset,
                                  int panToTop,
                                  OptionsSelector *selector) {
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();

  snprintf(lineBuffer, sizeof(lineBuffer), "Options");
  drawTextWindow(baseX, headerHeight - lineHeight,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, lineBuffer);

  const int tabY = headerHeight + lineHeight / 4;
  const int middle = gsGlobal->Width / 2;
  static const int tabOffsets[] = {-175, -75, 30, 135};
  static const char *const tabLabels[] = {"Game", "System", "Views", "Orbs"};
  static const char *const dirtyTabLabels[] = {"Game *", "System *", "Views *", "Orbs"};
  const int tabDirty[] = {gameDirty, systemDirty, viewsDirty, 0};
  drawIconWindow(middle - 230, tabY, middle - 200, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_L1);
  for (int tab = OPTIONS_PER_GAME; tab <= OPTIONS_ORBS; tab++) {
    drawText(middle + tabOffsets[tab], tabY, 0, 0, 0,
             page == tab ? ColorSelected : HeaderTextColor,
             tabDirty[tab] ? dirtyTabLabels[tab] : tabLabels[tab]);
  }
  drawIconWindow(middle + 215, tabY, middle + 245, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_R1);
  int underlineX = middle + tabOffsets[page];
  int underlineWidth = getLineWidth(tabLabels[page]);
  gsKit_prim_sprite(gsGlobal, underlineX, tabY + lineHeight,
                    underlineX + underlineWidth, tabY + lineHeight + 1, 0, ColorSelected);

  const int menuTop = headerHeight + 2 * lineHeight + 8;
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (page == OPTIONS_GLOBAL) {
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(selector, page, selectedGlobal,
                                     optionsGlobalRowY(selectedGlobal, firstY, rowStep, lineHeight));
    drawOptionsSection(menuTop, "Appearance", 0);
    drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                       selectedGlobal == 0, selectorY, "Background (Experimental)",
                       pendingBackground ? "Ambient Orbs" : "Stars & cubes");
    static const char *const glassColorLabels[GLASS_COLOR_COUNT] = {
        "Original", "Luminous", "Cosmic"};
    drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 1, selectorY, "Glass color",
                       glassColorLabels[pendingGlassColor]);
    drawOptionsSection(firstY + 2 * rowStep, "Audio", 1);
    drawOptionsTextRow(baseX, optionsGlobalRowY(2, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, selectedGlobal == 2, selectorY,
                       "Ambient sound", pendingAmbient ? "On" : "Off");
    static const char *const descriptions[] = {
        "Choose Ambient Orbs or the stars and cubes background.",
        "Change the tint of the glass interface.",
        "Play ambient music while browsing."};
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT, descriptions[selectedGlobal]);
  } else if (page == OPTIONS_VIEWS) {
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(selector, page, selectedView,
                                     firstY + selectedView * rowStep);
    drawOptionsSection(menuTop, "Library views", 0);
    for (int row = UI_VIEW_CLASSIC; row <= UI_VIEW_ORBS; row++) {
      const int optionRow = optionsViewRowForLibraryView(row);
      drawOptionsTextRow(baseX, firstY + optionRow * rowStep,
                         gsGlobal->Width - baseX, selectedView == optionRow,
                         selectorY, viewRowLabels[row],
                         pendingViews & (1U << row) ? "On" : "Off");
    }
    drawOptionsTextRow(baseX + 18,
                       firstY + OPTIONS_VIEW_ART_LAYOUT_ROW * rowStep,
                       gsGlobal->Width - baseX,
                       selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW, selectorY,
                       "Classic art layout",
                       pendingOverlap ? "Overlap" : "Separate");
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT,
                   selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW
                       ? "Choose how cover art sits in Classic view."
                       : "Circle cycles through enabled views. Keep at least one on.");
  } else if (page == OPTIONS_ORBS) {
    drawOptionsSection(menuTop, "Orbs", 0);
    if (orbsPreview != NULL) {
      const int availableHeight = menuBottom - menuTop;
      const int size = availableHeight / 2 < 150 ? availableHeight / 2 : 150;
      drawOrbsComingSoon(orbsPreview, middle,
                         menuTop + availableHeight * 45 / 100,
                         size, uiNowMs());
    }
    drawTextWindow(baseX, menuBottom - 2 * lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   ColorSelected, ALIGN_HCENTER, "Coming soon");
  } else {
    snprintf(lineBuffer, sizeof(lineBuffer), "%.38s  (%s)", target->name, target->id);
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    const int contentTop = menuTop + lineHeight + 5;
    const int gameStep = lineHeight + lineHeight / 3;
    int selectedY = optionsGameRowY(selectedGameRow, contentTop, gameStep);
    int scrollOffset = selectedY + lineHeight > menuBottom
                           ? selectedY + lineHeight - menuBottom : 0;
    int focusY = selectedY - scrollOffset;
    int selectorY = optionsSelectorY(selector, page, selectedGameRow, focusY);
    const int headingRows[] = {LUNA_GAME_FAST_READS, LUNA_GAME_VIDEO_MODE,
                               LUNA_GAME_PS2_LOGO};
    const char *const headings[] = {"Compatibility", "Video", "Launch"};
    for (int section = 0; section < 3; section++) {
      int y = optionsGameRowY(headingRows[section], contentTop, gameStep) -
              gameStep - scrollOffset;
      if (y >= contentTop && y + lineHeight <= menuBottom)
        drawOptionsSection(y, headings[section], 0);
    }
    for (int row = 0; row < LUNA_GAME_ROW_COUNT; row++) {
      int y = optionsGameRowY(row, contentTop, gameStep) - scrollOffset;
      if (y < contentTop || y + lineHeight > menuBottom)
        continue;
      drawOptionsTextRow(baseX, y, gsGlobal->Width - baseX,
                         row == selectedGameRow, selectorY,
                         gameRowLabels[row],
                         lunaGameOptionsValue(gameOptions, (LunaGameRow)row));
    }
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, gameRowDescriptions[selectedGameRow]);
  }
  drawOptionsFooter(page == OPTIONS_PER_GAME, 0,
                    gameDirty || systemDirty || viewsDirty,
                    page == OPTIONS_ORBS);
  if (saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   page == OPTIONS_GLOBAL ? "Could not save global settings" :
                   page == OPTIONS_VIEWS ? "Could not save views" :
                                           "Could not save game settings");
  if (libraryFrame != NULL)
    drawOptionsPanCover(libraryFrame, panOffset, panToTop);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

static int optionsGlobalDirty(int pendingBackground, int pendingGlassColor,
                              int pendingAmbient,
                              int background, int glassColor, int ambient) {
  return pendingBackground != background || pendingGlassColor != glassColor ||
         pendingAmbient != ambient;
}

// Handles the Game and System settings tabs.
// Returns -1 if error occurs
int uiTitleOptionsLoop(Target *target, int *classicArtOverlap,
                       int *ambientOrbsBackgroundSetting, int *glassColorSetting,
                       int *ambientEnabled, uint32_t *enabledViews,
                       int panToTop) {
  int res = 0;
  int saveError = 0;
  int pendingOverlap = *classicArtOverlap;
  int pendingBackground = *ambientOrbsBackgroundSetting;
  int pendingGlassColor = *glassColorSetting;
  int pendingAmbient = *ambientEnabled;
  uint32_t pendingViews = *enabledViews;
  int titleArgumentsChanged = 0;
  OptionsPage page = OPTIONS_PER_GAME;
  OptionsSelector selector = {0};
  int selectedGameRow = LUNA_GAME_FAST_READS;
  int selectedGlobal = 0;
  int selectedView = UI_VIEW_CLASSIC;

  // Load arguments from config files
  ArgumentList *titleArguments = loadLaunchArgumentLists(target);
  LunaGameOptions gameOptions;
  lunaGameOptionsRead(&gameOptions, titleArguments);
  int input = 0;

  GSTEXTURE orbsPreview = {0};
  int orbsPreviewAttempted = 0;
  int orbsPreviewLoaded = 0;

  GSTEXTURE libraryFrame = {0};
  libraryFrame.Width = gsGlobal->Width;
  libraryFrame.Height = gsGlobal->Height;
  libraryFrame.PSM = gsGlobal->PSM;
  libraryFrame.TBW = gsGlobal->Width / 64;
  libraryFrame.Vram = gsGlobal->ScreenBuffer[(gsGlobal->ActiveBuffer ^ 1) & 1];
  libraryFrame.Filter = GS_FILTER_NEAREST;

  uint32_t panStart = uiNowMs();
  int panOffset;
  do {
    panOffset = optionsPanOffset(uiNowMs() - panStart, 1, panToTop);
    int systemDirty = optionsGlobalDirty(
        pendingBackground, pendingGlassColor, pendingAmbient,
        *ambientOrbsBackgroundSetting,
        *glassColorSetting, *ambientEnabled);
    int viewsDirty = pendingViews != *enabledViews ||
                     pendingOverlap != *classicArtOverlap;
    drawTitleOptionsFrame(target, page, selectedGameRow,
                          selectedGlobal, selectedView, pendingOverlap, pendingViews,
                          pendingBackground, pendingGlassColor, pendingAmbient,
                          NULL, &gameOptions, titleArgumentsChanged,
                          systemDirty, viewsDirty, saveError,
                          &libraryFrame, panOffset, panToTop, &selector);
    pollInput();
  } while (panOffset < (panToTop ? gsGlobal->Height : gsGlobal->Width));

  while (1) {
    if (page == OPTIONS_ORBS && !orbsPreviewAttempted) {
      orbsPreviewAttempted = 1;
      orbsPreviewLoaded = loadPNGTextureRGBAMemory(gsGlobal, &orbsPreview,
          ORBS_COMING_SOON_PNG, SIZE_ORBS_COMING_SOON_PNG) == 0;
      if (orbsPreviewLoaded)
        orbsPreview.Filter = GS_FILTER_LINEAR;
    }
    int systemDirty = optionsGlobalDirty(
        pendingBackground, pendingGlassColor, pendingAmbient,
        *ambientOrbsBackgroundSetting,
        *glassColorSetting, *ambientEnabled);
    int viewsDirty = pendingViews != *enabledViews ||
                     pendingOverlap != *classicArtOverlap;
    drawTitleOptionsFrame(target, page, selectedGameRow,
                          selectedGlobal, selectedView, pendingOverlap, pendingViews,
                          pendingBackground, pendingGlassColor, pendingAmbient,
                          orbsPreviewLoaded ? &orbsPreview : NULL,
                          &gameOptions, titleArgumentsChanged,
                          systemDirty, viewsDirty, saveError,
                          NULL, 0, panToTop, &selector);

    // Process user inputs
    input = readInput();
    if (input & PAD_L1) {
      page = (OptionsPage)((page + OPTIONS_ORBS) % (OPTIONS_ORBS + 1));
      saveError = 0;
      continue;
    }
    if (input & PAD_R1) {
      page = (OptionsPage)((page + 1) % (OPTIONS_ORBS + 1));
      saveError = 0;
      continue;
    }
    if (page == OPTIONS_ORBS) {
      if (input & PAD_TRIANGLE)
        goto exit;
      continue;
    }
    if (page == OPTIONS_VIEWS) {
      if (input & PAD_UP) {
        selectedView = (selectedView + OPTIONS_VIEW_ROW_COUNT - 1) %
                       OPTIONS_VIEW_ROW_COUNT;
      } else if (input & PAD_DOWN) {
        selectedView = (selectedView + 1) % OPTIONS_VIEW_ROW_COUNT;
      } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
        if (selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW) {
          pendingOverlap = !pendingOverlap;
        } else {
          int view = selectedView == UI_VIEW_CLASSIC
                         ? UI_VIEW_CLASSIC : selectedView - 1;
          uint32_t bit = 1U << view;
          if (pendingViews != bit)
            pendingViews ^= bit;
        }
      } else if (input & PAD_START) {
        saveError = 0;
        if (pendingOverlap != *classicArtOverlap) {
          saveError = saveClassicArtOverlap(target, pendingOverlap);
          if (!saveError) {
            *classicArtOverlap = pendingOverlap;
            setClassicArtOverlap(pendingOverlap);
          }
        }
        if (!saveError && pendingViews != *enabledViews) {
          saveError = saveEnabledLibraryViews(target, pendingViews);
          if (!saveError)
            *enabledViews = pendingViews;
        }
      } else if (input & PAD_TRIANGLE) {
        goto exit;
      }
      continue;
    }
    if (page == OPTIONS_GLOBAL) {
      if (input & PAD_UP) {
        selectedGlobal = (selectedGlobal + 2) % 3;
      } else if (input & PAD_DOWN) {
        selectedGlobal = (selectedGlobal + 1) % 3;
      } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
        if (selectedGlobal == 0)
          pendingBackground = !pendingBackground;
        else if (selectedGlobal == 1)
          pendingGlassColor = (pendingGlassColor + 1) % GLASS_COLOR_COUNT;
        else
          pendingAmbient = !pendingAmbient;
      } else if (input & PAD_START) {
        saveError = 0;
        if (pendingBackground != *ambientOrbsBackgroundSetting) {
          saveError = saveAmbientOrbsBackground(target, pendingBackground);
          if (!saveError)
            *ambientOrbsBackgroundSetting = pendingBackground;
        }
        if (!saveError && pendingGlassColor != *glassColorSetting) {
          saveError = saveGlassColorPreset(target, (GlassColorPreset)pendingGlassColor);
          if (!saveError)
            *glassColorSetting = pendingGlassColor;
        }
        if (!saveError && pendingAmbient != *ambientEnabled) {
          saveError = saveAmbientSoundEnabled(target, pendingAmbient);
          if (!saveError) {
            *ambientEnabled = pendingAmbient;
            ambientSetEnabled(pendingAmbient);
          }
        }
      } else if (input & PAD_TRIANGLE) {
        goto exit;
      }
      continue;
    }
    if (selectedGameRow == LUNA_GAME_LAUNCH_ARGUMENTS &&
        (input & (PAD_CROSS | PAD_CIRCLE))) {
      // Open the full argument list from its visible Game row.
      res = uiArgumentListLoop(target, titleArguments);
      if (res < 0)
        goto exit;
      if (res == 2) {
        titleArgumentsChanged = 0;
      } else if (res == 1) {
        titleArgumentsChanged = 1;
      }
      lunaGameOptionsRead(&gameOptions, titleArguments);
      res = 0;
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments, NULL);
      res = -1; // If this was somehow reached, something went terribly wrong
      goto exit;
    } else if (input & PAD_START) {
      saveError = updateTitleLaunchArguments(target, titleArguments);
      if (!saveError)
        titleArgumentsChanged = 0;
    } else if (input & PAD_TRIANGLE) {
      goto exit;
    } else if (input & PAD_UP) {
      selectedGameRow = (selectedGameRow + LUNA_GAME_ROW_COUNT - 1) %
                        LUNA_GAME_ROW_COUNT;
    } else if (input & PAD_DOWN) {
      selectedGameRow = (selectedGameRow + 1) % LUNA_GAME_ROW_COUNT;
    } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
      int direction = (input & PAD_LEFT) ? -1 : 1;
      if (lunaGameOptionsChange(&gameOptions, titleArguments,
                                (LunaGameRow)selectedGameRow, direction))
        titleArgumentsChanged = 1;
    }
  }
exit:
  if (res >= 0) {
    panStart = uiNowMs();
    do {
      panOffset = optionsPanOffset(uiNowMs() - panStart, 0, panToTop);
      drawTitleOptionsFrame(target, page, selectedGameRow,
                            selectedGlobal, selectedView, pendingOverlap, pendingViews,
                            pendingBackground, pendingGlassColor, pendingAmbient,
                            orbsPreviewLoaded ? &orbsPreview : NULL,
                            &gameOptions, 0, 0, 0, 0,
                            &libraryFrame, panOffset, panToTop, &selector);
    } while (panOffset > 0);
  }
  if (orbsPreviewLoaded)
    gsKit_TexManager_free(gsGlobal, &orbsPreview);
  free(orbsPreview.Mem);
  freeArgumentList(titleArguments);
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
