// LUNA 2026
#include "common.h"
#include "dprintf.h"
#include "favorites.h"
#include "neutrino.h"
#include "options.h"
#include "ui/ambient.h"
#include "ui/ambient_orbs.h"
#include "ui/art_cache.h"
#include "ui/file_manager.h"
#include "storage.h"
#include "ui/graphics.h"
#include "ui/handoff.h"
#include "ui/navigation.h"
#include "ui/options_menu.h"
#include "ui/view_scroll.h"
#include "ui/pad.h"
#include "ui/ui.h"
#include "ui/view_internal.h"
#include "ui/view_state.h"
#include <dmaKit.h>
#include <errno.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <libpad.h>
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>

#define PSBBN_TIMER_TICKS_PER_MS 576ULL
#define GRID_LEFT_SHOULDERS PAD_L2
#define GRID_RIGHT_SHOULDERS PAD_R2
#define SPLASH_MIN_VISIBLE_MS 3400
#define LIBRARY_RETURN_FADE_MS 180
#define CLASSIC_LIST_ENTRY_SLIDE_MS 360
#define LIBRARY_VIEW_ENTRY_MS 320
#define QUICK_MENU_SLIDE_MS 200

void closeUI();
int uiLoop(TargetList *titles, int preparedCollectionIdx);
void uiSplashThread();

GSGLOBAL *gsGlobal;
char lineBuffer[255];
static int libraryBackground = LIBRARY_BACKGROUND_STARS;
static int orbsThemeSetting = ORBS_THEME_LUNA;
static int orbsAppearanceSetting = ORBS_APPEARANCE_LUNA;
static int orbsColorSetting = ORBS_COLOR_ORIGINAL;
static int tailsColorSetting = ORBS_COLOR_ORIGINAL;
static int glassColorSetting = GLASS_COLOR_ORIGINAL;
static int fontSetting = UI_FONT_DEJAVU;
static uint32_t splashVisibleStartMs;
static uint32_t libraryReturnFadeStartMs;

const int keepoutArea = 20;
const int headerHeight = 40;
const int footerHeight = 60;

uint32_t uiNowMs(void) {
  return (uint32_t)((GetTimerSystemTime() >> 8) / PSBBN_TIMER_TICKS_PER_MS);
}

static int libraryViewEntryProgress(int entryView, UILibraryView view,
                                    uint32_t startMs, uint32_t now) {
  if (entryView != (int)view)
    return 1000;
  uint32_t elapsed = now - startMs;
  if (elapsed >= LIBRARY_VIEW_ENTRY_MS)
    return 1000;
  return lunaNavEase((int)(elapsed * 1000U / LIBRARY_VIEW_ENTRY_MS));
}

static void drawLibraryFooter(int canLaunch) {
  const ButtonPrompt prompts[] = {
      {ICON_CIRCLE, "View"}, {ICON_CROSS, canLaunch ? "Launch" : NULL},
      {ICON_START, "Menu"}, {ICON_R1, "More"}};
  drawPromptBar(20, gsGlobal->Height - footerHeight + 8,
                gsGlobal->Width - 20, gsGlobal->Height, 8, FontMainColor,
                (PromptBar){NULL, prompts, 4});
}

static void drawLibraryQuickMenu(int progress, int closing, UILibraryView view,
                                 int favoritesOnly, int isFavorite,
                                 int total, const char *title) {
  if (progress <= 0)
    return;
  const char *labels[] = {
      favoritesOnly ? "Show All" : "Show Favorites",
      isFavorite ? "Remove from Favorites" : "Add to Favorites",
      "Options", "Random"};
  const IconType icons[] = {ICON_CROSS, ICON_CIRCLE, ICON_TRIANGLE, ICON_R3};
  int count = view == UI_VIEW_ORBIT ? 4 : 3;
  int rowHeight = getFontLineHeight() + 10;
  int height = (count + 1) * rowHeight + 16;
  int slide = 354 * (1000 - lunaNavEase(progress)) / 1000;
  int left = gsGlobal->Width - 354 + slide;
  int right = gsGlobal->Width - 20 + slide;
  int top = 16;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0x00, 0x04, 0x0A, (0x14 * progress) / 1000));
  // A pair of faint rails trails the glass as it accelerates offscreen.
  int wake = closing ? (4 * progress * (1000 - progress)) / 1000 : 0;
  if (wake > 0) {
    gsKit_prim_sprite(gsGlobal, left - 16, top + 12, left - 12,
                      top + height - 12, 0,
                      glassPresetColor(0x64, 0xB0, 0xD8, (0x25 * wake) / 1000));
    gsKit_prim_sprite(gsGlobal, left - 29, top + 22, left - 26,
                      top + height - 22, 0,
                      glassPresetColor(0x50, 0x92, 0xB8, (0x13 * wake) / 1000));
  }
  drawGlassPanelWithFillAlpha(left, top, right, top + height, 0, 0x44);
  if (wake > 0)
    gsKit_prim_sprite(gsGlobal, left, top + 8, left + 3,
                      top + height - 8, 1,
                      glassPresetColor(0xA0, 0xE0, 0xF8, (0x30 * wake) / 1000));
  drawTextWindow(left + 16, top + 10, right - 16,
                 top + rowHeight + 10, 0, HeaderTextColor,
                 ALIGN_CENTER, total > 0 ? title : "No favorites yet");
  for (int i = 0; i < count; i++) {
    int y = top + (i + 1) * rowHeight + 8;
    int enabled = i == 0 || (total > 0 && (i != 3 || total > 1));
    drawIconWindow(left + 16, y, 0, y + rowHeight, 0,
                   enabled ? FontMainColor : HeaderTextColor, ALIGN_VCENTER, icons[i]);
    drawTextWindow(left + 16 + getIconWidth(icons[i]) + 10, y,
                   right - 16, y + rowHeight, 0,
                   enabled ? FontMainColor : HeaderTextColor, ALIGN_VCENTER, labels[i]);
  }
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
}

