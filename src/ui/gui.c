// LUNA 2026
#include "common.h"
#include "dprintf.h"
#include "favorites.h"
#include "neutrino.h"
#include "options.h"
#include "ui/ambient.h"
#include "ui/art_cache.h"
#include "ui/graphics.h"
#include "ui/handoff.h"
#include "ui/navigation.h"
#include "ui/options_menu.h"
#include "ui/view_orbs.h"
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
#include <timer.h>

#define PSBBN_TIMER_TICKS_PER_MS 576ULL
#define GRID_LEFT_SHOULDERS (PAD_L1 | PAD_L2)
#define GRID_RIGHT_SHOULDERS (PAD_R1 | PAD_R2)

void closeUI();
int uiLoop(TargetList *titles);
void drawGameID(const char *game_id);
void uiSplashThread();

GSGLOBAL *gsGlobal;
char lineBuffer[255];
static int orbsBackground = 0;
static int glassColorSetting = GLASS_COLOR_ORIGINAL;

const int keepoutArea = 20;
const int headerHeight = 40;
const int footerHeight = 60;

uint32_t uiNowMs(void) {
  return (uint32_t)((GetTimerSystemTime() >> 8) / PSBBN_TIMER_TICKS_PER_MS);
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
  closeFont();
  artCacheShutdown();
  gsKit_deinit_global(gsGlobal);
}

