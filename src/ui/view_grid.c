// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include <stdio.h>

#define GRID_COVER_INSET 2
#define GRID_CELL_SIZE (GRID_THUMBNAIL_SIZE + 2 * GRID_COVER_INSET)
#define GRID_SELECTOR_GLIDE_MS 190
#define GRID_SELECTOR_IDLE_RESET_MS 500

typedef struct {
  float upperLeftX;
  float upperLeftY;
  float upperRightX;
  float upperRightY;
  float lowerLeftX;
  float lowerLeftY;
  float lowerRightX;
  float lowerRightY;
} GridQuad;

static GridQuad gridFlatQuad(float u1, float v1, float u2, float v2, float left, float top, float right, float bottom) {
  GridQuad quad;

  quad.upperLeftX = left + (right - left) * u1 / 1000.0f;
  quad.upperLeftY = top + (bottom - top) * v1 / 1000.0f;
  quad.upperRightX = left + (right - left) * u2 / 1000.0f;
  quad.upperRightY = quad.upperLeftY;
  quad.lowerLeftX = quad.upperLeftX;
  quad.lowerLeftY = top + (bottom - top) * v2 / 1000.0f;
  quad.lowerRightX = quad.upperRightX;
  quad.lowerRightY = quad.lowerLeftY;
  return quad;
}

static void drawGridQuadSolid(GridQuad quad, int z, uint64_t color) {
  gsKit_prim_quad_gouraud(gsGlobal, quad.upperLeftX, quad.upperLeftY, quad.upperRightX, quad.upperRightY, quad.lowerLeftX,
                          quad.lowerLeftY, quad.lowerRightX, quad.lowerRightY, z, color, color, color, color);
}

static void drawGridQuadOutline(GridQuad quad, int z, uint64_t color) {
  gsKit_prim_line(gsGlobal, quad.upperLeftX, quad.upperLeftY, quad.upperRightX, quad.upperRightY, z, color);
  gsKit_prim_line(gsGlobal, quad.upperRightX, quad.upperRightY, quad.lowerRightX, quad.lowerRightY, z, color);
  gsKit_prim_line(gsGlobal, quad.lowerRightX, quad.lowerRightY, quad.lowerLeftX, quad.lowerLeftY, z, color);
  gsKit_prim_line(gsGlobal, quad.lowerLeftX, quad.lowerLeftY, quad.upperLeftX, quad.upperLeftY, z, color);
}

