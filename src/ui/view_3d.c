// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include <stdio.h>

static int pageBases[GRID_PAGE_BUFFERS];
static int pageComplete[GRID_PAGE_BUFFERS];
static int nextSlots[GRID_PAGE_BUFFERS];
static int selectedArtIdx;

#define CASE_FOCUS_MS 180U
#define CASE_TURN_MS 300U
#define CASE_FOCUS_ZOOM 0.18f
#define CASE_SHIMMER_MS 1200U
#define CASE_SHIMMER_PERIOD_MS 3200U
#define CASE_TITLE_SCROLL_SPEED 30U
#define CASE_TITLE_START_PAUSE_MS 1000U
#define CASE_TITLE_END_PAUSE_MS 1200U
#define CASE_ENTRY_ROW_STAGGER_MS 75U
#define CASE_ENTRY_COLUMN_STAGGER_MS 22U
#define CASE_ENTRY_CASE_MS 360U
#define CASE_ENTRY_PREVIEW_DELAY_MS 170U
#define CASE_ENTRY_PREVIEW_MS 430U
static float caseFocus[CASE_GRID_PAGE_SIZE];
static float caseFocusStart[CASE_GRID_PAGE_SIZE];
static float caseTurn[CASE_GRID_PAGE_SIZE];
static float caseTurnStart[CASE_GRID_PAGE_SIZE];
static uint32_t caseFocusStartMs;
static uint32_t caseEntryStartMs;
static int caseEntryPending;
static int caseFocusPage = -1;
static int caseFocusSlot = -1;

static float caseEntryEase(uint32_t now, uint32_t delay, uint32_t duration) {
  uint32_t elapsed = now - caseEntryStartMs;
  if (elapsed <= delay)
    return 0.0f;
  elapsed -= delay;
  if (elapsed >= duration)
    return 1.0f;
  return lunaNavEase((int)(elapsed * 1000U / duration)) / 1000.0f;
}

// Standard Amaray shell: 135 x 190 x 14 mm, with a 130 x 184 mm insert.
#define CASE_ASPECT (135.0f / 190.0f)
typedef struct { float x, y; } CasePoint;
typedef struct {
  float x, foot, width, height, depth;
  float yawCos, yawSin, pitchCos, pitchSin;
} CaseGeometry;

// View from above and the right: 35 degrees of yaw, 25 degrees of pitch.
// Ease both angles to zero after the selected case finishes zooming.
// Project the shell and the artwork through exactly the same camera.
static CasePoint projectCase(const CaseGeometry *g, float x, float y, float z) {
  x -= g->width * 0.5f;
  y -= g->height;
  float rotatedX = x * g->yawCos + z * g->yawSin;
  float rotatedZ = -x * g->yawSin + z * g->yawCos;
  float rotatedY = y * g->pitchCos - rotatedZ * g->pitchSin;
  float cameraZ = y * g->pitchSin + rotatedZ * g->pitchCos;
  float focal = g->height * 7.0f;
  float scale = focal / (focal + cameraZ);
  return (CasePoint){g->x + rotatedX * scale, g->foot + rotatedY * scale};
}

static void projectCaseOutline(const CaseGeometry *g, CasePoint *points, float z) {
  for (int i = 0; i < 8; i++)
    points[i] = projectCase(g, points[i].x, points[i].y, z);
}

static void caseOutline(CasePoint *p, float x, float y, float w, float h, float r) {
  p[0] = (CasePoint){x + r, y};
  p[1] = (CasePoint){x + w - r, y};
  p[2] = (CasePoint){x + w, y + r};
  p[3] = (CasePoint){x + w, y + h - r};
  p[4] = (CasePoint){x + w - r, y + h};
  p[5] = (CasePoint){x + r, y + h};
  p[6] = (CasePoint){x, y + h - r};
  p[7] = (CasePoint){x, y + r};
}

static void caseFace(const CasePoint *p, int z, uint64_t color) {
  for (int i = 1; i < 7; i++)
    gsKit_prim_triangle_gouraud(gsGlobal, p[0].x, p[0].y, p[i].x, p[i].y,
        p[i + 1].x, p[i + 1].y, z, color, color, color);
}

