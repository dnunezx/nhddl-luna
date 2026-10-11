// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include "ui/language.h"
#include "ui/ambient_orbs.h"

#include <stdio.h>

#define ORBIT_PHASE_UNITS (PSBBN_COVER_CACHE_COUNT * 1000)

static int visibleTitleIndex = -1;

int orbitVisibleTitleIndex(void) { return visibleTitleIndex; }

typedef struct {
  float upperLeftX;
  float upperLeftY;
  float upperRightX;
  float upperRightY;
  float lowerLeftX;
  float lowerLeftY;
  float lowerRightX;
  float lowerRightY;
} OrbitQuad;

typedef struct {
  OrbitQuad quad;
  int cacheIdx;
  int targetIdx;
  int depth;
  int emphasis;
  int visibility;
  int centerX;
  int centerY;
  int size;
  int drawable;
} OrbitCover;

static int orbitAbsolute(int value) {
  return (value < 0) ? -value : value;
}

static int orbitWrappedPosition(int position) {
  position %= ORBIT_PHASE_UNITS;
  if (position > ORBIT_PHASE_UNITS / 2)
    position -= ORBIT_PHASE_UNITS;
  else if (position < -ORBIT_PHASE_UNITS / 2)
    position += ORBIT_PHASE_UNITS;
  return position;
}

static uint32_t orbitPhase(int position) {
  return (uint32_t)(((int64_t)position * 65536LL) / ORBIT_PHASE_UNITS);
}

static OrbitQuad orbitCoverQuad(int centerX, int centerY, int size, int sine) {
  OrbitQuad quad;
  float projectedWidth = size * (1000.0f - orbitAbsolute(sine) * 340.0f / 127.0f) / 1000.0f;
  float leftScale = 1.0f + sine * 0.16f / 127.0f;
  float rightScale = 1.0f - sine * 0.16f / 127.0f;
  float leftHalfHeight = size * leftScale / 2.0f;
  float rightHalfHeight = size * rightScale / 2.0f;

  quad.upperLeftX = centerX - projectedWidth / 2.0f;
  quad.upperLeftY = centerY - leftHalfHeight;
  quad.upperRightX = centerX + projectedWidth / 2.0f;
  quad.upperRightY = centerY - rightHalfHeight;
  quad.lowerLeftX = quad.upperLeftX;
  quad.lowerLeftY = centerY + leftHalfHeight;
  quad.lowerRightX = quad.upperRightX;
  quad.lowerRightY = centerY + rightHalfHeight;
  return quad;
}

static void drawOrbitQuadSolid(OrbitQuad quad, int z, uint64_t color) {
  gsKit_prim_quad_gouraud(gsGlobal, quad.upperLeftX, quad.upperLeftY,
                          quad.upperRightX, quad.upperRightY,
                          quad.lowerLeftX, quad.lowerLeftY,
                          quad.lowerRightX, quad.lowerRightY,
                          z, color, color, color, color);
}

