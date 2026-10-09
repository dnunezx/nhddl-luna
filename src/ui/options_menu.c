// LUNA options screen extracted from gui.c.
#include "ui/options_menu.h"
#include "ui/language.h"
#include "ui/view_internal.h"
#include "ui/ui.h"
#include "ui/game_options.h"
#include "opl_devices.h"
#include "ui/ambient.h"
#include "ui/pad.h"
#include "ui/view_state.h"
#include "options.h"
#include "devices/devices.h"
#include "vmc_create.h"
#include "storage.h"
#include "cheat_storage.h"
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
#define OPTIONS_TAB_SELECTOR_GLIDE_DURATION_MS 200
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
  GAME_CHEATS,
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
  int initialized;
  int fromLeft;
  int fromRight;
  int toLeft;
  int toRight;
  uint32_t startMs;
} OptionsTabSelector;

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
  int coreSetting;
  int *orbsThemeSetting;
  int *orbsAppearanceSetting;
  int *orbsColorSetting;
  int *tailsColorSetting;
  uint32_t *enabledOrbShapes;
  uint32_t *enabledViews;
  int pendingOverlap;
  int pendingBackground;
  int pendingGlassColor;
  int pendingFont;
  int pendingAmbient;
  int pendingLogo;
  int pendingCore;
  int pendingOrbsTheme;
  int pendingOrbsAppearance;
  int pendingOrbsColor;
  int pendingTailsColor;
  uint32_t pendingOrbShapes;
  uint32_t pendingViews;
  int titleArgumentsChanged;
  LunaCheatSettings cheats;
  LunaCheatFile cheatFile;
  int cheatsChanged;
  int cheatLoadResult;
  int cheatFirstRow;
  char cheatStatus[192];
  char cheatPath[PATH_MAX + 1];
  int saveError;
  const char *saveErrorLabel;
  char vmcStatus[96];
  OptionsPage page;
  OptionsSelector selector;
  OptionsTabSelector tabSelector;
  GameSection gameSection;
  int selectedGameHubRow;
  int selectedGameRow;
  int videoOutOpl;
  int videoOutFirstRow;
  int selectedGlobal;
  int selectedView;
  int selectedOrbsRow;
  int orbsFirstRow;
  int orbsShapesPage;
  int selectedOrbShape;
  int orbShapesFirstRow;
} OptionsMenuState;

static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const LunaCheatSettings *cheats);
static int uiVMCPickerLoop(Target *target, ArgumentList *arguments,
                           LunaGameOptions *gameOptions, int slot);

static const char *const gameRowLabels[LUNA_GAME_ROW_COUNT] = {
    "IOP: Fast reads", "IOP: Sync reads",
    "EE: Unhook syscalls", "IOP: Emulate DVD-DL",
    "IOP: Fix game buffer overrun", "Launch arguments",
    "VMC slot 1", "VMC slot 2",
    "Video out", "Field flipping",
    "Show PS2 logo", "Debug colors", "Game core",
    "Accurate reads", "Synchronous reads", "Unhook syscalls",
    "Skip videos", "Emulate DVD-DL", "Disable IGR", "Disable IGR",
    "Video out", "Field flipping"};

static const char *const gameRowDescriptions[LUNA_GAME_ROW_COUNT] = {
    "Use faster IOP disc reads for this game.",
    "Synchronize IOP disc reads for this game.",
    "Leave EE system calls unhooked for compatibility.",
    "Emulate a dual-layer DVD for this game.",
    "Work around a game buffer overrun.",
    "Review every launch argument, including global overrides.",
    "Assign an existing OPL card image from this drive's VMC folder.",
    "Assign an existing OPL card image to the second slot.",
    "Open the Neutrino and OPL video output modes.",
    "Choose field flipping for a forced video mode.",
    "Choose Inherit to use the Global PS2 logo setting.",
    "Display debug colors while loading.",
    "Inherit or override the default core for this game.",
    "Use OPL's accurate disc-read behavior.",
    "Use OPL's synchronous disc-read method.",
    "Leave EE system calls unhooked in OPL.",
    "Skip PSS and Bink videos in OPL.",
    "Emulate a dual-layer DVD in OPL.",
    "Disable OPL's in-game reset for this title.",
    "Disable Neutrino's in-game return for this title.",
    "Open the Neutrino and OPL video output modes.",
    "Emulate field flipping when OPL forces a video output mode."};

static const char *const gameSectionLabels[GAME_SECTION_COUNT] = {
    "Virtual memory cards", "Compatibility", "Video", "Cheats", "Launch & debug"};
static const char *const gameSectionDescriptions[GAME_SECTION_COUNT] = {
    "Enable virtual cards by assigning one to a slot.",
    "Change the active core's compatibility switches.",
    "Adjust this game's video output.",
    "Select individual codes from CHT/<title ID>.cht on this drive.",
    "Set the startup logo and review launch options."};
static const LunaGameRow gameSectionRows[GAME_SECTION_COUNT][5] = {
    {LUNA_GAME_VMC_SLOT1, LUNA_GAME_VMC_SLOT2},
    {LUNA_GAME_FAST_READS, LUNA_GAME_SYNC_READS,
     LUNA_GAME_UNHOOK_SYSCALLS, LUNA_GAME_DVD_DL,
     LUNA_GAME_BUFFER_OVERRUN},
    {LUNA_GAME_VIDEO_MODE, LUNA_GAME_FIELD_FLIP},
    {0},
    {LUNA_GAME_PS2_LOGO, LUNA_GAME_LAUNCH_ARGUMENTS,
     LUNA_GAME_DEBUG_COLORS, LUNA_GAME_CORE}};
static const LunaGameRow oplCompatRows[LUNA_OPL_COMPAT_COUNT] = {
    LUNA_GAME_OPL_ACCURATE_READS, LUNA_GAME_OPL_SYNC_READS,
    LUNA_GAME_OPL_UNHOOK_SYSCALLS, LUNA_GAME_OPL_SKIP_VIDEOS,
    LUNA_GAME_OPL_DVD_DL, LUNA_GAME_OPL_DISABLE_IGR};
static const LunaGameRow neutrinoCompatRows[LUNA_NEUTRINO_COMPAT_ROW_COUNT] = {
    LUNA_GAME_FAST_READS, LUNA_GAME_SYNC_READS,
    LUNA_GAME_UNHOOK_SYSCALLS, LUNA_GAME_DVD_DL,
    LUNA_GAME_BUFFER_OVERRUN, LUNA_GAME_NEUTRINO_DISABLE_IGR};
static const int gameSectionRowCounts[GAME_SECTION_COUNT] = {4, 6, 2, 3, 4};

static int gameUsesOpl(const OptionsMenuState *state) {
  return lunaOplDevice(state->target->device->mode) != NULL && state->gameOptions.oplCore;
}

static int gameSectionRowCount(const OptionsMenuState *state, GameSection section) {
  if (section == GAME_VIDEO_OUT)
    return lunaGameVideoModeCount(state->videoOutOpl);
  if (section == GAME_COMPATIBILITY)
    return gameUsesOpl(state) ? LUNA_OPL_COMPAT_COUNT :
                               LUNA_NEUTRINO_COMPAT_ROW_COUNT;
  if (section == GAME_LAUNCH && lunaOplDevice(state->target->device->mode) == NULL)
    return 3;
  return gameSectionRowCounts[section];
}

static LunaGameRow gameSectionRow(const OptionsMenuState *state,
                                  GameSection section, int row) {
  if (section == GAME_VIDEO && gameUsesOpl(state))
    return row == 0 ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_OPL_FIELD_FLIP;
  if (section == GAME_COMPATIBILITY)
    return gameUsesOpl(state) ? oplCompatRows[row] : neutrinoCompatRows[row];
  return gameSectionRows[section][row];
}

