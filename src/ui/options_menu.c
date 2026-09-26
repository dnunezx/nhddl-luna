// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include <libpad.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define OPTIONS_FADE_DURATION_MS 360
#define OPTIONS_SELECTION_GLOW_DURATION_MS 110
#define OPTIONS_GLOW_ROW_SCALE 256

typedef struct {
  GSTEXTURE libraryFrame;
} OptionsBackdrop;

typedef enum {
  OPTIONS_PER_GAME,
  OPTIONS_GLOBAL
} OptionsPage;

typedef struct {
  OptionsPage page;
  int selectedRow;
  int fromRow;
  int toRow;
  uint32_t startMs;
  int initialized;
} OptionsSelector;

static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const OptionsBackdrop *backdrop);

static const char *const gameRowLabels[LUNA_GAME_ROW_COUNT] = {
    "Launch arguments", "IOP: Fast reads", "IOP: Sync reads",
    "EE: Unhook syscalls", "IOP: Emulate DVD-DL",
    "IOP: Fix game buffer overrun", "Video mode", "Field flipping",
    "Show PS2 logo", "Debug colors"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Review every launch argument, including global overrides.",
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Choose a forced output mode for this game.",
    "Choose field flipping for a forced video mode.",
    "Show the PlayStation 2 startup logo.",
    "Display debug colors while loading."};

// The last library frame stays in the other screen buffer while the options
// screen draws repeatedly into the current buffer. No artwork texture or extra
// full-size framebuffer allocation is needed for the backdrop.
static void drawOptionsBackdrop(const OptionsBackdrop *backdrop) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const GSTEXTURE *frame = &backdrop->libraryFrame;
  GSTEXTURE sharpFrame = *frame;
  sharpFrame.Filter = GS_FILTER_NEAREST;
  static const int sampleX[] = {-9, 9, 0, 0, 0};
  static const int sampleY[] = {0, 0, -9, 9, 0};
  // Each successive fixed-alpha blend gives all five samples equal weight.
  static const int sampleAlpha[] = {0x80, 0x40, 0x2B, 0x20, 0x1A};

  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, &sharpFrame, 0, 0, 0, 0, width, height,
                            width, height, 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));

  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  for (int i = 0; i < 5; i++) {
    // The GS can fetch outside the framebuffer when a shifted sample crosses
    // an edge. Clip both rectangles so every texture coordinate stays valid.
    const int left = sampleX[i] < 0 ? -sampleX[i] : 0;
    const int top = sampleY[i] < 0 ? -sampleY[i] : 0;
    const int right = sampleX[i] > 0 ? width - sampleX[i] : width;
    const int bottom = sampleY[i] > 0 ? height - sampleY[i] : height;
    gsKit_set_primalpha(gsGlobal,
                        GS_SETREG_ALPHA(0, 1, 2, 1, sampleAlpha[i]), 0);
    gsKit_prim_sprite_texture(gsGlobal, frame, left, top,
                              left + sampleX[i], top + sampleY[i],
                              right, bottom, right + sampleX[i],
                              bottom + sampleY[i], 0,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

static void drawOptionsSheet(const OptionsBackdrop *backdrop) {
  drawOptionsBackdrop(backdrop);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0x02, 0x08, 0x16, 0x70));
}