static void caseQuad(CasePoint a, CasePoint b, CasePoint c, CasePoint d,
                     int z, uint64_t outer, uint64_t inner) {
  gsKit_prim_quad_gouraud(gsGlobal, a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y,
      z, outer, outer, inner, inner);
}

static void updateCaseFocus(int pageBase, int selectedSlot, uint32_t now) {
  if (caseFocusPage != pageBase) {
    for (int i = 0; i < CASE_GRID_PAGE_SIZE; i++) {
      caseFocus[i] = caseFocusStart[i] = 0;
      caseTurn[i] = caseTurnStart[i] = 0;
    }
    caseFocusPage = pageBase;
    caseFocusSlot = -1;
    caseFocusStartMs = now;
  }
  uint32_t elapsed = now - caseFocusStartMs;
  int progress = elapsed >= CASE_FOCUS_MS ? 1000 :
      (int)(elapsed * 1000U / CASE_FOCUS_MS);
  float eased = lunaNavEase(progress) / 1000.0f;
  int turnInProgress = elapsed <= CASE_FOCUS_MS ? 0 :
      elapsed - CASE_FOCUS_MS >= CASE_TURN_MS ? 1000 :
      (int)((elapsed - CASE_FOCUS_MS) * 1000U / CASE_TURN_MS);
  int turnOutProgress = elapsed >= CASE_TURN_MS ? 1000 :
      (int)(elapsed * 1000U / CASE_TURN_MS);
  float turnInEase = lunaNavEase(turnInProgress) / 1000.0f;
  float turnOutEase = lunaNavEase(turnOutProgress) / 1000.0f;
  for (int i = 0; i < CASE_GRID_PAGE_SIZE; i++) {
    float target = i == caseFocusSlot ? 1.0f : 0.0f;
    caseFocus[i] = caseFocusStart[i] + (target - caseFocusStart[i]) * eased;
    float turnEase = target > 0 ? turnInEase : turnOutEase;
    caseTurn[i] = caseTurnStart[i] + (target - caseTurnStart[i]) * turnEase;
  }
  if (caseFocusSlot != selectedSlot) {
    // Retarget both motions from their current values so rapid input stays smooth.
    for (int i = 0; i < CASE_GRID_PAGE_SIZE; i++) {
      caseFocusStart[i] = caseFocus[i];
      caseTurnStart[i] = caseTurn[i];
    }
    caseFocusSlot = selectedSlot;
    caseFocusStartMs = now;
  }
}

void resetCaseGrid(void) {
  releaseGridCovers();
  setGridCaseArtwork(1);
  for (int i = 0; i < GRID_PAGE_BUFFERS; i++) {
    pageBases[i] = -1;
    pageComplete[i] = nextSlots[i] = 0;
  }
  selectedArtIdx = -1;
  caseFocusPage = caseFocusSlot = -1;
  caseEntryPending = 1;
}

static void drawCaseTexture(const CaseGeometry *g, GSTEXTURE *cover, float x, float y,
                            float width, float height, int z) {
  int test = gsGlobal->Test->ATST;
  int ref = gsGlobal->Test->AREF;
  int fail = gsGlobal->Test->AFAIL;
  gsKit_TexManager_bind(gsGlobal, cover);
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  // Subdivide both axes so the GS's affine texture interpolation follows the
  // projected plane without a conspicuous diagonal distortion on the cover.
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 4; col++) {
      float x1 = x + width * col / 4, x2 = x + width * (col + 1) / 4;
      float y1 = y + height * row / 4, y2 = y + height * (row + 1) / 4;
      CasePoint a = projectCase(g, x1, y1, 0), b = projectCase(g, x2, y1, 0);
      CasePoint c = projectCase(g, x1, y2, 0), d = projectCase(g, x2, y2, 0);
      float u1 = (cover->Width - 1) * col / 4.0f;
      float u2 = (cover->Width - 1) * (col + 1) / 4.0f;
      float v1 = (cover->Height - 1) * row / 4.0f;
      float v2 = (cover->Height - 1) * (row + 1) / 4.0f;
      gsKit_prim_quad_texture(gsGlobal, cover,
          a.x, a.y, u1, v1, b.x, b.y, u2, v1,
          c.x, c.y, u1, v2, d.x, d.y, u2, v2, z,
          GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
    }
  }
  gsGlobal->Test->ATST = test;
  gsGlobal->Test->AREF = ref;
  gsGlobal->Test->AFAIL = fail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static float clampCaseX(float x, float width) {
  if (x < 0) return 0;
  return x > width ? width : x;
}