static void drawGridQuadTexture(GSTEXTURE *texture, GridQuad quad, int z, uint64_t color) {
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
  gsKit_prim_quad_texture(gsGlobal, texture, quad.upperLeftX, quad.upperLeftY, 0.0f, 0.0f, quad.upperRightX, quad.upperRightY,
                          texture->Width - 1, 0.0f, quad.lowerLeftX, quad.lowerLeftY, 0.0f, texture->Height - 1,
                          quad.lowerRightX, quad.lowerRightY, texture->Width - 1, texture->Height - 1, z, color);
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawGridTexture(GSTEXTURE *texture, float x, float y, float size, int z, uint64_t color) {
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
  gsKit_prim_sprite_texture(gsGlobal, texture, x, y, 0.0f, 0.0f, x + size, y + size, texture->Width - 1, texture->Height - 1, z, color);
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawGridPage(TargetList *titles, int pageBase, int buffer, int selectedTitleIdx,
                         float left, float top, float right, float bottom, float offsetX, float offsetY,
                         int opacity, int firstRow, int rowCount) {
  if (pageBase < 0 || opacity <= 0)
    return;

  left += offsetX;
  right += offsetX;
  top += offsetY;
  bottom += offsetY;
  int firstSlot = firstRow * GRID_COLUMNS;
  int finalSlot = (firstRow + rowCount) * GRID_COLUMNS;
  if (firstSlot < 0)
    firstSlot = 0;
  if (finalSlot > GRID_PAGE_SIZE)
    finalSlot = GRID_PAGE_SIZE;
  // The grid spans the same number of pixels in each direction.
  const float inset = GRID_COVER_INSET * 1000.0f / (right - left);

  for (int slot = firstSlot; slot < finalSlot; slot++) {
    int row = slot / GRID_COLUMNS;
    int column = slot % GRID_COLUMNS;
    int targetIdx = pageBase + slot;
    int selected = (targetIdx == selectedTitleIdx && targetIdx < titles->total);
    float u1 = column * 250 + inset;
    float v1 = row * 250 + inset;
    float u2 = (column + 1) * 250 - inset;
    float v2 = (row + 1) * 250 - inset;
    GridQuad quad = gridFlatQuad(u1, v1, u2, v2, left, top, right, bottom);
    GridQuad frame = gridFlatQuad(u1 - 4, v1 - 4, u2 + 4, v2 + 4, left, top, right, bottom);

    if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL ||
        targetIdx >= titles->total || (buffer >= 0 && gridCoverLoaded[buffer][slot]))
      drawGridQuadSolid(frame, 3,
                        glassPresetColor(0x28, 0x68, 0xA8, (0x24 * opacity) / 0x80));
    if (targetIdx >= titles->total) {
      drawGridQuadSolid(quad, 4, GS_SETREG_RGBA(0x02, 0x08, 0x18, (0x38 * opacity) / 0x80));
    } else if (buffer >= 0 && gridCoverLoaded[buffer][slot]) {
      drawGridQuadTexture(gridCoverTextures[buffer][slot], quad, 5,
                          GS_SETREG_RGBA(0x54, 0x64, 0x74, ((selected ? 0x80 : 0x60) * opacity) / 0x80));
    } else {
      float centerX = (quad.upperLeftX + quad.upperRightX + quad.lowerLeftX + quad.lowerRightX) / 4.0f;
      float centerY = (quad.upperLeftY + quad.upperRightY + quad.lowerLeftY + quad.lowerRightY) / 4.0f;
      if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL)
        drawGridQuadSolid(quad, 5, glassMissingCoverColor((0x58 * opacity) / 0x80));
      drawGlassDiamond((int)centerX, (int)centerY - 8, 8, 6,
                       glassMissingCoverDiamondColor((0x48 * opacity) / 0x80));
      if (opacity >= 0x40) {
        snprintf(lineBuffer, sizeof(lineBuffer), "%d", targetIdx + 1);
        drawTextWindow(centerX - 18, centerY + 3, centerX + 18, 0, 6,
                       glassMissingCoverTextColor(), ALIGN_CENTER, lineBuffer);
      }
    }
  }
}

typedef struct {
  int valid;
  int pageBase;
  int targetIndex;
  float startU, startV;
  float currentU, currentV;
  float targetU, targetV;
  uint32_t startMs, lastFrameMs;
} GridSelectorMotion;

static GridSelectorMotion gridSelectorMotion;

int gridSelectorIsMoving(int selectedTitleIdx, int pageBase, uint32_t now) {
  const GridSelectorMotion *motion = &gridSelectorMotion;
  if (!motion->valid || now - motion->lastFrameMs > GRID_SELECTOR_IDLE_RESET_MS)
    return 0;
  if (motion->pageBase != pageBase || motion->targetIndex != selectedTitleIdx)
    return 1;
  return motion->currentU != motion->targetU || motion->currentV != motion->targetV;
}

static void gridSelectorVisualPosition(int selectedTitleIdx, int windowBase, uint32_t now,
                                       float *visualU, float *visualV) {
  int slot = selectedTitleIdx - windowBase;
  float targetU = (slot % GRID_COLUMNS) * 250.0f;
  float targetV = (slot / GRID_COLUMNS) * 250.0f;
  GridSelectorMotion *motion = &gridSelectorMotion;

  if (!motion->valid || motion->pageBase != windowBase ||
      now - motion->lastFrameMs > GRID_SELECTOR_IDLE_RESET_MS) {
    motion->valid = 1;
    motion->pageBase = windowBase;
    motion->targetIndex = selectedTitleIdx;
    motion->startU = motion->currentU = motion->targetU = targetU;
    motion->startV = motion->currentV = motion->targetV = targetV;
    motion->startMs = now;
  } else {
    uint32_t elapsed = now - motion->startMs;
    int progress = (elapsed >= GRID_SELECTOR_GLIDE_MS)
                       ? 1000 : (int)(elapsed * 1000 / GRID_SELECTOR_GLIDE_MS);
    float eased = lunaNavEase(progress) / 1000.0f;
    motion->currentU = motion->startU + (motion->targetU - motion->startU) * eased;
    motion->currentV = motion->startV + (motion->targetV - motion->startV) * eased;

    if (motion->targetIndex != selectedTitleIdx) {
      // Retarget from the current visual position during rapid input.
      motion->targetIndex = selectedTitleIdx;
      motion->startU = motion->currentU;
      motion->startV = motion->currentV;
      motion->targetU = targetU;
      motion->targetV = targetV;
      motion->startMs = now;
    }
  }
  motion->lastFrameMs = now;
  *visualU = motion->currentU;
  *visualV = motion->currentV;
}

