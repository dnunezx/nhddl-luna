// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"

#include <stdio.h>

#define ORBIT_PHASE_UNITS (PSBBN_COVER_CACHE_COUNT * 1000)

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
  gsKit_TexManager_bind(gsGlobal, texture);
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
                    GS_SETREG_RGBA(0x38, 0xA8, 0xF0, alpha));
  }
}

static void drawOrbitFooter(void) {
  int baseY = gsGlobal->Height - footerHeight + 8;
  int circleX = 26;
  int squareX = gsGlobal->Width * 27 / 100;
  int crossX = gsGlobal->Width * 52 / 100;
  int triangleX = gsGlobal->Width * 76 / 100;

  drawIconWindow(circleX, baseY, 0, gsGlobal->Height, 8, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawTextWindow(circleX + getIconWidth(ICON_CIRCLE) + 6, baseY, squareX - 8,
                 gsGlobal->Height, 8, FontMainColor, ALIGN_VCENTER, "Classic");
  drawIconWindow(squareX, baseY, 0, gsGlobal->Height, 8, FontMainColor, ALIGN_CENTER, ICON_SQUARE);
  drawTextWindow(squareX + getIconWidth(ICON_SQUARE) + 6, baseY, crossX - 8,
                 gsGlobal->Height, 8, FontMainColor, ALIGN_VCENTER, "Random");
  drawIconWindow(crossX, baseY, 0, gsGlobal->Height, 8, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(crossX + getIconWidth(ICON_CROSS) + 6, baseY, triangleX - 8,
                 gsGlobal->Height, 8, FontMainColor, ALIGN_VCENTER, "Launch");
  drawIconWindow(triangleX, baseY, 0, gsGlobal->Height, 8, FontMainColor, ALIGN_CENTER, ICON_TRIANGLE);
  drawTextWindow(triangleX + getIconWidth(ICON_TRIANGLE) + 6, baseY,
                 gsGlobal->Width - keepoutArea, gsGlobal->Height, 8,
                 FontMainColor, ALIGN_VCENTER, "Options");
}

void drawOrbit(TargetList *titles, int selectedTitleIdx, GSTEXTURE **covers, int flowOffset,
               int randomActive, uint32_t frameNowMs) {
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

  if (selectedSize > maxSelectedSize)
    selectedSize = maxSelectedSize;

  drawSharedLibraryBackground(frameNowMs);
  drawTextWindow(keepoutArea + 10, headerHeight - getFontLineHeight(),
                 gsGlobal->Width - keepoutArea, 0, 6,
                 HeaderTextColor, ALIGN_LEFT, "ORBIT");
  if (randomActive)
    drawTextWindow(gsGlobal->Width / 2, headerHeight - getFontLineHeight(),
                   gsGlobal->Width - keepoutArea - 8, 0, 6,
                   FontMainColor, ALIGN_RIGHT, "RANDOM SCAN");
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
    int size = baseSize + (focusBoost * lunaNavEase(focusProgress)) / 1000;
    int itemCenterX = centerX + (sine * radiusX) / 127;
    int itemCenterY = centerY + ((depth - 127) * radiusY) / 254;

    items[cacheIdx].quad = orbitCoverQuad(itemCenterX, itemCenterY, size, sine);
    items[cacheIdx].cacheIdx = cacheIdx;
    items[cacheIdx].targetIdx = lunaNavWrap(titles->total,
        selectedTitleIdx + cacheIdx - PSBBN_COVER_CACHE_FOCUS);
    items[cacheIdx].depth = depth;
    items[cacheIdx].emphasis = (focusProgress > depthProgress) ? focusProgress : depthProgress;
    items[cacheIdx].visibility = 300 + (depthProgress * 700) / 1000;
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
    int z;

    if (!item->drawable)
      continue;
    brightness = (0x42 + (item->emphasis * 0x3E) / 1000) * item->visibility / 1000;
    alpha = (0x38 + (item->emphasis * 0x48) / 1000) * item->visibility / 1000;
    z = 4 + ((item->depth + 127) * 2) / 254;

    drawOrbitQuadSolid(item->quad, z - 1,
                       GS_SETREG_RGBA(0x18, 0x78, 0xC8,
                                      (0x24 * item->visibility) / 1000));
    if (covers[item->cacheIdx] != NULL && psbbnCoverLoaded[item->cacheIdx]) {
      drawOrbitQuadTexture(covers[item->cacheIdx], item->quad, z,
                           GS_SETREG_RGBA(brightness, brightness, brightness, alpha));
    } else {
      drawOrbitQuadSolid(item->quad, z,
                         GS_SETREG_RGBA(0x04, 0x14, 0x34,
                                        (0x58 * item->visibility) / 1000));
      if (item->cacheIdx == visualFocus)
      {
        drawTextWindow(item->centerX - item->size / 2,
                       item->centerY - getFontLineHeight(),
                       item->centerX + item->size / 2,
                       item->centerY + getFontLineHeight(), z + 1,
                       HeaderTextColor, ALIGN_CENTER, "ART\nUNAVAILABLE");
      }
    }
  }

  if (visualFocus >= 0) {
    int focusTargetIdx = items[visualFocus].targetIdx;
    int titleLeft = keepoutArea + 34;
    int titleRight = gsGlobal->Width - keepoutArea - 34;
    int titleY = bottom - getFontLineHeight() * 2 - 2;

    formatPSBBNTitle(getTargetByIdx(titles, focusTargetIdx)->name,
                     selectedTitle, titleRight - titleLeft);
    gsKit_prim_sprite(gsGlobal, titleLeft - 6, titleY - 3, titleRight + 6,
                      titleY + getFontLineHeight() * 2 + 3, 7,
                      GS_SETREG_RGBA(0x04, 0x18, 0x38, 0x42));
    drawTextWindow(titleLeft, titleY, titleRight, 0, 8,
                   FontMainColor, ALIGN_HCENTER, selectedTitle);
    snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", focusTargetIdx + 1, titles->total);
    drawTextWindow(titleLeft, titleY + getFontLineHeight(), titleRight, 0, 8,
                   HeaderTextColor, ALIGN_HCENTER, lineBuffer);
  }

  drawOrbitFooter();
}