#define OPTIONS_VIEW_ART_LAYOUT_ROW UI_VIEW_COUNT
#define OPTIONS_VIEW_ROW_COUNT (UI_VIEW_COUNT + 1)

static int optionsHeadingY(void) {
  return headerHeight - getFontLineHeight() + getFontLineHeight() / 4;
}

static int optionsMenuTop(void) {
  return headerHeight + getFontLineHeight() + 8;
}

// Give Options its own scene without carrying library text into the menu.
static void drawOptionsSheet(void) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int panelTop = 78 - getFontLineHeight();
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, 32, panelTop + 2, width - 32,
                    height - footerHeight + 1, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(30, panelTop, width - 30, height - footerHeight + 3, 2);
}

static int optionsTransitionProgress(uint32_t elapsed, uint32_t duration) {
  int progress = elapsed >= duration ? 1000 : (int)(elapsed * 1000U / duration);
  return (int)((int64_t)progress * progress *
               (3000 - 2 * progress) / 1000000);
}

static void drawOptionsTabSelector(OptionsTabSelector *selector, int left,
                                    int right, int y) {
  const uint32_t now = uiNowMs();
  int progress = optionsTransitionProgress(now - selector->startMs,
                                            OPTIONS_TAB_SELECTOR_GLIDE_DURATION_MS);
  if (!selector->initialized) {
    selector->fromLeft = selector->toLeft = left * OPTIONS_SELECTOR_ROW_SCALE;
    selector->fromRight = selector->toRight = right * OPTIONS_SELECTOR_ROW_SCALE;
    selector->startMs = now;
    selector->initialized = 1;
  } else if (selector->toLeft != left * OPTIONS_SELECTOR_ROW_SCALE ||
             selector->toRight != right * OPTIONS_SELECTOR_ROW_SCALE) {
    selector->fromLeft += (selector->toLeft - selector->fromLeft) * progress / 1000;
    selector->fromRight += (selector->toRight - selector->fromRight) * progress / 1000;
    selector->toLeft = left * OPTIONS_SELECTOR_ROW_SCALE;
    selector->toRight = right * OPTIONS_SELECTOR_ROW_SCALE;
    selector->startMs = now;
    progress = 0;
  }
  float stripLeft = (selector->fromLeft +
      (selector->toLeft - selector->fromLeft) * progress / 1000) /
      (float)OPTIONS_SELECTOR_ROW_SCALE;
  float stripRight = (selector->fromRight +
      (selector->toRight - selector->fromRight) * progress / 1000) /
      (float)OPTIONS_SELECTOR_ROW_SCALE;
  const int stripY = psbbnFieldStableY(y);
  gsKit_prim_sprite(gsGlobal, stripLeft, stripY - 4, stripRight, stripY + 6, 3,
                    GS_SETREG_RGBA(0x48, 0xB8, 0xF0, 0x10));
  gsKit_prim_sprite(gsGlobal, stripLeft, stripY - 2, stripRight, stripY + 4, 4,
                    GS_SETREG_RGBA(0x70, 0xD0, 0xFF, 0x24));
  gsKit_prim_sprite(gsGlobal, stripLeft, stripY, stripRight, stripY + 2, 5,
                    GS_SETREG_RGBA(0xB0, 0xE8, 0xFF, 0x78));
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
        {ICON_CROSS, argumentList ? lunaText("Toggle") : actionLabel ? actionLabel : lunaText("Change")}};
    drawPromptBar(8, top, slot, gsGlobal->Height, 0, HeaderTextColor,
                  (PromptBar){NULL, action, 1});
    if (gamePage || argumentList) {
      const ButtonPrompt test[] = {{ICON_SQUARE, lunaText("Test")}};
      drawPromptBar(slot, top, 2 * slot, gsGlobal->Height, 0,
                    HeaderTextColor, (PromptBar){NULL, test, 1});
    }
    const ButtonPrompt save[] = {
        {ICON_START, argumentList ? lunaText("Save") : lunaText("Save tab")}};
    drawPromptBar(2 * slot, top, 3 * slot, gsGlobal->Height, 0,
                  HeaderTextColor, (PromptBar){NULL, save, 1});
  }
  const ButtonPrompt back[] = {
      {ICON_TRIANGLE, argumentList || gameDetail ? lunaText("Back") :
                      (dirty ? lunaText("Discard") : lunaText("Close"))}};
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

static void drawCompatibilityTabs(const OptionsMenuState *state, int baseX, int y) {
  const int middle = (baseX + gsGlobal->Width) / 2;
  const int neutrinoX = middle - 100;
  const int oplX = middle + 48;
  const int videoOut = state->gameSection == GAME_VIDEO_OUT;
  const int opl = videoOut ? state->videoOutOpl : gameUsesOpl(state);
  const uint64_t disabled = GS_SETREG_RGBA(0x48, 0x4B, 0x50, 0x80);
  drawText(neutrinoX, y, 0, 0, 0, opl ? disabled : ColorSelected, "Neutrino");
  drawText(oplX, y, 0, 0, 0, opl ? ColorSelected : disabled, "OPL");
  if (videoOut && lunaOplDevice(state->target->device->mode) != NULL) {
    drawText(neutrinoX - 22, y, 0, 0, 0, HeaderTextColor, "<");
    drawText(oplX + 48, y, 0, 0, 0, HeaderTextColor, ">");
  }
}

static void drawVideoOutRows(OptionsMenuState *state, int baseX, int firstY,
                             int rowStep, int menuBottom) {
  drawCompatibilityTabs(state, baseX, firstY);
  const int rowStart = firstY + rowStep;
  const int count = lunaGameVideoModeCount(state->videoOutOpl);
  const int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                       state->gameOptions.videoMode;
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
                       selectorY, lunaGameVideoModeLabel(state->videoOutOpl, row),
                       mode == row ? lunaText("On") : lunaText("Off"));
  char position[20];
  snprintf(position, sizeof(position), "%d/%d", state->selectedGameRow + 1, count);
  // Place the counter on the section heading, clear of the first row's value.
  drawText(gsGlobal->Width - baseX - getLineWidth(position) - 12,
           firstY - getFontLineHeight() - 4, 0, 0, 0, FontMainColor, position);
  drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT,
                 state->selectedGameRow == 0 ?
                     lunaText("Keep the game's original output. Left/right changes tabs.") :
                     lunaText("Select one output per core. Left/right changes tabs."));
}

static int optionsGlobalRowY(int index, int firstY, int rowStep, int lineHeight) {
  return firstY + index * rowStep +
         (index >= 3 ? lineHeight : 0) +
         (index >= 5 ? lineHeight : 0);
}