// Main UI loop. Displays the target list.
int uiLoop(TargetList *titles) {
  // Reinitialize UI if video mode doesn't match
  if ((LAUNCHER_OPTIONS.vmode != VMODE_NONE) && (gsGlobal->Mode != LAUNCHER_OPTIONS.vmode)) {
    uiInit();
  }

  int res = 0;
  uint8_t *favoriteFlags = NULL;
  TargetList *favoriteTitles = NULL;
  if ((gsGlobal == NULL) && (res = uiInit())) {
    DPRINTF("ERROR: Failed to init UI: %d\n", res);
    goto exit;
  }

  // The splash logo is never drawn in either library view. Releasing its large
  // texture leaves stable VRAM for Classic's cover and disc pair.
  releaseBootLogo();
  // Init gamepad inputs
  initPad();

  int isCoverUninitialized = 1;
  int isDiscUninitialized = 1;
  int selectedTitleIdx = 0;
  int maxTitlesPerPage = (gsGlobal->Height - (headerHeight + footerHeight)) / getFontLineHeight();
  int psbbnCoverBaseIdx = -1;
  int psbbnAnimationTargetIdx = -1;
  int psbbnOutgoingTitleIdx = -1;
  int psbbnAnimationStartOffset = 0;
  uint32_t psbbnAnimationStart = 0;
  uint32_t psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
  LunaCollectionScan collectionScan = {0};
  int collectionVisualTitleIdx = -1;
  int collectionVisualCoverIdx = PSBBN_COVER_CACHE_FOCUS;
  int collectionActionCoverIdx = PSBBN_COVER_CACHE_FOCUS;
  int gridActivePageBuffer = 0;
  int gridIncomingPageBuffer = -1;
  int gridPreviousPageBuffer = -1;
  int gridActivePageBase = -1;
  int gridIncomingPageBase = -1;
  int gridPageBases[GRID_PAGE_BUFFERS] = {-1, -1, -1};
  int gridPageComplete[GRID_PAGE_BUFFERS] = {0, 0, 0};
  int gridPageNextSlot[GRID_PAGE_BUFFERS] = {0, 0, 0};
  int gridSelectedActiveBuffer = 0;
  int gridSelectedIncomingBuffer = 1;
  int gridSelectedActiveIdx = -1;
  int gridSelectedRequestedIdx = -1;
  int gridSelectedAttemptedIdx = -1;
  int gridPendingSelectedIdx = -1;
  int gridPendingSelectedCoverIdx = -1;
  int gridPageDirection = 0;
  int gridPrefetchDirection = 1;
  int gridCascadeActive = 0;
  int gridCascadeDirection = 0;
  uint32_t gridCascadeStart = 0;
  int gridShoulderDirection = 0;
  uint32_t gridShoulderHoldStart = 0;
  int gridFastTrackActive = 0;
  int gridFastTrackSettling = 0;
  int gridFastTrackDirection = 0;
  int gridFastTrackSelectedIdx = -1;
  int gridFastTrackPreviousPageBase = -1;
  int gridFastTrackPageBase = -1;
  uint32_t gridFastTrackSlideStart = 0;
  uint32_t gridFastTrackNextStep = 0;
  int orbitRandomActive = 0;
  int orbitRandomTargetIdx = -1;
  int orbitRandomDirection = 1;
  uint32_t orbitRandomNextStep = 0;
  int orbitRandomButtonHeld = 0;
  int orbsVisualTitleIdx = -1;
  int favoriteButtonHeld = 0;
  int favoritesTabButtonHeld = 0;
  int favoritesOnly = 0;
  int collectionFavoritesOnly = 0;
  int classicArtRequestedIdx = -1;
  int classicNavHeld = 0;
  int classicDisplayedCoverAvailable = 0;
  int classicDisplayedDiscAvailable = 0;
  int classicPreviousCoverAvailable = 0;
  int classicArtOverlap = 0;
  int orbsEnabled = 0;
  int ambientEnabled = 1;
  uint32_t classicArtDueMs = 0;
  uint32_t classicCoverFadeStartMs = 0;
  LunaNavRepeatState classicRepeat = {0};
  UILibraryView view = UI_VIEW_CLASSIC;
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
  orbsEnabled = loadOrbsViewEnabled(curTarget);
  orbsBackground = loadOrbsBackground(curTarget);
  setOrbsBackgroundStyle(orbsBackground);
  glassColorSetting = loadGlassColorPreset(curTarget);
  setGlassColorPreset((GlassColorPreset)glassColorSetting);
  ambientEnabled = loadAmbientSoundEnabled(curTarget);
  ambientSetEnabled(ambientEnabled);
  if (view == UI_VIEW_ORBS && !orbsEnabled)
    view = UI_VIEW_CLASSIC;
  classicArtOverlap = loadClassicArtOverlap(curTarget);
  setClassicArtOverlap(classicArtOverlap);

  favoriteFlags = calloc((size_t)titles->total, sizeof(*favoriteFlags));
  if (favoriteFlags == NULL) {
    res = -ENOMEM;
    goto exit;
  }
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
  }

  // Main UI loop
  int frameCount = 0;
  int prevInput = 0;
  int input = 0;
  int optionsTriangleHeld = 0;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    // A random destination may be far from the current title. Move toward it
    // one adjacent cache position at a time so refreshPSBBNCovers() recycles
    // nine entries and loads only one new PNG per step instead of ten at once.
    if (view == UI_VIEW_ORBIT && orbitRandomActive && uiNowMs() >= orbitRandomNextStep) {
      selectedTitleIdx = lunaNavWrap(titles->total, selectedTitleIdx + orbitRandomDirection);
      if (selectedTitleIdx == orbitRandomTargetIdx) {
        orbitRandomActive = 0;
        orbitRandomTargetIdx = -1;
      } else {
        orbitRandomNextStep = uiNowMs() + ORBIT_RANDOM_STEP_MS;
      }
    }
    observeOrbSelection(selectedTitleIdx, uiNowMs());

    // Reload target if index has changed
    if (curTarget->idx != selectedTitleIdx) {
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      if (view == UI_VIEW_CLASSIC) {
        // Keep input polling light while moving through the list. The old art
        // remains visible, but is not eligible for a launch handoff.
        isCoverUninitialized = 1;
        isDiscUninitialized = 1;
        classicArtRequestedIdx = selectedTitleIdx;
        classicArtDueMs = uiNowMs() + CLASSIC_ART_SETTLE_MS;
      }
    }

    if (view == UI_VIEW_CLASSIC && classicArtRequestedIdx == selectedTitleIdx &&
        !classicNavHeld && (int32_t)(uiNowMs() - classicArtDueMs) >= 0) {
      classicPreviousCoverAvailable = classicDisplayedCoverAvailable;
      isCoverUninitialized = loadNextClassicCoverArt(curTarget->device, curTarget->id);
      isDiscUninitialized = loadDiscArt(curTarget->device, curTarget->id);
      classicDisplayedCoverAvailable = !isCoverUninitialized;
      classicDisplayedDiscAvailable = !isDiscUninitialized;
      classicCoverFadeStartMs = uiNowMs();
      classicArtRequestedIdx = -1;
    }

    if (view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT || view == UI_VIEW_ORBS) {
      TargetList *flowTitles = (view == UI_VIEW_PSBBN && collectionFavoritesOnly) ? favoriteTitles : titles;
      int flowSelectedTitleIdx = (view == UI_VIEW_PSBBN && collectionFavoritesOnly)
                                     ? lunaNavMarkedRank(favoriteFlags, titles->total, selectedTitleIdx)
                                     : selectedTitleIdx;
      uint32_t now = uiNowMs();
      int flowOffset;

      if (view == UI_VIEW_PSBBN && flowTitles->total <= 0) {
        if (psbbnCoverBaseIdx >= 0)
          releasePSBBNCovers();
        psbbnCoverBaseIdx = -1;
        psbbnAnimationTargetIdx = -1;
        psbbnOutgoingTitleIdx = -1;
        psbbnAnimationStartOffset = 0;
        drawPSBBNCollection(flowTitles, 0, psbbnCoverTextures, 0, -1, collectionFavoritesOnly, now);
        goto library_view_drawn;
      }

      // Finish synchronous artwork loading before sampling the glide clock.
      if (view != UI_VIEW_ORBS) {
        if (psbbnCoverBaseIdx != flowSelectedTitleIdx)
          refreshPSBBNCovers(flowTitles, flowSelectedTitleIdx, psbbnCoverBaseIdx,
                             view == UI_VIEW_ORBIT);
        psbbnCoverBaseIdx = flowSelectedTitleIdx;
      } else {
        refreshOrbsLogos(flowTitles, flowSelectedTitleIdx);
      }
      now = uiNowMs();

      if (psbbnAnimationTargetIdx < 0) {
        psbbnAnimationTargetIdx = flowSelectedTitleIdx;
        psbbnOutgoingTitleIdx = -1;
        psbbnAnimationStartOffset = 0;
        psbbnAnimationStart = now;
        psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
      } else if (psbbnAnimationTargetIdx != flowSelectedTitleIdx) {
        int currentOffset = lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart, psbbnAnimationDuration, now);
        int direction = lunaNavDirection(flowTitles->total, psbbnAnimationTargetIdx, flowSelectedTitleIdx);
        psbbnOutgoingTitleIdx = psbbnAnimationTargetIdx;
        psbbnAnimationStartOffset = currentOffset + direction * 1000;
        if (view == UI_VIEW_PSBBN && collectionScan.active) {
          psbbnAnimationStartOffset = collectionScan.heldDirection * 1000;
          psbbnAnimationDuration = COLLECTION_SCAN_STEP_MS;
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
      }

      flowOffset = lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart, psbbnAnimationDuration, now);
      if (view == UI_VIEW_ORBIT)
        updatePSBBNCoverResidency(flowOffset);
      now = uiNowMs();
      flowOffset = lunaNavAnimatedOffset(psbbnAnimationStartOffset, psbbnAnimationStart, psbbnAnimationDuration, now);
      if (view == UI_VIEW_PSBBN) {
        collectionVisualCoverIdx = PSBBN_COVER_CACHE_FOCUS;
        if (flowOffset >= 500)
          collectionVisualCoverIdx--;
        else if (flowOffset < -500)
          collectionVisualCoverIdx++;
        int visualRank = lunaNavWrap(flowTitles->total, flowSelectedTitleIdx +
                                    collectionVisualCoverIdx - PSBBN_COVER_CACHE_FOCUS);
        collectionVisualTitleIdx = collectionFavoritesOnly
                                      ? lunaNavMarkedByRank(favoriteFlags, titles->total, visualRank)
                                      : visualRank;
        drawPSBBNCollection(flowTitles, flowSelectedTitleIdx, psbbnCoverTextures,
                            flowOffset, psbbnOutgoingTitleIdx, collectionFavoritesOnly, now);
      } else if (view == UI_VIEW_ORBIT)
        drawOrbit(titles, selectedTitleIdx, psbbnCoverTextures, flowOffset,
                  orbitRandomActive, now);
      else {
        int visualFocus = orbsVisualCacheIndex(flowOffset);
        orbsVisualTitleIdx = lunaNavWrap(titles->total,
            selectedTitleIdx + visualFocus - ORBS_LOGO_CACHE_FOCUS);
        refreshOrbsBackground(getTargetByIdx(titles, orbsVisualTitleIdx));
        drawOrbsView(titles, selectedTitleIdx, flowOffset, visualFocus, now);
      }
    } else if (view == UI_VIEW_GRID) {
      int didLoadArtwork = 0;
      int gridCascadeProgress = 0;
      int pendingPageBuffer = -1;
      int pendingPageBase = -1;
      int loadPageBuffer = -1;
      int loadPrioritySlot = -1;
      uint32_t now = uiNowMs();

      if (gridActivePageBase < 0) {
        gridActivePageBase = (selectedTitleIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
        prepareGridPageBuffer(gridActivePageBuffer, gridActivePageBase, gridPageBases,
                              gridPageComplete, gridPageNextSlot);
      }
      const int activeSelectedSlot = selectedTitleIdx - gridActivePageBase;
      const int selectorMoving = gridSelectorIsMoving(selectedTitleIdx, gridActivePageBase, now);

      // A held shoulder deliberately performs no artwork work, including the
      // half-second decision window. Fast-track moves lightweight page shells,
      // then the normal loader prepares only the page where the user stops.
      if (!gridFastTrackActive && gridShoulderDirection == 0) {
        if (!gridCascadeActive && gridPendingSelectedIdx >= 0) {
          pendingPageBase = (gridPendingSelectedIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
          pendingPageBuffer = lunaNavFindBuffer(gridPageBases, GRID_PAGE_BUFFERS, pendingPageBase);
          if (pendingPageBuffer < 0) {
            pendingPageBuffer = lunaNavChooseBuffer(gridPageBases, GRID_PAGE_BUFFERS, gridActivePageBuffer,
                                                    gridPreviousPageBuffer, gridIncomingPageBuffer);
            if (pendingPageBuffer >= 0)
              prepareGridPageBuffer(pendingPageBuffer, pendingPageBase, gridPageBases,
                                    gridPageComplete, gridPageNextSlot);
          }
          int pendingSlot = gridPendingSelectedIdx - pendingPageBase;
          if (pendingPageBuffer >= 0 && !gridPageSlotAttempted(pendingPageBuffer, pendingSlot)) {
            loadPageBuffer = pendingPageBuffer;
            loadPrioritySlot = pendingSlot;
          }
        }

        // Finish visible placeholders only while the selector is at rest. A
        // newly selected tile still gets priority over the rest of its page.
        if (loadPageBuffer < 0 && gridPendingSelectedIdx < 0 && !gridCascadeActive &&
            !gridPageComplete[gridActivePageBuffer] &&
            (!selectorMoving || !gridPageSlotAttempted(gridActivePageBuffer, activeSelectedSlot))) {
          loadPageBuffer = gridActivePageBuffer;
          if (!gridPageSlotAttempted(gridActivePageBuffer, activeSelectedSlot))
            loadPrioritySlot = activeSelectedSlot;
        }

        if (loadPageBuffer >= 0) {
          gridPageComplete[loadPageBuffer] =
              loadGridPageStep(titles, gridPageBases[loadPageBuffer], loadPageBuffer,
                               &gridPageNextSlot[loadPageBuffer], loadPrioritySlot,
                               &didLoadArtwork);
        }

        if (!gridCascadeActive && pendingPageBuffer >= 0 &&
            gridPageSlotAttempted(pendingPageBuffer, gridPendingSelectedIdx - pendingPageBase)) {
          if (!didLoadArtwork && gridPendingSelectedCoverIdx != gridPendingSelectedIdx) {
            Target *pendingTarget = getTargetByIdx(titles, gridPendingSelectedIdx);
            refreshGridSelectedCover(pendingTarget, gridSelectedIncomingBuffer);
            gridPendingSelectedCoverIdx = gridPendingSelectedIdx;
            didLoadArtwork = 1;
          }

          if (gridPendingSelectedCoverIdx == gridPendingSelectedIdx) {
            int previousSelectedBuffer = gridSelectedActiveBuffer;
            gridSelectedActiveBuffer = gridSelectedIncomingBuffer;
            gridSelectedIncomingBuffer = previousSelectedBuffer;
            gridSelectedActiveIdx = gridPendingSelectedIdx;
            gridSelectedRequestedIdx = gridPendingSelectedIdx;
            gridSelectedAttemptedIdx = gridPendingSelectedIdx;
            releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
            gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;

            selectedTitleIdx = gridPendingSelectedIdx;
            curTarget = getTargetByIdx(titles, selectedTitleIdx);
            if (gridFastTrackSettling) {
              // Fast-track already animated the lightweight destination shell.
              // Promote its completed artwork in place instead of replaying the
              // normal page cascade after the last PNG finishes loading.
              int previousActiveBuffer = gridActivePageBuffer;
              gridActivePageBuffer = pendingPageBuffer;
              gridActivePageBase = pendingPageBase;
              gridPreviousPageBuffer = previousActiveBuffer;
              gridIncomingPageBuffer = -1;
              gridIncomingPageBase = -1;
              gridPrefetchDirection = (gridFastTrackDirection != 0) ? gridFastTrackDirection : gridPageDirection;
              gridPendingSelectedIdx = -1;
              gridPendingSelectedCoverIdx = -1;
              gridPageDirection = 0;
              gridCascadeActive = 0;
              gridCascadeDirection = 0;
              gridCascadeProgress = 0;
              gridFastTrackSettling = 0;
            } else {
              gridIncomingPageBuffer = pendingPageBuffer;
              gridIncomingPageBase = pendingPageBase;
              gridCascadeDirection = gridPageDirection;
              if (gridCascadeDirection == 0)
                gridCascadeDirection = (gridIncomingPageBase > gridActivePageBase) ? 1 : -1;
              gridCascadeStart = uiNowMs();
              gridCascadeActive = 1;
            }
          }
        }

      now = uiNowMs();
      if (gridCascadeActive) {
        uint32_t elapsed = now - gridCascadeStart;
        gridCascadeProgress = (elapsed >= GRID_CASCADE_DURATION_MS)
                                  ? 1000
                                  : (int)((elapsed * 1000ULL) / GRID_CASCADE_DURATION_MS);
        if (gridCascadeProgress >= 1000) {
          int previousActiveBuffer = gridActivePageBuffer;
          gridActivePageBuffer = gridIncomingPageBuffer;
          gridActivePageBase = gridIncomingPageBase;
          gridPreviousPageBuffer = previousActiveBuffer;
          gridIncomingPageBuffer = -1;
          gridIncomingPageBase = -1;
          gridPrefetchDirection = gridCascadeDirection;
          gridPendingSelectedIdx = -1;
          gridPendingSelectedCoverIdx = -1;
          gridPageDirection = 0;
          gridCascadeActive = 0;
          gridCascadeDirection = 0;
          gridCascadeProgress = 0;
        }
      }

      if (gridSelectedRequestedIdx != selectedTitleIdx) {
        gridSelectedRequestedIdx = selectedTitleIdx;
        gridSelectedAttemptedIdx = -1;
      }

      if (!didLoadArtwork && gridSelectedActiveIdx != gridSelectedRequestedIdx &&
          gridSelectedAttemptedIdx != gridSelectedRequestedIdx) {
        Target *selectedTarget = getTargetByIdx(titles, gridSelectedRequestedIdx);
        gridSelectedAttemptedIdx = gridSelectedRequestedIdx;
        if (refreshGridSelectedCover(selectedTarget, gridSelectedIncomingBuffer)) {
          int previousActiveBuffer = gridSelectedActiveBuffer;
          gridSelectedActiveBuffer = gridSelectedIncomingBuffer;
          gridSelectedActiveIdx = gridSelectedRequestedIdx;
          gridSelectedIncomingBuffer = previousActiveBuffer;
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
        } else {
          releaseGridTexture(gridSelectedTextures[gridSelectedActiveBuffer]);
          gridSelectedLoaded[gridSelectedActiveBuffer] = 0;
          gridSelectedActiveIdx = gridSelectedRequestedIdx;
        }
        didLoadArtwork = 1;
      }

        if (!didLoadArtwork && !selectorMoving && !gridCascadeActive && gridPendingSelectedIdx < 0 &&
            gridPageComplete[gridActivePageBuffer] && gridSelectedActiveIdx == gridSelectedRequestedIdx) {
          for (int prefetchPass = 0; prefetchPass < 2; prefetchPass++) {
            int direction = (prefetchPass == 0) ? gridPrefetchDirection : -gridPrefetchDirection;
            int prefetchPageBase = lunaNavPageBase(titles->total, gridActivePageBase, direction);
            int prefetchBuffer = lunaNavFindBuffer(gridPageBases, GRID_PAGE_BUFFERS, prefetchPageBase);

            if (prefetchBuffer < 0) {
              prefetchBuffer = lunaNavChooseBuffer(gridPageBases, GRID_PAGE_BUFFERS, gridActivePageBuffer,
                                                   gridPreviousPageBuffer, gridIncomingPageBuffer);
              if (prefetchBuffer >= 0)
                prepareGridPageBuffer(prefetchBuffer, prefetchPageBase, gridPageBases,
                                      gridPageComplete, gridPageNextSlot);
            }
            if (prefetchBuffer >= 0 && !gridPageComplete[prefetchBuffer]) {
              gridPageComplete[prefetchBuffer] =
                  loadGridPageStep(titles, gridPageBases[prefetchBuffer], prefetchBuffer,
                                   &gridPageNextSlot[prefetchBuffer], -1, &didLoadArtwork);
              break;
            }
          }
        }
      }

      // All artwork work for this frame is complete. From here onward every
      // moving element uses this one timestamp.
      now = uiNowMs();
      if (gridCascadeActive) {
        uint32_t elapsed = now - gridCascadeStart;
        gridCascadeProgress = (elapsed >= GRID_CASCADE_DURATION_MS)
                                  ? 1000
                                  : (int)((elapsed * 1000ULL) / GRID_CASCADE_DURATION_MS);
      }

      if (gridFastTrackActive) {
        uint32_t elapsed = now - gridFastTrackSlideStart;
        int fastTrackProgress = (elapsed >= GRID_FAST_TRACK_STEP_MS)
                                    ? 1000
                                    : (int)((elapsed * 1000ULL) / GRID_FAST_TRACK_STEP_MS);
        int previousBuffer = lunaNavFindBuffer(gridPageBases, GRID_PAGE_BUFFERS, gridFastTrackPreviousPageBase);
        int destinationBuffer = lunaNavFindBuffer(gridPageBases, GRID_PAGE_BUFFERS, gridFastTrackPageBase);

        if (previousBuffer < 0 || !gridPageComplete[previousBuffer])
          previousBuffer = -1;
        if (destinationBuffer < 0 || !gridPageComplete[destinationBuffer])
          destinationBuffer = -1;
        drawPSBBNGrid(titles, gridFastTrackSelectedIdx, gridFastTrackPreviousPageBase, previousBuffer,
                      gridFastTrackPageBase, destinationBuffer, -1,
                      gridFastTrackDirection, fastTrackProgress, now);
      } else if (gridFastTrackSettling && !gridCascadeActive) {
        int destinationBuffer = lunaNavFindBuffer(gridPageBases, GRID_PAGE_BUFFERS, gridFastTrackPageBase);
        if (destinationBuffer < 0)
          destinationBuffer = -1;
        drawPSBBNGrid(titles, gridFastTrackSelectedIdx, gridFastTrackPageBase, destinationBuffer,
                      -1, -1, -1, gridFastTrackDirection, 0, now);
      } else {
        drawPSBBNGrid(titles, selectedTitleIdx, gridActivePageBase, gridActivePageBuffer,
                      gridIncomingPageBase, gridIncomingPageBuffer, gridSelectedActiveBuffer,
                      gridCascadeDirection, gridCascadeProgress, now);
      }
    } else {
      int favoritesEmpty = favoritesOnly && lunaNavMarkedCount(favoriteFlags, titles->total) == 0;
      const uint32_t frameNowMs = uiNowMs();
      const int coverPending = classicArtRequestedIdx == selectedTitleIdx;
      uint32_t fadeElapsed = frameNowMs - classicCoverFadeStartMs;
      int coverFadeProgress = (classicPreviousCoverAvailable && !coverPending &&
                               fadeElapsed < CLASSIC_COVER_FADE_DURATION_MS)
                                  ? (int)(fadeElapsed * 1000U / CLASSIC_COVER_FADE_DURATION_MS)
                                  : 1000;
      drawTitleList(titles, selectedTitleIdx, maxTitlesPerPage,
                    (classicDisplayedCoverAvailable && !favoritesEmpty) ? coverTexture : NULL,
                    (classicPreviousCoverAvailable && !favoritesEmpty && !coverPending &&
                     coverFadeProgress < 1000) ? classicPreviousCoverTexture : NULL,
                    (classicDisplayedDiscAvailable && !favoritesEmpty) ? discTexture : NULL,
                    favoriteFlags, favoritesOnly, coverPending && !favoritesEmpty,
                    coverFadeProgress, frameNowMs);
    }

  library_view_drawn:
    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_sync_flip(gsGlobal);
    usleep(1000);

    // Keep rendering after options close, while ignoring the Triangle press
    // that closed them until the button is released.
    input = pollInput();
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

    if (view == UI_VIEW_GRID) {
      uint32_t now = uiNowMs();
      int leftHeld = (input & GRID_LEFT_SHOULDERS) != 0;
      int rightHeld = (input & GRID_RIGHT_SHOULDERS) != 0;
      int shoulderDirection = (rightHeld && !leftHeld) ? 1 : ((leftHeld && !rightHeld) ? -1 : 0);

      if (shoulderDirection == 0) {
        if (gridFastTrackActive) {
          int destinationPageBase = (gridFastTrackSelectedIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;

          gridFastTrackActive = 0;
          gridPageDirection = gridFastTrackDirection;
          if (destinationPageBase == gridActivePageBase) {
            gridPendingSelectedIdx = -1;
            gridPendingSelectedCoverIdx = -1;
            selectedTitleIdx = gridFastTrackSelectedIdx;
            curTarget = getTargetByIdx(titles, selectedTitleIdx);
            gridFastTrackSettling = 0;
          } else {
            gridPendingSelectedIdx = gridFastTrackSelectedIdx;
            gridPendingSelectedCoverIdx = -1;
            gridFastTrackSettling = 1;
          }
          input = 0;
        } else if (gridShoulderDirection != 0) {
          // A shoulder released before the hold threshold is a normal
          // one-page tap. Deferring this decision prevents any art decode
          // from starting while the user may still enter fast-track.
          int navigationIdx = (gridPendingSelectedIdx >= 0) ? gridPendingSelectedIdx : selectedTitleIdx;
          int candidate = lunaNavGridPage(titles->total, navigationIdx, gridShoulderDirection);

          if ((candidate / GRID_PAGE_SIZE) * GRID_PAGE_SIZE == gridActivePageBase) {
            gridPendingSelectedIdx = -1;
            selectedTitleIdx = candidate;
          } else {
            gridPendingSelectedIdx = candidate;
          }
          if (gridPendingSelectedCoverIdx >= 0 && gridPendingSelectedCoverIdx != gridPendingSelectedIdx) {
            releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
            gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
            gridPendingSelectedCoverIdx = -1;
          }
          gridPageDirection = gridShoulderDirection;
          input = 0;
        }
        gridShoulderDirection = 0;
        gridShoulderHoldStart = 0;
      } else if (shoulderDirection != gridShoulderDirection) {
        gridShoulderDirection = shoulderDirection;
        gridShoulderHoldStart = now;

        // Once fast-track is active, reversing direction remains immediate;
        // the user has already satisfied the hold threshold.
        if (gridFastTrackActive) {
          gridFastTrackDirection = shoulderDirection;
          gridFastTrackPreviousPageBase = gridFastTrackPageBase;
          gridFastTrackSelectedIdx = lunaNavGridPage(titles->total, gridFastTrackSelectedIdx, shoulderDirection);
          gridFastTrackPageBase = (gridFastTrackSelectedIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
          gridFastTrackSlideStart = now;
          gridFastTrackNextStep = now + GRID_FAST_TRACK_STEP_MS;
        }
      } else if (!gridFastTrackActive && now - gridShoulderHoldStart >= GRID_FAST_TRACK_HOLD_MS) {
        int navigationIdx = (gridPendingSelectedIdx >= 0) ? gridPendingSelectedIdx : selectedTitleIdx;

        // If the normal first page change already reached its cascade, accept
        // that prepared page immediately before entering lightweight tracking.
        if (gridCascadeActive && gridIncomingPageBuffer >= 0) {
          int previousActiveBuffer = gridActivePageBuffer;
          gridActivePageBuffer = gridIncomingPageBuffer;
          gridActivePageBase = gridIncomingPageBase;
          gridPreviousPageBuffer = previousActiveBuffer;
          gridIncomingPageBuffer = -1;
          gridIncomingPageBase = -1;
          gridPrefetchDirection = gridCascadeDirection;
          gridCascadeActive = 0;
          gridCascadeDirection = 0;
          gridCascadeStart = 0;
          navigationIdx = selectedTitleIdx;
        } else if (gridPendingSelectedCoverIdx >= 0) {
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
        }

        gridPendingSelectedIdx = -1;
        gridPendingSelectedCoverIdx = -1;
        gridFastTrackActive = 1;
        gridFastTrackSettling = 0;
        gridFastTrackDirection = shoulderDirection;
        gridFastTrackPreviousPageBase = (navigationIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
        gridFastTrackSelectedIdx = lunaNavGridPage(titles->total, navigationIdx, shoulderDirection);
        gridFastTrackPageBase = (gridFastTrackSelectedIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
        gridFastTrackSlideStart = now;
        gridFastTrackNextStep = now + GRID_FAST_TRACK_STEP_MS;
      } else if (gridFastTrackActive && now >= gridFastTrackNextStep) {
        gridFastTrackPreviousPageBase = gridFastTrackPageBase;
        gridFastTrackSelectedIdx = lunaNavGridPage(titles->total, gridFastTrackSelectedIdx, shoulderDirection);
        gridFastTrackPageBase = (gridFastTrackSelectedIdx / GRID_PAGE_SIZE) * GRID_PAGE_SIZE;
        gridFastTrackSlideStart = now;
        gridFastTrackNextStep = now + GRID_FAST_TRACK_STEP_MS;
      }
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

    if (view == UI_VIEW_PSBBN) {
      const int rawInput = input;
      const int actionButtons = PAD_CROSS | PAD_TRIANGLE | PAD_CIRCLE | PAD_SELECT | PAD_START;
      int scanDirection = 0;
      if (!(rawInput & actionButtons)) {
        int left = (rawInput & PAD_L2) != 0;
        int right = (rawInput & PAD_R2) != 0;
        scanDirection = right == left ? 0 : (right ? 1 : -1);
      }
      int scanStep = lunaCollectionScanUpdate(&collectionScan, scanDirection, uiNowMs());
      input = rawInput & ~(PAD_L2 | PAD_R2);
      if (scanDirection)
        input &= ~(PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN | PAD_L1 | PAD_R1);
      if (scanStep) {
        if (collectionFavoritesOnly) {
          int next = lunaNavMarkedStep(favoriteFlags, titles->total, selectedTitleIdx, scanStep);
          if (next >= 0)
            selectedTitleIdx = next;
        } else if (titles->total > 1) {
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
      if (!input)
        continue;
    } else {
      if (frameCount && (input == prevInput))
        continue;
      frameCount = 0;
      prevInput = input;
    }

    // Grid shoulders are handled by the tap/hold state machine above.
    if (view == UI_VIEW_GRID && (input & (GRID_LEFT_SHOULDERS | GRID_RIGHT_SHOULDERS)))
      continue;

    // Page preparation is background work. Only the visible cascade gates
    // interaction so page loading never feels like a frozen UI.
    if (view == UI_VIEW_GRID && gridCascadeActive)
      continue;
    if (view == UI_VIEW_GRID && gridFastTrackActive)
      continue;

    // Actions use the logo at the fixed Orbs marker even when the next
    // queued selection has not finished gliding into place.
    if (view == UI_VIEW_ORBS && orbsVisualTitleIdx >= 0 &&
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
    if (view == UI_VIEW_PSBBN &&
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

    if ((view == UI_VIEW_CLASSIC || view == UI_VIEW_PSBBN) &&
        (input & PAD_SELECT) && !favoritesTabButtonHeld) {
      favoritesTabButtonHeld = 1;
      if (view == UI_VIEW_CLASSIC)
        favoritesOnly = !favoritesOnly;
      else
        collectionFavoritesOnly = !collectionFavoritesOnly;
      if ((favoritesOnly || collectionFavoritesOnly) && !favoriteFlags[selectedTitleIdx]) {
        int firstFavorite = lunaNavMarkedByRank(favoriteFlags, titles->total, 0);
        if (firstFavorite >= 0)
          selectedTitleIdx = firstFavorite;
      }
      if (view == UI_VIEW_PSBBN) {
        releasePSBBNCovers();
        psbbnCoverBaseIdx = -1;
        psbbnAnimationTargetIdx = -1;
        psbbnOutgoingTitleIdx = -1;
        psbbnAnimationStartOffset = 0;
      }
    } else if ((input & PAD_CROSS) &&
               (!(favoritesOnly || collectionFavoritesOnly) ||
                lunaNavMarkedCount(favoriteFlags, titles->total) > 0)) {
      // Copy target, free title list and launch
      Target *target = copyTarget(curTarget);
      freeTargetList(favoriteTitles);
      favoriteTitles = NULL;
      free(favoriteFlags);
      favoriteFlags = NULL;
      freeTargetList(titles);
      GSTEXTURE *handoffCover = NULL;
      if (view == UI_VIEW_CLASSIC && !isCoverUninitialized) {
        handoffCover = coverTexture;
      } else if (view == UI_VIEW_GRID && gridSelectedActiveBuffer >= 0 &&
                 gridSelectedLoaded[gridSelectedActiveBuffer]) {
        handoffCover = gridSelectedTextures[gridSelectedActiveBuffer];
      } else if ((view == UI_VIEW_PSBBN || view == UI_VIEW_ORBIT) &&
                 psbbnCoverLoaded[collectionActionCoverIdx]) {
        handoffCover = psbbnCoverTextures[collectionActionCoverIdx];
      }
      uiLaunchTitle(target, NULL, handoffCover);
      // Something went wrong, main loop must exit immediately
      return -1;
    } else if (input & PAD_CIRCLE) {
      UILibraryView previousView = view;
      view = lunaNavNextView(view, orbsEnabled);
      collectionScan = (LunaCollectionScan){0};
      favoritesOnly = 0;
      collectionFavoritesOnly = 0;

      if (previousView == UI_VIEW_CLASSIC) {
        releaseClassicArtVRAM();
        isCoverUninitialized = 1;
        isDiscUninitialized = 1;
        classicDisplayedCoverAvailable = 0;
        classicDisplayedDiscAvailable = 0;
        classicPreviousCoverAvailable = 0;
      } else if (previousView == UI_VIEW_PSBBN || previousView == UI_VIEW_ORBIT) {
        releasePSBBNCovers();
      } else if (previousView == UI_VIEW_ORBS) {
        releaseOrbsArt();
      } else if (previousView == UI_VIEW_GRID) {
        releaseGridCovers();
      }

      psbbnCoverBaseIdx = -1;
      psbbnAnimationTargetIdx = -1;
      psbbnOutgoingTitleIdx = -1;
      psbbnAnimationStartOffset = 0;
      psbbnAnimationDuration = PSBBN_ANIMATION_DURATION_MS;
      gridActivePageBuffer = 0;
      gridIncomingPageBuffer = -1;
      gridPreviousPageBuffer = -1;
      gridActivePageBase = -1;
      gridIncomingPageBase = -1;
      for (int buffer = 0; buffer < GRID_PAGE_BUFFERS; buffer++) {
        gridPageBases[buffer] = -1;
        gridPageComplete[buffer] = 0;
        gridPageNextSlot[buffer] = 0;
      }
      gridSelectedActiveBuffer = 0;
      gridSelectedIncomingBuffer = 1;
      gridSelectedActiveIdx = -1;
      gridSelectedRequestedIdx = -1;
      gridSelectedAttemptedIdx = -1;
      gridPendingSelectedIdx = -1;
      gridPendingSelectedCoverIdx = -1;
      gridPageDirection = 0;
      gridPrefetchDirection = 1;
      gridCascadeActive = 0;
      gridCascadeDirection = 0;
      gridCascadeStart = 0;
      gridShoulderDirection = 0;
      gridShoulderHoldStart = 0;
      gridFastTrackActive = 0;
      gridFastTrackSettling = 0;
      gridFastTrackDirection = 0;
      gridFastTrackSelectedIdx = -1;
      gridFastTrackPreviousPageBase = -1;
      gridFastTrackPageBase = -1;
      gridFastTrackSlideStart = 0;
      gridFastTrackNextStep = 0;
      orbitRandomActive = 0;
      orbitRandomTargetIdx = -1;
      orbitRandomButtonHeld = 0;
      if (view == UI_VIEW_CLASSIC) {
        isCoverUninitialized = loadCoverArt(curTarget->device, curTarget->id);
        isDiscUninitialized = loadDiscArt(curTarget->device, curTarget->id);
        classicDisplayedCoverAvailable = !isCoverUninitialized;
        classicDisplayedDiscAvailable = !isDiscUninitialized;
        classicPreviousCoverAvailable = 0;
        classicArtRequestedIdx = -1;
        classicNavHeld = 0;
        classicRepeat.direction = 0;
      }
      if (saveLastLibraryView(curTarget, view))
        DPRINTF("WARN: Could not save selected library view\n");
    } else if (view == UI_VIEW_CLASSIC && (input & PAD_SQUARE) && !favoriteButtonHeld &&
               (!favoritesOnly || lunaNavMarkedCount(favoriteFlags, titles->total) > 0)) {
      int wasFavorite = favoriteFlags[selectedTitleIdx] != 0;
      int previousRank = lunaNavMarkedRank(favoriteFlags, titles->total, selectedTitleIdx);
      favoriteButtonHeld = 1;
      favoriteFlags[selectedTitleIdx] = !wasFavorite;
      if (saveFavoriteFlags(titles, favoriteFlags, (size_t)titles->total, curTarget)) {
        favoriteFlags[selectedTitleIdx] = wasFavorite;
      } else {
        TargetList *updatedFavorites = buildFavoriteTargetList(titles, favoriteFlags,
                                                                (size_t)titles->total);
        if (updatedFavorites != NULL) {
          freeTargetList(favoriteTitles);
          favoriteTitles = updatedFavorites;
        }
      }
      if (favoritesOnly && wasFavorite && favoriteFlags[selectedTitleIdx] == 0) {
        int remaining = lunaNavMarkedCount(favoriteFlags, titles->total);
        if (remaining > 0) {
          selectedTitleIdx = lunaNavMarkedByRank(favoriteFlags, titles->total,
                                                (previousRank < remaining) ? previousRank : 0);
          curTarget = getTargetByIdx(titles, selectedTitleIdx);
          isCoverUninitialized = 1;
          isDiscUninitialized = 1;
          classicArtRequestedIdx = selectedTitleIdx;
          classicArtDueMs = uiNowMs() + CLASSIC_ART_SETTLE_MS;
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
    } else if (view == UI_VIEW_GRID && (input & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN))) {
      int navigationIdx = (gridPendingSelectedIdx >= 0) ? gridPendingSelectedIdx : selectedTitleIdx;
      int candidate = navigationIdx;
      int direction;

      if (input & PAD_LEFT)
        candidate = ((navigationIdx - 1) + titles->total) % titles->total;
      else if (input & PAD_RIGHT)
        candidate = (navigationIdx + 1) % titles->total;
      else if (input & PAD_UP)
        candidate = lunaNavGridVertical(titles->total, navigationIdx, -1);
      else if (input & PAD_DOWN)
        candidate = lunaNavGridVertical(titles->total, navigationIdx, 1);

      direction = lunaNavDirection(titles->total, navigationIdx, candidate);
      if ((candidate / GRID_PAGE_SIZE) * GRID_PAGE_SIZE == gridActivePageBase) {
        if (gridPendingSelectedCoverIdx >= 0) {
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
        }
        gridPendingSelectedIdx = -1;
        gridPendingSelectedCoverIdx = -1;
        selectedTitleIdx = candidate;
      } else {
        if (gridPendingSelectedCoverIdx >= 0 && gridPendingSelectedCoverIdx != candidate) {
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
          gridPendingSelectedCoverIdx = -1;
        }
        gridPendingSelectedIdx = candidate;
      }
      gridPageDirection = direction;
    } else if (input & (PAD_LEFT | PAD_UP)) {
      // Point to the previous title
      if (favoritesOnly || collectionFavoritesOnly) {
        int favoriteIdx = lunaNavMarkedStep(favoriteFlags, titles->total, selectedTitleIdx, -1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else {
        selectedTitleIdx = ((selectedTitleIdx - 1) + titles->total) % titles->total;
      }
    } else if (input & (PAD_RIGHT | PAD_DOWN)) {
      // Advance to the next title
      if (favoritesOnly || collectionFavoritesOnly) {
        int favoriteIdx = lunaNavMarkedStep(favoriteFlags, titles->total, selectedTitleIdx, 1);
        if (favoriteIdx >= 0)
          selectedTitleIdx = favoriteIdx;
      } else {
        selectedTitleIdx = (selectedTitleIdx + 1) % titles->total;
      }
    } else if (input & GRID_RIGHT_SHOULDERS) {
      // Switch to the next page.
      if (view == UI_VIEW_GRID) {
        int navigationIdx = (gridPendingSelectedIdx >= 0) ? gridPendingSelectedIdx : selectedTitleIdx;
        int candidate = lunaNavGridPage(titles->total, navigationIdx, 1);
        if ((candidate / GRID_PAGE_SIZE) * GRID_PAGE_SIZE == gridActivePageBase) {
          gridPendingSelectedIdx = -1;
          selectedTitleIdx = candidate;
        } else {
          gridPendingSelectedIdx = candidate;
        }
        if (gridPendingSelectedCoverIdx >= 0 && gridPendingSelectedCoverIdx != gridPendingSelectedIdx) {
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
          gridPendingSelectedCoverIdx = -1;
        }
        gridPageDirection = 1;
      } else if (favoritesOnly || collectionFavoritesOnly) {
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
      if (view == UI_VIEW_GRID) {
        int navigationIdx = (gridPendingSelectedIdx >= 0) ? gridPendingSelectedIdx : selectedTitleIdx;
        int candidate = lunaNavGridPage(titles->total, navigationIdx, -1);
        if ((candidate / GRID_PAGE_SIZE) * GRID_PAGE_SIZE == gridActivePageBase) {
          gridPendingSelectedIdx = -1;
          selectedTitleIdx = candidate;
        } else {
          gridPendingSelectedIdx = candidate;
        }
        if (gridPendingSelectedCoverIdx >= 0 && gridPendingSelectedCoverIdx != gridPendingSelectedIdx) {
          releaseGridTexture(gridSelectedTextures[gridSelectedIncomingBuffer]);
          gridSelectedLoaded[gridSelectedIncomingBuffer] = 0;
          gridPendingSelectedCoverIdx = -1;
        }
        gridPageDirection = -1;
      } else if (favoritesOnly || collectionFavoritesOnly) {
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
    } else if ((input & PAD_TRIANGLE) &&
               (!(favoritesOnly || collectionFavoritesOnly) ||
                lunaNavMarkedCount(favoriteFlags, titles->total) > 0)) {
      prevInput = 0; // Reset previous input
      // Enter title options screen
      if ((res = uiTitleOptionsLoop(curTarget, &classicArtOverlap, &orbsEnabled,
                                    &orbsBackground, &glassColorSetting,
                                    &ambientEnabled)) < 0) {
        // Something went wrong, main loop must exit immediately
        ambientStop();
        freeTargetList(favoriteTitles);
        free(favoriteFlags);
        return -1;
      }
      if (view == UI_VIEW_ORBS && !orbsEnabled) {
        releaseOrbsArt();
        view = UI_VIEW_CLASSIC;
        isCoverUninitialized = loadCoverArt(curTarget->device, curTarget->id);
        isDiscUninitialized = loadDiscArt(curTarget->device, curTarget->id);
        classicDisplayedCoverAvailable = !isCoverUninitialized;
        classicDisplayedDiscAvailable = !isDiscUninitialized;
        classicPreviousCoverAvailable = 0;
        classicArtRequestedIdx = -1;
        classicNavHeld = 0;
        classicRepeat.direction = 0;
        if (saveLastLibraryView(curTarget, view))
          DPRINTF("WARN: Could not save selected library view\n");
      }
      setOrbsBackgroundStyle(orbsBackground);
      setGlassColorPreset((GlassColorPreset)glassColorSetting);
      optionsTriangleHeld = (pollInput() & PAD_TRIANGLE) != 0;
      input = 0;
    } else if (input & PAD_START) {
      // Quit
      break;
    }
  }

exit:
  ambientStop();
  if (favoriteTitles != NULL)
    freeTargetList(favoriteTitles);
  free(favoriteFlags);
  closePad();
  closeUI();
  return res;
}
// Displays Game ID and launches the title
void uiLaunchTitle(Target *target, ArgumentList *arguments, GSTEXTURE *cover) {
  UILaunchHandoff handoff = {.target = target, .cover = cover};

  // Present immediately, then continue with real launch work. There is no
  // minimum display time or transition delay.
  uiPresentLaunchHandoff(target, cover, LAUNCH_STAGE_PREPARING);
  closePad();

  if (arguments == NULL)
    arguments = loadLaunchArgumentLists(target);

  // Keep the final framebuffer resident while Neutrino loads. The process
  // replacement reclaims these UI resources without exposing a black frame.
  launchTitleWithProgress(target, arguments, uiLaunchHandoffProgress, &handoff);

  // launchTitleWithProgress normally never returns. Retain cleanup for an
  // unsupported target mode or another pre-exec failure.
  closeUI();
}

//
// GameID code based on https://github.com/CosmicScale/Retro-GEM-PS2-Disc-Launcher
//

static uint8_t calculateCRC(const uint8_t *data, int len) {
  uint8_t crc = 0x00;
  for (int i = 0; i < len; i++) {
    crc += data[i];
  }
  return 0x100 - crc;
}

void drawGameID(const char *gameID) {
  uint8_t data[64] = {0};
  int gidlen = strnlen(gameID, 11); // Ensure the length does not exceed 11 characters

  int dpos = 0;
  data[dpos++] = 0xA5; // detect word
  data[dpos++] = 0x00; // address offset
  dpos++;
  data[dpos++] = gidlen;

  memcpy(&data[dpos], gameID, gidlen);
  dpos += gidlen;

  data[dpos++] = 0x00;
  data[dpos++] = 0xD5; // end word
  data[dpos++] = 0x00; // padding

  int data_len = dpos;
  data[2] = calculateCRC(&data[3], data_len - 3);

  int xstart = (gsGlobal->Width / 2) - (data_len * 8);
  int ystart = gsGlobal->Height - (((gsGlobal->Height / 8) * 2) + 20);
  int height = 2;

  for (int i = 0; i < data_len; i++) {
    for (int j = 7; j >= 0; j--) {
      int x = xstart + (i * 16 + ((7 - j) * 2));
      int x1 = x + 1;
      gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 0, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x80));

      uint32_t color = (data[i] >> j) & 1 ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x80);
      gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 0, color);
    }
  }
}

//
// Splash screen functions
//

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

  // Redraw continuously so the loading crystal keeps moving between boot
  // messages. The main thread still owns every initialization and scan step.
  while (PollSema(logBuffer.doneSema) != logBuffer.doneSema) {
    if (PollSema(logBuffer.newStringSema) == logBuffer.newStringSema) {
      SignalSema(logBuffer.drawnSema);
    }

    gsKit_TexManager_nextFrame(gsGlobal);
    drawSplashGlassBackground(uiNowMs());

    drawBootLogo(gsGlobal->Width / 2, keepoutArea + 8, gsGlobal->Width * 41 / 100, 2);

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
