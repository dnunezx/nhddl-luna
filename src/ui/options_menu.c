// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/args.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include <libpad.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DIV_ROUND(n, d) (n + (d - 1)) / d
#define OPTIONS_FADE_DURATION_MS 360
#define OPTIONS_SELECTION_GLOW_DURATION_MS 110
#define OPTIONS_GLOW_ROW_SCALE 256

typedef struct {
  GSTEXTURE libraryFrame;
} OptionsBackdrop;

typedef enum {
  OPTIONS_MENU,
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

static void drawTitleOptionsFooter(int baseX) {
  drawIconWindow(baseX, gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawIconWindow(baseX + getIconWidth(ICON_CIRCLE), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(baseX + 5 + getIconWidth(ICON_CIRCLE) + getIconWidth(ICON_CROSS), gsGlobal->Height - 1 - footerHeight, 0, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Toggle");

  drawIconWindow((gsGlobal->Width * 3 / 8) - getIconWidth(ICON_SQUARE), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_SQUARE);
  drawTextWindow((gsGlobal->Width * 3 / 8) + 5, gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER,
                 "Test");

  drawIconWindow((gsGlobal->Width * 5 / 8), gsGlobal->Height - footerHeight, gsGlobal->Width - getLineWidth("Save") - 5, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_START);
  drawTextWindow((gsGlobal->Width * 5 / 8) + 5 + getIconWidth(ICON_START), gsGlobal->Height - 1 - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Save");

  drawIconWindow(gsGlobal->Width - baseX - 5 - getIconWidth(ICON_TRIANGLE) - getLineWidth("Back"), gsGlobal->Height - footerHeight,
                 gsGlobal->Width - baseX, gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_TRIANGLE);
  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight, gsGlobal->Width - baseX, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT,
                 "Back");

  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight - getFontLineHeight() / 2, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor,
                 ALIGN_TOP | ALIGN_HCENTER, "Switch views");
  drawIconWindow(0, gsGlobal->Height - footerHeight - getFontLineHeight() / 2, (gsGlobal->Width - getLineWidth("Switch views")) / 2 - 5,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_TOP | ALIGN_RIGHT, ICON_L1);
  drawIconWindow((gsGlobal->Width + getLineWidth("Switch views")) / 2 + 5, gsGlobal->Height - footerHeight - getFontLineHeight() / 2, gsGlobal->Width,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_TOP | ALIGN_LEFT, ICON_R1);
}

static void drawOptionsCategoryFooter(int baseX, OptionsPage page) {
  drawIconWindow(baseX, gsGlobal->Height - footerHeight, 0, gsGlobal->Height,
                 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(baseX + getIconWidth(ICON_CROSS) + 5,
                 gsGlobal->Height - 1 - footerHeight, 0, gsGlobal->Height,
                 0, HeaderTextColor, ALIGN_VCENTER,
                 page == OPTIONS_MENU ? "Select" : "Toggle");
  if (page == OPTIONS_GLOBAL) {
    drawIconWindow((gsGlobal->Width * 5 / 8), gsGlobal->Height - footerHeight,
                   gsGlobal->Width - getLineWidth("Save") - 5, gsGlobal->Height,
                   0, FontMainColor, ALIGN_VCENTER, ICON_START);
    drawTextWindow((gsGlobal->Width * 5 / 8) + 5 + getIconWidth(ICON_START),
                   gsGlobal->Height - 1 - footerHeight, gsGlobal->Width,
                   gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER, "Save");
  }
  drawIconWindow(gsGlobal->Width - baseX - 5 - getIconWidth(ICON_TRIANGLE) -
                     getLineWidth("Back"),
                 gsGlobal->Height - footerHeight, gsGlobal->Width - baseX,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT,
                 ICON_TRIANGLE);
  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight,
                 gsGlobal->Width - baseX, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT, "Back");
}

static int optionsSelectorProgress(const OptionsSelector *selector, uint32_t now) {
  uint32_t elapsed = now - selector->startMs;
  if (elapsed >= OPTIONS_SELECTION_GLOW_DURATION_MS)
    return 1000;
  int progress = (int)(elapsed * 1000ULL / OPTIONS_SELECTION_GLOW_DURATION_MS);
  return (int)((int64_t)progress * progress * (3000 - 2 * progress) / 1000000);
}

static int optionsSelectorY(OptionsSelector *selector, OptionsPage page,
                            int selectedRow, int top, int rowStep) {
  const uint32_t now = uiNowMs();
  if (!selector->initialized || selector->page != page) {
    selector->page = page;
    selector->selectedRow = selectedRow;
    selector->fromRow = selectedRow * OPTIONS_GLOW_ROW_SCALE;
    selector->toRow = selector->fromRow;
    selector->startMs = now;
    selector->initialized = 1;
  } else if (selector->selectedRow != selectedRow) {
    int progress = optionsSelectorProgress(selector, now);
    selector->fromRow += (selector->toRow - selector->fromRow) * progress / 1000;
    selector->toRow = selectedRow * OPTIONS_GLOW_ROW_SCALE;
    selector->selectedRow = selectedRow;
    selector->startMs = now;
  }
  int progress = optionsSelectorProgress(selector, now);
  int row = selector->fromRow +
            (selector->toRow - selector->fromRow) * progress / 1000;
  return top + row * rowStep / OPTIONS_GLOW_ROW_SCALE;
}

static void drawOptionsTextRow(int x, int y, int right, int selected,
                               int selectorY, const char *label) {
  if (selected) {
    int textRight = x + (int)getLineWidth(label);
    if (textRight > right - 12)
      textRight = right - 12;
    drawPSBBNFocusGlow(x, selectorY, right, textRight);
  }
  drawText(x, y, 0, right - x, 0,
           selected ? GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80) : HeaderTextColor,
           label);
}

static void drawTitleOptionsFrame(const OptionsBackdrop *backdrop, Target *target,
                                  OptionsPage page, int selectedCategory,
                                  int selectedGlobal, int pendingOverlap,
                                  int pendingOrbs, int pendingBackground,
                                  int pendingAmbient,
                                  int activeArgumentIdx,
                                  int saveError, int progress,
                                  OptionsSelector *selector) {
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  int i;
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet(backdrop);

  if (page == OPTIONS_MENU)
    snprintf(lineBuffer, sizeof(lineBuffer), "Options");
  else if (page == OPTIONS_GLOBAL)
    snprintf(lineBuffer, sizeof(lineBuffer), "Global settings");
  else
    snprintf(lineBuffer, sizeof(lineBuffer), "%s\n%s", target->name, target->id);
  drawTextWindow(baseX, headerHeight - lineHeight,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, lineBuffer);

  const int menuTop = headerHeight + 1.5 * lineHeight;
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (page == OPTIONS_MENU) {
    int selectorY = optionsSelectorY(selector, page, selectedCategory,
                                     menuTop, rowStep);
    drawOptionsTextRow(baseX, menuTop, gsGlobal->Width - baseX,
                       selectedCategory == 0, selectorY, "Per-game settings");
    drawOptionsTextRow(baseX, menuTop + rowStep, gsGlobal->Width - baseX,
                       selectedCategory == 1, selectorY, "Global settings");
    drawOptionsCategoryFooter(baseX, page);
  } else if (page == OPTIONS_GLOBAL) {
    int selectorY = optionsSelectorY(selector, page, selectedGlobal,
                                     menuTop, rowStep);
    snprintf(lineBuffer, sizeof(lineBuffer), "Classic art layout: %s",
             pendingOverlap ? "Overlap" : "Separate");
    drawOptionsTextRow(baseX, menuTop, gsGlobal->Width - baseX,
                       selectedGlobal == 0, selectorY, lineBuffer);
    snprintf(lineBuffer, sizeof(lineBuffer), "Orbs view (Experimental): %s",
             pendingOrbs ? "On" : "Off");
    drawOptionsTextRow(baseX, menuTop + rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 1, selectorY, lineBuffer);
    snprintf(lineBuffer, sizeof(lineBuffer), "Background (Experimental): %s",
             pendingBackground ? "Orbs" : "Stars & cubes");
    drawOptionsTextRow(baseX, menuTop + 2 * rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 2, selectorY, lineBuffer);
    snprintf(lineBuffer, sizeof(lineBuffer), "Ambient sound: %s",
             pendingAmbient ? "On" : "Off");
    drawOptionsTextRow(baseX, menuTop + 3 * rowStep, gsGlobal->Width - baseX,
                       selectedGlobal == 3, selectorY, lineBuffer);
    drawOptionsCategoryFooter(baseX, page);
  } else {
    int focusY = menuTop;
    int scrollOffset = 0;
    for (i = 0; i < activeArgumentIdx; i++)
      focusY += uiArguments[i].rowCount * lineHeight + lineHeight / 2;
    focusY += (uiArguments[activeArgumentIdx].focusRowOffset +
               uiArguments[activeArgumentIdx].activeElementIdx) * lineHeight;
    if (focusY + lineHeight > menuBottom)
      scrollOffset = focusY + lineHeight - menuBottom;

    int startY = menuTop - scrollOffset;
    for (i = 0; i < uiArgumentsTotal; i++) {
      startY = lineHeight / 2 +
               uiArguments[i].draw(&uiArguments[i], (i == activeArgumentIdx) ? 1 : 0,
                                    baseX, startY, 0, gsGlobal->Width - baseX,
                                    menuTop, menuBottom);
    }
    drawTitleOptionsFooter(baseX);
  }
  if (saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   page == OPTIONS_GLOBAL ? "Could not save global settings" : "Could not save game settings");
  drawOptionsFade(backdrop, progress);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  presentOptionsFrame();
}

// Handles the options menu and its per-game and global settings pages.
// Returns -1 if error occurs
int uiTitleOptionsLoop(Target *target, int *classicArtOverlap, int *orbsEnabled,
                       int *orbsBackgroundSetting, int *ambientEnabled) {
  int res = 0;
  int saveError = 0;
  int pendingOverlap = *classicArtOverlap;
  int pendingOrbs = *orbsEnabled;
  int pendingBackground = *orbsBackgroundSetting;
  int pendingAmbient = *ambientEnabled;
  int titleArgumentsChanged = 0;
  OptionsPage page = OPTIONS_MENU;
  OptionsSelector selector = {0};
  int selectedCategory = 0;
  int selectedGlobal = 0;

  // Load arguments from config files
  ArgumentList *titleArguments = loadLaunchArgumentLists(target);
  int input = 0;
  int activeArgumentIdx = 0;

  // Parse arguments
  for (int i = 0; i < (uiArgumentsTotal); i++)
    uiArguments[i].parse(&uiArguments[i], titleArguments);

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
    drawTitleOptionsFrame(&backdrop, target, page, selectedCategory,
                          selectedGlobal, pendingOverlap, pendingOrbs,
                          pendingBackground, pendingAmbient,
                          activeArgumentIdx, saveError, progress, &selector);
    int heldInput = pollInput();
    if (!(heldInput & PAD_TRIANGLE))
      triangleReleased = 1;
    else if (triangleReleased)
      closeRequested = 1;
  } while (progress < 1000);

  if (closeRequested)
    goto exit;

  int i;
  while (1) {
    drawTitleOptionsFrame(&backdrop, target, page, selectedCategory,
                          selectedGlobal, pendingOverlap, pendingOrbs,
                          pendingBackground, pendingAmbient,
                          activeArgumentIdx, saveError, 1000, &selector);

    // Process user inputs
    input = readInput();
    if (page == OPTIONS_MENU) {
      if (input & (PAD_UP | PAD_DOWN))
        selectedCategory = 1 - selectedCategory;
      else if (input & (PAD_CROSS | PAD_CIRCLE)) {
        page = selectedCategory == 0 ? OPTIONS_PER_GAME : OPTIONS_GLOBAL;
        saveError = 0;
        activeArgumentIdx = 0;
        if (page == OPTIONS_PER_GAME)
          for (i = 0; i < uiArgumentsTotal; i++)
            uiArguments[i].parse(&uiArguments[i], titleArguments);
      } else if (input & PAD_TRIANGLE)
        goto exit;
      continue;
    }
    if (page == OPTIONS_GLOBAL) {
      if (input & PAD_UP) {
        selectedGlobal = (selectedGlobal + 3) % 4;
      } else if (input & PAD_DOWN) {
        selectedGlobal = (selectedGlobal + 1) % 4;
      } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
        if (selectedGlobal == 0)
          pendingOverlap = !pendingOverlap;
        else if (selectedGlobal == 1)
          pendingOrbs = !pendingOrbs;
        else if (selectedGlobal == 2)
          pendingBackground = !pendingBackground;
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
        if (!saveError && pendingAmbient != *ambientEnabled) {
          saveError = saveAmbientSoundEnabled(target, pendingAmbient);
          if (!saveError) {
            *ambientEnabled = pendingAmbient;
            ambientSetEnabled(pendingAmbient);
          }
        }
        if (!saveError)
          page = OPTIONS_MENU;
      } else if (input & PAD_TRIANGLE) {
        pendingOverlap = *classicArtOverlap;
        pendingOrbs = *orbsEnabled;
        pendingBackground = *orbsBackgroundSetting;
        pendingAmbient = *ambientEnabled;
        saveError = 0;
        page = OPTIONS_MENU;
      }
      continue;
    }
    if (input & (PAD_L1 | PAD_R1)) {
      // Show full argument list
      res = uiArgumentListLoop(target, titleArguments, &backdrop);
      if (res < 0)
        goto exit;
      if (res == 2) {
        page = OPTIONS_MENU;
        titleArgumentsChanged = 0;
      } else {
        titleArgumentsChanged = 1;
        for (i = 0; i < uiArgumentsTotal; i++)
          uiArguments[i].parse(&uiArguments[i], titleArguments);
      }
      res = 0;
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments, NULL);
      res = -1; // If this was somehow reached, something went terribly wrong
      goto exit;
    } else if (input & PAD_START) {
      saveError = updateTitleLaunchArguments(target, titleArguments);
      if (!saveError) {
        page = OPTIONS_MENU;
        titleArgumentsChanged = 0;
      }
    } else if (input & PAD_TRIANGLE) {
      // Back discards changes made on this settings page.
      if (titleArgumentsChanged) {
        freeArgumentList(titleArguments);
        titleArguments = loadLaunchArgumentLists(target);
        for (i = 0; i < uiArgumentsTotal; i++)
          uiArguments[i].parse(&uiArguments[i], titleArguments);
        titleArgumentsChanged = 0;
      }
      saveError = 0;
      page = OPTIONS_MENU;
    } else {
      switch (uiArguments[activeArgumentIdx].handleInput(&uiArguments[activeArgumentIdx], input)) {
      case ACTION_CHANGED:
        titleArgumentsChanged = 1;
        uiArguments[activeArgumentIdx].marshal(&uiArguments[activeArgumentIdx], titleArguments);
        break;
      case ACTION_NEXT_ARGUMENT:
        if (activeArgumentIdx < uiArgumentsTotal - 1)
          activeArgumentIdx++;
        break;
      case ACTION_PREV_ARGUMENT:
        if (activeArgumentIdx > 0)
          activeArgumentIdx--;
        break;
      default:
      }
    }
  }