static int optionsViewRowY(int row, int firstY, int rowStep,
                            int lineHeight) {
  return firstY + row * rowStep +
         (row >= OPTIONS_VIEW_ART_LAYOUT_ROW ? lineHeight : 0);
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

static int gameLaunchEnabled(const OptionsMenuState *state) {
  int enabled = state->gameOptions.debugColors;
  for (Argument *argument = state->titleArguments->first;
       argument != NULL; argument = argument->next) {
    const char *name = argument->arg;
    if (!strcmp(name, "logo")) {
      // Inherit is the default; either explicit logo choice is a title override.
      enabled += !argument->isGlobal;
    } else if (strcmp(name, "dbc") && strcmp(name, "gc") &&
               strcmp(name, "gsm") && strcmp(name, "mc0") &&
               strcmp(name, "mc1") && strcmp(name, "luna_neutrino_disable_igr") &&
               strcmp(name, "luna_opl_compat") && strcmp(name, "luna_opl_gsm") &&
               strcmp(name, "luna_opl_field_flip")) {
      if (!strcmp(name, "luna_core")) {
        enabled += !argument->isGlobal;
        continue;
      }
      // Other arguments belong to Launch arguments, including disabled overrides.
      enabled += !argument->isDisabled || !argument->isGlobal;
    }
  }
  return enabled;
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
                              int pendingCore,
                              int background, int glassColor, int fontSetting,
                              int ambient, int logo, int core);

static const char *gamePS2LogoValue(const OptionsMenuState *state) {
  Argument *logo = getArgument(state->titleArguments, "logo");
  if (logo == NULL || logo->isGlobal)
    return state->logoSetting ? lunaText("Inherit (On)") : lunaText("Inherit (Off)");
  return state->gameOptions.ps2Logo ? lunaText("Override (On)") : lunaText("Override (Off)");
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

static int optionsOrbsShapeFirstRow(const OptionsMenuState *state) {
  return optionsOrbsColorsEditable(state) ? 4 : 2;
}

static int optionsOrbsRowCount(const OptionsMenuState *state) {
  return optionsOrbsShapeFirstRow(state) +
         (state->pendingOrbsTheme == ORBS_THEME_LUNA ? 1 : 0);
}

static void drawOrbsUnavailablePopup(int baseX, int menuTop, int menuBottom) {
  const int left = baseX + 28;
  const int right = gsGlobal->Width - baseX - 28;
  const int middleY = (menuTop + menuBottom) / 2;
  const int top = middleY - 58;
  const int bottom = middleY + 58;
  drawGlassPanel(left, top, right, bottom, 3);
  drawTextWindow(left + 14, top + 14, right - 14, top + 36, 4,
                 ColorSelected, ALIGN_HCENTER, lunaText("Ambient Orbs required"));
  drawTextWindow(left + 14, top + 43, right - 14, top + 65, 4,
                 FontMainColor, ALIGN_HCENTER,
                 lunaText("Shown when Global background is Ambient Orbs."));
  drawTextWindow(left + 14, top + 73, right - 14, bottom - 10, 4,
                 HeaderTextColor, ALIGN_HCENTER,
                 lunaText("Set and save Global > Background to Ambient Orbs."));
}

static void drawGameVMCRows(OptionsMenuState *state, int baseX,
                            int firstY, int rowStep, int menuBottom) {
  const int enabled = gameVMCEnabled(state->titleArguments);
  const int visibleRows = enabled ? 4 : 3;
  const char *const labels[] = {
      lunaText("VMC slot 1"), lunaText("VMC slot 2"), lunaText("Create new card"), lunaText("Disable virtual cards")};
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
          lunaText("Create an 8 MB card on this game's drive.") :
      state->selectedGameRow == 3 ?
          lunaText("Use physical cards in both slots for this game.") :
          lunaText(gameRowDescriptions[LUNA_GAME_VMC_SLOT1 + state->selectedGameRow]);
  drawTextWindow(baseX + 18, menuBottom,
                 gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0,
                 HeaderTextColor, ALIGN_LEFT, description);
}

static void reloadGameCheats(OptionsMenuState *state) {
  lunaCheatFileFree(&state->cheatFile);
  state->cheatLoadResult = lunaCheatsLoad(state->target, &state->cheatFile,
      state->cheatPath, sizeof(state->cheatPath), state->cheatStatus, sizeof(state->cheatStatus));
  state->cheatFirstRow = 0;
  if (state->cheatLoadResult) return;
  int removed = 0;
  for (int i = 0; i < state->cheats.count;) {
    int found = 0;
    for (int e = 0; e < state->cheatFile.entryCount; e++)
      if (!state->cheatFile.entries[e].required &&
          state->cheatFile.entries[e].id == state->cheats.ids[i]) found = 1;
    if (found) i++;
    else {
      lunaCheatToggle(&state->cheats, state->cheats.ids[i]);
      removed++;
    }
  }
  if (removed) {
    state->cheatsChanged = 1;
    snprintf(state->cheatStatus, sizeof(state->cheatStatus),
             lunaText("%d changed or missing selections cleared. Press Start to save."), removed);
  } else {
    snprintf(state->cheatStatus, sizeof(state->cheatStatus),
             lunaText("%d entries from CHT/%s.cht"), state->cheatFile.entryCount, state->target->id);
  }
}

static void drawGameCheatRows(OptionsMenuState *state, int baseX, int firstY, int bottom) {
  int count = 3 + state->cheatFile.entryCount;
  int step = getFontLineHeight() + getFontLineHeight() / 3;
  int visible = (bottom - firstY) / step;
  if (visible < 1) visible = 1;
  if (state->selectedGameRow >= count) state->selectedGameRow = count - 1;
  if (state->selectedGameRow < state->cheatFirstRow) state->cheatFirstRow = state->selectedGameRow;
  if (state->selectedGameRow >= state->cheatFirstRow + visible)
    state->cheatFirstRow = state->selectedGameRow - visible + 1;
  for (int row = state->cheatFirstRow; row < count && row < state->cheatFirstRow + visible; row++) {
    const char *label, *value;
    char name[48];
    if (row == 0) { label = lunaText("Enable cheats"); value = state->cheats.enabled ? lunaText("On") : lunaText("Off"); }
    else if (row == 1) { label = lunaText("Clear selections"); value = ">"; }
    else if (row == 2) { label = lunaText("Reload file"); value = ">"; }
    else {
      const LunaCheatEntry *entry = &state->cheatFile.entries[row - 3];
      snprintf(name, sizeof(name), "%.43s%s", entry->name, strlen(entry->name) > 43 ? "..." : "");
      label = name;
      value = entry->required ? lunaText("Required") :
          lunaCheatSelected(&state->cheats, entry->id) ? "[x]" : "[ ]";
    }
    int y = firstY + (row - state->cheatFirstRow) * step;
    drawOptionsTextRow(baseX, y, gsGlobal->Width - baseX,
                       row == state->selectedGameRow, y, label, value);
  }
  char detail[192];
  if (state->selectedGameRow >= 3) {
    const LunaCheatEntry *entry = &state->cheatFile.entries[state->selectedGameRow - 3];
    snprintf(detail, sizeof(detail), "%.110s: %d lines. %s", entry->name, entry->pairCount,
             entry->required ? lunaText("Included with selected cheats.") : lunaText("Toggle this whole code block."));
  } else if (state->cheatLoadResult || state->selectedGameRow == 2) {
    snprintf(detail, sizeof(detail), "%s", state->cheatStatus);
  } else {
    snprintf(detail, sizeof(detail), lunaText("%d selected. %s"), state->cheats.count,
             state->cheats.enabled ? lunaText("Press Start to save or Square to test.") :
                                     lunaText("Cheats are off; selections are retained."));
  }
  drawTextWindow(baseX + 18, bottom, gsGlobal->Width - baseX,
                 gsGlobal->Height - footerHeight, 0,
                 state->cheatLoadResult ? ErrorTextColor : HeaderTextColor, ALIGN_LEFT, detail);
}

static void drawTitleOptionsFrame(OptionsMenuState *state,
                                  int transitionProgress, int transitionMode) {
  const int showDirty = transitionMode != 2;
  const int gameDirty = showDirty && (state->titleArgumentsChanged || state->cheatsChanged);
  const int systemDirty = showDirty && optionsGlobalDirty(
      state->pendingBackground, state->pendingGlassColor,
      state->pendingFont, state->pendingAmbient, state->pendingLogo,
      state->pendingCore,
      *state->ambientOrbsBackgroundSetting, *state->glassColorSetting,
      *state->fontSetting, *state->ambientEnabled, state->logoSetting,
      state->coreSetting);
  const int viewsDirty = showDirty &&
                         (state->pendingViews != *state->enabledViews ||
                          state->pendingOverlap != *state->classicArtOverlap);
  const int orbsDirty = showDirty &&
                        (state->pendingOrbsTheme != *state->orbsThemeSetting ||
                         state->pendingOrbsAppearance != *state->orbsAppearanceSetting ||
                         state->pendingOrbsColor != *state->orbsColorSetting ||
                         state->pendingTailsColor != *state->tailsColorSetting ||
                         state->pendingOrbShapes != *state->enabledOrbShapes);
  int baseX = keepoutArea + 10;
  const int lineHeight = getFontLineHeight();
  // The destination buffer still has the library's old depth values. Draw
  // this composed screen in command order, then restore normal library depth.
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();

  const int tabY = optionsHeadingY();
  const int middle = gsGlobal->Width / 2;
  static const int englishTabOffsets[] = {-175, -75, 30, 135};
  int tabOffsets[4];
  static const char *const tabLabels[] = {"Game", "Global", "Views", "Orbs"};
  static const char *const dirtyTabLabels[] = {"Game *", "Global *", "Views *", "Orbs *"};
  const int tabDirty[] = {gameDirty, systemDirty, viewsDirty, orbsDirty};
  // Fit translated headings by their rendered widths, including dirty marks.
  // Preserve the existing English layout.
  int tabWidths[4], totalTabWidth = 3 * 24;
  for (int tab = 0; tab < 4; tab++) {
    tabWidths[tab] = (int)getLineWidth(lunaText(dirtyTabLabels[tab]));
    totalTabWidth += tabWidths[tab];
  }
  int tabLeft = -totalTabWidth / 2;
  for (int tab = 0; tab < 4; tab++) {
    tabOffsets[tab] = lunaLanguage() == LUNA_LANGUAGE_ENGLISH ? englishTabOffsets[tab] : tabLeft;
    tabLeft += tabWidths[tab] + 24;
  }
  drawIconWindow(middle - 230, tabY, middle - 200, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_L1);
  for (int tab = OPTIONS_PER_GAME; tab <= OPTIONS_ORBS; tab++) {
    drawText(middle + tabOffsets[tab], tabY, 0, 0, 0,
             HeaderTextColor,
             tabDirty[tab] ? lunaText(dirtyTabLabels[tab]) : lunaText(tabLabels[tab]));
  }
  const int selectedTabX = middle + tabOffsets[state->page];
  drawOptionsTabSelector(&state->tabSelector, selectedTabX - 4,
      selectedTabX + (int)getLineWidth(lunaText(tabLabels[state->page])) + 4,
      tabY + lineHeight + 4);
  drawIconWindow(middle + 215, tabY, middle + 245, tabY + lineHeight, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_R1);
  const int menuTop = optionsMenuTop();
  const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
  const int rowStep = lineHeight + lineHeight / 2;
  if (state->page == OPTIONS_GLOBAL) {
    static const char *const backgroundLabels[LIBRARY_BACKGROUND_COUNT] = {
        "Stars & cubes", "Ambient Orbs", "Red Clouds", "Midnight Cubes",
        "System Configuration"};
    const int firstY = menuTop + lineHeight + 4;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedGlobal,
                                     optionsGlobalRowY(state->selectedGlobal, firstY, rowStep, lineHeight));
    drawOptionsSection(menuTop, lunaText("Appearance"), 0);
    drawOptionsTextRow(baseX, firstY, gsGlobal->Width - baseX,
                       state->selectedGlobal == 0, selectorY, lunaText("Background (Experimental)"),
                       lunaText(backgroundLabels[state->pendingBackground]));
    static const char *const glassColorLabels[GLASS_COLOR_COUNT] = {
        "Original", "Luminous", "Cosmic"};
    drawOptionsTextRow(baseX, firstY + rowStep, gsGlobal->Width - baseX,
                       state->selectedGlobal == 1, selectorY, lunaText("Glass color"),
                       lunaText(glassColorLabels[state->pendingGlassColor]));
    drawOptionsTextRow(baseX, firstY + 2 * rowStep, gsGlobal->Width - baseX,
                       state->selectedGlobal == 2, selectorY, lunaText("Font"),
                       state->pendingFont == UI_FONT_PSBBN ? "PSBBN" : "DejaVu Sans");
    drawOptionsSection(firstY + 3 * rowStep, lunaText("Game defaults"), 0);
    drawOptionsTextRow(baseX, optionsGlobalRowY(3, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, state->selectedGlobal == 3, selectorY,
                       lunaText("PlayStation 2 logo"), state->pendingLogo ? lunaText("On") : lunaText("Off"));
    drawOptionsTextRow(baseX, optionsGlobalRowY(4, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, state->selectedGlobal == 4, selectorY,
                       lunaText("Game core"), state->pendingCore ? "OPL" : "Neutrino");
    drawOptionsSection(firstY + 5 * rowStep + lineHeight,
                       lunaText("Audio"), 1);
    drawOptionsTextRow(baseX, optionsGlobalRowY(5, firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX, state->selectedGlobal == 5, selectorY,
                       lunaText("Ambient sound"), state->pendingAmbient ? lunaText("On") : lunaText("Off"));
    static const char *const descriptions[] = {
        "Choose a library background; customize Ambient Orbs in Orbs.",
        "Change the tint of the glass interface.",
        "Use LUNA's font or the PSBBN keyboard lettering.",
        "Default PS2 startup logo setting for every game.",
        "Default game core for every supported device.",
        "Play ambient music while browsing."};
    drawTextWindow(baseX + 18, menuBottom - lineHeight,
                   gsGlobal->Width - baseX, menuBottom, 0,
                   HeaderTextColor, ALIGN_LEFT, lunaText(descriptions[state->selectedGlobal]));
  } else if (state->page == OPTIONS_VIEWS) {
    const int firstY = menuTop + lineHeight + 4;
    // Leave room for List appearance and the description.
    const int rowStep = (menuBottom - firstY - 3 * lineHeight) / UI_VIEW_COUNT;
    int selectorY = optionsSelectorY(&state->selector, state->page, state->selectedView,
                                     optionsViewRowY(state->selectedView,
                                                     firstY, rowStep, lineHeight));
    drawOptionsSection(menuTop, lunaText("Enabled views"), 0);
    for (int row = 0; row < UI_VIEW_COUNT; row++) {
      const UILibraryView view = lunaViewCycleOrder[row];
      const char *label = view == UI_VIEW_ORBS ? lunaText("Scroll (Experimental)") :
                          lunaNavViewLabel(view);
      drawOptionsTextRow(baseX, firstY + row * rowStep,
                         gsGlobal->Width - baseX, state->selectedView == row,
                         selectorY, label,
                         state->pendingViews & (1U << view) ? lunaText("On") : lunaText("Off"));
    }
    drawOptionsSection(firstY + OPTIONS_VIEW_ART_LAYOUT_ROW * rowStep,
                       lunaText("View appearance"), 0);
    drawOptionsTextRow(baseX + 18,
                       optionsViewRowY(OPTIONS_VIEW_ART_LAYOUT_ROW,
                                       firstY, rowStep, lineHeight),
                       gsGlobal->Width - baseX,
                       state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW, selectorY,
                       lunaText("List art layout"),
                       state->pendingOverlap ? lunaText("Overlap") : lunaText("Separate"));
    if (state->selectedView == OPTIONS_VIEW_ART_LAYOUT_ROW)
      drawTextWindow(baseX + 18, menuBottom - lineHeight,
                     gsGlobal->Width - baseX, menuBottom, 0,
                     HeaderTextColor, ALIGN_LEFT,
                     lunaText("Choose how cover art sits in List view."));
    else {
      const ButtonPrompt cycle[] = {
          {ICON_CIRCLE, lunaText("Cycle views; keep at least one on")}};
      drawPromptBar(baseX + 18, menuBottom - lineHeight,
                    gsGlobal->Width - baseX, menuBottom, 0,
                    HeaderTextColor, (PromptBar){NULL, cycle, 1});
    }
  } else if (state->page == OPTIONS_ORBS) {
    drawOptionsSection(menuTop, lunaText(state->orbsShapesPage ? "Shapes" : "Orbs"), 0);
    if (!optionsOrbsEditable(state)) {
      drawOrbsUnavailablePopup(baseX, menuTop, menuBottom);
    } else {
      static const char *const colorLabels[ORBS_COLOR_COUNT] = {
          "Original", "Cyan", "Violet", "Rose", "Green", "Gold", "White"};
      static const char *const shapeLabels[ORB_SHAPE_PLAYTIME] = {
          "Diamond", "Cube", "Octahedron", "Sphere", "LUNA", "Skull", "Atom"};
      int enabledShapeCount = 0;
      for (int shape = 0; shape < ORB_SHAPE_PLAYTIME; shape++)
        if (state->pendingOrbShapes & (1U << shape))
          enabledShapeCount++;
      char shapeCount[24];
      snprintf(shapeCount, sizeof(shapeCount), "%d/%d", enabledShapeCount,
               ORB_SHAPE_PLAYTIME);
      const int firstY = menuTop + lineHeight + 4;
      const int shapeFirst = optionsOrbsShapeFirstRow(state);
      const int rowCount = state->orbsShapesPage ? ORB_SHAPE_PLAYTIME :
                                                 optionsOrbsRowCount(state);
      int *selected = state->orbsShapesPage ? &state->selectedOrbShape :
                                            &state->selectedOrbsRow;
      int *first = state->orbsShapesPage ? &state->orbShapesFirstRow :
                                         &state->orbsFirstRow;
      if (*selected >= rowCount)
        *selected = state->orbsShapesPage ? 0 : 1;
      int visible = (menuBottom - 2 * lineHeight - firstY) / rowStep;
      if (visible < 1)
        visible = 1;
      if (*selected < *first)
        *first = *selected;
      if (*selected >= *first + visible)
        *first = *selected - visible + 1;
      if (*first > rowCount - visible)
        *first = rowCount > visible ? rowCount - visible : 0;
      const int selectorY = optionsSelectorY(&state->selector, state->page,
                                             *selected,
                                             firstY + (*selected - *first) * rowStep);
      for (int row = *first; row < rowCount && row < *first + visible; row++) {
        const char *label, *value;
        if (state->orbsShapesPage) {
          label = shapeLabels[row];
          value = state->pendingOrbShapes & (1U << row) ? "On" : "Off";
        } else if (row == 0) {
          label = "Behavior";
          value = state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ? "PS2 original" : "LUNA";
        } else if (row == 1) {
          label = "Appearance";
          value = state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ? "PS2 original" : "LUNA";
        } else if (row < shapeFirst) {
          label = row == 2 ? "Orb color" : "Tail color";
          value = colorLabels[row == 2 ? state->pendingOrbsColor : state->pendingTailsColor];
        } else {
          label = "Shapes";
          value = shapeCount;
        }
        drawOptionsTextRow(baseX, firstY + (row - *first) * rowStep,
                           gsGlobal->Width - baseX, *selected == row,
                           selectorY, lunaText(label), lunaText(value));
      }
      drawTextWindow(baseX, menuBottom - 2 * lineHeight,
                     gsGlobal->Width - baseX, menuBottom, 0,
                     HeaderTextColor, ALIGN_HCENTER,
                     state->orbsShapesPage ?
                         lunaText("Choose shapes for LUNA. All off keeps playtime.") :
                     state->selectedOrbsRow == shapeFirst ?
                         lunaText("Open Shapes to choose which formations appear.") :
                     state->selectedOrbsRow >= 2 && !optionsOrbsColorsEditable(state) ?
                         lunaText("PS2 original appearance uses its original colors.") :
                     state->selectedOrbsRow >= 2 ?
                         lunaText("Color choices affect only the shared orb background.") :
                     state->selectedOrbsRow == 0 ?
                         (state->pendingOrbsTheme == ORBS_THEME_PS2_ORIGINAL ?
                          lunaText("Seven clock-driven lights with long trails.") :
                          lunaText("Animated LUNA formations and title reactions.")) :
                         (state->pendingOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                          lunaText("Original halo and core masks from the PS2 ROM.") :
                          lunaText("LUNA's soft glass lights and bright cores.")));
    }
  } else if (state->target->platform == TARGET_PS1) {
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    drawOptionsSection(menuTop + lineHeight * 2, "PlayStation", 0);
    drawOptionsTextRow(baseX, menuTop + lineHeight * 4, gsGlobal->Width - baseX,
                       0, 0, "Game core", "PSXCore");
    drawOptionsTextRow(baseX, menuTop + lineHeight * 6, gsGlobal->Width - baseX,
                       0, 0, "Memory cards", "Isolated per disc");
    drawTextWindow(baseX + 18, menuBottom - lineHeight * 2,
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   HeaderTextColor, ALIGN_LEFT,
                   "PSXCore manages compatibility and saves.\nMissing cards can be created when launching.");
  } else {
    drawTextWindow(baseX, menuTop, gsGlobal->Width - baseX, 0, 0,
                   HeaderTextColor, ALIGN_HCENTER, state->titleHeader);
    const int contentTop = menuTop + lineHeight + 5;
    const int gameStep = lineHeight + lineHeight / 2;
    const int firstY = contentTop + lineHeight + 4;
    if (state->gameSection == GAME_HUB) {
      char compatSummary[24], launchSummary[24];
      int compatEnabled = 0;
      int compatCount = gameUsesOpl(state) ?
          LUNA_OPL_COMPAT_COUNT : LUNA_GAME_COMPAT_COUNT;
      uint8_t compatMask = gameUsesOpl(state) ?
          state->gameOptions.oplCompat : state->gameOptions.compat;
      for (int bit = 0; bit < compatCount; bit++)
        compatEnabled += (compatMask & (1U << bit)) != 0;
      if (!gameUsesOpl(state))
        compatEnabled += state->gameOptions.neutrinoIgrDisabled;
      if (compatEnabled)
        snprintf(compatSummary, sizeof(compatSummary), lunaText("%d enabled"), compatEnabled);
      else
        snprintf(compatSummary, sizeof(compatSummary), lunaText("Default"));
      int launchEnabled = gameLaunchEnabled(state);
      if (launchEnabled)
        snprintf(launchSummary, sizeof(launchSummary), lunaText("%d enabled"), launchEnabled);
      else
        snprintf(launchSummary, sizeof(launchSummary), lunaText("Default"));
      const char *const summaries[GAME_SECTION_COUNT] = {
          gameVMCEnabled(state->titleArguments) ? lunaText("Enabled") : lunaText("Disabled"),
          compatSummary,
          lunaGameOptionsValue(&state->gameOptions,
              gameUsesOpl(state) ? LUNA_GAME_OPL_VIDEO_MODE : LUNA_GAME_VIDEO_MODE),
          state->cheats.enabled ? lunaText("Enabled") : lunaText("Disabled"), launchSummary};
      drawOptionsSection(contentTop, lunaText("Game settings"), 0);
      int selectorY = optionsSelectorY(&state->selector, state->page,
          state->selectedGameHubRow, firstY + state->selectedGameHubRow * gameStep);
      for (int row = 0; row < GAME_SECTION_COUNT; row++)
        drawOptionsTextRow(baseX, firstY + row * gameStep,
                           gsGlobal->Width - baseX,
                           row == state->selectedGameHubRow, selectorY,
                           lunaText(gameSectionLabels[row]), summaries[row]);
      drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                     ALIGN_LEFT, lunaText(gameSectionDescriptions[state->selectedGameHubRow]));
    } else {
      const GameSection section = state->gameSection;
      if (section != GAME_CHEATS && state->selectedGameRow >= gameSectionRowCount(state, section))
        state->selectedGameRow = 0;
      drawOptionsSection(contentTop, section == GAME_VIDEO_OUT ? lunaText("Video out") :
                                                          lunaText(gameSectionLabels[section]), 0);
      if (section == GAME_CHEATS) {
        drawGameCheatRows(state, baseX, firstY, menuBottom);
      } else if (section == GAME_VIDEO_OUT) {
        drawVideoOutRows(state, baseX, firstY, gameStep, menuBottom);
      } else if (section == GAME_MEMORY_CARDS) {
        drawGameVMCRows(state, baseX, firstY, gameStep, menuBottom);
      } else {
        int rowStart = firstY;
        if (section == GAME_COMPATIBILITY) {
          drawCompatibilityTabs(state, baseX, firstY);
          rowStart += gameStep;
        }
        const LunaGameRow selectedRow =
            gameSectionRow(state, section, state->selectedGameRow);
        int selectorY = optionsSelectorY(&state->selector, state->page,
            GAME_SECTION_COUNT + section * 6 + state->selectedGameRow,
            rowStart + state->selectedGameRow * gameStep);
        for (int row = 0; row < gameSectionRowCount(state, section); row++) {
          LunaGameRow option = gameSectionRow(state, section, row);
          drawOptionsTextRow(baseX, rowStart + row * gameStep,
                             gsGlobal->Width - baseX,
                             row == state->selectedGameRow, selectorY,
                             lunaText(gameRowLabels[option]),
                             option == LUNA_GAME_PS2_LOGO ?
                                 gamePS2LogoValue(state) :
                                 lunaGameOptionsValue(&state->gameOptions, option));
        }
        drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                       gsGlobal->Height - footerHeight, 0,
                       HeaderTextColor, ALIGN_LEFT,
                       lunaText(gameRowDescriptions[selectedRow]));
      }
    }
  }
  if (state->page == OPTIONS_PER_GAME && state->target->platform == TARGET_PS1) {
    const ButtonPrompt prompts[] = {{ICON_SQUARE, "Launch"}, {ICON_TRIANGLE, "Back"}};
    drawPromptBar(20, gsGlobal->Height-footerHeight, gsGlobal->Width-20,
                  gsGlobal->Height, 16, HeaderTextColor, (PromptBar){NULL,prompts,2});
  } else drawOptionsFooter(state->page == OPTIONS_PER_GAME, 0,
                    gameDirty || systemDirty || viewsDirty || orbsDirty,
                    state->page == OPTIONS_ORBS && !optionsOrbsEditable(state),
                    state->page == OPTIONS_ORBS && state->orbsShapesPage ? lunaText("Toggle") :
                    state->page == OPTIONS_ORBS &&
                    state->pendingOrbsTheme == ORBS_THEME_LUNA &&
                    state->selectedOrbsRow == optionsOrbsShapeFirstRow(state) ? lunaText("Open") :
                    state->page == OPTIONS_PER_GAME && state->gameSection == GAME_HUB ?
                        (state->selectedGameHubRow == GAME_MEMORY_CARDS ?
                            (gameVMCEnabled(state->titleArguments) ? lunaText("Manage") : lunaText("Enable")) :
                            lunaText("Open")) :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_VIDEO_OUT ? lunaText("Select") :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_VIDEO && state->selectedGameRow == 0 ?
                        lunaText("Open") :
                    state->page == OPTIONS_PER_GAME &&
                    state->gameSection == GAME_MEMORY_CARDS ?
                        (state->selectedGameRow == 2 ? lunaText("Create") :
                         state->selectedGameRow == 3 ? lunaText("Disable") : lunaText("Assign")) : NULL,
                    (state->page == OPTIONS_PER_GAME &&
                     state->gameSection != GAME_HUB) ||
                    (state->page == OPTIONS_ORBS &&
                     (!optionsOrbsEditable(state) || state->orbsShapesPage)));
  if (showDirty && state->saveError)
    drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                   gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ErrorTextColor, ALIGN_HCENTER,
                   state->saveErrorLabel ? state->saveErrorLabel :
                   state->page == OPTIONS_GLOBAL ? lunaText("Could not save global settings") :
                   state->page == OPTIONS_VIEWS ? lunaText("Could not save views") :
                   state->page == OPTIONS_ORBS ? lunaText("Could not save orb settings") :
                                           lunaText("Could not save game settings"));
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
                              int pendingCore,
                              int background, int glassColor, int fontSetting,
                              int ambient, int logo, int core) {
  return pendingBackground != background || pendingGlassColor != glassColor ||
         pendingFont != fontSetting || pendingAmbient != ambient ||
         pendingLogo != logo || pendingCore != core;
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
  snprintf(message, sizeof(message), lunaText("Creating card: %d%%"), percent);
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawOptionsSheet();
  const int baseX = keepoutArea + 10;
  drawOptionsSection(optionsHeadingY(),
                     lunaText("Virtual memory cards"), 0);
  drawTextWindow(baseX, gsGlobal->Height / 2,
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, message);
  drawTextWindow(baseX, gsGlobal->Height - footerHeight - getFontLineHeight(),
                 gsGlobal->Width - baseX, 0, 0, HeaderTextColor,
                 ALIGN_HCENTER, lunaText("Please wait until creation finishes"));
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
    const int menuTop = optionsMenuTop();
    const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
    const int rowStep = lineHeight + lineHeight / 3;
    const int rowRight = gsGlobal->Width - baseX;
    const int focusY = menuTop + selected * rowStep;
    const int scrollOffset = focusY + lineHeight > menuBottom
                                 ? focusY + lineHeight - menuBottom : 0;
    drawOptionsSection(optionsHeadingY(),
                       slot == 0 ? lunaText("VMC slot 1") : lunaText("VMC slot 2"), 0);
    drawTextWindow(baseX, menuTop - lineHeight, gsGlobal->Width - baseX,
                   menuTop, 0, HeaderTextColor, ALIGN_HCENTER,
                   gameOptions->vmcSlotLabel[slot]);
    for (int index = 0; index <= count; index++) {
      int y = menuTop + index * rowStep - scrollOffset;
      if (y < menuTop || y + lineHeight > menuBottom)
        continue;
      drawOptionsTextRow(baseX, y, rowRight,
                         index == selected, y,
                         index == 0 ? lunaText("Physical card") : files[index - 1], NULL);
    }
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, !supported ? lunaText("File cards need enabled local storage. Use a physical card.")
                               : count ? lunaText("Select a card from /VMC on this drive.")
                                     : lunaText("Create a card from the Virtual memory cards page."));
    const ButtonPrompt assign[] = {
        {ICON_CROSS, lunaText("Assign")}, {ICON_TRIANGLE, lunaText("Back")}};
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
  const int rowCount = state->orbsShapesPage ? ORB_SHAPE_PLAYTIME :
                                             optionsOrbsRowCount(state);
  int *selected = state->orbsShapesPage ? &state->selectedOrbShape :
                                        &state->selectedOrbsRow;
  if (*selected >= rowCount)
    *selected = state->orbsShapesPage ? 0 : 1;
  if (input & PAD_UP) {
    *selected = (*selected + rowCount - 1) % rowCount;
  } else if (input & PAD_DOWN) {
    *selected = (*selected + 1) % rowCount;
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
    const int direction = input & PAD_LEFT ? -1 : 1;
    if (state->orbsShapesPage)
      state->pendingOrbShapes ^= 1U << state->selectedOrbShape;
    else if (state->selectedOrbsRow == 0)
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
    else if (state->pendingOrbsTheme == ORBS_THEME_LUNA &&
             state->selectedOrbsRow == optionsOrbsShapeFirstRow(state) &&
             (input & (PAD_CROSS | PAD_CIRCLE | PAD_RIGHT))) {
      state->orbsShapesPage = 1;
      state->selector.initialized = 0;
      state->saveError = 0;
    }
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingOrbsTheme != *state->orbsThemeSetting) {
      state->saveErrorLabel = lunaText("Could not save orb behavior");
      state->saveError = saveAmbientOrbsTheme(state->target,
                        (AmbientOrbsTheme)state->pendingOrbsTheme);
      if (!state->saveError) {
        *state->orbsThemeSetting = state->pendingOrbsTheme;
        setAmbientOrbsTheme((AmbientOrbsTheme)state->pendingOrbsTheme, uiNowMs());
      }
    }
    if (!state->saveError && state->pendingOrbsAppearance != *state->orbsAppearanceSetting) {
      state->saveErrorLabel = lunaText("Could not save orb appearance");
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
      state->saveErrorLabel = lunaText("Could not save orb color");
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
      state->saveErrorLabel = lunaText("Could not save tail color");
      state->saveError = saveAmbientOrbsColor(state->target, ORBS_COLOR_PART_TAILS,
                                      (AmbientOrbsColor)state->pendingTailsColor);
      if (!state->saveError) {
        *state->tailsColorSetting = state->pendingTailsColor;
        setAmbientOrbsColor(ORBS_COLOR_PART_TAILS,
                            (AmbientOrbsColor)state->pendingTailsColor);
      }
    }
    if (!state->saveError && state->pendingOrbShapes != *state->enabledOrbShapes) {
      state->saveErrorLabel = lunaText("Could not save orb shapes");
      state->saveError = saveEnabledOrbShapes(state->target, state->pendingOrbShapes);
      if (!state->saveError) {
        *state->enabledOrbShapes = state->pendingOrbShapes;
        setAmbientOrbsShapes(state->pendingOrbShapes, uiNowMs());
      }
    }
  } else if (input & PAD_TRIANGLE) {
    if (state->orbsShapesPage) {
      state->orbsShapesPage = 0;
      state->selector.initialized = 0;
      state->saveError = 0;
      return 0;
    }
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
  } else if (input & (PAD_CROSS | PAD_CIRCLE | PAD_LEFT | PAD_RIGHT)) {
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
      state->saveErrorLabel = lunaText("Could not save List art layout");
      state->saveError = saveClassicArtOverlap(state->target, state->pendingOverlap);
      if (!state->saveError) {
        *state->classicArtOverlap = state->pendingOverlap;
        setClassicArtOverlap(state->pendingOverlap);
      }
    }
    if (!state->saveError && state->pendingViews != *state->enabledViews) {
      state->saveErrorLabel = lunaText("Could not save enabled views");
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
    state->selectedGlobal = (state->selectedGlobal + 5) % 6;
  } else if (input & PAD_DOWN) {
    state->selectedGlobal = (state->selectedGlobal + 1) % 6;
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
      state->pendingLogo = !state->pendingLogo;
    else if (state->selectedGlobal == 4)
      state->pendingCore = !state->pendingCore;
    else
      state->pendingAmbient = !state->pendingAmbient;
  } else if (input & PAD_START) {
    state->saveError = 0;
    if (state->pendingBackground != *state->ambientOrbsBackgroundSetting) {
      if (setLibraryBackground((LibraryBackground)state->pendingBackground)) {
        state->saveError = -1;
        state->saveErrorLabel = lunaText("Could not load background");
      } else {
        int result = saveLibraryBackground(state->target,
                                           (LibraryBackground)state->pendingBackground);
        if (!result)
          *state->ambientOrbsBackgroundSetting = state->pendingBackground;
        else {
          setLibraryBackground((LibraryBackground)*state->ambientOrbsBackgroundSetting);
          state->saveError = result;
          state->saveErrorLabel = lunaText("Could not save background");
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
        state->saveErrorLabel = lunaText("Could not save glass color");
      }
    }
    if (state->pendingFont != *state->fontSetting) {
      if (setUIFont((UIFont)state->pendingFont)) {
        if (!state->saveError) {
          state->saveError = -1;
          state->saveErrorLabel = lunaText("Could not load font");
        }
      } else {
        int result = saveUIFont(state->target, (UIFont)state->pendingFont);
        if (!result)
          *state->fontSetting = state->pendingFont;
        else {
          setUIFont((UIFont)*state->fontSetting);
          if (!state->saveError) {
            state->saveError = result;
            state->saveErrorLabel = lunaText("Could not save font");
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
        state->saveErrorLabel = lunaText("Could not save ambient sound");
      }
    }
    if (state->pendingCore != state->coreSetting) {
      int result = saveGameCoreOpl(state->target, state->pendingCore);
      if (!result) {
        state->coreSetting = state->pendingCore;
        lunaApplyGlobalGameCore(state->titleArguments, state->coreSetting);
        lunaGameOptionsRead(&state->gameOptions, state->titleArguments);
      } else if (!state->saveError) {
        state->saveError = result;
        state->saveErrorLabel = "Could not save game core setting";
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
        state->saveErrorLabel = lunaText("Could not save PS2 logo setting");
      }
    }
  } else if (input & PAD_TRIANGLE) {
    return 1;
  }
  return 0;
}

// Returns 1 for Back, -1 if a test launch unexpectedly returns, or 0 to stay.
static int handleGameInput(OptionsMenuState *state, int input) {
  if (state->target->platform == TARGET_PS1) {
    if (input & PAD_TRIANGLE) return 1;
    if (input & PAD_SQUARE) return uiLaunchTitleWithCheats(state->target,NULL,NULL);
    return 0;
  }
  if (input & PAD_SQUARE) {
    // Launch title without saving arguments
    return uiLaunchTitleWithCheats(state->target, state->titleArguments, &state->cheats);
  }
  if (input & PAD_START) {
    state->saveErrorLabel = lunaText("Could not save game settings");
    state->saveError = 0;
    if (state->titleArgumentsChanged || !state->cheatsChanged) {
      state->saveError = updateTitleLaunchArguments(state->target, state->titleArguments);
      if (!state->saveError) state->titleArgumentsChanged = 0;
    }
    if (!state->saveError && state->cheatsChanged) {
      state->saveErrorLabel = lunaText("Could not save cheat selections");
      state->saveError = lunaCheatsSaveSettings(state->target, &state->cheats);
      if (!state->saveError) state->cheatsChanged = 0;
    }
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
      if (state->gameSection == GAME_CHEATS) reloadGameCheats(state);
    }
    return 0;
  }
  if (input & PAD_TRIANGLE) {
    state->gameSection = state->gameSection == GAME_VIDEO_OUT ? GAME_VIDEO : GAME_HUB;
    state->selectedGameRow = 0;
    state->selector.initialized = 0;
    return 0;
  }
  if (state->gameSection == GAME_CHEATS) {
    int count = 3 + state->cheatFile.entryCount;
    if (input & PAD_UP) state->selectedGameRow = (state->selectedGameRow + count - 1) % count;
    else if (input & PAD_DOWN) state->selectedGameRow = (state->selectedGameRow + 1) % count;
    else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int row = state->selectedGameRow;
      if (row == 0 && (!state->cheatLoadResult || state->cheats.enabled)) {
        state->cheats.enabled = !state->cheats.enabled;
        state->cheatsChanged = 1;
      } else if (row == 1) {
        state->cheats.count = 0;
        state->cheatsChanged = 1;
      } else if (row == 2) reloadGameCheats(state);
      else if (row >= 3 && !state->cheatFile.entries[row - 3].required) {
        state->cheatsChanged |= lunaCheatToggle(&state->cheats, state->cheatFile.entries[row - 3].id);
      }
    }
    return 0;
  }
  int count = state->gameSection == GAME_MEMORY_CARDS &&
              !gameVMCEnabled(state->titleArguments) ? 3 :
              gameSectionRowCount(state, state->gameSection);
  if (input & PAD_UP) {
    state->selectedGameRow = (state->selectedGameRow + count - 1) % count;
    return 0;
  }
  if (input & PAD_DOWN) {
    state->selectedGameRow = (state->selectedGameRow + 1) % count;
    return 0;
  }
  if (state->gameSection == GAME_VIDEO_OUT) {
    if ((input & (PAD_LEFT | PAD_RIGHT)) &&
        lunaOplDevice(state->target->device->mode) != NULL) {
      state->videoOutOpl = !state->videoOutOpl;
      int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                     state->gameOptions.videoMode;
      state->selectedGameRow = mode > 0 ? mode : 0;
      state->videoOutFirstRow = 0;
      state->selector.initialized = 0;
    } else if (input & (PAD_CROSS | PAD_CIRCLE)) {
      int current = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                        state->gameOptions.videoMode;
      int mode = current == state->selectedGameRow ? 0 : state->selectedGameRow;
      if (lunaGameOptionsSetVideoMode(&state->gameOptions, state->titleArguments,
                                     state->videoOutOpl, mode))
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
                 lunaText("File cards need enabled local storage. MMCE switches cards automatically when enabled."));
        return 0;
      }
      if (createNextGameVMC(state, created, sizeof(created)))
        snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                 lunaText("Created %.24s. Select a slot to assign it."),
                 strrchr(created, '/') + 1);
      else
        snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                 lunaText("Could not create card. Check drive and free space."));
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
                   lunaText("Virtual cards disabled. Press Start to save."));
        } else {
          snprintf(state->vmcStatus, sizeof(state->vmcStatus),
                   lunaText("Could not disable virtual cards."));
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
  LunaGameRow selectedRow = gameSectionRow(state, state->gameSection,
                                            state->selectedGameRow);
  if ((selectedRow == LUNA_GAME_VIDEO_MODE || selectedRow == LUNA_GAME_OPL_VIDEO_MODE) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    state->gameSection = GAME_VIDEO_OUT;
    state->videoOutOpl = gameUsesOpl(state);
    int mode = state->videoOutOpl ? state->gameOptions.oplVideoMode :
                                   state->gameOptions.videoMode;
    state->selectedGameRow = mode > 0 ? mode : 0;
    state->videoOutFirstRow = 0;
    state->selector.initialized = 0;
  } else if ((selectedRow == LUNA_GAME_VMC_SLOT1 || selectedRow == LUNA_GAME_VMC_SLOT2) &&
      (input & (PAD_CROSS | PAD_CIRCLE))) {
    int slot = selectedRow - LUNA_GAME_VMC_SLOT1;
    if (uiVMCPickerLoop(state->target, state->titleArguments, &state->gameOptions, slot)) {
      state->titleArgumentsChanged = 1;
      snprintf(state->vmcStatus, sizeof(state->vmcStatus),
               lunaText("Card selection changed. Press Start to save."));
    }
  } else if (selectedRow == LUNA_GAME_LAUNCH_ARGUMENTS &&
             (input & (PAD_CROSS | PAD_CIRCLE))) {
    int result = uiArgumentListLoop(state->target, state->titleArguments, &state->cheats);
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
        selectedRow == LUNA_GAME_CORE ?
        lunaGameOptionsCycleCore(&state->gameOptions,
                                 state->titleArguments,
                                 state->coreSetting, direction) :
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
                       int *tailsColorSetting, uint32_t *enabledOrbShapes,
                        uint32_t *enabledViews) {
  int res = 0;
  int logoSetting = loadPS2LogoEnabled(target);
  int coreSetting = loadGameCoreOpl(target);
  OptionsMenuState state = {
      .target = target,
      .classicArtOverlap = classicArtOverlap,
      .ambientOrbsBackgroundSetting = ambientOrbsBackgroundSetting,
      .glassColorSetting = glassColorSetting,
      .fontSetting = fontSetting,
      .ambientEnabled = ambientEnabled,
      .logoSetting = logoSetting,
      .coreSetting = coreSetting,
      .orbsThemeSetting = orbsThemeSetting,
      .orbsAppearanceSetting = orbsAppearanceSetting,
      .orbsColorSetting = orbsColorSetting,
      .tailsColorSetting = tailsColorSetting,
      .enabledOrbShapes = enabledOrbShapes,
      .enabledViews = enabledViews,
      .pendingOverlap = *classicArtOverlap,
      .pendingBackground = *ambientOrbsBackgroundSetting,
      .pendingGlassColor = *glassColorSetting,
      .pendingFont = *fontSetting,
      .pendingAmbient = *ambientEnabled,
      .pendingLogo = logoSetting,
      .pendingCore = coreSetting,
      .pendingOrbsTheme = *orbsThemeSetting,
      .pendingOrbsAppearance = *orbsAppearanceSetting,
      .pendingOrbsColor = *orbsColorSetting,
      .pendingTailsColor = *tailsColorSetting,
      .pendingOrbShapes = *enabledOrbShapes,
      .pendingViews = *enabledViews,
      .page = OPTIONS_PER_GAME,
      .gameSection = GAME_HUB,
      .selectedView = UI_VIEW_CLASSIC};
  snprintf(state.titleHeader, sizeof(state.titleHeader), "%.38s  (%s)",
           target->name, target->id);

  // Load arguments from config files
  state.titleArguments = target->platform == TARGET_PS1 ? calloc(1,sizeof(ArgumentList)) :
                                                        loadLaunchArgumentLists(state.target);
  if (!state.titleArguments) return 0;
  lunaGameOptionsRead(&state.gameOptions, state.titleArguments);
  if (target->platform != TARGET_PS1 && lunaCheatsLoadSettings(target, &state.cheats)) {
    state.cheatsChanged = 1; // Save can repair an unreadable selection file.
    state.saveError = 1;
    state.saveErrorLabel = lunaText("Could not read cheat settings. Cheats are off; save to reset.");
  }
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
      state.orbsShapesPage = 0;
      state.selector.initialized = 0;
      state.saveError = 0;
      continue;
    }
    if (input & PAD_R1) {
      state.page = optionsNextPage(state.page, 1);
      state.orbsShapesPage = 0;
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
    state.pendingOrbShapes = *state.enabledOrbShapes;
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
  lunaCheatFileFree(&state.cheatFile);
  return res;
}

// LUNA's advanced view of the merged game and global launch arguments.
// Returns -1 after a failed launch, 0 on Back, 1 after edits, or 2 after Save.
static int uiArgumentListLoop(Target *target, ArgumentList *titleArguments,
                              const LunaCheatSettings *cheats) {
  int selectedArgIdx = 0;
  int saveError = 0;
  int changed = 0;
  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawOptionsSheet();
    const int baseX = keepoutArea + 10;
    const int lineHeight = getFontLineHeight();
    const int menuTop = optionsMenuTop();
    const int menuBottom = gsGlobal->Height - footerHeight - lineHeight;
    const int rowStep = lineHeight + lineHeight / 3;
    const int focusY = menuTop + selectedArgIdx * rowStep;
    const int scrollOffset = focusY + lineHeight > menuBottom
                                 ? focusY + lineHeight - menuBottom : 0;

    drawOptionsSection(optionsHeadingY(),
                       lunaText("Launch arguments"), 0);
    snprintf(lineBuffer, sizeof(lineBuffer), "%.38s  (%s)", target->name, target->id);
    drawTextWindow(baseX, menuTop - lineHeight, gsGlobal->Width - baseX,
                   menuTop, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    if (titleArguments->total == 0)
      drawTextWindow(baseX + 18, menuTop, gsGlobal->Width - baseX, 0, 0,
                     FontMainColor, ALIGN_LEFT, lunaText("No launch arguments set"));

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
                         argument->isDisabled ? lunaText("Off") : lunaText("On"));
    }
    drawTextWindow(baseX + 18, menuBottom, gsGlobal->Width - baseX,
                   gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                   ALIGN_LEFT, lunaText("[G] Inherited from global settings"));
    drawOptionsFooter(1, 1, changed, 0, NULL, 0);
    if (saveError)
      drawTextWindow(baseX, menuBottom, gsGlobal->Width - baseX,
                     gsGlobal->Height - footerHeight, 0, ErrorTextColor, ALIGN_HCENTER,
                     lunaText("Could not save game settings"));
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    presentOptionsFrame();

    int input = waitForInput(-1);
    if (input & PAD_SQUARE) {
      if (uiLaunchTitleWithCheats(target, titleArguments, cheats)) return -1;
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