static void drawGridHighlight(float cellU, float cellV, float left, float top, float right, float bottom,
                              float offsetX, int opacity) {
  left += offsetX;
  right += offsetX;
  if (gridSelector != NULL) {
    // Place the PNG's bright frame just outside the 64-pixel cover. The cover
    // is drawn afterward, hiding the PNG's center while the border glows.
    const float shiftU = 2.0f * 1000.0f / (right - left);
    GridQuad highlight = gridFlatQuad(cellU - 57 - shiftU, cellV - 57,
                                      cellU + 307 - shiftU,
                                      cellV + 307, left, top, right, bottom);
    drawGridQuadTexture(gridSelector, highlight, 4,
                        GS_SETREG_RGBA(0x80, 0x80, 0x80, opacity));
  } else {
    GridQuad highlight = gridFlatQuad(cellU, cellV, cellU + 250,
                                      cellV + 250, left, top, right, bottom);
    drawGridQuadSolid(highlight, 6, glassPresetColor(0x24, 0x78, 0xD8, (0x30 * opacity) / 0x80));
    drawGridQuadOutline(highlight, 7, glassPresetColor(0xC8, 0xF0, 0xFF, (0x78 * opacity) / 0x80));
  }
}

static void drawGridSelectionPlate(int selectedTitleIdx, int windowBase, float left, float top, float right, float bottom,
                                   float offsetX, int opacity) {
  if (gridSelector != NULL)
    return;
  if (selectedTitleIdx < windowBase || selectedTitleIdx >= windowBase + GRID_PAGE_SIZE)
    return;

  int selectedSlot = selectedTitleIdx - windowBase;
  int plateU = (selectedSlot % GRID_COLUMNS) * 250;
  int plateV = (selectedSlot / GRID_COLUMNS) * 250;
  left += offsetX;
  right += offsetX;

  // Sit just outside the cover quad so the dark-blue plate remains visible
  // as a stable halo behind the selected artwork.
  GridQuad plate = gridFlatQuad(plateU + 2, plateV + 2, plateU + 248, plateV + 248, left, top, right, bottom);
  drawGridQuadSolid(plate, 4, glassPresetColor(0x0C, 0x38, 0x78, (0x38 * opacity) / 0x80));
}

static int gridEntryRowProgress(int entryProgress, int row) {
  int staggered = entryProgress * 13 / 10 - row * 100;
  if (staggered <= 0)
    return 0;
  if (staggered >= 1000)
    return 1000;
  return lunaNavEase(staggered);
}