// A skewed highlight follows the projected front like light through the
// clear plastic sleeve. It uses no extra texture or VRAM.
static void drawCaseShimmer(const CaseGeometry *g, int z, float phase,
                            int large) {
  if (phase < 0.0f || phase > 1.0f)
    return;
  float width = g->width;
  float band = width * 0.24f;
  float sweep = -band - width * 0.16f +
      phase * (width + 2.0f * band + width * 0.16f);
  float edge[4] = {sweep, sweep + band * 0.28f,
                   sweep + band * 0.55f, sweep + band};
  int peak = large ? 0x22 : 0x18;
  int alpha[4] = {0, peak / 3, peak, 0};
  for (int i = 0; i < 3; i++) {
    float topLeft = clampCaseX(edge[i], width);
    float topRight = clampCaseX(edge[i + 1], width);
    float bottomLeft = clampCaseX(edge[i] + width * 0.16f, width);
    float bottomRight = clampCaseX(edge[i + 1] + width * 0.16f, width);
    if (topLeft == topRight && bottomLeft == bottomRight)
      continue;
    CasePoint a = projectCase(g, topLeft, 0, 0);
    CasePoint b = projectCase(g, topRight, 0, 0);
    CasePoint c = projectCase(g, bottomLeft, g->height, 0);
    CasePoint d = projectCase(g, bottomRight, g->height, 0);
    uint64_t left = GS_SETREG_RGBA(0xE0, 0xF2, 0xFF, alpha[i]);
    uint64_t right = GS_SETREG_RGBA(0xE0, 0xF2, 0xFF, alpha[i + 1]);
    gsKit_prim_quad_gouraud(gsGlobal, a.x, a.y, b.x, b.y,
        c.x, c.y, d.x, d.y, z, left, right, left, right);
  }
}

static void drawCaseContact(float x, float foot, float width, float height,
                            float slope, int z, int selected) {
  CasePoint a = {x, foot - width * 0.5f * slope};
  CasePoint b = {x + width, foot + width * 0.5f * slope};
  CasePoint c = {x + width * 0.20f, a.y - height * 0.18f};
  CasePoint d = {x + width * 1.15f, b.y - height * 0.18f};
  caseQuad(a, b, c, d, z, GS_SETREG_RGBA(0, 0, 0, 0x28),
      GS_SETREG_RGBA(0, 0, 0, 0));
  static const float ring[12][2] = {
      {1, 0}, {0.866f, 0.5f}, {0.5f, 0.866f}, {0, 1},
      {-0.5f, 0.866f}, {-0.866f, 0.5f}, {-1, 0}, {-0.866f, -0.5f},
      {-0.5f, -0.866f}, {0, -1}, {0.5f, -0.866f}, {0.866f, -0.5f}};
  float center = x + width * 0.52f;
  float radiusX = width * 0.61f, radiusY = width * 0.075f;
  for (int i = 0; i < 12; i++) {
    int j = (i + 1) % 12;
    gsKit_prim_triangle_gouraud(gsGlobal, center, foot + 0.6f,
        center + ring[i][0] * radiusX,
        foot + 0.6f + ring[i][1] * radiusY + ring[i][0] * radiusX * slope,
        center + ring[j][0] * radiusX,
        foot + 0.6f + ring[j][1] * radiusY + ring[j][0] * radiusX * slope,
        z + 1, GS_SETREG_RGBA(0, 0, 0, 0x58),
        GS_SETREG_RGBA(0, 0, 0, 0), GS_SETREG_RGBA(0, 0, 0, 0));
  }
  if (selected)
    gsKit_prim_line(gsGlobal, a.x - 2, a.y + 1, b.x + 2, b.y + 1,
        z + 2, glassPresetColor(0x70, 0xC8, 0xFF, 0x50));
}