static void drawOrbitQuadTexture(GSTEXTURE *texture, OrbitQuad quad, int z, uint64_t color) {
  int previousAlphaTest = gsGlobal->Test->ATST;
  int previousAlphaReference = gsGlobal->Test->AREF;
  int previousAlphaFail = gsGlobal->Test->AFAIL;
  if (!bindTextureSafe(gsGlobal, texture))
    return;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_prim_quad_texture(gsGlobal, texture,
                          quad.upperLeftX, quad.upperLeftY, 0.0f, 0.0f,
                          quad.upperRightX, quad.upperRightY, texture->Width - 1, 0.0f,
                          quad.lowerLeftX, quad.lowerLeftY, 0.0f, texture->Height - 1,
                          quad.lowerRightX, quad.lowerRightY, texture->Width - 1, texture->Height - 1,
                          z, color);
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawOrbitGuide(int centerX, int centerY, int radiusX, int radiusY) {
  int segment;

  for (segment = 0; segment < 32; segment++) {
    uint32_t phase = (uint32_t)segment << 11;
    uint32_t nextPhase = (uint32_t)((segment + 1) & 31) << 11;
    int x1 = centerX + (discWave(phase) * radiusX) / 127;
    int y1 = centerY + ((discWave(phase + (8 << 11)) - 127) * radiusY) / 254;
    int x2 = centerX + (discWave(nextPhase) * radiusX) / 127;
    int y2 = centerY + ((discWave(nextPhase + (8 << 11)) - 127) * radiusY) / 254;
    int depth = discWave(phase + (8 << 11));
    int alpha = 0x0C + ((depth + 127) * 0x20) / 254;

    gsKit_prim_line(gsGlobal, x1, psbbnFieldStableY(y1), x2, psbbnFieldStableY(y2), 2,
                    glassPresetColor(0x38, 0xA8, 0xF0, alpha));
  }
}


void drawOrbit(TargetList *titles, int selectedTitleIdx, GSTEXTURE **covers, int flowOffset,
               int randomActive, int entryProgress, uint32_t frameNowMs,
               const char *nextViewLabel) {
  const int top = headerHeight + 8;
  const int bottom = gsGlobal->Height - footerHeight - 8;
  const int centerX = gsGlobal->Width / 2;
  const int centerY = (top + bottom) / 2;
  const int radiusX = (gsGlobal->Width - keepoutArea * 2) * 43 / 100;
  const int radiusY = (bottom - top) * 29 / 100;
  int selectedSize = (bottom - top) * 66 / 100;
  int maxSelectedSize = gsGlobal->Width * 36 / 100;
  OrbitCover items[PSBBN_COVER_CACHE_COUNT];
  int order[PSBBN_COVER_CACHE_COUNT];
  int visualFocus = -1;
  int visualFocusDistance = 0x7FFFFFFF;
  char selectedTitle[255];
  int cacheIdx;
  const int entryRadius = 700 + entryProgress * 300 / 1000;
  const int entrySize = 850 + entryProgress * 150 / 1000;
  const int entryVisibility = 300 + entryProgress * 700 / 1000;

  if (selectedSize > maxSelectedSize)
    selectedSize = maxSelectedSize;

  if (getLibraryBackground() == LIBRARY_BACKGROUND_ORBS) {
    const uint64_t black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
    gsKit_prim_quad_gouraud(gsGlobal, 0, 0, gsGlobal->Width, 0,
                            0, gsGlobal->Height, gsGlobal->Width,
                            gsGlobal->Height, 0, black, black, black, black);
    drawAmbientOrbsOrbit(gsGlobal->Width / 2, gsGlobal->Height / 2,
                         gsGlobal->Width * 25 / 100,
                         gsGlobal->Height * 35 / 100, frameNowMs, 0);
  } else {
    drawSharedLibraryBackground(frameNowMs);
  }
  drawTextWindow(keepoutArea + 10, headerHeight - getFontLineHeight(),
                 gsGlobal->Width - keepoutArea, 0, 6,
                 HeaderTextColor, ALIGN_LEFT, lunaText("ORBIT"));
  if (randomActive)
    drawTextWindow(gsGlobal->Width / 2, headerHeight - getFontLineHeight(),
                   gsGlobal->Width - keepoutArea - 8, 0, 6,
                   FontMainColor, ALIGN_RIGHT, lunaText("RANDOM SCAN"));
  drawOrbitGuide(centerX, centerY, radiusX, radiusY);

  for (cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    int rawPosition = (cacheIdx - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    int position = orbitWrappedPosition(rawPosition);
    int distance = orbitAbsolute(position);
    uint32_t phase = orbitPhase(position);
    int sine = discWave(phase);
    int depth = discWave(phase + (8 << 11));
    int depthProgress = ((depth + 127) * 1000) / 254;
    int baseSize = selectedSize * (28 + (depthProgress * 30) / 1000) / 100;
    int focusProgress = (distance < 1000) ? 1000 - distance : 0;
    int focusBoost = selectedSize * 42 / 100;
    int size = (baseSize + (focusBoost * lunaNavEase(focusProgress)) / 1000) *
               entrySize / 1000;
    int itemCenterX = centerX + (sine * radiusX * entryRadius) / (127 * 1000);
    int itemCenterY = centerY + ((depth - 127) * radiusY * entryRadius) / (254 * 1000);

    items[cacheIdx].quad = orbitCoverQuad(itemCenterX, itemCenterY, size, sine);
    items[cacheIdx].cacheIdx = cacheIdx;
    items[cacheIdx].targetIdx = lunaNavWrap(titles->total,
        selectedTitleIdx + cacheIdx - PSBBN_COVER_CACHE_FOCUS);
    items[cacheIdx].depth = depth;
    items[cacheIdx].emphasis = (focusProgress > depthProgress) ? focusProgress : depthProgress;
    items[cacheIdx].visibility = (300 + (depthProgress * 700) / 1000) *
                                 entryVisibility / 1000;
    items[cacheIdx].centerX = itemCenterX;
    items[cacheIdx].centerY = itemCenterY;
    items[cacheIdx].size = size;
    items[cacheIdx].drawable = 1;
    order[cacheIdx] = cacheIdx;

    if (distance < visualFocusDistance) {
      visualFocus = cacheIdx;
      visualFocusDistance = distance;
    }
  }

  // A short library wraps inside the ten-entry cache. Retain only the most
  // forward occurrence of each title so the ring never shows duplicates.
  for (cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    int prior;
    for (prior = 0; prior < cacheIdx; prior++) {
      if (!items[prior].drawable || items[prior].targetIdx != items[cacheIdx].targetIdx)
        continue;
      if (items[prior].depth >= items[cacheIdx].depth)
        items[cacheIdx].drawable = 0;
      else
        items[prior].drawable = 0;
    }
  }

  // The GS does not provide a scene-level depth sort for these translucent
  // covers. Sort the ten slots explicitly and paint the rear arc first.
  for (cacheIdx = 1; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    int value = order[cacheIdx];
    int insertion = cacheIdx;
    while (insertion > 0 && items[order[insertion - 1]].depth > items[value].depth) {
      order[insertion] = order[insertion - 1];
      insertion--;
    }
    order[insertion] = value;
  }

  for (cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    OrbitCover *item = &items[order[cacheIdx]];
    int brightness;
    int alpha;
    int backplateAlpha;
    int z;

    if (!item->drawable)
      continue;
    int outgoingVisibility, incomingVisibility;
    GSTEXTURE *outgoing = orbitCoverHandoff(item->cacheIdx, frameNowMs,
        &outgoingVisibility, &incomingVisibility);
    brightness = (0x42 + (item->emphasis * 0x3E) / 1000) * item->visibility / 1000;
    alpha = (0x38 + (item->emphasis * 0x48) / 1000) * item->visibility / 1000;
    z = 4 + ((item->depth + 127) * 2) / 254;
    // Keep temporary old artwork on the rear arc, away from the focused title.
    if (item->depth < 0) {
      if (outgoing != NULL && outgoingVisibility > 0)
        drawOrbitQuadTexture(outgoing, item->quad, z,
            GS_SETREG_RGBA(brightness, brightness, brightness,
                           alpha * outgoingVisibility / 1000));
      item->visibility = item->visibility * incomingVisibility / 1000;
      alpha = alpha * incomingVisibility / 1000;
    }
    // A pending decode is not a missing cover. Do not flash its placeholder.
    if (!psbbnCoverLoaded[item->cacheIdx] && !collectionCoverMissing(item->cacheIdx))
      continue;
    backplateAlpha = getGlassColorPreset() == GLASS_COLOR_ORIGINAL
                         ? 0x24 : item->cacheIdx == visualFocus ? 0x18 : 0x10;

    // New themes reveal a faint backplate through feathered cover edges.
    // Original retains its existing backplate and texture treatment.
    if ((covers[item->cacheIdx] != NULL && psbbnCoverLoaded[item->cacheIdx]) ||
        getGlassColorPreset() == GLASS_COLOR_ORIGINAL) {
      uint64_t backplateColor = getGlassColorPreset() == GLASS_COLOR_WHITE_GRAY
                                    ? GS_SETREG_RGBA(0x7A, 0xA6, 0xC0,
                                        (backplateAlpha * item->visibility) / 1000)
                                    : glassCoverAccentColor((backplateAlpha * item->visibility) / 1000);
      drawOrbitQuadSolid(item->quad, z - 1, backplateColor);
    }
    if (covers[item->cacheIdx] != NULL && psbbnCoverLoaded[item->cacheIdx] &&
        prepareCollectionCoverTexture(item->cacheIdx)) {
      drawOrbitQuadTexture(covers[item->cacheIdx], item->quad, z,
                           GS_SETREG_RGBA(brightness, brightness, brightness, alpha));
    } else {
      int radius = (int)((item->quad.upperRightX - item->quad.upperLeftX) / 8.0f);
      int showLabel = item->cacheIdx == visualFocus;
      if (radius > 24)
        radius = 24;
      if (radius < 4)
        radius = 4;
      if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL)
        drawOrbitQuadSolid(item->quad, z,
                           glassMissingCoverColor((0x58 * item->visibility) / 1000));
      drawGlassDiamond(item->centerX,
                       item->centerY - (showLabel ? getFontLineHeight() : 0),
                       radius, z + 1,
                       glassMissingCoverDiamondColor((0x48 * item->visibility) / 1000));
      if (showLabel)
        drawTextWindow((int)item->quad.upperLeftX, item->centerY + radius / 2,
                       (int)item->quad.upperRightX, 0, z + 1,
                       glassMissingCoverTextColor(), ALIGN_HCENTER,
                       lunaText("COVER\nUNAVAILABLE"));
    }
  }

  if (visualFocus >= 0) {
    int focusTargetIdx = items[visualFocus].targetIdx;
    visibleTitleIndex = focusTargetIdx;
    int titleLeft = keepoutArea + 34;
    int titleRight = gsGlobal->Width - keepoutArea - 34;
    int titleY = bottom - getFontLineHeight() * 2 - 2;

    formatPSBBNTitle(getTargetByIdx(titles, focusTargetIdx)->name,
                     selectedTitle, titleRight - titleLeft);
    drawTextWindow(titleLeft, titleY, titleRight, 0, 8,
                   FontMainColor, ALIGN_HCENTER, selectedTitle);
    snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", focusTargetIdx + 1, titles->total);
    drawTextWindow(titleLeft, titleY + getFontLineHeight(), titleRight, 0, 8,
                   HeaderTextColor, ALIGN_HCENTER, lineBuffer);
  }
}