static const int discSin[32] = {0,   25,  49,  71,  90,  106, 117, 125, 127, 125, 117, 106, 90,  71,  49,  25,
                                0,  -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

int discWave(uint32_t phase) {
  int index = (phase >> 11) & 31;
  int next = (index + 1) & 31;
  int fraction = (phase >> 3) & 0xFF;
  return discSin[index] + ((discSin[next] - discSin[index]) * fraction) / 256;
}

int psbbnFieldStableY(int y) {
  return (gsGlobal->Interlace == GS_INTERLACED) ? (y & ~1) : y;
}

int psbbnFieldStableHeight(void) {
  return (gsGlobal->Interlace == GS_INTERLACED) ? 2 : 1;
}

void initVMode(GSGLOBAL *gsGlobal) {
  switch (LAUNCHER_OPTIONS.vmode) {
  case GS_MODE_NTSC:
    DPRINTF("Forcing NTSC mode\n");
    gsGlobal->Mode = GS_MODE_NTSC;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FIELD;
    gsGlobal->Width = 640;
    gsGlobal->Height = 448;
    break;
  case GS_MODE_PAL:
    DPRINTF("Forcing PAL mode\n");
    gsGlobal->Mode = GS_MODE_PAL;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FIELD;
    gsGlobal->Width = 640;
    gsGlobal->Height = 512;
    break;
  case GS_MODE_DTV_480P:
    DPRINTF("Forcing 480p mode\n");
    gsGlobal->Mode = GS_MODE_DTV_480P;
    gsGlobal->Interlace = GS_NONINTERLACED;
    gsGlobal->Field = GS_FRAME;
    gsGlobal->Width = 640;
    gsGlobal->Height = 448;
    break;
  default:
  }
}

int uiInit() {
  if (gsGlobal != NULL) {
    DPRINTF("Reinitializing UI\n");
    closeUI();
  }
  StartTimerSystemTime();
  resetGlassVisuals(uiNowMs());
  resetAmbientOrbsTextures();
  gsGlobal = gsKit_init_global();
  initVMode(gsGlobal);
  gsGlobal->PSM = GS_PSM_CT24; // Set color depth to avoid PAL VRAM issues
  gsGlobal->PSMZ = GS_PSMZ_16S;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->DoubleBuffering = GS_SETTING_ON;
  // Setup TEST register to ignore fully transparent pixels
  gsGlobal->Test->ATST = 7;    // Set alpha test method to NOTEQUAL (pixels with A not equal to AREF pass)
  gsGlobal->Test->AREF = 0x00; // Set reference value to 0x00 (transparent)
  gsGlobal->Test->AFAIL = 0;   // Don't update buffers when test fails

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

  // Initialize the DMAC
  int res;
  if ((res = dmaKit_chan_init(DMA_CHANNEL_GIF))) {
    DPRINTF("ERROR: Failed to initlize DMAC: %d\n", res);
    return res;
  }

  // Init screen
  gsKit_vram_clear(gsGlobal);
  gsKit_init_screen(gsGlobal);
  gsKit_display_buffer(gsGlobal); // Switch display buffer to avoid garbage appearing on screen
  initOpeningCubeCapture();
  if (reserveUIFontVRAM()) {
    DPRINTF("ERROR: Failed to reserve font VRAM\n");
    return -1;
  }
  gsKit_TexManager_init(gsGlobal);
  initGlassStarAtlas();
  // Set alpha and mode, clear active buffer
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  gsKit_clear(gsGlobal, BGColor);

  // Initialize resources
  if (initGraphics()) {
    DPRINTF("ERROR: Failed to initialize font\n");
    return -1;
  };

  // Init cover geometry and artwork caches.
  calculateCoverArtGeometry();
  if (artCacheInit())
    return -1;

  return 0;
}

// Invalidates currently loaded texture and loads a new one
// Frees textures and deinits gsKit
void closeUI() {
  gsKit_vram_clear(gsGlobal);
  artCacheShutdown();
  closeFont();
  gsKit_deinit_global(gsGlobal);
  gsGlobal = NULL;
}

// Main UI loop. Displays the target list.
int uiLoop(TargetList *titles, int preparedCollectionIdx) {
  // Reinitialize UI if video mode doesn't match
  if ((LAUNCHER_OPTIONS.vmode != VMODE_NONE) && (gsGlobal->Mode != LAUNCHER_OPTIONS.vmode)) {
    uiInit();
  }

  int res = 0;
  uint8_t *favoriteFlags = NULL;
  uint8_t *allFavoriteFlags = NULL;
  uint8_t *visibleFavoriteFlags = NULL;
  TargetList *allTitles = titles;
  TargetList *favoriteTitles = NULL;
  if ((gsGlobal == NULL) && (res = uiInit())) {
    DPRINTF("ERROR: Failed to init UI: %d\n", res);
    goto exit;
  }

  // Init gamepad inputs
  initPad();

  if (titles->total == 0) {
    res = uiMainMenuLoop(0);
    ambientStop();
    closePad();
    closeUI();
    return res == STORAGE_UI_REFRESH ? res : 0;
  }

  int isCoverUninitialized = 1;
  int isDiscUninitialized = 1;
  int selectedTitleIdx = 0;
  int maxTitlesPerPage = (gsGlobal->Height - (headerHeight + footerHeight)) / getFontLineHeight();
  int psbbnCoverBaseIdx = preparedCollectionIdx;
  int psbbnAnimationTargetIdx = -1;
  int psbbnOutgoingTitleIdx = -1;
  int psbbnAnimationStartOffset = 0;
  uint32_t psbbnAnimationStart = 0;
  uint32_t psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
  uint32_t psbbnAnimationElapsedFrames = 0;
  LunaCollectionScan collectionScan = {0};
  int collectionVisualTitleIdx = -1;
  int collectionVisualCoverIdx = PSBBN_COVER_CACHE_FOCUS;
  int collectionActionCoverIdx = PSBBN_COVER_CACHE_FOCUS;
  int orbitRandomActive = 0;
  int orbitRandomTargetIdx = -1;
  int orbitRandomDirection = 1;
  uint32_t orbitRandomNextStep = 0;
  int orbitRandomButtonHeld = 0;
  int orbsVisualTitleIdx = -1;
  int favoriteButtonHeld = 0;
  int favoritesTabButtonHeld = 0;
  int favoritesOnly = 0;
  int classicArtRequestedIdx = -1;
  int classicArtSubmittedIdx = -1;
  int classicEntryFirstFramePending = 0;
  Target *classicPrefetchCandidate = NULL;
  Target *classicPrefetchSubmitted = NULL;
  uint32_t classicPrefetchDueMs = 0;
  int classicNavHeld = 0;
  int classicDisplayedCoverAvailable = 0;
  int classicDisplayedDiscAvailable = 0;
  int classicEntryListSlideActive = 0;
  uint32_t classicEntryListSlideStartMs = 0;
  int classicArtOverlap = 0;
  int ambientEnabled = 0;
  uint32_t enabledViews = UI_VIEW_DEFAULT_MASK;
  uint32_t classicArtDueMs = 0;
  LunaNavRepeatState classicRepeat = {0};
  LunaScrollFast scrollFast = {0};
  UILibraryView view = UI_VIEW_CLASSIC;
  int entryView = -1;
  int entryPending = 0;
  uint32_t entryStartMs = 0;
  Target *curTarget = titles->first;

  // Get last launched title and find it in the target list
  char *lastTitle = calloc(sizeof(char), PATH_MAX + 1);
  if (!getLastLaunchedTitle(lastTitle, PATH_MAX + 1)) {
    int mountpointLen;
    while (curTarget != NULL) {
      // Compare paths without the mountpoint
      mountpointLen = getRelativePathIdx(curTarget->fullPath);
      if (mountpointLen == -1)
        mountpointLen = 0;

      if (!strcmp(lastTitle, &curTarget->fullPath[mountpointLen])) {
        selectedTitleIdx = curTarget->idx;
        break;
      }
      curTarget = curTarget->next;
    }
    // Reinitialize target if last launched title couldn't be loaded
    if (curTarget == NULL) {
      curTarget = titles->first;
    }
  }
  free(lastTitle);
  view = loadLastLibraryView(curTarget);
  int restoredView = view;
  int restoredIdx = storageRestoreSelection(titles, &restoredView);
  if (restoredIdx >= 0) {
    selectedTitleIdx = restoredIdx;
    curTarget = getTargetByIdx(titles, restoredIdx);
  }
  view = (UILibraryView)restoredView;
  enabledViews = loadEnabledLibraryViews(curTarget);
  if (!(enabledViews & (1U << view)))
    view = lunaNavNextView(view, enabledViews);
  if (view == UI_VIEW_PSBBN) {
    entryView = UI_VIEW_PSBBN;
    entryPending = 1;
  }
  if (view == UI_VIEW_ORBIT)
    resetAmbientOrbsOrbit(uiNowMs());
  if (view == UI_VIEW_3D)
    resetCaseGrid();
  libraryBackground = loadLibraryBackground(curTarget);
  if (setLibraryBackground((LibraryBackground)libraryBackground))
    libraryBackground = LIBRARY_BACKGROUND_STARS;
  orbsThemeSetting = loadAmbientOrbsTheme(curTarget);
  setAmbientOrbsTheme((AmbientOrbsTheme)orbsThemeSetting, uiNowMs());
  orbsAppearanceSetting = loadAmbientOrbsAppearance(curTarget);
  if (setAmbientOrbsAppearance((AmbientOrbsAppearance)orbsAppearanceSetting))
    orbsAppearanceSetting = ORBS_APPEARANCE_LUNA;
  orbsColorSetting = loadAmbientOrbsColor(curTarget, ORBS_COLOR_PART_ORBS);
  tailsColorSetting = loadAmbientOrbsColor(curTarget, ORBS_COLOR_PART_TAILS);
  setAmbientOrbsColor(ORBS_COLOR_PART_ORBS,
                      (AmbientOrbsColor)orbsColorSetting);
  setAmbientOrbsColor(ORBS_COLOR_PART_TAILS,
                      (AmbientOrbsColor)tailsColorSetting);
  glassColorSetting = loadGlassColorPreset(curTarget);
  setGlassColorPreset((GlassColorPreset)glassColorSetting);
  fontSetting = loadUIFont(curTarget);
  if (setUIFont((UIFont)fontSetting))
    fontSetting = UI_FONT_DEJAVU;
  ambientEnabled = loadAmbientSoundEnabled(curTarget);
  ambientSetEnabled(ambientEnabled);
  classicArtOverlap = loadClassicArtOverlap(curTarget);
  setClassicArtOverlap(classicArtOverlap);

  favoriteFlags = calloc((size_t)titles->total, sizeof(*favoriteFlags));
  if (favoriteFlags == NULL) {
    res = -ENOMEM;
    goto exit;
  }
  allFavoriteFlags = favoriteFlags;
  visibleFavoriteFlags = malloc((size_t)titles->total);
  if (visibleFavoriteFlags == NULL) {
    res = -ENOMEM;
    goto exit;
  }
  memset(visibleFavoriteFlags, 1, (size_t)titles->total);
  loadFavoriteFlags(titles, favoriteFlags, (size_t)titles->total);
  favoriteTitles = buildFavoriteTargetList(titles, favoriteFlags, (size_t)titles->total);
  if (favoriteTitles == NULL) {
    res = -ENOMEM;
    goto exit;
  }

  // Classic textures are unnecessary when restoring another view.
  if (view == UI_VIEW_CLASSIC) {
    isCoverUninitialized = loadCoverArt(curTarget->device, curTarget->id);
    isDiscUninitialized = loadDiscArt(curTarget->device, curTarget->id);
    classicDisplayedCoverAvailable = !isCoverUninitialized;
    classicDisplayedDiscAvailable = !isDiscUninitialized;
    if (classicDisplayedDiscAvailable)
      gsKit_TexManager_bind(gsGlobal, discTexture);
  }

  // Main UI loop
  int frameCount = 0;
  int prevInput = 0;
  int input = 0;
  int circleButtonHeld = 0;
  int optionsTriangleHeld = 0;
  int forceViewSwitch = 0;
  LunaQuickMenu quickMenu = {0};
  int quickPreviousInput = 0;
  int quickDeferredAction = -1;
  int quickMenuProgress = 0;
  uint32_t quickMenuFrameMs = uiNowMs();
  const char *quickMenuMessage = NULL;
  uint32_t quickMenuMessageUntil = 0;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);
    // A decode may finish after 3D releases its slots. Drain the
    // invalidated result even while another view is active.
    if (view != UI_VIEW_3D)
      serviceGridArt();
    const UILibraryView nextView = lunaNavNextView(view, enabledViews);
    const char *nextViewLabel = nextView == view ? "Only view" :
                                lunaNavViewLabel(nextView);

    if (titles->total == 0) {
      drawSharedLibraryBackground(uiNowMs());
      drawTextWindow(20, headerHeight, gsGlobal->Width - 20,
                     gsGlobal->Height - footerHeight, 6, HeaderTextColor,
                     ALIGN_CENTER, "NO FAVORITES YET\nHold R1 to show all games");
      goto library_view_drawn;
    }

    // A random destination may be far from the current title. Move toward it
    // one adjacent cache position at a time so Orbit recycles nine entries
    // and queues only one new PNG per step instead of ten at once.
    if (view == UI_VIEW_ORBIT && orbitRandomActive && !quickMenu.captured && uiNowMs() >= orbitRandomNextStep) {
      selectedTitleIdx = lunaNavWrap(titles->total, selectedTitleIdx + orbitRandomDirection);
      if (selectedTitleIdx == orbitRandomTargetIdx) {
        orbitRandomActive = 0;
        orbitRandomTargetIdx = -1;
      } else {
        orbitRandomNextStep = uiNowMs() + ORBIT_RANDOM_STEP_MS;
      }
    }
    observeAmbientOrbsSelection(selectedTitleIdx, uiNowMs());

    // Reload target if index has changed
    if (curTarget->idx != selectedTitleIdx) {
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      if (view == UI_VIEW_CLASSIC) {
        classicEntryListSlideActive = 0;
        // Keep input polling light while moving through the list. The old art
        // remains visible, but is not eligible for a launch handoff.
        cancelClassicArt();
        isCoverUninitialized = 1;
        isDiscUninitialized = 1;
        classicArtRequestedIdx = selectedTitleIdx;
        classicArtSubmittedIdx = -1;
        classicArtDueMs = uiNowMs() + CLASSIC_ART_SETTLE_MS;
      }
    }

    if (view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT) {
      if (classicPrefetchCandidate != curTarget) {
        classicPrefetchCandidate = curTarget;
        classicPrefetchDueMs = uiNowMs() + 200;
      }
      if (classicPrefetchSubmitted != curTarget &&
          (int32_t)(uiNowMs() - classicPrefetchDueMs) >= 0 &&
          requestClassicArt(curTarget->device, curTarget->id) == 0)
        classicPrefetchSubmitted = curTarget;
      pumpClassicArtPrefetch();
    }

    if (view == UI_VIEW_CLASSIC && classicArtRequestedIdx == selectedTitleIdx &&
        classicArtSubmittedIdx != selectedTitleIdx && !classicEntryFirstFramePending &&
        !classicNavHeld &&
        (int32_t)(uiNowMs() - classicArtDueMs) >= 0) {
      if (requestClassicArt(curTarget->device, curTarget->id) == 0) {
        classicArtSubmittedIdx = selectedTitleIdx;
      } else {
        // Keep the original loader available if the worker could not start.
        isCoverUninitialized = loadNextClassicCoverArt(curTarget->device, curTarget->id);
        isDiscUninitialized = loadDiscArt(curTarget->device, curTarget->id);
        classicDisplayedCoverAvailable = !isCoverUninitialized;
        classicDisplayedDiscAvailable = !isDiscUninitialized;
        classicArtRequestedIdx = -1;
        // Upload artwork before text is queued so VRAM reuse cannot replace glyphs.
        if (classicDisplayedCoverAvailable)
          gsKit_TexManager_bind(gsGlobal, coverTexture);
        if (classicDisplayedDiscAvailable)
          gsKit_TexManager_bind(gsGlobal, discTexture);
      }
    }
    if (view == UI_VIEW_CLASSIC && classicArtSubmittedIdx == selectedTitleIdx) {
      int coverAvailable;
      int discAvailable;
      if (serviceClassicArt(&coverAvailable, &discAvailable)) {
        classicDisplayedCoverAvailable = coverAvailable;
        classicDisplayedDiscAvailable = discAvailable;
        isCoverUninitialized = !coverAvailable;
        isDiscUninitialized = !discAvailable;
        classicArtRequestedIdx = -1;
        classicArtSubmittedIdx = -1;
        // Adopted textures must be resident before the frame draws its text.
        if (coverAvailable)
          gsKit_TexManager_bind(gsGlobal, coverTexture);
        if (discAvailable)
          gsKit_TexManager_bind(gsGlobal, discTexture);
      }
    }

    if (view == UI_VIEW_3D) {
      drawCaseGrid(titles, selectedTitleIdx, uiNowMs());
    } else if (view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT || view == UI_VIEW_ORBS) {
      TargetList *flowTitles = titles;
      int flowSelectedTitleIdx = selectedTitleIdx;
      uint32_t now = uiNowMs();
      int flowOffset;
      int collectionFps = (gsGlobal->Mode == GS_MODE_PAL) ? 50 : 60;

      // Both cover workers return finished thumbnails without making the
      // glide clock wait for file I/O or decoding.
      if (view != UI_VIEW_ORBS) {
        if (psbbnCoverBaseIdx != flowSelectedTitleIdx) {
          if (view == UI_VIEW_PSBBN)
            refreshCollectionCovers(flowTitles, flowSelectedTitleIdx, psbbnCoverBaseIdx);
          else
            refreshOrbitCovers(flowTitles, flowSelectedTitleIdx, psbbnCoverBaseIdx);
        }
        psbbnCoverBaseIdx = flowSelectedTitleIdx;
        if (view != UI_VIEW_PSBBN)
          serviceOrbitCovers(flowTitles, flowSelectedTitleIdx);
      }
      now = uiNowMs();
      if (psbbnAnimationTargetIdx < 0) {
        psbbnAnimationTargetIdx = flowSelectedTitleIdx;
        psbbnOutgoingTitleIdx = -1;
        psbbnAnimationStartOffset = 0;
        psbbnAnimationStart = now;
        psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
        psbbnAnimationElapsedFrames = 0;
      } else if (psbbnAnimationTargetIdx != flowSelectedTitleIdx) {
        int currentOffset = view == UI_VIEW_PSBBN
            ? lunaNavClassicGlideFrameOffset(psbbnAnimationStartOffset,
                psbbnAnimationElapsedFrames,
                lunaNavDurationFrames(psbbnAnimationDuration, collectionFps))
            : lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart,
                                    psbbnAnimationDuration, now);
        int direction = lunaNavDirection(flowTitles->total, psbbnAnimationTargetIdx, flowSelectedTitleIdx);
        psbbnOutgoingTitleIdx = psbbnAnimationTargetIdx;
        psbbnAnimationStartOffset = currentOffset + direction * 1000;
        if (view == UI_VIEW_PSBBN && collectionScan.active) {
          psbbnAnimationStartOffset = collectionScan.heldDirection * 1000;
          psbbnAnimationDuration = COLLECTION_SCAN_STEP_MS;
        } else if (view == UI_VIEW_ORBS && scrollFast.active) {
          psbbnAnimationDuration = SCROLL_FAST_ANIMATION_MS;
        } else if (currentOffset * direction < 0) {
          int reversalDistance = (psbbnAnimationStartOffset < 0) ? -psbbnAnimationStartOffset : psbbnAnimationStartOffset;
          if (reversalDistance > 1000)
            reversalDistance = 1000;
          // Opposite input clears the remaining travel promptly instead of
          // letting two depth directions linger for a full new glide.
          psbbnAnimationDuration = 180 + (reversalDistance * 240) / 1000;
        } else {
          psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
        }
        psbbnAnimationTargetIdx = flowSelectedTitleIdx;
        psbbnAnimationStart = now;
        psbbnAnimationElapsedFrames = 0;
      }

      flowOffset = view == UI_VIEW_PSBBN
          ? lunaNavClassicGlideFrameOffset(psbbnAnimationStartOffset,
              psbbnAnimationElapsedFrames,
              lunaNavDurationFrames(psbbnAnimationDuration, collectionFps))
          : lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart,
                                  psbbnAnimationDuration, now);
      if (view == UI_VIEW_PSBBN) {
        serviceCollectionCoversNavigating(flowTitles, flowSelectedTitleIdx,
            collectionScan.active ? collectionScan.heldDirection : 0,
            collectionScan.active, flowOffset);
        updateCollectionCoverResidency(flowOffset);
      } else if (view == UI_VIEW_ORBIT)
        updatePSBBNCoverResidency(flowOffset);
      now = uiNowMs();
      if (entryPending &&
          (view == UI_VIEW_ORBIT ||
           (view == UI_VIEW_PSBBN && collectionCoversReady(flowTitles, flowSelectedTitleIdx)))) {
        entryStartMs = now;
        entryPending = 0;
      }
      if (view != UI_VIEW_PSBBN)
        flowOffset = lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart,
                                          psbbnAnimationDuration, now);
      if (view == UI_VIEW_PSBBN) {
        collectionVisualCoverIdx = PSBBN_COVER_CACHE_FOCUS;
        if (flowOffset >= 500)
          collectionVisualCoverIdx--;
        else if (flowOffset < -500)
          collectionVisualCoverIdx++;
        int visualRank = lunaNavWrap(flowTitles->total, flowSelectedTitleIdx +
                                    collectionVisualCoverIdx - PSBBN_COVER_CACHE_FOCUS);
        collectionVisualTitleIdx = visualRank;
        drawPSBBNCollection(flowTitles, flowSelectedTitleIdx, psbbnCoverTextures,
                            flowOffset, psbbnOutgoingTitleIdx, favoritesOnly,
                            collectionScan.active,
                            entryPending ? -1 :
                                libraryViewEntryProgress(entryView, view, entryStartMs, now),
                            now, nextViewLabel);
      } else if (view == UI_VIEW_ORBIT)
        drawOrbit(titles, selectedTitleIdx, psbbnCoverTextures, flowOffset,
                  orbitRandomActive, libraryViewEntryProgress(entryView, view, entryStartMs, now),
                  now, nextViewLabel);
      else {
        int visualFocus = orbsVisualCacheIndex(flowOffset);
        orbsVisualTitleIdx = lunaNavWrap(titles->total,
            selectedTitleIdx + visualFocus - ORBS_LOGO_CACHE_FOCUS);
        if (!scrollFast.active) {
          refreshOrbsBackground(getTargetByIdx(titles, orbsVisualTitleIdx));
          refreshOrbsLogos(flowTitles, flowSelectedTitleIdx);
          serviceScrollArt();
        }
        now = uiNowMs();
        if (entryPending) {
          entryStartMs = now;
          entryPending = 0;
        }
        drawOrbsView(titles, selectedTitleIdx, flowOffset, visualFocus,
                     scrollFast.active, libraryViewEntryProgress(entryView, view, entryStartMs, now),
                     now, nextViewLabel);
      }
    } else {
      int favoritesEmpty = favoritesOnly && lunaNavMarkedCount(favoriteFlags, titles->total) == 0;
      const uint32_t frameNowMs = uiNowMs();
      const int coverPending = classicArtRequestedIdx == selectedTitleIdx;
      int listEntryProgress = 1000;
      if (classicEntryListSlideActive) {
        const uint32_t elapsed = frameNowMs - classicEntryListSlideStartMs;
        if (elapsed >= CLASSIC_LIST_ENTRY_SLIDE_MS)
          classicEntryListSlideActive = 0;
        else
          listEntryProgress = lunaNavEase(
              (int)(elapsed * 1000U / CLASSIC_LIST_ENTRY_SLIDE_MS));
      }
      drawTitleList(titles, selectedTitleIdx, maxTitlesPerPage,
                    (classicDisplayedCoverAvailable && !favoritesEmpty) ? coverTexture : NULL,
                    (classicDisplayedDiscAvailable && !favoritesEmpty) ? discTexture : NULL,
                    favoriteFlags, favoritesOnly, coverPending && !favoritesEmpty,
                    listEntryProgress,
                    frameNowMs, nextViewLabel);
    }

  library_view_drawn:
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawLibraryFooter(titles->total > 0);
    if (favoritesOnly && view != UI_VIEW_CLASSIC && view != UI_VIEW_PSBBN)
      drawTextWindow(20, 8, gsGlobal->Width - 20, headerHeight, 0,
                     HeaderTextColor, ALIGN_RIGHT, "Favorites");
    gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    uint32_t quickNow = uiNowMs();
    uint32_t quickElapsed = quickNow - quickMenuFrameMs;
    quickMenuFrameMs = quickNow;
    int quickStep = quickElapsed >= QUICK_MENU_SLIDE_MS ? 1000 :
                    (int)(quickElapsed * 1000U / QUICK_MENU_SLIDE_MS);
    quickMenuProgress += quickMenu.open ? quickStep : -quickStep;
    if (quickMenuProgress < 0) quickMenuProgress = 0;
    if (quickMenuProgress > 1000) quickMenuProgress = 1000;
    int favoriteIndex = favoritesOnly && titles->total > 0
        ? lunaNavMarkedByRank(allFavoriteFlags, allTitles->total, selectedTitleIdx)
        : selectedTitleIdx;
    drawLibraryQuickMenu(quickMenuProgress, !quickMenu.open, view,
                         favoritesOnly, titles->total > 0 && allFavoriteFlags[favoriteIndex],
                         titles->total, curTarget->name);
    if (quickMenuMessage != NULL && (int32_t)(quickMenuMessageUntil - quickNow) > 0) {
      gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
      gsKit_prim_sprite(gsGlobal, 20, headerHeight, gsGlobal->Width - 20,
                        headerHeight + getFontLineHeight() + 12, 0, ColorPanel);
      drawTextWindow(28, headerHeight + 6, gsGlobal->Width - 28, 0, 0,
                     ErrorTextColor, ALIGN_HCENTER, quickMenuMessage);
      gsKit_set_test(gsGlobal, GS_ZTEST_ON);
    }
    // Fill the active Collection or Favorites cover window while another view is idle.
    // Orbit owns the shared PSBBN slots, so it cannot prewarm Collection.
    if (titles->total > 0 &&
        ((view == UI_VIEW_CLASSIC && !classicNavHeld && classicArtRequestedIdx < 0) ||
         (view == UI_VIEW_ORBS && !scrollFast.active))) {
      if (psbbnCoverBaseIdx != selectedTitleIdx)
        refreshCollectionCovers(titles, selectedTitleIdx, psbbnCoverBaseIdx);
      psbbnCoverBaseIdx = selectedTitleIdx;
      serviceCollectionCovers(titles, selectedTitleIdx);
    }
    if (libraryReturnFadeStartMs != 0) {
      uint32_t fadeElapsed = uiNowMs() - libraryReturnFadeStartMs;
      if (fadeElapsed >= LIBRARY_RETURN_FADE_MS) {
        libraryReturnFadeStartMs = 0;
      } else {
        int alpha = (int)((LIBRARY_RETURN_FADE_MS - fadeElapsed) * 0x80 /
                          LIBRARY_RETURN_FADE_MS);
        gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
        gsGlobal->PrimAlphaEnable = alpha >= 0x80 ? GS_SETTING_OFF : GS_SETTING_ON;
        gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
        gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height,
                          0, GS_SETREG_RGBA(0, 0, 0, alpha));
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsKit_set_test(gsGlobal, GS_ZTEST_ON);
      }
    }
    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_sync_flip(gsGlobal);
    if (view == UI_VIEW_CLASSIC)
      classicEntryFirstFramePending = 0;
    if (view == UI_VIEW_PSBBN && psbbnAnimationTargetIdx >= 0 &&
        psbbnAnimationElapsedFrames <
            lunaNavDurationFrames(psbbnAnimationDuration,
                                  gsGlobal->Mode == GS_MODE_PAL ? 50 : 60))
      psbbnAnimationElapsedFrames++;
    usleep(view == UI_VIEW_CLASSIC && classicArtSubmittedIdx >= 0 ? 3000 : 1000);

    // Keep rendering after options close, while ignoring the Triangle press
    // that closed them until the button is released.
    input = pollInput();
    int quickAction = -1;
    int quickWasCaptured = quickMenu.captured;
    int quickWasOpen = quickMenu.open;
    int quickPressed = input & ~quickPreviousInput;
    quickPreviousInput = input;
    int quickShortcut = (quickPressed & PAD_CROSS) ? 0 :
                        (quickPressed & PAD_CIRCLE) ? 1 :
                        (quickPressed & PAD_TRIANGLE) ? 2 :
                        (view == UI_VIEW_ORBIT && (quickPressed & PAD_R3)) ? 3 : -1;
    quickAction = lunaQuickMenuUpdate(&quickMenu, (input & PAD_R1) != 0,
                                     input != 0, view == UI_VIEW_ORBIT ? 4 : 3,
                                     quickShortcut);
    if (!quickWasOpen && (quickMenu.open || quickAction >= 0)) {
      // Freeze the visible title, including during a scan or cover glide.
      if (titles->total > 0 && view == UI_VIEW_PSBBN && collectionVisualTitleIdx >= 0)
        selectedTitleIdx = collectionVisualTitleIdx;
      else if (titles->total > 0 && view == UI_VIEW_ORBIT && orbitVisibleTitleIndex() >= 0)
        selectedTitleIdx = orbitVisibleTitleIndex();
      else if (titles->total > 0 && view == UI_VIEW_ORBS && orbsVisualTitleIdx >= 0)
        selectedTitleIdx = orbsVisualTitleIdx;
      if (titles->total > 0)
        curTarget = getTargetByIdx(titles, selectedTitleIdx);
      psbbnAnimationTargetIdx = -1;
      psbbnAnimationStartOffset = 0;
      psbbnOutgoingTitleIdx = -1;
      collectionScan = (LunaCollectionScan){0};
      scrollFast = (LunaScrollFast){0};
      orbitRandomActive = 0;
      classicNavHeld = 0;
      classicRepeat = (LunaNavRepeatState){0};
    }
    if (quickMenu.captured || quickWasCaptured) {
      prevInput = 0;
      frameCount = 0;
      circleButtonHeld = 0;
      // Wait for all chord buttons to be released before handing control
      // to Options, so the same press cannot edit that screen.
      if (quickAction == 2) {
        quickDeferredAction = quickAction;
        continue;
      }
      if (!quickMenu.captured && quickDeferredAction >= 0) {
        quickAction = quickDeferredAction;
        quickDeferredAction = -1;
      }
      if (quickAction < 0)
        continue;
      if (quickAction != 0 && titles->total == 0)
        continue;
      input = quickAction == 0 ? PAD_SELECT :
              quickAction == 2 ? PAD_TRIANGLE :
              quickAction == 3 ? PAD_SQUARE : 0;
      favoriteButtonHeld = 0;
      orbitRandomButtonHeld = 0;
    }
    if (titles->total == 0)
      input &= PAD_SELECT | PAD_CIRCLE | PAD_START;
    int circlePressed = (input & PAD_CIRCLE) && !circleButtonHeld;
    circleButtonHeld = (input & PAD_CIRCLE) != 0;
    if (!circlePressed)
      input &= ~PAD_CIRCLE;
    if (optionsTriangleHeld) {
      if (input & PAD_TRIANGLE)
        input &= ~PAD_TRIANGLE;
      else
        optionsTriangleHeld = 0;
    }

    if ((input & PAD_SQUARE) == 0) {
      orbitRandomButtonHeld = 0;
      favoriteButtonHeld = 0;
    }
    if ((input & PAD_SELECT) == 0)
      favoritesTabButtonHeld = 0;

    int scrollFastStep = 0;
    if (view == UI_VIEW_ORBS) {
      int direction = 0;
      if (input & (PAD_LEFT | PAD_UP))
        direction = -1;
      else if (input & (PAD_RIGHT | PAD_DOWN))
        direction = 1;
      scrollFastStep = lunaScrollFastUpdate(&scrollFast, direction, uiNowMs());
    }

    if (forceViewSwitch) {
      // Reuse the normal view transition after disabling the active view.
      input = PAD_CIRCLE;
      prevInput = 0;
      frameCount = 0;
      forceViewSwitch = 0;
    }

    if (view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT || view == UI_VIEW_ORBS) {
      // Held navigation starts a new step roughly every 180 ms while each
      // glide lasts 420 ms. The accumulated fractional offset keeps the whole
      // stream continuous while several cover transitions overlap.
      frameCount = (frameCount + 1) % ((gsGlobal->Mode == GS_MODE_PAL) ? PSBBN_REPEAT_FRAMES_PAL : PSBBN_REPEAT_FRAMES_NTSC);
    } else if (gsGlobal->Mode == GS_MODE_PAL) {
      frameCount = (frameCount + 1) % 8; // Preserve Classic's established repeat cadence.
    } else {
      frameCount = (frameCount + 1) % 10;
    }

    if (view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT) {
      const int rawInput = input;
      const int actionButtons = PAD_CROSS | PAD_TRIANGLE | PAD_CIRCLE | PAD_SELECT | PAD_START;
      const int shoulderButtons = PAD_L2 | PAD_R2;
      int scanDirection = 0;
      if (view == UI_VIEW_ORBIT && (rawInput & shoulderButtons))
        orbitRandomActive = 0;
      if (!(rawInput & actionButtons)) {
        int left = (rawInput & PAD_L2) != 0;
        int right = (rawInput & PAD_R2) != 0;
        scanDirection = right == left ? 0 : (right ? 1 : -1);
      }
      int scanStep = lunaCollectionScanUpdate(&collectionScan, scanDirection, uiNowMs(),
                                             view == UI_VIEW_ORBIT ? ORBIT_RANDOM_STEP_MS : COLLECTION_SCAN_STEP_MS);
      input = rawInput & ~shoulderButtons;
      if (scanDirection)
        input &= ~(PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN);
      if (scanStep) {
        if (titles->total > 1) {
          selectedTitleIdx = lunaNavWrap(titles->total, selectedTitleIdx + scanStep);
        }
      }
    }

    if (view == UI_VIEW_CLASSIC) {
      const int rawInput = input;
      const int navButtons = PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN;
      int direction = 0;
      int navInput = 0;
      if (rawInput & (PAD_LEFT | PAD_UP)) {
        direction = -1;
        navInput = (rawInput & PAD_UP) ? PAD_UP : PAD_LEFT;
      } else if (rawInput & (PAD_RIGHT | PAD_DOWN)) {
        direction = 1;
        navInput = (rawInput & PAD_DOWN) ? PAD_DOWN : PAD_RIGHT;
      }
      classicNavHeld = direction != 0;
      input = rawInput & ~prevInput & ~navButtons;
      if (lunaNavRepeatStep(&classicRepeat, direction, uiNowMs(),
                            CLASSIC_REPEAT_DELAY_MS, CLASSIC_REPEAT_INTERVAL_MS))
        input |= navInput;
      prevInput = rawInput;
      if (!input && quickAction < 0)
        continue;
    } else if (view == UI_VIEW_ORBS && scrollFast.active) {
      const int rawInput = input;
      const int navButtons = PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN;
      input = (rawInput & ~navButtons) & ~(prevInput & ~navButtons);
      if (scrollFastStep)
        input |= scrollFastStep < 0 ? PAD_UP : PAD_DOWN;
      prevInput = rawInput;
      frameCount = 0;
      if (!input && quickAction < 0)
        continue;
    } else {
      if (frameCount && (input == prevInput) && quickAction < 0)
        continue;
      frameCount = 0;
      prevInput = input;
    }

    // Actions use the logo at the fixed Orbs marker even when the next
    // queued selection has not finished gliding into place.
    if (titles->total > 0 && view == UI_VIEW_ORBS && orbsVisualTitleIdx >= 0 &&
        (input & (PAD_CROSS | PAD_TRIANGLE | PAD_CIRCLE | PAD_START))) {
      selectedTitleIdx = orbsVisualTitleIdx;
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      psbbnAnimationTargetIdx = -1;
      psbbnOutgoingTitleIdx = -1;
      psbbnAnimationStartOffset = 0;
    }

    // Any deliberate input interrupts an in-progress random scan and resumes
    // normal manual control immediately. Square below starts a fresh scan.
    if (view == UI_VIEW_ORBIT && orbitRandomActive && (input & ~PAD_SQUARE) != 0)
      orbitRandomActive = 0;

    collectionActionCoverIdx = PSBBN_COVER_CACHE_FOCUS;
    if (titles->total > 0 && view == UI_VIEW_PSBBN &&
        psbbnAnimationDuration == COLLECTION_SCAN_STEP_MS &&
        (input & (PAD_CROSS | PAD_TRIANGLE | PAD_CIRCLE | PAD_SELECT | PAD_START)) &&
        collectionVisualTitleIdx >= 0) {
      selectedTitleIdx = collectionVisualTitleIdx;
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      collectionActionCoverIdx = collectionVisualCoverIdx;
      psbbnAnimationTargetIdx = -1;
      psbbnOutgoingTitleIdx = -1;
      psbbnAnimationStartOffset = 0;
      psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
    }

    UILibraryView previousView = view;
    int wasCollectionFavorites = 0;
    int libraryListChanged = 0;
    if ((input & PAD_SELECT) && (!favoritesTabButtonHeld || quickAction == 0)) {
      favoritesTabButtonHeld = 1;
      int originalIndex = favoritesOnly && titles->total > 0
          ? lunaNavMarkedByRank(allFavoriteFlags, allTitles->total, selectedTitleIdx)
          : curTarget->idx;
      DPRINTF("Library filter switch begin: view=%d favorites=%d total=%d\n",
              view, favoritesOnly, titles->total);
      favoritesOnly = !favoritesOnly;
      titles = favoritesOnly ? favoriteTitles : allTitles;
      favoriteFlags = favoritesOnly ? visibleFavoriteFlags : allFavoriteFlags;
      selectedTitleIdx = favoritesOnly
          ? lunaNavMarkedRank(allFavoriteFlags, allTitles->total, originalIndex)
          : originalIndex;
      if (selectedTitleIdx < 0) selectedTitleIdx = 0;
      curTarget = titles->total > 0 ? getTargetByIdx(titles, selectedTitleIdx)
                                   : getTargetByIdx(allTitles, originalIndex);
      libraryListChanged = 1;
      goto restart_library_view;
    } else if ((input & PAD_CROSS) && titles->total > 0) {
      // Copy target, free title list and launch
      Target *target = copyTarget(curTarget);
      freeTargetList(favoriteTitles);
      favoriteTitles = NULL;
      free(allFavoriteFlags);
      allFavoriteFlags = NULL;
      free(visibleFavoriteFlags);
      visibleFavoriteFlags = NULL;
      favoriteFlags = NULL;
      freeTargetList(allTitles);
      uiLaunchTitle(target, NULL);
      // Something went wrong, main loop must exit immediately
      return -1;
    } else if (input & PAD_CIRCLE) {
      wasCollectionFavorites = favoritesOnly;
      view = lunaNavNextView(view, enabledViews);
      if (view == previousView)
        continue;
    restart_library_view:
      if (previousView == UI_VIEW_PSBBN && view != UI_VIEW_PSBBN)
        stopCollectionFarArtWorker(titles, selectedTitleIdx);
      // PSBBN and Orbit release their cache in the view cleanup below.
      // Releasing it here as well tears down the same texture window twice
      // when switching Collection between All and Favorites.
      if (libraryListChanged && previousView != UI_VIEW_PSBBN &&
          previousView != UI_VIEW_ORBIT)
        releasePSBBNCovers();
      entryView = (int)view;
      entryPending = view != UI_VIEW_CLASSIC;
      classicEntryFirstFramePending = view == UI_VIEW_CLASSIC &&
                                      previousView != UI_VIEW_CLASSIC;
      classicEntryListSlideActive = view == UI_VIEW_CLASSIC &&
                                    previousView != UI_VIEW_CLASSIC && titles->total > 0;
      classicEntryListSlideStartMs = uiNowMs();
      if (previousView == UI_VIEW_CLASSIC || libraryListChanged ||
          (view != UI_VIEW_CLASSIC && view != UI_VIEW_PSBBN && view != UI_VIEW_ORBIT)) {
        cancelClassicArt();
        classicPrefetchCandidate = NULL;
        classicPrefetchSubmitted = NULL;
      }
      classicArtRequestedIdx = -1;
      classicArtSubmittedIdx = -1;
      if (view == UI_VIEW_ORBIT)
        resetAmbientOrbsOrbit(uiNowMs());
      if (previousView == UI_VIEW_ORBS || view == UI_VIEW_ORBS)
        resetAmbientOrbsScroll();
      scrollFast = (LunaScrollFast){0};
      collectionScan = (LunaCollectionScan){0};

      if (previousView == UI_VIEW_CLASSIC) {
        releaseClassicArtVRAM();
        isCoverUninitialized = 1;
        isDiscUninitialized = 1;
        classicDisplayedCoverAvailable = 0;
        classicDisplayedDiscAvailable = 0;
      } else if (previousView == UI_VIEW_PSBBN || previousView == UI_VIEW_ORBIT) {
        if (previousView == UI_VIEW_ORBIT && view == UI_VIEW_PSBBN)
          adoptOrbitCoversForCollection();
        else if (previousView == UI_VIEW_PSBBN && view == UI_VIEW_ORBIT &&
                 !libraryListChanged)
          adoptCollectionCoversForOrbit();
        else if (previousView == UI_VIEW_ORBIT || view == UI_VIEW_ORBIT || wasCollectionFavorites || libraryListChanged)
          releasePSBBNCovers();
        else
          suspendCollectionCovers();
      } else if (previousView == UI_VIEW_ORBS) {
        releaseOrbsArt();
      } else if (previousView == UI_VIEW_3D) {
        releaseGridCovers();
        setGridCaseArtwork(0);
      }
      if (view == UI_VIEW_3D)
        resetCaseGrid();

      if (view == UI_VIEW_ORBIT && previousView != UI_VIEW_PSBBN &&
          previousView != UI_VIEW_ORBIT)
        releasePSBBNCovers();

      if ((previousView == UI_VIEW_ORBIT && view != UI_VIEW_PSBBN) ||
          (view == UI_VIEW_ORBIT && previousView != UI_VIEW_PSBBN) ||
          (wasCollectionFavorites && view != UI_VIEW_ORBIT) || libraryListChanged)
        psbbnCoverBaseIdx = -1;
      psbbnAnimationTargetIdx = -1;
      psbbnOutgoingTitleIdx = -1;
      psbbnAnimationStartOffset = 0;
      psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
      orbitRandomActive = 0;
      orbitRandomTargetIdx = -1;
      orbitRandomButtonHeld = 0;
      if (view == UI_VIEW_CLASSIC && titles->total > 0) {
        isCoverUninitialized = 1;
        isDiscUninitialized = 1;
        classicDisplayedCoverAvailable = 0;
        classicDisplayedDiscAvailable = 0;
        classicArtRequestedIdx = selectedTitleIdx;
        classicArtDueMs = uiNowMs();
        // Start decoding on entry; requestClassicArt keeps a matching prefetch.
        if (requestClassicArt(curTarget->device, curTarget->id) == 0)
          classicArtSubmittedIdx = selectedTitleIdx;
        classicNavHeld = 0;
        classicRepeat.direction = 0;
      }
      if (saveLastLibraryView(curTarget, view))
        DPRINTF("WARN: Could not save selected library view\n");
      if (libraryListChanged)
        DPRINTF("Library filter switch done: view=%d favorites=%d total=%d\n",
                view, favoritesOnly, titles->total);
    } else if ((quickAction == 1 || (view == UI_VIEW_CLASSIC && (input & PAD_SQUARE))) &&
               !favoriteButtonHeld && titles->total > 0) {
      int originalIndex = favoritesOnly
          ? lunaNavMarkedByRank(allFavoriteFlags, allTitles->total, selectedTitleIdx)
          : selectedTitleIdx;
      int wasFavorite = allFavoriteFlags[originalIndex] != 0;
      Target *originalTarget = getTargetByIdx(allTitles, originalIndex);
      favoriteButtonHeld = 1;
      allFavoriteFlags[originalIndex] = !wasFavorite;
      TargetList *updated = buildFavoriteTargetList(allTitles, allFavoriteFlags,
                                                    (size_t)allTitles->total);
      if (updated == NULL || saveFavoriteFlags(allTitles, allFavoriteFlags,
                                               (size_t)allTitles->total, originalTarget)) {
        allFavoriteFlags[originalIndex] = wasFavorite;
        if (updated != NULL) freeTargetList(updated);
        quickMenuMessage = "Could not save favorites";
        quickMenuMessageUntil = uiNowMs() + 2500;
      } else {
        freeTargetList(favoriteTitles);
        favoriteTitles = updated;
        if (favoritesOnly) {
          titles = favoriteTitles;
          if (selectedTitleIdx >= titles->total)
            selectedTitleIdx = titles->total > 0 ? titles->total - 1 : 0;
          curTarget = titles->total > 0 ? getTargetByIdx(titles, selectedTitleIdx) : originalTarget;
          libraryListChanged = 1;
          goto restart_library_view;
        }
      }
    } else if (view == UI_VIEW_ORBIT && (input & PAD_SQUARE) && !orbitRandomButtonHeld) {
      // Pick a different destination every time, then let the adjacent-step
      // scanner reach it without forcing a ten-PNG cache rebuild in one frame.
      if (titles->total > 1) {
        uint32_t randomSeed = uiNowMs() ^ ((uint32_t)(selectedTitleIdx + 1) * 2654435761U);
        orbitRandomTargetIdx = lunaNavRandomTarget(titles->total, selectedTitleIdx, randomSeed);
        orbitRandomDirection = lunaNavDirection(titles->total, selectedTitleIdx, orbitRandomTargetIdx);
        orbitRandomActive = 1;
        orbitRandomNextStep = uiNowMs();
      }
      orbitRandomButtonHeld = 1;
    } else if (view == UI_VIEW_3D && (input & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN))) {
      if (input & PAD_LEFT)
        selectedTitleIdx = lunaNavWrap(titles->total, selectedTitleIdx - 1);
      else if (input & PAD_RIGHT)
        selectedTitleIdx = lunaNavWrap(titles->total, selectedTitleIdx + 1);
      else
        selectedTitleIdx = lunaNavCaseGridVertical(titles->total, selectedTitleIdx,
                                                  input & PAD_UP ? -1 : 1);
    } else if (input & (PAD_LEFT | PAD_UP)) {
      // Point to the previous title
      if (favoritesOnly) {
        int favoriteIdx = lunaNavMarkedStep(favoriteFlags, titles->total, selectedTitleIdx, -1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else {
        selectedTitleIdx = ((selectedTitleIdx - 1) + titles->total) % titles->total;
      }
    } else if (input & (PAD_RIGHT | PAD_DOWN)) {
      // Advance to the next title
      if (favoritesOnly) {
        int favoriteIdx = lunaNavMarkedStep(favoriteFlags, titles->total, selectedTitleIdx, 1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else {
        selectedTitleIdx = (selectedTitleIdx + 1) % titles->total;
      }
    } else if (input & GRID_RIGHT_SHOULDERS) {
      // Switch to the next page.
      if (view == UI_VIEW_3D) {
        selectedTitleIdx = lunaNavCaseGridPage(titles->total, selectedTitleIdx, 1);
      } else if (favoritesOnly) {
        int favoriteIdx = lunaNavMarkedPage(favoriteFlags, titles->total, selectedTitleIdx,
                                            maxTitlesPerPage, 1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else if (selectedTitleIdx == titles->total - 1) {
        selectedTitleIdx = 0; // Wrap around if the last title is selected
      } else {
        selectedTitleIdx += maxTitlesPerPage;
        if (selectedTitleIdx >= titles->total)
          selectedTitleIdx = titles->total - 1;
      }
    } else if (input & GRID_LEFT_SHOULDERS) {
      // Switch to the previous page.
      if (view == UI_VIEW_3D) {
        selectedTitleIdx = lunaNavCaseGridPage(titles->total, selectedTitleIdx, -1);
      } else if (favoritesOnly) {
        int favoriteIdx = lunaNavMarkedPage(favoriteFlags, titles->total, selectedTitleIdx,
                                            maxTitlesPerPage, -1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else if (selectedTitleIdx == 0) {
        selectedTitleIdx = titles->total - 1; // Wrap around if the first title is selected
      } else {
        selectedTitleIdx -= maxTitlesPerPage;
        if (selectedTitleIdx < 0)
          selectedTitleIdx = 0;
      }
    } else if ((input & PAD_TRIANGLE) && titles->total > 0) {
      prevInput = 0; // Reset previous input
      // Enter title options screen
      if ((res = uiTitleOptionsLoop(curTarget, &classicArtOverlap,
                                    &libraryBackground, &glassColorSetting,
                                    &fontSetting, &ambientEnabled, &orbsThemeSetting,
                                    &orbsAppearanceSetting, &orbsColorSetting,
                                    &tailsColorSetting,
                                    &enabledViews)) < 0) {
        // Something went wrong, main loop must exit immediately
        ambientStop();
        freeTargetList(favoriteTitles);
        free(allFavoriteFlags);
        free(visibleFavoriteFlags);
        return -1;
      }
      if (view == UI_VIEW_CLASSIC)
        holdClassicDiscRotation(uiNowMs());
      libraryReturnFadeStartMs = uiNowMs();
      setLibraryBackground((LibraryBackground)libraryBackground);
      setGlassColorPreset((GlassColorPreset)glassColorSetting);
      if (!(enabledViews & (1U << view)))
        forceViewSwitch = 1;
      optionsTriangleHeld = (pollInput() & PAD_TRIANGLE) != 0;
      input = 0;
    } else if (input & PAD_START) {
      int menuResult = uiMainMenuLoop(1);
      if (menuResult == STORAGE_UI_REFRESH) {
        storageRememberSelection(curTarget, view);
        res = STORAGE_UI_REFRESH;
        break;
      }
      if (menuResult)
        break;
      // Prevent the menu selection press from acting on a library title.
      while (pollInput() & (PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE | PAD_START))
        usleep(1000);
      prevInput = 0;
      frameCount = 0;
      input = 0;
    }
  }

exit:
  ambientStop();
  if (favoriteTitles != NULL)
    freeTargetList(favoriteTitles);
  free(allFavoriteFlags);
  free(visibleFavoriteFlags);
  closePad();
  closeUI();
  return res;
}
// Displays Game ID and launches the title
void uiLaunchTitle(Target *target, ArgumentList *arguments) {
  uiPlayLaunchTransition();
  closePad();

  if (arguments == NULL)
    arguments = loadLaunchArgumentLists(target);

  // Keep the black framebuffer resident while Neutrino loads.
  launchTitleWithProgress(target, arguments, uiLaunchHandoffProgress, NULL);

  // launchTitleWithProgress normally never returns. Retain cleanup for an
  // unsupported target mode or another pre-exec failure.
  closeUI();
}

// Splash screen functions

struct {
  int32_t doneSema;          // Used to signal UI splash thread to exit
  int32_t newStringSema;     // Used to signal UI splash thread that a new string is ready
  int32_t drawnSema;         // Used to signal that UI splash thread has finished drawing or closed
  UILogLevelType level;      // Log level
  char neutrinoVersion[100]; // Neutrino version string
  char buf[255];             // String buffer. String must be null-terminated
} logBuffer = {};
#define THREAD_STACK_SIZE 0x1000
static uint8_t threadStack[THREAD_STACK_SIZE] __attribute__((aligned(16)));

// Initializes and starts UI splash thread
int startSplashScreen() {
  DPRINTF("Starting UI splash thread\n");
  splashVisibleStartMs = uiNowMs();
  // Use the PS2 ROM's original orb sprites for the boot formation. The
  // library loads its saved appearance after the splash closes.
  if (setAmbientOrbsAppearance(ORBS_APPEARANCE_PS2_ORIGINAL))
    DPRINTF("WARNING: PS2 original orb masks unavailable for splash\n");
  resetAmbientOrbsSplash(splashVisibleStartMs);
  // Initialize splash semaphores
  ee_sema_t semaphore;
  semaphore.init_count = 0;
  semaphore.max_count = 1;
  semaphore.option = 0;
  logBuffer.drawnSema = CreateSema(&semaphore);
  logBuffer.newStringSema = CreateSema(&semaphore);
  logBuffer.doneSema = CreateSema(&semaphore);

  // Initialize thread
  ee_thread_t thread;
  thread.func = uiSplashThread;
  thread.stack = threadStack;
  thread.stack_size = THREAD_STACK_SIZE;
  thread.gp_reg = &_gp;
  thread.initial_priority = 0x2;
  thread.attr = thread.option = 0;

  // Start thread
  int32_t threadID;
  if ((threadID = CreateThread(&thread)) >= 0) {
    if (StartThread(threadID, NULL) < 0) {
      DeleteThread(threadID);
      threadID = -1;
    }
  }

  return threadID;
}

// Draws loading splash screen in a separate thread
void uiSplashThread() {
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  int logStartY = gsGlobal->Height - footerHeight - getFontLineHeight() * 3;

  // Redraw continuously so the formations keep moving between boot
  // messages. The main thread still owns every initialization and scan step.
  while (PollSema(logBuffer.doneSema) != logBuffer.doneSema) {
    if (PollSema(logBuffer.newStringSema) == logBuffer.newStringSema) {
      SignalSema(logBuffer.drawnSema);
    }

    gsKit_TexManager_nextFrame(gsGlobal);
    gsKit_clear(gsGlobal, GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80));
    const uint32_t now = uiNowMs();
    drawAmbientOrbsSplash(gsGlobal->Width / 2, gsGlobal->Height * 48 / 100,
                          gsGlobal->Width * 31 / 100,
                          gsGlobal->Height * 36 / 100, now, 1);

    // Successful boot stays intentionally minimal. Only surface a fatal error
    // so a failed initialization cannot be mistaken for endless loading.
    if ((logBuffer.level == LEVEL_ERROR) && (logBuffer.buf[0] != '\0'))
      drawTextWindow(0, logStartY, gsGlobal->Width, gsGlobal->Height - footerHeight, 2, ErrorTextColor, ALIGN_CENTER, logBuffer.buf);

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_sync_flip(gsGlobal);
    usleep(16000);
  }
  gsKit_queue_reset(gsGlobal->Per_Queue);
  DeleteSema(logBuffer.doneSema);
  DeleteSema(logBuffer.newStringSema);
  SignalSema(logBuffer.drawnSema);
  ExitDeleteThread();
}

// Stops UI splash thread
void stopUISplashThread() {
  // A fast scan otherwise exits before the LUNA-to-cube animation is visible.
  while ((uint32_t)(uiNowMs() - splashVisibleStartMs) < SPLASH_MIN_VISIBLE_MS)
    usleep(16000);
  SignalSema(logBuffer.doneSema);
  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);
  DeleteSema(logBuffer.drawnSema);
}

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...) {
  va_list args;
  va_start(args, str);

  logBuffer.level = level;
  vsnprintf(logBuffer.buf, 255, str, args);
  va_end(args);
  DPRINTF(logBuffer.buf);

  if (!gsGlobal)
    return;

  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);

  switch (level) {
  case LEVEL_INFO_NODELAY:
    return;
  case LEVEL_INFO:
    sleep(1);
    return;
  case LEVEL_WARN:
  case LEVEL_ERROR:
    sleep(2);
    return;
  }
}

// Sets Neutrino version on the splash screen
void uiSplashSetNeutrinoVersion(const char *str) {
  if (!gsGlobal)
    return;

  if (str[0] == '\0')
    return;

  strcpy(logBuffer.neutrinoVersion, "Neutrino");
  strncat(logBuffer.neutrinoVersion, str, 100 - 10);

  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);
}