// A closed case with a genuinely projected front, top, and right side.
static void drawCase(float x, float y, float width, float height,
                     GSTEXTURE *cover, int selected, int resolved, int large,
                     int rowZ, float turn, float shimmerPhase) {
  CaseGeometry g = {x + width * 0.5f, y + height, width, height,
                    width * (14.0f / 135.0f),
                    0.819152f + 0.180848f * turn, 0.573576f * (1.0f - turn),
                    0.906308f + 0.093692f * turn, 0.422618f * (1.0f - turn)};
  // At thumbnail size, retain enough thickness to distinguish the two faces.
  if (g.depth < 5.0f) g.depth = 5.0f;
  float radius = width * 0.025f, bevel = width * 0.009f;
  if (radius < 1.1f) radius = 1.1f;
  if (bevel < 0.65f) bevel = 0.65f;
  int z = rowZ;
  uint64_t black = GS_SETREG_RGBA(0x12, 0x13, 0x16, 0x80);
  CasePoint front[8], back[8], inner[8], shadow[8];
  caseOutline(front, 0, 0, width, height, radius);
  caseOutline(back, 0, 0, width, height, radius);
  caseOutline(inner, bevel, bevel, width - 2 * bevel,
      height - 2 * bevel, radius - bevel * 0.5f);
  projectCaseOutline(&g, front, 0);
  projectCaseOutline(&g, back, g.depth);
  projectCaseOutline(&g, inner, 0);
  float left = front[0].x, right = left, top = front[0].y, bottom = top;
  for (int i = 0; i < 8; i++) {
    if (front[i].x < left) left = front[i].x;
    if (back[i].x < left) left = back[i].x;
    if (front[i].x > right) right = front[i].x;
    if (back[i].x > right) right = back[i].x;
    if (front[i].y < top) top = front[i].y;
    if (back[i].y < top) top = back[i].y;
    if (front[i].y > bottom) bottom = front[i].y;
  }
  if (selected) {
    caseOutline(shadow, left - 4, top - 4, right - left + 8,
        bottom - top + 8, radius + 3);
    caseFace(shadow, z - 3, glassPresetColor(0x28, 0x88, 0xD8, 0x24));
  }
  CasePoint footLeft = projectCase(&g, 0, height, 0);
  CasePoint footRight = projectCase(&g, width, height, 0);
  float footSlope = (footRight.y - footLeft.y) / (footRight.x - footLeft.x);
  drawCaseContact(footLeft.x, (footLeft.y + footRight.y) * 0.5f,
      footRight.x - footLeft.x, height, footSlope, z - 1, selected);
  caseFace(back, z, GS_SETREG_RGBA(0x0A, 0x0B, 0x0D, 0x80));
  for (int i = 0; i < 4; i++) {
    int j = i + 1;
    uint64_t lit = i < 2 ? GS_SETREG_RGBA(0x60, 0x63, 0x6A, 0x80) :
                            GS_SETREG_RGBA(0x32, 0x35, 0x3D, 0x80);
    caseQuad(front[i], front[j], back[i], back[j], z + 1,
        lit, GS_SETREG_RGBA(0x16, 0x18, 0x1E, 0x80));
  }
  // Both closing seams follow the same 3D shell as the cover.
  CasePoint seamTop = projectCase(&g, width, radius, g.depth * 0.54f);
  CasePoint seamBottom = projectCase(&g, width, height - radius, g.depth * 0.54f);
  CasePoint seamLeft = projectCase(&g, radius, 0, g.depth * 0.54f);
  CasePoint seamRight = projectCase(&g, width - radius, 0, g.depth * 0.54f);
  gsKit_prim_line(gsGlobal, seamTop.x, seamTop.y, seamBottom.x, seamBottom.y,
      z + 2, GS_SETREG_RGBA(5, 6, 8, 0x80));
  gsKit_prim_line(gsGlobal, seamLeft.x, seamLeft.y, seamRight.x, seamRight.y,
      z + 2, GS_SETREG_RGBA(0x14, 0x16, 0x1A, 0x80));
  CasePoint a = projectCase(&g, width, height * 0.44f, g.depth * 0.38f);
  CasePoint b = projectCase(&g, width, height * 0.44f, g.depth * 0.80f);
  CasePoint c = projectCase(&g, width, height * 0.56f, g.depth * 0.38f);
  CasePoint d = projectCase(&g, width, height * 0.56f, g.depth * 0.80f);
  caseQuad(a, b, c, d, z + 3, GS_SETREG_RGBA(6, 7, 9, 0x80),
      GS_SETREG_RGBA(6, 7, 9, 0x80));
  caseFace(front, z + 3, black);
  static const uint8_t edgeLight[8] = {79, 62, 33, 23, 19, 25, 48, 69};
  for (int i = 0; i < 8; i++) {
    int j = (i + 1) & 7;
    int light = edgeLight[i];
    caseQuad(front[i], front[j], inner[i], inner[j], z + 4,
        GS_SETREG_RGBA(light, light + 1, light + 3, 0x80), black);
  }
  float paperX = width * (2.5f / 135.0f);
  float paperY = height * (3.0f / 190.0f);
  float paperWidth = width * (130.0f / 135.0f);
  float paperHeight = height * (184.0f / 190.0f);
  if (cover != NULL) {
    drawCaseTexture(&g, cover, paperX, paperY, paperWidth, paperHeight, z + 5);
  } else {
    a = projectCase(&g, paperX, paperY, 0);
    b = projectCase(&g, paperX + paperWidth, paperY, 0);
    c = projectCase(&g, paperX, paperY + paperHeight, 0);
    d = projectCase(&g, paperX + paperWidth, paperY + paperHeight, 0);
    uint64_t empty = GS_SETREG_RGBA(0x22, 0x26, 0x32, 0x80);
    caseQuad(a, b, c, d, z + 5, empty, empty);
    CasePoint center = projectCase(&g, width * 0.5f, height * 0.5f, 0);
    drawGlassDiamond(center.x, center.y, large ? 16 : 7,
        z + 6, glassMissingCoverDiamondColor(0x60));
    if (large)
      drawTextWindow(left + 4, bottom - height * 0.25f, right - 4, 0,
          z + 6, HeaderTextColor, ALIGN_HCENTER, resolved ? "NO COVER" : "LOADING");
  }
  if (cover != NULL)
    drawCaseShimmer(&g, z + 6, shimmerPhase, large);
  a = projectCase(&g, paperX, paperY, 0);
  b = projectCase(&g, paperX + paperWidth, paperY, 0);
  c = projectCase(&g, paperX, paperY + height * 0.075f, 0);
  d = projectCase(&g, paperX + paperWidth, paperY + height * 0.075f, 0);
  caseQuad(a, b, c, d, z + 7, GS_SETREG_RGBA(0xD0, 0xDC, 0xE8, 7),
      GS_SETREG_RGBA(0xD0, 0xDC, 0xE8, 0));
  c = projectCase(&g, paperX, paperY + paperHeight, 0);
  d = projectCase(&g, paperX + paperWidth, paperY + paperHeight, 0);
  gsKit_prim_line(gsGlobal, a.x, a.y, c.x, c.y, z + 7,
      GS_SETREG_RGBA(0xB0, 0xBA, 0xC8, 0x0B));
  gsKit_prim_line(gsGlobal, b.x, b.y, d.x, d.y, z + 7,
      GS_SETREG_RGBA(0, 0, 0, 0x24));
  if (selected) {
    caseOutline(shadow, left - 2, top - 2, right - left + 4,
        bottom - top + 4, radius + 2);
    uint64_t glow = glassPresetColor(0x70, 0xC8, 0xFF, 0x70);
    for (int i = 0; i < 8; i++) {
      int j = (i + 1) & 7;
      gsKit_prim_line(gsGlobal, shadow[i].x, shadow[i].y,
          shadow[j].x, shadow[j].y, z + 8, glow);
    }
  }
}
void drawCaseGrid(TargetList *titles, int selectedTitleIdx, uint32_t frameNowMs) {
  drawSharedLibraryBackground(frameNowMs);
  if (titles->total <= 0)
    return;
  if (caseEntryPending) {
    caseEntryStartMs = frameNowMs;
    caseEntryPending = 0;
  }
  serviceGridArt();
  int pageBase = selectedTitleIdx / CASE_GRID_PAGE_SIZE * CASE_GRID_PAGE_SIZE;
  updateCaseFocus(pageBase, selectedTitleIdx - pageBase, frameNowMs);
  // No neighboring page is drawn or prefetched. Reuse one texture page so
  // browsing 3D cannot leave three case pages resident in EE RAM and VRAM.
  int buffer = 0;
  if (pageBases[buffer] != pageBase) {
    prepareGridPageBuffer(buffer, pageBase, pageBases, pageComplete, nextSlots);
  }
  Target *target = getTargetByIdx(titles, selectedTitleIdx);
  int selectedReady = refreshGridSelectedCover(target, 0);
  int didLoad = 0;
  if (!pageComplete[buffer])
    pageComplete[buffer] = loadGridPageStep(titles, pageBase, buffer,
        &nextSlots[buffer], selectedTitleIdx - pageBase, &didLoad);
  // Never show the previous game's full-size jacket for a new selection.
  if (selectedReady >= 0)
    selectedArtIdx = selectedTitleIdx;

  const int left = keepoutArea + 10;
  const int gridRight = gsGlobal->Width * 68 / 100;
  const int infoLeft = gridRight + 18;
  const int right = gsGlobal->Width - keepoutArea;
  const int top = headerHeight + 14;
  const int bottom = gsGlobal->Height - footerHeight - 22;
  float cellWidth = (gridRight - left) / (float)CASE_GRID_COLUMNS;
  float cellHeight = (bottom - top - 16) / (float)CASE_GRID_ROWS;
  float height = (cellHeight - 13) * 1.20f;
  float width = height * CASE_ASPECT;
  if (width > cellWidth - 16) {
    width = cellWidth - 16;
    height = width / CASE_ASPECT;
  }
  float rearFoot = top + height * 0.78f + 12;
  float frontFoot = bottom - 10;
  float centerX = (left + gridRight) * 0.5f;
  float shimmerPhase = -1.0f;
  uint32_t focusElapsed = frameNowMs - caseFocusStartMs;
  if (focusElapsed >= CASE_FOCUS_MS + CASE_TURN_MS) {
    uint32_t sweepElapsed = (focusElapsed - CASE_FOCUS_MS - CASE_TURN_MS) %
        CASE_SHIMMER_PERIOD_MS;
    if (sweepElapsed < CASE_SHIMMER_MS)
      shimmerPhase = sweepElapsed / (float)CASE_SHIMMER_MS;
  }
  // Draw rows back to front, then the focused case above its neighbors.
  for (int pass = 0; pass < 2; pass++) {
    for (int slot = 0; slot < CASE_GRID_PAGE_SIZE && pageBase + slot < titles->total; slot++) {
      int selected = pageBase + slot == selectedTitleIdx;
      if (selected != pass) continue;
      int row = slot / CASE_GRID_COLUMNS;
      int column = slot % CASE_GRID_COLUMNS;
      uint32_t entryDelay = row * CASE_ENTRY_ROW_STAGGER_MS +
                            column * CASE_ENTRY_COLUMN_STAGGER_MS;
      float entry = caseEntryEase(frameNowMs, entryDelay, CASE_ENTRY_CASE_MS);
      if (entry <= 0.0f) continue;
      float distance = row / (float)(CASE_GRID_ROWS - 1);
      float rowScale = 0.78f + distance * 0.22f;
      float foot = rearFoot + (frontFoot - rearFoot) * distance +
                   (1.0f - entry) * 24.0f;
      float zoom = 1.0f + CASE_FOCUS_ZOOM * caseFocus[slot];
      float caseHeight = height * rowScale * zoom * (0.82f + 0.18f * entry);
      float caseWidth = caseHeight * CASE_ASPECT;
      float x = centerX + (column - (CASE_GRID_COLUMNS - 1) * 0.5f) *
          cellWidth * rowScale - caseWidth / 2;
      float y = foot - caseHeight;
      drawCase(x, y, caseWidth, caseHeight,
          gridCoverLoaded[buffer][slot] ? gridCoverTextures[buffer][slot] : NULL,
          selected, gridPageSlotReady(buffer, slot), 0,
          selected ? 68 : 8 + row * 16, caseTurn[slot],
          selected ? shimmerPhase : -1.0f);
    }
  }
  int titleLeft = infoLeft + 4;
  int titleRight = right - 4;
  float titleWidth = getLineWidth(target->name);
  if (titleWidth <= titleRight - titleLeft) {
    drawTextWindow(titleLeft, top, titleRight, 0, 6,
                   HeaderTextColor, ALIGN_HCENTER, target->name);
  } else {
    int overflow = (int)(titleWidth - (titleRight - titleLeft) + 0.99f);
    uint32_t travelMs = (uint32_t)((uint64_t)overflow * 1000U /
                                   CASE_TITLE_SCROLL_SPEED);
    if (travelMs == 0) travelMs = 1;
    uint32_t cycleMs = CASE_TITLE_START_PAUSE_MS + CASE_TITLE_END_PAUSE_MS +
                       2 * travelMs;
    uint32_t phase = (frameNowMs - caseFocusStartMs) % cycleMs;
    int scrollX = 0;
    if (phase > CASE_TITLE_START_PAUSE_MS) {
      phase -= CASE_TITLE_START_PAUSE_MS;
      if (phase < travelMs)
        scrollX = (int)((uint64_t)phase * overflow / travelMs);
      else if (phase < travelMs + CASE_TITLE_END_PAUSE_MS)
        scrollX = overflow;
      else
        scrollX = overflow - (int)((uint64_t)(phase - travelMs -
                  CASE_TITLE_END_PAUSE_MS) * overflow / travelMs);
    }
    drawTextMarquee(titleLeft, top, titleRight, 6, HeaderTextColor,
                    target->name, scrollX);
  }
  // Leave two title lines free above the large selected case.
  float previewTop = top + getFontLineHeight() * 2 + 16;
  float previewHeight = (bottom - previewTop - getFontLineHeight() - 10) /
      (1.0f + CASE_ASPECT * 0.16f);
  float previewWidth = previewHeight * CASE_ASPECT;
  if (previewWidth > right - infoLeft - 14) {
    previewWidth = right - infoLeft - 14;
    previewHeight = previewWidth / CASE_ASPECT;
  }
  int slot = selectedTitleIdx - pageBase;
  GSTEXTURE *preview = selectedArtIdx == selectedTitleIdx && selectedReady == 1 &&
      gridSelectedLoaded[0] ? gridSelectedTextures[0] :
      gridCoverLoaded[buffer][slot] ? gridCoverTextures[buffer][slot] : NULL;
  float previewEntry = caseEntryEase(frameNowMs, CASE_ENTRY_PREVIEW_DELAY_MS,
                                     CASE_ENTRY_PREVIEW_MS);
  if (previewEntry > 0.0f) {
    float previewScale = 0.86f + 0.14f * previewEntry;
    float drawnWidth = previewWidth * previewScale;
    float drawnHeight = previewHeight * previewScale;
    drawCase(infoLeft + (right - infoLeft - drawnWidth) / 2,
        previewTop + (previewHeight - drawnHeight) +
            (1.0f - previewEntry) * 28.0f,
        drawnWidth, drawnHeight, preview, 0, selectedReady >= 0, 1, 72,
        caseTurn[slot], shimmerPhase);
  }
  snprintf(lineBuffer, sizeof(lineBuffer), "%d / %d", selectedTitleIdx + 1, titles->total);
  drawTextWindow(infoLeft, previewTop + previewHeight + previewWidth * 0.16f + 10, right, 0,
      6, FontMainColor, ALIGN_HCENTER, lineBuffer);
  snprintf(lineBuffer, sizeof(lineBuffer), "%d / %d",
      pageBase / CASE_GRID_PAGE_SIZE + 1,
      (titles->total + CASE_GRID_PAGE_SIZE - 1) / CASE_GRID_PAGE_SIZE);
  drawTextWindow(left, bottom + 6, gridRight, 0, 6,
      FontMainColor, ALIGN_HCENTER, lineBuffer);
}
