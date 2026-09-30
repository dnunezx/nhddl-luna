// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include <stdio.h>

#define DIV_ROUND(n, d) (n + (d - 1)) / d
#define COVER_ART_RATIO_W 7
#define COVER_ART_RATIO_H 10
#define COVER_ART_MAX_WIDTH 140
#define COVER_ART_LOWER_RESERVE 96
#define COVER_ART_VERTICAL_OFFSET -4
#define OVERLAP_LAYOUT_VERTICAL_OFFSET -8
#define DISC_ART_SIZE 112
#define DISC_ROTATION_PERIOD_MS 30000
#define CLASSIC_SELECTION_GLOW_DURATION_MS 110
#define CLASSIC_GLOW_ROW_SCALE 256

static int coverArtX2;
static int coverArtY2;
static int coverArtX1;
static int coverArtY1;
static int discArtX2;
static int discArtY2;
static int discArtX1;
static int discArtY1;
static int classicArtOverlap;

static void drawClassicGlowLayer(int centerX, int centerY, int radiusX, int radiusY,
                                 int z, uint64_t centerColor) {
  int point;
  const uint64_t transparent = glassPresetColor(0x18, 0xA8, 0xF0, 0x00);

  for (point = 0; point < 16; point++) {
    int next = (point + 1) & 15;
    uint32_t phase = (uint32_t)point << 12;
    uint32_t nextPhase = (uint32_t)next << 12;
    int x1 = centerX + (discWave(phase + (8 << 11)) * radiusX) / 127;
    int y1 = centerY + (discWave(phase) * radiusY) / 127;
    int x2 = centerX + (discWave(nextPhase + (8 << 11)) * radiusX) / 127;
    int y2 = centerY + (discWave(nextPhase) * radiusY) / 127;
    gsKit_prim_triangle_gouraud(gsGlobal, centerX, centerY, x1, y1, x2, y2,
                                z, centerColor, transparent, transparent);
  }
}

void drawPSBBNFocusGlow(int left, int top, int rowRight, int textRight) {
  const int lineHeight = getFontLineHeight();
  const int glowLeft = left - 10;
  int glowRight = textRight + 18;
  int centerX;
  int centerY;
  int radiusX;
  int lobeX;
  int lastLobeX;

  if (textRight > rowRight - 12)
    textRight = rowRight - 12;
  if (glowRight > rowRight)
    glowRight = rowRight;
  if (glowRight < left + 28)
    glowRight = left + 28;
  centerX = (glowLeft + glowRight) / 2;
  centerY = top + lineHeight / 2;
  radiusX = (glowRight - glowLeft) / 2;

  // Layered radial falloff keeps the selected title luminous without a bar,
  // beam, hard edge, or visible horizontal rule.
  drawClassicGlowLayer(centerX, centerY, radiusX, lineHeight, 2,
                       glassPresetColor(0x08, 0x54, 0xA0, 0x28));

  // Overlapping radial lobes carry a consistent glow beneath every visible
  // character while retaining a completely soft, non-rectilinear silhouette.
  lastLobeX = textRight - 12;
  if (lastLobeX < left + 4)
    lastLobeX = left + 4;
  if (lastLobeX > rowRight - 30)
    lastLobeX = rowRight - 30;
  for (lobeX = left + 4; lobeX < lastLobeX; lobeX += 28)
    drawClassicGlowLayer(lobeX, centerY, 30, lineHeight * 3 / 4, 3,
                         glassPresetColor(0x28, 0xB8, 0xF0, 0x24));
  drawClassicGlowLayer(lastLobeX, centerY, 30, lineHeight * 3 / 4, 3,
                       glassPresetColor(0x28, 0xB8, 0xF0, 0x24));
  drawClassicGlowLayer(left + 4, centerY, 18, lineHeight / 2, 4,
                       glassPresetColor(0xE0, 0xFA, 0xFF, 0x40));
}

static void drawFavoriteDot(int centerX, int centerY, int z, uint64_t color) {
  // A five-pixel cross reads as a round dot after native interlaced output.
  gsKit_prim_sprite(gsGlobal, centerX - 1, centerY, centerX + 2, centerY + 1, z, color);
  gsKit_prim_sprite(gsGlobal, centerX, centerY - 1, centerX + 1, centerY + 2, z, color);
}