// Blend the untouched library frame over the finished menu. Fading that copy
// away reveals the options and gradually brings in the dark, blurred backdrop.
static void drawOptionsFade(const OptionsBackdrop *backdrop, int progress) {
  if (progress >= 1000)
    return;
  const GSTEXTURE *frame = &backdrop->libraryFrame;
  GSTEXTURE sharpFrame = *frame;
  sharpFrame.Filter = GS_FILTER_NEAREST;
  int alpha = ((1000 - progress) * 0x80 + 500) / 1000;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 2, 1, alpha), 0);
  gsKit_prim_sprite_texture(gsGlobal, &sharpFrame, 0, 0, 0, 0,
                            gsGlobal->Width, gsGlobal->Height,
                            gsGlobal->Width, gsGlobal->Height, 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

static void presentOptionsFrame(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_vsync_wait();
  // Keep ActiveBuffer fixed: the other buffer holds the untouched library view.
  gsKit_display_buffer(gsGlobal);
  usleep(1000);
}

static void drawOptionsFooter(int gamePage, int argumentList, int dirty) {
  const int top = gsGlobal->Height - footerHeight;
  const int width = gsGlobal->Width;
  const int slot = width / 4;
  const char *action = argumentList ? "Toggle" : "Change";
  const char *back = argumentList ? "Back" : (dirty ? "Discard" : "Close");
  const int iconX = 24;
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
  return firstY + index * rowStep + (index == 4 ? lineHeight : 0);
}

static int optionsGameRowY(int index, int firstY, int rowStep) {
  if (index == LUNA_GAME_LAUNCH_ARGUMENTS)
    return firstY;
  if (index <= LUNA_GAME_BUFFER_OVERRUN)
    return firstY + (index + 1) * rowStep;
  if (index <= LUNA_GAME_FIELD_FLIP)
    return firstY + (index + 2) * rowStep;
  return firstY + (index + 3) * rowStep;
}

static void drawOptionsSection(int x, int y, const char *title, int audio) {
  const uint64_t color = HeaderTextColor;
  if (audio) {
    gsKit_prim_line(gsGlobal, x + 6, y + 3, x + 6, y + 13, 0, color);
    gsKit_prim_line(gsGlobal, x + 6, y + 3, x + 12, y + 1, 0, color);
    gsKit_prim_sprite(gsGlobal, x + 2, y + 12, x + 6, y + 15, 0, color);
  } else {
    drawGlassDiamond(x + 7, y + 8, 6, 0, color);
  }
  drawText(x + 22, y, 0, 0, 0, HeaderTextColor, title);
}

static void drawTitleOptionsFrame(const OptionsBackdrop *backdrop, Target *target,
                                  OptionsPage page, int selectedGameRow,
                                  int selectedGlobal, int pendingOverlap,
                                  int pendingOrbs, int pendingBackground,
                                  int pendingGlassColor, int pendingAmbient,
                                  const LunaGameOptions *gameOptions, int gameDirty,
                                  int systemDirty,
                                  int saveError, int progress,
                                  OptionsSelector *selector) {
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet(backdrop);

  snprintf(lineBuffer, sizeof(lineBuffer), "Options");
  drawTextWindow(baseX, headerHeight - lineHeight,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, lineBuffer);

  const int tabY = headerHeight + lineHeight / 4;
  const int middle = gsGlobal->Width / 2;
  drawIconWindow(middle - 150, tabY, middle - 120, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_L1);
  drawText(middle - 110, tabY, 0, 0, 0,
           page == OPTIONS_PER_GAME ? ColorSelected : HeaderTextColor,
           gameDirty ? "Game *" : "Game");
  drawText(middle + 20, tabY, 0, 0, 0,
           page == OPTIONS_GLOBAL ? ColorSelected : HeaderTextColor,
           systemDirty ? "System *" : "System");
  drawIconWindow(middle + 110, tabY, middle + 140, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_R1);
  int underlineX = page == OPTIONS_PER_GAME ? middle - 110 : middle + 20;
  int underlineWidth = page == OPTIONS_PER_GAME ? getLineWidth("Game") : getLineWidth("System");
  gsKit_prim_sprite(gsGlobal, underlineX, tabY + lineHeight,
                    underlineX + underlineWidth, tabY + lineHeight + 1, 0, ColorSelected);

  const int menuTop = headerHeight + 2 * lineHeight + 8;
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (page == OPTIONS_GLOBAL) {
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(selector, page, selectedGlobal,
                                     optionsGlobalRowY(selectedGlobal, firstY, rowStep, lineHeight));
    drawOptionsSection(baseX, menuTop, "Appearance", 0);
    drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                       selectedGlobal == 0, selectorY, "Classic art layout",
                       pendingOverlap ? "Overlap" : "Separate");
    drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 1, selectorY, "Orbs view (Experimental)",
                       pendingOrbs ? "On" : "Off");
    drawOptionsTextRow(baseX, firstY + 2 * rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 2, selectorY, "Background (Experimental)",
                       pendingBackground ? "Orbs" : "Stars & cubes");
    static const char *const glassColorLabels[GLASS_COLOR_COUNT] = {
        "Original", "Luminous", "Cosmic"};
    drawOptionsTextRow(baseX, firstY + 3 * rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 3, selectorY, "Glass color",
                       glassColorLabels[pendingGlassColor]);
    drawOptionsSection(baseX, firstY + 4 * rowStep, "Audio", 1);
    drawOptionsTextRow(baseX, optionsGlobalRowY(4, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, selectedGlobal == 4, selectorY,
                       "Ambient sound", pendingAmbient ? "On" : "Off");
    static const char *const descriptions[] = {
        "Choose how cover art sits in Classic view.",
        "Show the experimental Orbs library view.",
        "Choose the library's animated background.",
        "Change the tint of the glass interface.",
        "Play ambient music while browsing."};
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT, descriptions[selectedGlobal]);
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
        drawOptionsSection(baseX, y, headings[section], 0);
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
  drawOptionsFooter(page == OPTIONS_PER_GAME, 0, gameDirty || systemDirty);
  if (saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   page == OPTIONS_GLOBAL ? "Could not save global settings" : "Could not save game settings");
  drawOptionsFade(backdrop, progress);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

static int optionsGlobalDirty(int pendingOverlap, int pendingOrbs,
                              int pendingBackground, int pendingGlassColor,
                              int pendingAmbient, int overlap, int orbs,
                              int background, int glassColor, int ambient) {
  return pendingOverlap != overlap || pendingOrbs != orbs ||
         pendingBackground != background || pendingGlassColor != glassColor ||
         pendingAmbient != ambient;
}

// Handles the Game and System settings tabs.
// Returns -1 if error occurs
int uiTitleOptionsLoop(Target *target, int *classicArtOverlap, int *orbsEnabled,
                       int *orbsBackgroundSetting, int *glassColorSetting,
                       int *ambientEnabled) {
  int res = 0;
  int saveError = 0;
  int pendingOverlap = *classicArtOverlap;
  int pendingOrbs = *orbsEnabled;
  int pendingBackground = *orbsBackgroundSetting;
  int pendingGlassColor = *glassColorSetting;
  int pendingAmbient = *ambientEnabled;
  int titleArgumentsChanged = 0;
  OptionsPage page = OPTIONS_PER_GAME;
  OptionsSelector selector = {0};
  int selectedGameRow = LUNA_GAME_LAUNCH_ARGUMENTS;
  int selectedGlobal = 0;

  // Load arguments from config files
  ArgumentList *titleArguments = loadLaunchArgumentLists(target);
  LunaGameOptions gameOptions;
  lunaGameOptionsRead(&gameOptions, titleArguments);
  int input = 0;

  OptionsBackdrop backdrop = {0};
  backdrop.libraryFrame.Width = gsGlobal->Width;
  backdrop.libraryFrame.Height = gsGlobal->Height;
  backdrop.libraryFrame.PSM = gsGlobal->PSM;
  backdrop.libraryFrame.TBW = gsGlobal->Width / 64;
  backdrop.libraryFrame.Vram = gsGlobal->ScreenBuffer[(gsGlobal->ActiveBuffer ^ 1) & 1];
  backdrop.libraryFrame.Filter = GS_FILTER_LINEAR;

  uint32_t fadeStart = uiNowMs();
  int progress;
  int closeRequested = 0;
  int triangleReleased = 0;
  do {
    uint32_t elapsed = uiNowMs() - fadeStart;
    progress = elapsed >= OPTIONS_FADE_DURATION_MS
                   ? 1000 : (int)(elapsed * 1000U / OPTIONS_FADE_DURATION_MS);
    int systemDirty = optionsGlobalDirty(pendingOverlap, pendingOrbs,
        pendingBackground, pendingGlassColor, pendingAmbient,
        *classicArtOverlap, *orbsEnabled, *orbsBackgroundSetting,
        *glassColorSetting, *ambientEnabled);
    drawTitleOptionsFrame(&backdrop, target, page, selectedGameRow,
                          selectedGlobal, pendingOverlap, pendingOrbs,
                          pendingBackground, pendingGlassColor, pendingAmbient,
                          &gameOptions, titleArgumentsChanged,
                          systemDirty, saveError, progress, &selector);
    int heldInput = pollInput();
    if (!(heldInput & PAD_TRIANGLE))
      triangleReleased = 1;
    else if (triangleReleased)
      closeRequested = 1;
  } while (progress < 1000);

  if (closeRequested)
    goto exit;

  while (1) {
    int systemDirty = optionsGlobalDirty(pendingOverlap, pendingOrbs,
        pendingBackground, pendingGlassColor, pendingAmbient,
        *classicArtOverlap, *orbsEnabled, *orbsBackgroundSetting,
        *glassColorSetting, *ambientEnabled);
    drawTitleOptionsFrame(&backdrop, target, page, selectedGameRow,
                          selectedGlobal, pendingOverlap, pendingOrbs,
                          pendingBackground, pendingGlassColor, pendingAmbient,
                          &gameOptions, titleArgumentsChanged,
                          systemDirty, saveError, 1000, &selector);

    // Process user inputs
    input = readInput();
    if (input & (PAD_L1 | PAD_R1)) {
      page = page == OPTIONS_PER_GAME ? OPTIONS_GLOBAL : OPTIONS_PER_GAME;
      saveError = 0;
      continue;
    }
    if (page == OPTIONS_GLOBAL) {
      if (input & PAD_UP) {
        selectedGlobal = (selectedGlobal + 4) % 5;
      } else if (input & PAD_DOWN) {
        selectedGlobal = (selectedGlobal + 1) % 5;
      } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
        if (selectedGlobal == 0)
          pendingOverlap = !pendingOverlap;
        else if (selectedGlobal == 1)
          pendingOrbs = !pendingOrbs;
        else if (selectedGlobal == 2)
          pendingBackground = !pendingBackground;
        else if (selectedGlobal == 3)
          pendingGlassColor = (pendingGlassColor + 1) % GLASS_COLOR_COUNT;
        else
          pendingAmbient = !pendingAmbient;
      } else if (input & PAD_START) {
        saveError = 0;
        if (pendingOverlap != *classicArtOverlap) {
          saveError = saveClassicArtOverlap(target, pendingOverlap);
          if (!saveError) {
            *classicArtOverlap = pendingOverlap;
            setClassicArtOverlap(pendingOverlap);
          }
        }
        if (!saveError && pendingOrbs != *orbsEnabled) {
          saveError = saveOrbsViewEnabled(target, pendingOrbs);
          if (!saveError)
            *orbsEnabled = pendingOrbs;
        }
        if (!saveError && pendingBackground != *orbsBackgroundSetting) {
          saveError = saveOrbsBackground(target, pendingBackground);
          if (!saveError)
            *orbsBackgroundSetting = pendingBackground;
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
      res = uiArgumentListLoop(target, titleArguments, &backdrop);
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
    fadeStart = uiNowMs();
    do {
      uint32_t elapsed = uiNowMs() - fadeStart;
      progress = elapsed >= OPTIONS_FADE_DURATION_MS
                     ? 0 : 1000 - (int)(elapsed * 1000U / OPTIONS_FADE_DURATION_MS);
      drawTitleOptionsFrame(&backdrop, target, page, selectedGameRow,
                            selectedGlobal, pendingOverlap, pendingOrbs,
                            pendingBackground, pendingGlassColor, pendingAmbient,
                            &gameOptions, 0, 0, 0, progress, &selector);
    } while (progress > 0);
  }
  freeArgumentList(titleArguments);
  return res;
}

// LUNA's advanced view of the merged game and global launch arguments.
// Returns -1 after a failed launch, 0 on Back, 1 after edits, or 2 after Save.
static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const OptionsBackdrop *backdrop) {
  int selectedArgIdx = 0;
  int saveError = 0;
  int changed = 0;
  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet(backdrop);
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
    drawOptionsSection(baseX, headerHeight + lineHeight / 4,
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
    drawOptionsFooter(1, 1, changed);
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