exit:
  if (res >= 0) {
    fadeStart = uiNowMs();
    do {
      uint32_t elapsed = uiNowMs() - fadeStart;
      progress = elapsed >= OPTIONS_FADE_DURATION_MS
                     ? 0 : 1000 - (int)(elapsed * 1000U / OPTIONS_FADE_DURATION_MS);
      drawTitleOptionsFrame(&backdrop, target, page, selectedCategory,
                            selectedGlobal, pendingOverlap, pendingOrbs,
                            pendingBackground, pendingAmbient,
                            activeArgumentIdx, 0, progress, &selector);
    } while (progress > 0);
  }
  freeArgumentList(titleArguments);
  return res;
}

// Handles all arguments in arugment list
// Returns -1 after a failed launch, 0 to return to per-game settings,
// or 2 after saving the game settings.
static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const OptionsBackdrop *backdrop) {
  int selectedArgIdx = 0;
  int input = 0;
  int saveError = 0;

  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet(backdrop);
    int baseX = keepoutArea + 10;

    // Draw header
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    drawTextWindow(baseX, headerHeight + 1.5 * getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, FontMainColor, ALIGN_HCENTER, "Launch arguments");

    // Draw footer
    drawTitleOptionsFooter(baseX);
    if (saveError)
      drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(), gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, ErrorTextColor, ALIGN_HCENTER,
                     "Could not save game settings");

    int startY = headerHeight + 2.5 * getFontLineHeight();
    int idx = 0;

    // Set number of elements per page according to line height and available screen height
    int maxArguments = (gsGlobal->Height - startY - footerHeight - getFontLineHeight() / 2) / getFontLineHeight();
    int curPage = selectedArgIdx / maxArguments;

    snprintf(lineBuffer, 255, "Page %d/%d", curPage + 1, (!titleArguments->total) ? 1 : DIV_ROUND(titleArguments->total, maxArguments));
    startY = drawTextWindow(baseX, startY - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

    Argument *argument = titleArguments->first;
    while (argument != NULL) {
      // Do not display arguments before the current page
      if (idx < maxArguments * curPage) {
        idx++;
        goto next;
      }
      // Do not display arguments beyond the current page
      if (idx >= maxArguments * (curPage + 1)) {
        break;
      }

      // Draw argument
      if (!argument->isDisabled)
        drawIconWindow(baseX, startY, 20, startY + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);

      snprintf(lineBuffer, 255, "%s%s%s %s", ((argument->isGlobal) ? "[G] " : ""), argument->arg, (!strlen(argument->value)) ? "" : ":",
               argument->value);
      startY = drawText(baseX + getIconWidth(ICON_ENABLED), startY, 0, 0, 0, ((selectedArgIdx == idx) ? ColorSelected : FontMainColor), lineBuffer);

      idx++;
    next:
      argument = argument->next;
    }

    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    // Process user inputs
    input = waitForInput(-1);
    if (input & (PAD_L1 | PAD_R1)) {
      return 0;
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments, NULL);
      return -1; // If this was somehow reached, something went terribly wrong
    } else if (input & PAD_START) {
      saveError = updateTitleLaunchArguments(target, titleArguments);
      if (!saveError)
        return 2;
    } else if (input & PAD_TRIANGLE) {
      return 0;
    }

    // Ignore inputs when the argument is not initialized
    if (!curArgument)
      continue;

    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      // Toggle argument
      curArgument->isDisabled = !curArgument->isDisabled;
      // If the argument was disabled, reset global flag
      if (curArgument->isDisabled)
        curArgument->isGlobal = 0;
    } else if (input & PAD_UP) {
      // Point to the previous argument
      selectedArgIdx = (selectedArgIdx - 1 + titleArguments->total) % titleArguments->total;
      curArgument = (curArgument->prev) ? curArgument->prev : titleArguments->last;
    } else if (input & PAD_DOWN) {
      // Advance to the next argument
      selectedArgIdx = (selectedArgIdx + 1) % titleArguments->total;
      curArgument = (curArgument->next) ? curArgument->next : titleArguments->first;
    }
  }
}