void drawPSBBNGrid(TargetList *titles, int selectedTitleIdx, int activeWindowBase, int activeWindowBuffer,
                   int incomingWindowBase, int incomingWindowBuffer, int selectedCoverBuffer,
                   int cascadeDirection, int cascadeProgress, int entryProgress,
                   uint32_t frameNowMs, const char *nextViewLabel) {
  const int top = headerHeight + 12;
  const int bottom = gsGlobal->Height - footerHeight - 10;
  const int leftX = keepoutArea + 10;
  const int leftRight = gsGlobal->Width * 49 / 100;
  const float gridRight = leftRight - 5;
  const float gridLeft = gridRight - GRID_COLUMNS * GRID_CELL_SIZE;
  const int rightLeft = leftRight + 24;
  const int rightRight = gsGlobal->Width - keepoutArea;
  const int selectedAreaTop = top + getFontLineHeight() + 8;
  const int selectedAreaBottom = bottom - getFontLineHeight() - 8;
  int selectedSize = rightRight - rightLeft;
  int selectedX;
  int selectedY;
  int displayPageBase = activeWindowBase;
  int pageCount = (titles->total + GRID_PAGE_SIZE - 1) / GRID_PAGE_SIZE;
  char selectedTitle[255];

  if (selectedSize > selectedAreaBottom - selectedAreaTop)
    selectedSize = selectedAreaBottom - selectedAreaTop;
  selectedX = rightLeft + (rightRight - rightLeft - selectedSize) / 2;
  selectedY = selectedAreaTop + (selectedAreaBottom - selectedAreaTop - selectedSize) / 2;
  const float gridTop = selectedY;
  const float gridBottom = gridTop + GRID_ROWS * GRID_CELL_SIZE;

  drawSharedLibraryBackground(frameNowMs);
  drawTextWindow(leftX, headerHeight - getFontLineHeight(), leftRight, 0, 5, HeaderTextColor, ALIGN_LEFT,
                 getGridSaveIconArtwork() ? "SAVE ICONS" : "GRID");

  if (cascadeProgress > 0 && incomingWindowBase >= 0) {
    float travel = (gridRight - gridLeft) / GRID_COLUMNS + 8.0f;
    displayPageBase = incomingWindowBase;

    for (int row = 0; row < GRID_ROWS; row++) {
      int outgoingProgress = lunaNavEase(lunaNavGridCascadeProgress(cascadeProgress, row, 0));
      int incomingProgress = lunaNavEase(lunaNavGridCascadeProgress(cascadeProgress, row, 1));
      float outgoingOffset = ((cascadeDirection > 0) ? -travel : travel) * outgoingProgress / 1000.0f;
      float incomingOffset = ((cascadeDirection > 0) ? travel : -travel) * (1000 - incomingProgress) / 1000.0f;
      int outgoingOpacity = (0x80 * (1000 - outgoingProgress)) / 1000;
      int incomingOpacity = (0x80 * incomingProgress) / 1000;

      drawGridSelectionPlate(selectedTitleIdx, activeWindowBase, gridLeft, gridTop, gridRight, gridBottom,
                             outgoingOffset, outgoingOpacity);
      drawGridSelectionPlate(selectedTitleIdx, incomingWindowBase, gridLeft, gridTop, gridRight, gridBottom,
                             incomingOffset, incomingOpacity);
      // The selector extends beyond its tile, so it can appear at the screen
      // edge before the selected row slides in. Show it once the page settles.
      drawGridPage(titles, activeWindowBase, activeWindowBuffer, selectedTitleIdx, gridLeft, gridTop, gridRight, gridBottom,
                   outgoingOffset, 0.0f, outgoingOpacity, row, 1);
      drawGridPage(titles, incomingWindowBase, incomingWindowBuffer, selectedTitleIdx, gridLeft, gridTop, gridRight, gridBottom,
                   incomingOffset, 0.0f, incomingOpacity, row, 1);
    }
  } else {
    int selectedRow = (selectedTitleIdx - activeWindowBase) / GRID_COLUMNS;
    if (selectedRow < 0)
      selectedRow = 0;
    if (selectedRow >= GRID_ROWS)
      selectedRow = GRID_ROWS - 1;
    int selectedEntry = gridEntryRowProgress(entryProgress, selectedRow);
    float selectedOffset = (1000 - selectedEntry) * 20.0f / 1000.0f;
    int selectedOpacity = 0x80 * selectedEntry / 1000;
    drawGridSelectionPlate(selectedTitleIdx, activeWindowBase, gridLeft, gridTop, gridRight, gridBottom,
                           selectedOffset, selectedOpacity);
    if (selectedTitleIdx >= activeWindowBase && selectedTitleIdx < activeWindowBase + GRID_PAGE_SIZE) {
      float visualU, visualV;
      gridSelectorVisualPosition(selectedTitleIdx, activeWindowBase, frameNowMs, &visualU, &visualV);
      drawGridHighlight(visualU, visualV, gridLeft, gridTop, gridRight, gridBottom,
                        selectedOffset, selectedOpacity);
    }
    if (entryProgress >= 1000) {
      drawGridPage(titles, activeWindowBase, activeWindowBuffer, selectedTitleIdx,
                   gridLeft, gridTop, gridRight, gridBottom,
                   0.0f, 0.0f, 0x80, 0, GRID_ROWS);
    } else {
      for (int row = 0; row < GRID_ROWS; row++) {
        int rowEntry = gridEntryRowProgress(entryProgress, row);
        drawGridPage(titles, activeWindowBase, activeWindowBuffer, selectedTitleIdx,
                     gridLeft, gridTop, gridRight, gridBottom,
                     (1000 - rowEntry) * 20.0f / 1000.0f, 0.0f,
                     0x80 * rowEntry / 1000, row, 1);
      }
    }
  }

  snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", displayPageBase / GRID_PAGE_SIZE + 1, pageCount);
  drawTextWindow(rightLeft, headerHeight - getFontLineHeight(), rightRight, 0, 5, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

  formatPSBBNTitle(getTargetByIdx(titles, selectedTitleIdx)->name, selectedTitle, rightRight - rightLeft);
  drawTextWindow(rightLeft, top - 2, rightRight, 0, 6, FontMainColor, ALIGN_HCENTER, selectedTitle);
  if (getGridSaveIconArtwork()) {
    const int iconSize = selectedSize * 82 / 100;
    const int iconX = rightLeft + (rightRight - rightLeft - iconSize) / 2;
    const int iconY = selectedY + (selectedSize - iconSize) / 2;
    GSTEXTURE *icon = (selectedCoverBuffer >= 0 && saveIconSpinLoaded) ? saveIconSpinTexture :
                      (selectedCoverBuffer >= 0 && gridSelectedLoaded[selectedCoverBuffer]
                           ? gridSelectedTextures[selectedCoverBuffer] : NULL);

    // Draw only the transparent rotating icon, centered with no backing plate.
    if (icon != NULL) {
      drawGridTexture(icon, iconX, iconY, iconSize, 5,
                      GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
    } else {
      drawGlassDiamond(iconX + iconSize / 2, iconY + iconSize / 2 - 12,
                       21, 5, glassMissingCoverDiamondColor(0x48));
      drawTextWindow(iconX, iconY + iconSize / 2 + 12,
                     iconX + iconSize, 0, 5, glassMissingCoverTextColor(),
                     ALIGN_HCENTER, "ICON\nUNAVAILABLE");
    }
  } else if (selectedCoverBuffer >= 0 && gridSelectedLoaded[selectedCoverBuffer]) {
    gsKit_prim_sprite(gsGlobal, selectedX - 3, selectedY - 3, selectedX + selectedSize + 3, selectedY + selectedSize + 3, 3,
                      glassPresetColor(0x28, 0x88, 0xD8, 0x28));
    drawGridTexture(gridSelectedTextures[selectedCoverBuffer], selectedX, selectedY, selectedSize, 5,
                    GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  } else {
    if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL)
      gsKit_prim_sprite(gsGlobal, selectedX, selectedY, selectedX + selectedSize, selectedY + selectedSize, 4,
                        glassMissingCoverColor(0x58));
    drawGlassDiamond(selectedX + selectedSize / 2,
                     selectedY + selectedSize / 2 - getFontLineHeight(), 24, 5,
                     glassMissingCoverDiamondColor(0x48));
    drawTextWindow(selectedX, selectedY + selectedSize / 2 + 12,
                   selectedX + selectedSize, 0, 5,
                   glassMissingCoverTextColor(), ALIGN_HCENTER,
                   (selectedCoverBuffer < 0) ? "LOADING\nICON" :
                   (getGridSaveIconArtwork() ? "ICON\nUNAVAILABLE" : "COVER\nUNAVAILABLE"));
  }
  snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", selectedTitleIdx + 1, titles->total);
  drawTextWindow(rightLeft, selectedY + selectedSize + 4, rightRight, 0, 6, FontMainColor, ALIGN_RIGHT, lineBuffer);

  const ButtonPrompt prompts[] = {
      {ICON_CIRCLE, nextViewLabel}, {ICON_CROSS, "Launch"},
      {ICON_START, "Menu"}, {ICON_TRIANGLE, "Options"}};
  drawPromptBar(20, gsGlobal->Height - footerHeight + 8,
                gsGlobal->Width - 20, gsGlobal->Height, 6, FontMainColor,
                (PromptBar){NULL, prompts, 4});
}