static void drawFavoriteMarker(int centerX, int centerY, int z) {
  const uint64_t color = GS_SETREG_RGBA(0xE8, 0xEE, 0xF2, 0x68);

  // Two points form the upper base; the centered lower point forms the tip.
  drawFavoriteDot(centerX - 3, centerY - 2, z, color);
  drawFavoriteDot(centerX + 3, centerY - 2, z, color);
  drawFavoriteDot(centerX, centerY + 3, z, color);
}

static uint32_t classicDiscPausedMs;
static uint32_t classicDiscLastFrameMs;

static uint32_t discRotationPhase(uint32_t frameNowMs) {
  return (uint32_t)(((uint64_t)(frameNowMs - classicDiscPausedMs) << 16) /
                    DISC_ROTATION_PERIOD_MS);
}

void holdClassicDiscRotation(uint32_t resumeMs) {
  // Options displays the last library frame as a still image. Keep the next
  // live disc frame at that same angle instead of advancing while hidden.
  classicDiscPausedMs += resumeMs - classicDiscLastFrameMs;
}

static int classicGlowInitialized;
static int classicGlowSelectedDisplayIdx;
static int classicGlowFromRow;
static int classicGlowToRow;
static uint32_t classicGlowStartMs;
static uint32_t classicGlowLastDrawMs;

static int classicGlowEasedProgress(uint32_t now) {
  uint32_t elapsed = now - classicGlowStartMs;
  int progress;

  if (elapsed >= CLASSIC_SELECTION_GLOW_DURATION_MS)
    return 1000;
  progress = (int)((elapsed * 1000ULL) / CLASSIC_SELECTION_GLOW_DURATION_MS);
  // Smoothstep gives the glow a soft start and stop without moving the text.
  return (int)(((int64_t)progress * progress * (3000 - 2 * progress)) / 1000000);
}

static void classicGlowSync(int selectedDisplayIdx, int maxTitlesPerPage,
                            int curPage, uint32_t frameNowMs) {
  const uint32_t now = frameNowMs;
  int currentRow;

  if (selectedDisplayIdx < 0 || maxTitlesPerPage <= 0) {
    classicGlowInitialized = 0;
    return;
  }
  currentRow = selectedDisplayIdx % maxTitlesPerPage;

  if (!classicGlowInitialized ||
      (classicGlowLastDrawMs != 0 && now - classicGlowLastDrawMs > 500)) {
    classicGlowSelectedDisplayIdx = selectedDisplayIdx;
    classicGlowFromRow = currentRow * CLASSIC_GLOW_ROW_SCALE;
    classicGlowToRow = classicGlowFromRow;
    classicGlowStartMs = now;
    classicGlowInitialized = 1;
  } else if (classicGlowSelectedDisplayIdx != selectedDisplayIdx) {
    const int previousPage = classicGlowSelectedDisplayIdx / maxTitlesPerPage;
    int currentPosition = classicGlowToRow;

    if (classicGlowEasedProgress(now) < 1000)
      currentPosition = classicGlowFromRow +
                        ((classicGlowToRow - classicGlowFromRow) *
                         classicGlowEasedProgress(now)) / 1000;
    if (previousPage != curPage)
      currentPosition = currentRow * CLASSIC_GLOW_ROW_SCALE;

    classicGlowSelectedDisplayIdx = selectedDisplayIdx;
    classicGlowFromRow = currentPosition;
    classicGlowToRow = currentRow * CLASSIC_GLOW_ROW_SCALE;
    classicGlowStartMs = now;
  }
  classicGlowLastDrawMs = now;
}

static int classicGlowY(int listStartY, int lineHeight, uint32_t frameNowMs) {
  const int progress = classicGlowEasedProgress(frameNowMs);
  const int row = classicGlowFromRow +
                  ((classicGlowToRow - classicGlowFromRow) * progress) / 1000;
  return listStartY + (row * lineHeight) / CLASSIC_GLOW_ROW_SCALE;
}

void calculateCoverArtGeometry(void) {
  const int top = headerHeight + 8;
  const int bottom = gsGlobal->Height - footerHeight - 8;
  const int availableHeight = bottom - top;
  int coverWidth = gsGlobal->Width * 22 / 100;
  int coverHeight;
  int stackHeight;
  int stackTop;

  if (coverWidth > COVER_ART_MAX_WIDTH)
    coverWidth = COVER_ART_MAX_WIDTH;
  coverHeight = (coverWidth * COVER_ART_RATIO_H) / COVER_ART_RATIO_W;
  coverArtX2 = gsGlobal->Width - keepoutArea - 10;
  coverArtX1 = coverArtX2 - coverWidth;
  discArtX1 = (coverArtX1 + coverArtX2 - DISC_ART_SIZE) / 2;
  discArtX2 = discArtX1 + DISC_ART_SIZE;
  if (classicArtOverlap) {
    // Keep the cover at its established overlap position in NTSC and PAL,
    // then lower only the disc so the cover hides its bottom half.
    const int coverAnchorHeight = DISC_ART_SIZE * 2 / 3;
    stackHeight = coverAnchorHeight + coverHeight;
    stackTop = top + (availableHeight - stackHeight) / 2 + COVER_ART_VERTICAL_OFFSET +
               OVERLAP_LAYOUT_VERTICAL_OFFSET;
    coverArtY1 = stackTop + coverAnchorHeight;
    discArtY1 = coverArtY1 - DISC_ART_SIZE / 2;
  } else {
    // Preserve the original positions exactly when the setting is off.
    stackHeight = coverHeight + COVER_ART_LOWER_RESERVE;
    stackTop = top + (availableHeight - stackHeight) / 2;
    coverArtY1 = stackTop + COVER_ART_VERTICAL_OFFSET;
    const int classicPanelBottom = gsGlobal->Height - footerHeight + 2;
    discArtY1 = (coverArtY1 + coverHeight + classicPanelBottom - DISC_ART_SIZE) / 2;
  }
  coverArtY2 = coverArtY1 + coverHeight;
  discArtY2 = discArtY1 + DISC_ART_SIZE;
}

void setClassicArtOverlap(int overlap) {
  classicArtOverlap = overlap != 0;
  if (gsGlobal != NULL)
    calculateCoverArtGeometry();
}




static void drawDiscOutline(int centerX, int centerY, int radius, int z, uint64_t color) {
  for (int i = 0; i < 32; i++) {
    int next = (i + 1) & 31;
    int x1 = centerX + (discWave((uint32_t)((i + 8) & 31) << 11) * radius) / 127;
    int y1 = centerY + (discWave((uint32_t)i << 11) * radius) / 127;
    int x2 = centerX + (discWave((uint32_t)((next + 8) & 31) << 11) * radius) / 127;
    int y2 = centerY + (discWave((uint32_t)next << 11) * radius) / 127;
    gsKit_prim_line(gsGlobal, x1, y1, x2, y2, z, color);
  }
}


static void drawClassicDisc(GSTEXTURE *disc, uint32_t frameNowMs) {
  const int centerX = (discArtX1 + discArtX2) / 2;
  const int centerY = (discArtY1 + discArtY2) / 2;
  const int radius = DISC_ART_SIZE / 2;
  const int outlineZ = classicArtOverlap ? 3 : 4;
  const int textureZ = classicArtOverlap ? 4 : 6;

  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  if (disc == NULL) {
    const int silhouetteZ = classicArtOverlap ? 4 : 5;
    drawDiscOutline(centerX, centerY, radius, silhouetteZ,
                    glassPresetColor(0x70, 0xB8, 0xD8, 0x22));
    drawDiscOutline(centerX, centerY, 7, silhouetteZ,
                    glassPresetColor(0x90, 0xD0, 0xE8, 0x1C));
    return;
  }

  drawOrbitalDisc(centerX, centerY, radius + 8, classicArtOverlap ? 2 : 3,
                  glassPresetColor(0x44, 0xB8, 0xF0, 0x0D),
                  glassPresetColor(0x28, 0x68, 0xB0, 0x02));
  drawDiscOutline(centerX, centerY, radius + 5, outlineZ,
                  glassPresetColor(0x78, 0xD8, 0xFF, 0x20));
  drawDiscOutline(centerX, centerY, radius + 2, outlineZ,
                  glassPresetColor(0x38, 0x88, 0xC8, 0x18));

  const uint32_t phase = discRotationPhase(frameNowMs);
  const float sine = (float)discWave(phase) / 127.0f;
  const float cosine = (float)discWave(phase + (8 << 11)) / 127.0f;
  const float half = (float)DISC_ART_SIZE / 2.0f;
  const float upperLeftX = centerX + half * (sine - cosine);
  const float upperLeftY = centerY - half * (sine + cosine);
  const float upperRightX = centerX + half * (cosine + sine);
  const float upperRightY = centerY + half * (sine - cosine);
  const float lowerLeftX = centerX - half * (cosine + sine);
  const float lowerLeftY = centerY + half * (cosine - sine);
  const float lowerRightX = centerX + half * (cosine - sine);
  const float lowerRightY = centerY + half * (sine + cosine);

  // Disc labels are decoded to a true RGBA texture. Reject fully transparent
  // texels so their PNG corners remain invisible while the quad rotates.
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;
  gsKit_TexManager_bind(gsGlobal, disc);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_prim_quad_texture(gsGlobal, disc, upperLeftX, upperLeftY, 0.0f, 0.0f, upperRightX, upperRightY, disc->Width - 1, 0.0f,
                          lowerLeftX, lowerLeftY, 0.0f, disc->Height - 1, lowerRightX, lowerRightY, disc->Width - 1,
                          disc->Height - 1, textureZ, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawClassicCoverTexture(GSTEXTURE *cover, int z) {
  gsKit_TexManager_bind(gsGlobal, cover);
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_prim_sprite_texture(gsGlobal, cover, coverArtX1, coverArtY1, 0.0f, 0.0f,
                            coverArtX2, coverArtY2, cover->Width, cover->Height,
                            z, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
}

void drawTitleList(TargetList *titles, int selectedTitleIdx, int maxTitlesPerPage,
                   GSTEXTURE *selectedTitleCover, GSTEXTURE *selectedTitleDisc,
                   const uint8_t *favoriteFlags, int favoritesOnly, int coverPending,
                   int listEntryProgress,
                   uint32_t frameNowMs,
                   const char *nextViewLabel) {

  classicDiscLastFrameMs = frameNowMs;

  int favoriteTotal = lunaNavMarkedCount(favoriteFlags, titles->total);
  int displayTotal = favoritesOnly ? favoriteTotal : titles->total;
  int selectedDisplayIdx = favoritesOnly ? lunaNavMarkedRank(favoriteFlags, titles->total, selectedTitleIdx) : selectedTitleIdx;
  int curPage = (selectedDisplayIdx >= 0) ? selectedDisplayIdx / maxTitlesPerPage : 0;
  int pageCount = DIV_ROUND(displayTotal, maxTitlesPerPage);
  if (pageCount < 1)
    pageCount = 1;

  // Draw header and footer
  int titleY = headerHeight;
  int baseX = keepoutArea + 10;
  drawSharedLibraryBackground(frameNowMs);
  // Borderless translucent fields preserve legibility over the orbital scene
  // without restoring the panel shine or any horizontal/vertical edge rules.
  gsKit_prim_sprite(gsGlobal, keepoutArea, headerHeight, coverArtX1 - 8,
                    gsGlobal->Height - footerHeight + 2, 1,
                    glassPresetColor(0x05, 0x0D, 0x22, 0x4C));
  gsKit_prim_sprite(gsGlobal, coverArtX1 - 4, headerHeight, coverArtX2 + 4,
                    gsGlobal->Height - footerHeight + 2, 1,
                    glassPresetColor(0x05, 0x0D, 0x22, 0x4C));
  drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 3, FontMainColor, ALIGN_HCENTER, "L  U  N  A");
  const int headerY = headerHeight - getFontLineHeight();
  const int classicRight = baseX + getLineWidth("List");
  const int favoriteTextX = classicRight + 22;
  const int favoriteRight = favoriteTextX + getLineWidth("Favorites");
  if (favoritesOnly)
    drawPSBBNFocusGlow(favoriteTextX, headerY, favoriteRight + 16, favoriteRight);
  else
    drawPSBBNFocusGlow(baseX, headerY, classicRight + 16, classicRight);
  drawTextWindow(baseX, headerY, classicRight + 2, 0, 6,
                 favoritesOnly ? HeaderTextColor : FontMainColor, ALIGN_LEFT, "List");
  drawTextWindow(favoriteTextX, headerY, favoriteRight + 2, 0, 6,
                 favoritesOnly ? FontMainColor : HeaderTextColor, ALIGN_LEFT, "Favorites");
  snprintf(lineBuffer, 255, "%d/%d", curPage + 1, pageCount);
  drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 3, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

  // Draw title list
  Target *curTitle = titles->first;
  int displayIdx = 0;
  int listStartY;
  const int listTextX = baseX + (1000 - listEntryProgress) * 64 / 1000;

  titleY += getFontLineHeight() / 2;
  listStartY = titleY;
  classicGlowSync(selectedDisplayIdx, maxTitlesPerPage, curPage, frameNowMs);
  if (favoritesOnly && favoriteTotal == 0) {
    drawTextWindow(listTextX, titleY + getFontLineHeight() * 3, coverArtX1 - 12,
                   titleY + getFontLineHeight() * 5, 6, HeaderTextColor,
                   ALIGN_CENTER, "NO FAVORITES YET");
  }
  while (curTitle != NULL) {
    int rowIdx;
    int titleRight;
    if (favoritesOnly && !favoriteFlags[curTitle->idx]) {
      curTitle = curTitle->next;
      continue;
    }
    rowIdx = favoritesOnly ? displayIdx++ : curTitle->idx;
    // Do not display titles before the current page
    if (rowIdx < maxTitlesPerPage * curPage) {
      goto next;
    }
    // Do not display titles beyond the current page
    if (rowIdx >= maxTitlesPerPage * (curPage + 1)) {
      break;
    }

    titleRight = favoriteFlags[curTitle->idx]
                     ? coverArtX1 - 32
                     : coverArtX1 - 5;

    // Draw title name
    if (selectedTitleIdx == curTitle->idx) {
      const int glowY = classicGlowY(listStartY, getFontLineHeight(), frameNowMs);
      const int selectionRight = coverArtX1 - 12;
      int textRight = listTextX + getLineWidth(curTitle->name);
      if (textRight > titleRight)
        textRight = titleRight;
      drawPSBBNFocusGlow(listTextX, glowY, selectionRight, textRight);
    }
    titleY = drawText(listTextX, titleY, 6, titleRight, 0,
                      ((selectedTitleIdx == curTitle->idx)
                           ? GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80)
                           : HeaderTextColor),
                      curTitle->name);
    if (favoriteFlags[curTitle->idx])
      drawFavoriteMarker(coverArtX1 - 23, titleY - getFontLineHeight() / 2, 7);

  next:
    curTitle = curTitle->next;
  }

  if (classicArtOverlap)
    drawClassicDisc(selectedTitleDisc, frameNowMs);

  const int coverTextureZ = classicArtOverlap ? 6 : 5;
  if (selectedTitleCover == NULL && !coverPending) {
    if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL)
      gsKit_prim_sprite(gsGlobal, coverArtX1, coverArtY1, coverArtX2, coverArtY2,
                        classicArtOverlap ? 6 : 5,
                        GS_SETREG_RGBA(0x04, 0x0C, 0x20, 0x60));
    drawGlassDiamond((coverArtX1 + coverArtX2) / 2, (coverArtY1 + coverArtY2) / 2 - 12, 24,
                     classicArtOverlap ? 7 : 6,
                     glassMissingCoverDiamondColor(0x48));
    drawTextWindow(coverArtX1, coverArtY1, coverArtX2, coverArtY2 + 44,
                   classicArtOverlap ? 7 : 6,
                   glassMissingCoverTextColor(), ALIGN_CENTER,
                   "COVER\nUNAVAILABLE");
  }
  if (selectedTitleCover != NULL)
    drawClassicCoverTexture(selectedTitleCover, coverTextureZ);

  if (!classicArtOverlap)
    drawClassicDisc(selectedTitleDisc, frameNowMs);
}
