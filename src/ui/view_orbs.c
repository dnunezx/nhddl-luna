// LUNA visual rendering extracted from gui.c.
#include "ui/view_internal.h"
#include "ui/view_orbs.h"
#include <stdio.h>

// The original seven-orb ellipse expands to twelve points for formations.
#define ORB_ORBIT_COUNT 7
#define ORB_COUNT 12
#define ORB_ORBIT_PERIOD_MS 2200
#define ORB_SPREAD_PERIOD_MS 4300
#define ORB_TILT_PERIOD_MS 6100
#define ORB_DESYNC_PERIOD_MS 5200
#define ORB_CONTRACT_PERIOD_MS 24000
#define ORB_PULSE_PERIOD_MS 1800
#define ORB_SPREAD_PHASE 2600
#define ORB_DESYNC_PHASE 3800
#define ORB_TRAIL_STEP_MS 40
#define ORB_TRAIL_SEGMENTS 6
#define ORB_FORMATION_PERIOD_MS 4600
#define ORB_FORMATION_MORPH_MS 1000
#define ORB_FORMATION_TRAVEL_MS 3600
#define ORB_SQUARE_TRAVEL_MS 9000
#define ORB_INFINITY_WOBBLE_MS 5200
#define ORB_INFINITY_WOBBLE_PIXELS 7.0f
#define ORB_CUBE_ROTATION_MS 8000
#define ORB_OCTAHEDRON_ROTATION_MS 7600
#define ORB_SPHERE_ROTATION_MS 8200
#define ORB_SPEED_SWELL_PERIOD_MS 12000
#define ORB_SELECTION_PULSE_MS 1100
#define ORB_SIZE_PULSE_MS 2400
#define ORB_SELECTION_EVENT_COUNT 12

static int orbsBackgroundStyle;
static GlassColorPreset glassColorPreset = GLASS_COLOR_ORIGINAL;

void setGlassColorPreset(GlassColorPreset preset) {
  glassColorPreset = (preset >= GLASS_COLOR_ORIGINAL && preset < GLASS_COLOR_COUNT)
                        ? preset : GLASS_COLOR_ORIGINAL;
}

GlassColorPreset getGlassColorPreset(void) {
  return glassColorPreset;
}

uint64_t glassPresetColor(int red, int green, int blue, int alpha) {
  static const int palette[GLASS_COLOR_COUNT][3] = {
      {0, 0, 0}, {0xE0, 0xE0, 0xE0}, {0x78, 0x78, 0x78}};
  if (glassColorPreset == GLASS_COLOR_ORIGINAL)
    return GS_SETREG_RGBA(red, green, blue, alpha);
  int brightness = red;
  if (green > brightness)
    brightness = green;
  if (blue > brightness)
    brightness = blue;
  return GS_SETREG_RGBA(palette[glassColorPreset][0] * brightness / 255,
                        palette[glassColorPreset][1] * brightness / 255,
                        palette[glassColorPreset][2] * brightness / 255, alpha);
}

uint64_t glassCoverAccentColor(int alpha) {
  return glassPresetColor(0x18, 0x78, 0xC8, alpha);
}

uint64_t glassMissingCoverColor(int alpha) {
  return GS_SETREG_RGBA(0x04, 0x14, 0x34, alpha);
}

uint64_t glassMissingCoverTextColor(void) {
  if (glassColorPreset == GLASS_COLOR_ORIGINAL)
    return HeaderTextColor;
  if (glassColorPreset == GLASS_COLOR_BLACK)
    return GS_SETREG_RGBA(0xC8, 0xC8, 0xC8, 0x80);
  return glassPresetColor(0xE0, 0xF0, 0xFF, 0x80);
}

uint64_t glassMissingCoverDiamondColor(int alpha) {
  if (glassColorPreset == GLASS_COLOR_BLACK)
    return GS_SETREG_RGBA(0xC8, 0xC8, 0xC8, alpha);
  return glassPresetColor(0x70, 0xD8, 0xFF, alpha);
}

static uint32_t glassStartMs = 0;
static uint32_t orbSelectionEvents[ORB_SELECTION_EVENT_COUNT];
static uint32_t orbLastSelectionEventMs;
static int orbSelectionEventNext;
static int orbSelectionEventCount;
static int orbObservedSelection = -1;

typedef struct {
  float x;
  float y;
  int depth;
} GlassPoint;

typedef struct {
  float yawSine, yawCosine;
  float pitchSine, pitchCosine;
  float rollSine, rollCosine;
} GlassRotation;

static const int glassSin[32] = {0,   25,  49,  71,  90,  106, 117, 125, 127, 125, 117, 106, 90,  71,  49,  25,
                                 0,  -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

#define GLASS_STAR_TILE_SIZE 16
#define GLASS_STAR_ATLAS_WIDTH 64
#define GLASS_STAR_ATLAS_HEIGHT 32

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} GlassStarPixel;

static GSTEXTURE glassStarAtlas;
static GlassStarPixel glassStarAtlasPixels[GLASS_STAR_ATLAS_WIDTH * GLASS_STAR_ATLAS_HEIGHT] __attribute__((aligned(128)));

static uint64_t glassColor(int red, int green, int blue, int alpha, int brightness);
static float orbWave(uint32_t phase);
void drawOrbitalDisc(int centerX, int centerY, int radius, int z, uint64_t centerColor, uint64_t edgeColor);

void resetGlassVisuals(uint32_t startMs) {
  glassStartMs = startMs;
  orbObservedSelection = -1;
  orbSelectionEventCount = 0;
  orbSelectionEventNext = 0;
  orbLastSelectionEventMs = 0;
}

static uint32_t glassElapsedMs(uint32_t frameNowMs) {
  return frameNowMs - glassStartMs;
}

static uint32_t glassPhase(uint32_t elapsedMs, uint32_t periodMs, uint32_t offsetMs) {
  return (uint32_t)((((uint64_t)(elapsedMs + offsetMs)) << 16) / periodMs);
}

static int glassStarEdgeAlpha(float x, int width, int fadeWidth) {
  float distance;

  if (x < 0)
    distance = x + fadeWidth;
  else if (x >= width)
    distance = width + fadeWidth - x;
  else {
    distance = x;
    if ((width - 1 - x) < distance)
      distance = width - 1 - x;
  }

  if (distance <= 0)
    return 0;
  if (distance >= fadeWidth)
    return 255;
  return (int)(distance * 255.0f / fadeWidth);
}

static int clampColor(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return value;
}

static int glassIntegerSqrt(int value) {
  int root = 0;
  while ((root + 1) * (root + 1) <= value)
    root++;
  return root;
}

static void compositeGlassStarDisc(GlassStarPixel *pixel, int distance, int radius, int red, int green, int blue,
                                   int centerAlpha) {
  if (distance >= radius)
    return;

  const int sourceAlpha = centerAlpha * (radius - distance) / radius;
  const int oldAlpha = pixel->alpha;
  const int combinedAlpha = sourceAlpha + (oldAlpha * (0x80 - sourceAlpha) + 0x40) / 0x80;
  if (combinedAlpha <= 0)
    return;

  const int denominator = combinedAlpha * 0x80;
  pixel->red = (red * sourceAlpha * 0x80 + pixel->red * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) /
               denominator;
  pixel->green =
      (green * sourceAlpha * 0x80 + pixel->green * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) / denominator;
  pixel->blue =
      (blue * sourceAlpha * 0x80 + pixel->blue * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) / denominator;
  pixel->alpha = combinedAlpha;
}

void initGlassStarAtlas(void) {
  static const int starRed[3] = {0x82, 0xA4, 0xC6};
  static const int starGreen[3] = {0xAA, 0xC4, 0xDE};
  static const int starBlue[3] = {0xD0, 0xE2, 0xF4};
  static const int discBrightness[4] = {-28, -12, 12, 32};
  static const int discAlpha[4] = {0x80 / 7, 0x80 / 3, 0x80, 0x80 / 2};
  // Normalized radii preserve the two existing star profiles: 4/2/1/1 and 5/3/2/1.
  static const int discRadius[2][4] = {{128, 64, 32, 32}, {128, 77, 51, 26}};

  for (int i = 0; i < GLASS_STAR_ATLAS_WIDTH * GLASS_STAR_ATLAS_HEIGHT; i++) {
    glassStarAtlasPixels[i].red = 0;
    glassStarAtlasPixels[i].green = 0;
    glassStarAtlasPixels[i].blue = 0;
    glassStarAtlasPixels[i].alpha = 0;
  }

  for (int sizeIndex = 0; sizeIndex < 2; sizeIndex++) {
    for (int layer = 0; layer < 3; layer++) {
      const int tileX = layer * GLASS_STAR_TILE_SIZE;
      const int tileY = sizeIndex * GLASS_STAR_TILE_SIZE;
      for (int y = 0; y < GLASS_STAR_TILE_SIZE; y++) {
        for (int x = 0; x < GLASS_STAR_TILE_SIZE; x++) {
          GlassStarPixel *pixel = &glassStarAtlasPixels[(tileY + y) * GLASS_STAR_ATLAS_WIDTH + tileX + x];
          const int dx = (x * 2 + 1 - GLASS_STAR_TILE_SIZE) * 8;
          const int dy = (y * 2 + 1 - GLASS_STAR_TILE_SIZE) * 8;
          const int distance = glassIntegerSqrt(dx * dx + dy * dy);

          // Give transparent edge texels the outer glow color to avoid dark linear-filter fringes.
          pixel->red = clampColor(starRed[layer] + discBrightness[0]);
          pixel->green = clampColor(starGreen[layer] + discBrightness[0]);
          pixel->blue = clampColor(starBlue[layer] + discBrightness[0]);
          for (int disc = 0; disc < 4; disc++)
            compositeGlassStarDisc(pixel, distance, discRadius[sizeIndex][disc],
                                   clampColor(starRed[layer] + discBrightness[disc]),
                                   clampColor(starGreen[layer] + discBrightness[disc]),
                                   clampColor(starBlue[layer] + discBrightness[disc]), discAlpha[disc]);
        }
      }
    }
  }

  glassStarAtlas.Width = GLASS_STAR_ATLAS_WIDTH;
  glassStarAtlas.Height = GLASS_STAR_ATLAS_HEIGHT;
  glassStarAtlas.PSM = GS_PSM_CT32;
  glassStarAtlas.ClutPSM = 0;
  glassStarAtlas.TBW = 0;
  glassStarAtlas.Mem = (u32 *)glassStarAtlasPixels;
  glassStarAtlas.Clut = NULL;
  glassStarAtlas.Vram = 0;
  glassStarAtlas.VramClut = 0;
  glassStarAtlas.Filter = GS_FILTER_LINEAR;
  glassStarAtlas.ClutStorageMode = 0;
  glassStarAtlas.Delayed = GS_SETTING_ON;
  gsKit_TexManager_bind(gsGlobal, &glassStarAtlas);
}

static void drawGlassStarDisc(int x, int y, int size, int red, int green, int blue, int alpha, int brightness) {
  drawOrbitalDisc(x, y, size, 0, glassColor(red, green, blue, alpha, brightness),
                  glassColor(red, green, blue, 0, brightness));
}

static void drawGlassStarSprite(int x, int y, int size, int red, int green, int blue, int alpha) {
  if (alpha <= 0)
    return;
  drawGlassStarDisc(x, y, size + 3, red, green, blue, alpha / 7, -28);
  drawGlassStarDisc(x, y, size + 1, red, green, blue, alpha / 3, -12);
  drawGlassStarDisc(x, y, size, red, green, blue, alpha, 12);
  drawGlassStarDisc(x, y, 1, red, green, blue, alpha / 2, 32);
}

static void drawGlassBackgroundStarSprite(float x, float y, int size, int layer, int alpha) {
  if (alpha <= 0)
    return;
  if (alpha > 0x80)
    alpha = 0x80;

  const int radius = size + 3;
  const int tileX = layer * GLASS_STAR_TILE_SIZE;
  const int tileY = (size - 1) * GLASS_STAR_TILE_SIZE;
  gsKit_prim_sprite_texture(gsGlobal, &glassStarAtlas, x - radius, y - radius, tileX, tileY, x + radius, y + radius,
                            tileX + GLASS_STAR_TILE_SIZE - 1, tileY + GLASS_STAR_TILE_SIZE - 1, 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha));
}

static uint64_t glassColor(int red, int green, int blue, int alpha, int brightness) {
  return glassPresetColor(clampColor(red + brightness), clampColor(green + brightness),
                          clampColor(blue + brightness), alpha);
}

void drawGlassDiamond(int centerX, int centerY, int radius, int z, uint64_t color) {
  gsKit_prim_line(gsGlobal, centerX, centerY - radius, centerX + radius, centerY, z, color);
  gsKit_prim_line(gsGlobal, centerX + radius, centerY, centerX, centerY + radius, z, color);
  gsKit_prim_line(gsGlobal, centerX, centerY + radius, centerX - radius, centerY, z, color);
  gsKit_prim_line(gsGlobal, centerX - radius, centerY, centerX, centerY - radius, z, color);
}

void drawGlassPanel(int x1, int y1, int x2, int y2, int z) {
  const uint64_t glass = glassPresetColor(0x05, 0x0D, 0x22, 0x54);
  const uint64_t glassInner = glassPresetColor(0x18, 0x46, 0x70, 0x14);
  const uint64_t shine = glassPresetColor(0x88, 0xD8, 0xFF, 0x4A);
  const uint64_t edge = glassPresetColor(0x24, 0x68, 0x98, 0x38);

  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, z, glass);
  gsKit_prim_sprite(gsGlobal, x1 + 2, y1 + 2, x2 - 2, y1 + 5, z + 1, glassInner);
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y1 + 1, z + 2, shine);
  gsKit_prim_sprite(gsGlobal, x1, y1, x1 + 1, y2, z + 2, shine);
  gsKit_prim_sprite(gsGlobal, x1, y2 - 1, x2, y2, z + 1, edge);
  gsKit_prim_sprite(gsGlobal, x2 - 1, y1, x2, y2, z + 1, edge);
}

static void projectCrystalPointRotated(GlassPoint *point, float centerX, float centerY,
                                       int size, const GlassRotation *rotation,
                                       int sourceX, int sourceY, int sourceZ) {
  const float yawX = sourceX * rotation->yawCosine - sourceZ * rotation->yawSine;
  const float yawZ = sourceX * rotation->yawSine + sourceZ * rotation->yawCosine;
  const float pitchY = sourceY * rotation->pitchCosine - yawZ * rotation->pitchSine;
  const float pitchZ = sourceY * rotation->pitchSine + yawZ * rotation->pitchCosine;
  const float rollX = yawX * rotation->rollCosine - pitchY * rotation->rollSine;
  const float rollY = yawX * rotation->rollSine + pitchY * rotation->rollCosine;
  const float perspective = 640.0f - pitchZ;

  point->x = centerX + rollX * size * 640.0f / (127.0f * perspective);
  point->y = centerY - rollY * size * 640.0f / (127.0f * perspective);
  point->depth = (int)pitchZ;
}

static void projectCrystalPoint(GlassPoint *point, float centerX, float centerY, int size,
                                uint32_t yawPhase, uint32_t pitchPhase, uint32_t rollPhase,
                                int sourceX, int sourceY, int sourceZ) {
  const GlassRotation rotation = {
      orbWave(yawPhase) / 127.0f, orbWave(yawPhase + (8 << 11)) / 127.0f,
      orbWave(pitchPhase) / 127.0f, orbWave(pitchPhase + (8 << 11)) / 127.0f,
      orbWave(rollPhase) / 127.0f, orbWave(rollPhase + (8 << 11)) / 127.0f};
  projectCrystalPointRotated(point, centerX, centerY, size, &rotation,
                             sourceX, sourceY, sourceZ);
}

static void drawGlassCube(float centerX, float centerY, int size, uint32_t yawPhase, int red, int green, int blue,
                          int stableOutline) {
  // Clean-room crystal renderer based only on observation of the stock System
  // Configuration animation: a tumbling translucent shell around a dark core.
  static const int source[8][3] = {{-127, -127, -127}, {127, -127, -127}, {127, 127, -127}, {-127, 127, -127},
                                    {-127, -127, 127},  {127, -127, 127},  {127, 127, 127},  {-127, 127, 127}};
  static const int faceVertices[6][4] = {{3, 2, 0, 1}, {7, 6, 4, 5}, {3, 7, 0, 4},
                                         {2, 6, 1, 5}, {3, 2, 7, 6}, {0, 1, 4, 5}};
  static const int faceShade[6] = {-18, 8, -26, -4, 28, -34};
  static const int edgeVertices[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const uint32_t pitchPhase = ((yawPhase * 5) / 7) + (5 << 11);
  const uint32_t rollPhase = ((yawPhase * 3) / 11) + (2 << 11);
  const GlassRotation rotation = {
      orbWave(yawPhase) / 127.0f, orbWave(yawPhase + (8 << 11)) / 127.0f,
      orbWave(pitchPhase) / 127.0f, orbWave(pitchPhase + (8 << 11)) / 127.0f,
      orbWave(rollPhase) / 127.0f, orbWave(rollPhase + (8 << 11)) / 127.0f};
  GlassPoint shell[8];
  GlassPoint core[8];
  int faceOrder[6] = {0, 1, 2, 3, 4, 5};
  int faceDepth[6];

  for (int i = 0; i < 8; i++) {
    projectCrystalPointRotated(&shell[i], centerX, centerY, size, &rotation,
                               source[i][0], source[i][1], source[i][2]);
    projectCrystalPointRotated(&core[i], centerX, centerY, (size * 68) / 100,
                               &rotation, source[i][0], source[i][1], source[i][2]);
  }

  for (int face = 0; face < 6; face++) {
    faceDepth[face] = 0;
    for (int vertex = 0; vertex < 4; vertex++)
      faceDepth[face] += shell[faceVertices[face][vertex]].depth;
  }

  // Draw rear faces first so the transparent front faces retain their depth.
  for (int i = 0; i < 5; i++) {
    for (int j = i + 1; j < 6; j++) {
      if (faceDepth[faceOrder[i]] > faceDepth[faceOrder[j]]) {
        int swap = faceOrder[i];
        faceOrder[i] = faceOrder[j];
        faceOrder[j] = swap;
      }
    }
  }

  // The low-alpha shell remains visible behind the central smoked volume.
  for (int order = 0; order < 6; order++) {
    int face = faceOrder[order];
    int a = faceVertices[face][0];
    int b = faceVertices[face][1];
    int c = faceVertices[face][2];
    int d = faceVertices[face][3];
    int shade = faceShade[face] + faceDepth[face] / 18;
    uint64_t light = glassColor(red, green, blue, 0x25, shade + 34);
    uint64_t mid = glassColor(red, green, blue, 0x1D, shade + 8);
    uint64_t dark = glassColor(red, green, blue, 0x16, shade - 24);
    gsKit_prim_quad_gouraud(gsGlobal, shell[a].x, shell[a].y, shell[b].x, shell[b].y, shell[c].x, shell[c].y, shell[d].x,
                            shell[d].y, 0, light, mid, dark, mid);
  }

  // A dark inner cube creates the stock crystal's dense central volume.
  for (int order = 0; order < 6; order++) {
    int face = faceOrder[order];
    int a = faceVertices[face][0];
    int b = faceVertices[face][1];
    int c = faceVertices[face][2];
    int d = faceVertices[face][3];
    int shade = faceShade[face] + faceDepth[face] / 22;
    uint64_t light = glassColor(red / 2, green / 2, blue / 2, 0x42, shade + 8);
    uint64_t mid = glassColor(red / 3, green / 3, blue / 3, 0x48, shade - 12);
    uint64_t dark = glassColor(red / 4, green / 4, blue / 4, 0x50, shade - 28);
    gsKit_prim_quad_gouraud(gsGlobal, core[a].x, core[a].y, core[b].x, core[b].y, core[c].x, core[c].y, core[d].x, core[d].y, 0,
                            light, mid, dark, mid);
  }

  // Keep the large loading cube's outline subdued so interlaced output does
  // not turn its moving one-pixel edges into a white shimmer.
  for (int i = 0; i < 12; i++) {
    int a = edgeVertices[i][0];
    int b = edgeVertices[i][1];
    int edgeDepth = (shell[a].depth + shell[b].depth) / 2;
    int edgeAlpha = stableOutline ? 0x30 + (edgeDepth + 180) / 14 : 0x38 + (edgeDepth + 180) / 8;
    int edgeBrightness = stableOutline ? 18 + (edgeDepth + 180) / 12 : 46 + (edgeDepth + 180) / 5;
    int maxAlpha = stableOutline ? 0x50 : 0x78;
    if (edgeAlpha > maxAlpha)
      edgeAlpha = maxAlpha;
    gsKit_prim_line(gsGlobal, shell[a].x, shell[a].y, shell[b].x, shell[b].y, 0,
                    glassColor(red, green, blue, edgeAlpha, edgeBrightness));
  }

  if (!stableOutline) {
    int nearest = 0;
    for (int i = 1; i < 8; i++) {
      if (shell[i].depth > shell[nearest].depth)
        nearest = i;
    }
    drawGlassStarSprite((int)(shell[nearest].x + 0.5f), (int)(shell[nearest].y + 0.5f), 1, clampColor(red + 90),
                        clampColor(green + 90), clampColor(blue + 90), 0x58);
  }
}

void drawOrbitalDisc(int centerX, int centerY, int radius, int z, uint64_t centerColor, uint64_t edgeColor) {
  for (int i = 0; i < 32; i++) {
    int next = (i + 1) & 31;
    int x1 = centerX + (glassSin[(i + 8) & 31] * radius) / 127;
    int y1 = centerY + (glassSin[i] * radius) / 127;
    int x2 = centerX + (glassSin[(next + 8) & 31] * radius) / 127;
    int y2 = centerY + (glassSin[next] * radius) / 127;
    gsKit_prim_triangle_gouraud(gsGlobal, centerX, centerY, x1, y1, x2, y2, z, centerColor, edgeColor, edgeColor);
  }
}

// Cubic interpolation keeps the orb path and its velocity smooth between the
// 32 entries in the shared sine table. gsKit accepts fractional coordinates.
static float orbWave(uint32_t phase) {
  const int index = (phase >> 11) & 31;
  const float p0 = glassSin[(index + 31) & 31];
  const float p1 = glassSin[index];
  const float p2 = glassSin[(index + 1) & 31];
  const float p3 = glassSin[(index + 2) & 31];
  const float t = (phase & 2047) / 2048.0f;
  return 0.5f * ((2.0f * p1) +
                 t * ((p2 - p0) +
                      t * ((2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) +
                           t * (-p0 + 3.0f * p1 - 3.0f * p2 + p3))));
}

static void orbSelectionChanged(int selectedTitleIdx, uint32_t elapsedMs) {
  if (orbObservedSelection < 0) {
    orbObservedSelection = selectedTitleIdx;
    return;
  }
  if (selectedTitleIdx == orbObservedSelection)
    return;
  orbObservedSelection = selectedTitleIdx;
  // Held navigation can advance quickly; keep a short burst readable.
  if (orbSelectionEventCount > 0 &&
      elapsedMs - orbLastSelectionEventMs < 120)
    return;
  orbSelectionEvents[orbSelectionEventNext] = elapsedMs;
  orbSelectionEventNext = (orbSelectionEventNext + 1) % ORB_SELECTION_EVENT_COUNT;
  if (orbSelectionEventCount < ORB_SELECTION_EVENT_COUNT)
    orbSelectionEventCount++;
  orbLastSelectionEventMs = elapsedMs;
}

void observeOrbSelection(int selectedTitleIdx, uint32_t now) {
  orbSelectionChanged(selectedTitleIdx, glassElapsedMs(now));
}

static float orbSelectionPulse(uint32_t elapsedMs) {
  float strongest = 0.0f;
  for (int i = 0; i < orbSelectionEventCount; i++) {
    if (elapsedMs < orbSelectionEvents[i])
      continue;
    const uint32_t age = elapsedMs - orbSelectionEvents[i];
    if (age >= ORB_SELECTION_PULSE_MS)
      continue;
    const float pulse = orbWave((age * 32768U) /
                                ORB_SELECTION_PULSE_MS) / 127.0f;
    if (pulse > strongest)
      strongest = pulse;
  }
  return strongest;
}

static uint32_t orbAnimationMs(uint32_t elapsedMs) {
  // A slow phase swell changes speed smoothly without jumping between paths.
  float animated = (float)elapsedMs +
                   orbWave(glassPhase(elapsedMs, ORB_SPEED_SWELL_PERIOD_MS, 0)) *
                       420.0f / 127.0f;
  float selectionAdvance = 0.0f;
  for (int i = 0; i < orbSelectionEventCount; i++) {
    if (elapsedMs < orbSelectionEvents[i])
      continue;
    const uint32_t age = elapsedMs - orbSelectionEvents[i];
    if (age < ORB_SELECTION_PULSE_MS)
      selectionAdvance += orbWave((age * 32768U) /
                                   ORB_SELECTION_PULSE_MS) * 110.0f / 127.0f;
  }
  if (selectionAdvance > 160.0f)
    selectionAdvance = 160.0f;
  animated += selectionAdvance;
  return animated > 0.0f ? (uint32_t)animated : 0;
}

static void drawOrbGlowDisc(float centerX, float centerY, float radius,
                            int z, uint64_t centerColor, uint64_t edgeColor) {
  const float scale = radius / 127.0f;
  for (int i = 0; i < 16; i++) {
    const int point = i * 2;
    const int next = (point + 2) & 31;
    const float x1 = centerX + glassSin[(point + 8) & 31] * scale;
    const float y1 = centerY + glassSin[point] * scale;
    const float x2 = centerX + glassSin[(next + 8) & 31] * scale;
    const float y2 = centerY + glassSin[next] * scale;
    gsKit_prim_triangle_gouraud(gsGlobal, centerX, centerY, x1, y1, x2, y2,
                                z, centerColor, edgeColor, edgeColor);
  }
}

typedef struct {
  uint32_t orbit;
  float breath;
  float desync;
  float tilt;
  float scale;
} OrbMotion;

typedef struct {
  uint32_t base;
  float spreadWeight;
  float desyncWeight;
} OrbPath;

static OrbMotion orbMotion(uint32_t elapsedMs) {
  OrbMotion motion;
  motion.orbit = glassPhase(elapsedMs, ORB_ORBIT_PERIOD_MS, 0);
  motion.breath = orbWave(glassPhase(elapsedMs, ORB_SPREAD_PERIOD_MS, 0));
  motion.desync = orbWave(glassPhase(elapsedMs, ORB_DESYNC_PERIOD_MS, 0));
  float tiltWave = orbWave(glassPhase(elapsedMs, ORB_TILT_PERIOD_MS, 0));
  if (tiltWave < 0)
    tiltWave = -tiltWave;
  motion.tilt = 26.0f + (tiltWave * 74.0f) / 127.0f;
  // Fast grouping rides on a slower swell, as in the reference animation.
  const float slowScale = 80.0f + orbWave(glassPhase(elapsedMs,
                                  ORB_CONTRACT_PERIOD_MS, 0) + (8 << 11)) * 20.0f / 127.0f;
  const float pulseScale = 92.0f + orbWave(glassPhase(elapsedMs,
                                   ORB_PULSE_PERIOD_MS, 0) + (8 << 11)) * 8.0f / 127.0f;
  motion.scale = slowScale * pulseScale / 100.0f;
  return motion;
}

static void orbPosition(const OrbPath *path, const OrbMotion *motion,
                        int centerX, int centerY, int radiusX, int radiusY,
                        float *x, float *y, float *depth) {
  const int spread = (int)(path->spreadWeight * motion->breath *
                           ORB_SPREAD_PHASE / (127.0f * 127.0f));
  const uint32_t phaseX = motion->orbit + path->base + spread;
  const int offsetY = (int)(path->desyncWeight * motion->desync *
                            ORB_DESYNC_PHASE / (127.0f * 127.0f));
  const uint32_t phaseY = phaseX + offsetY;
  if (depth != NULL)
    *depth = (orbWave(phaseX) + 127.0f) / 2.0f;
  *x = centerX + orbWave(phaseX + (8 << 11)) * radiusX * motion->scale / (127.0f * 100.0f);
  *y = centerY + orbWave(phaseY) * radiusY * motion->tilt * motion->scale / (127.0f * 100.0f * 100.0f);
}

typedef enum {
  ORB_SHAPE_ORBIT,
  ORB_SHAPE_SQUARE,
  ORB_SHAPE_DIAMOND,
  ORB_SHAPE_CUBE,
  ORB_SHAPE_OCTAHEDRON,
  ORB_SHAPE_INFINITY,
  ORB_SHAPE_SPHERE,
  ORB_SHAPE_LUNA,
  ORB_SHAPE_COUNT
} OrbShape;

typedef struct {
  int x1, y1, x2, y2;
} OrbLogoStroke;

// Solid alpha bounds from res/gfx/luna-logo-white.png (710x109). These twelve
// rectangles match its visible pixels except for one antialiased edge pixel.
static const OrbLogoStroke orbLogoStrokes[ORB_COUNT] = {
  {0, 0, 11, 106}, {0, 95, 139, 106},
  {191, 0, 202, 106}, {318, 0, 329, 59}, {191, 95, 329, 106},
  {381, 0, 520, 11}, {381, 47, 393, 106}, {509, 0, 520, 106},
  {572, 0, 710, 11}, {572, 47, 583, 106}, {699, 0, 710, 106},
  {572, 47, 710, 59}
};

typedef struct {
  uint32_t cycle;
  uint32_t random;
  uint32_t startMs;
  OrbShape from;
  OrbShape to;
  int initialized;
} OrbFormation;

static OrbFormation *orbFormationAt(uint32_t elapsedMs) {
  static OrbFormation formation;
  const uint32_t cycle = elapsedMs / ORB_FORMATION_PERIOD_MS;
  if (!formation.initialized || formation.startMs != glassStartMs ||
      cycle < formation.cycle) {
    formation.cycle = 0;
    formation.random = glassStartMs ^ 0xA341316CU;
    formation.startMs = glassStartMs;
    formation.from = ORB_SHAPE_ORBIT;
    formation.to = ORB_SHAPE_ORBIT;
    formation.initialized = 1;
  }
  while (formation.cycle < cycle) {
    formation.random = formation.random * 1664525U + 1013904223U;
    int next = (int)(formation.random % (ORB_SHAPE_COUNT - 1));
    if (next >= formation.to)
      next++;
    formation.from = formation.to;
    formation.to = (OrbShape)next;
    formation.cycle++;
  }
  return &formation;
}

static float orbLogoScale(int radiusX, const OrbMotion *motion) {
  const float breath = 0.85f + motion->scale / 500.0f;
  return radiusX * 2.4f * breath / 710.0f;
}

static void orbInfinityPoint(uint32_t phase, uint32_t elapsedMs,
                              const OrbMotion *motion, int centerX,
                              int centerY, int radiusX, int radiusY,
                              float *x, float *y) {
  const float sine = orbWave(phase) / 127.0f;
  const float cosine = orbWave(phase + 16384U) / 127.0f;
  const float sineDouble = orbWave(phase * 2U) / 127.0f;
  const float cosineDouble = orbWave(phase * 2U + 16384U) / 127.0f;
  const float scale = 0.85f + motion->scale / 500.0f;
  const float tilt = 0.92f + motion->tilt / 1000.0f;
  const float tangentX = 0.94f * radiusX * scale * cosine;
  const float tangentY = 1.10f * radiusY * scale * tilt * cosineDouble;
  const float absX = tangentX < 0.0f ? -tangentX : tangentX;
  const float absY = tangentY < 0.0f ? -tangentY : tangentY;
  const float longer = absX > absY ? absX : absY;
  const float shorter = absX > absY ? absY : absX;
  const float tangentLength = longer + shorter * 0.375f;
  const uint32_t wobblePhase = glassPhase(elapsedMs, ORB_INFINITY_WOBBLE_MS, 0);
  // Fade the wobble at both center crossings to keep the figure eight legible.
  const float displacement = ORB_INFINITY_WOBBLE_PIXELS * sine * sine *
      orbWave(phase * 3U - wobblePhase) / 127.0f;
  *x = centerX + sine * 0.94f * radiusX * scale;
  *y = centerY + sineDouble * 0.55f * radiusY * scale * tilt;
  if (tangentLength > 0.01f) {
    *x -= tangentY * displacement / tangentLength;
    *y += tangentX * displacement / tangentLength;
  }
}

static void orbPatternPosition(OrbShape shape, int index, uint32_t elapsedMs,
                               const OrbMotion *motion, int centerX, int centerY,
                               int radiusX, int radiusY, float *x, float *y,
                               float *depth) {
  if (shape == ORB_SHAPE_ORBIT) {
    OrbPath path;
    path.base = (uint32_t)(((uint64_t)index << 16) /
                           (index < ORB_ORBIT_COUNT ? ORB_ORBIT_COUNT : ORB_COUNT));
    path.spreadWeight = orbWave(path.base + (2 << 11));
    path.desyncWeight = orbWave(path.base);
    orbPosition(&path, motion, centerX, centerY, radiusX, radiusY,
                x, y, depth);
    return;
  }
  if (shape == ORB_SHAPE_LUNA) {
    const OrbLogoStroke *stroke = &orbLogoStrokes[index];
    const float scale = orbLogoScale(radiusX, motion);
    const float left = centerX - 355.0f * scale;
    const float top = centerY - 54.5f * scale;
    const uint32_t phase = glassPhase(elapsedMs, 2200 +
                                      (uint32_t)(index % 4) * 230U,
                                      (uint32_t)index * 170U);
    const float along = (orbWave(phase) + 127.0f) / 254.0f;
    float logoX, logoY;
    if (stroke->x2 - stroke->x1 > stroke->y2 - stroke->y1) {
      logoX = stroke->x1 + (stroke->x2 - stroke->x1) * along;
      logoY = (stroke->y1 + stroke->y2) * 0.5f;
    } else {
      logoX = (stroke->x1 + stroke->x2) * 0.5f;
      logoY = stroke->y1 + (stroke->y2 - stroke->y1) * along;
    }
    *x = left + logoX * scale;
    *y = top + logoY * scale;
    *depth = 65.0f + orbWave(phase + 16384) * 20.0f / 127.0f;
    return;
  }
  if (shape == ORB_SHAPE_CUBE) {
    // Eight corners and four orbs moving on the front-to-back edges.
    static const int points[ORB_COUNT][3] = {
      {-127, -127, -127}, {127, -127, -127},
      { 127,  127, -127}, {-127, 127, -127},
      {-127, -127,  127}, {127, -127,  127},
      { 127,  127,  127}, {-127, 127, 127},
      {-127, -127,    0}, {127, -127,   0},
      { 127,  127,    0}, {-127, 127,   0}
    };
    const int movingZ = index < 8 ? points[index][2] :
                        (int)(orbWave(glassPhase(elapsedMs, 2600,
                                                (uint32_t)(index - 8) * 410U)) *
                              72.0f / 127.0f);
    const uint32_t yaw = glassPhase(elapsedMs, ORB_CUBE_ROTATION_MS, 0);
    const uint32_t pitch = ((yaw * 5) / 7) + (5 << 11);
    const uint32_t roll = ((yaw * 3) / 11) + (2 << 11);
    const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
    GlassPoint point;
    projectCrystalPoint(&point, centerX, centerY,
                        smallerRadius * 65 / 100, yaw, pitch, roll,
                        points[index][0], points[index][1], movingZ);
    const float breath = 0.85f + motion->scale / 500.0f;
    *x = centerX + (point.x - centerX) * breath;
    *y = centerY + (point.y - centerY) * breath;
    *depth = 65.0f + point.depth / 5.0f;
    return;
  }
  if (shape == ORB_SHAPE_OCTAHEDRON) {
    // Six vertices form the double pyramid; six more orbs travel its edges.
    static const int vertices[6][3] = {
      {0, 127, 0}, {0, -127, 0}, {127, 0, 0},
      {-127, 0, 0}, {0, 0, 127}, {0, 0, -127}
    };
    static const int accentEdges[6][2] = {
      {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 5}, {3, 4}
    };
    int source[3];
    if (index < 6) {
      for (int axis = 0; axis < 3; axis++)
        source[axis] = vertices[index][axis];
    } else {
      const int accent = index - 6;
      const int a = accentEdges[accent][0];
      const int b = accentEdges[accent][1];
      const uint32_t accentPhase = glassPhase(elapsedMs, 2600,
                                              (uint32_t)accent * 310U);
      const float along = 0.5f + orbWave(accentPhase) * 0.30f / 127.0f;
      for (int axis = 0; axis < 3; axis++)
        source[axis] = vertices[a][axis] +
                       (int)((vertices[b][axis] - vertices[a][axis]) * along);
    }
    const uint32_t yaw = glassPhase(elapsedMs, ORB_OCTAHEDRON_ROTATION_MS, 0);
    const uint32_t pitch = ((yaw * 4) / 7) + (4 << 11);
    const uint32_t roll = ((yaw * 2) / 9) + (2 << 11);
    const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
    GlassPoint point;
    projectCrystalPoint(&point, centerX, centerY,
                        smallerRadius * 78 / 100, yaw, pitch, roll,
                        source[0], source[1], source[2]);
    const float breath = 0.85f + motion->scale / 500.0f;
    *x = centerX + (point.x - centerX) * breath;
    *y = centerY + (point.y - centerY) * breath;
    *depth = 65.0f + point.depth / 5.0f;
    return;
  }
  if (shape == ORB_SHAPE_SPHERE) {
    // Three latitude bands of four, circulating independently on one globe.
    const int band = index / 4;
    const int slot = index & 3;
    const int ringRadius = band == 1 ? 127 : 103;
    const int latitude = band == 0 ? -74 : band == 2 ? 74 : 0;
    uint32_t phase = glassPhase(elapsedMs, 2800 + band * 280,
                                (uint32_t)band * 190U);
    if (band == 1)
      phase = 0U - phase;
    phase += (uint32_t)slot * 16384U;
    const int sourceX = (int)(orbWave(phase + 16384) * ringRadius / 127.0f);
    const int sourceY = latitude +
                        (band == 1 ? 0 : (band == 0 ? -1 : 1) *
                         (int)(motion->breath * 6.0f / 127.0f));
    const int sourceZ = (int)(orbWave(phase) * ringRadius / 127.0f);
    const uint32_t yaw = glassPhase(elapsedMs, ORB_SPHERE_ROTATION_MS, 0);
    const uint32_t pitch = ((yaw * 3) / 7) + (4 << 11);
    const uint32_t roll = ((yaw * 2) / 9) + (2 << 11);
    const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
    GlassPoint point;
    projectCrystalPoint(&point, centerX, centerY,
                        smallerRadius * 77 / 100, yaw, pitch, roll,
                        sourceX, sourceY, sourceZ);
    const float breath = 0.85f + motion->scale / 500.0f;
    *x = centerX + (point.x - centerX) * breath;
    *y = centerY + (point.y - centerY) * breath;
    *depth = 65.0f + point.depth / 5.0f;
    return;
  }

  const uint32_t basePhase = (uint32_t)(((uint64_t)index << 16) / ORB_COUNT);
  const int spread = (int)(orbWave(basePhase + (2 << 11)) * motion->breath *
                           1100.0f / (127.0f * 127.0f));
  const uint32_t travelMs = shape == ORB_SHAPE_SQUARE ?
                            ORB_SQUARE_TRAVEL_MS : ORB_FORMATION_TRAVEL_MS;
  const uint32_t phase = (glassPhase(elapsedMs, travelMs, 0) +
                          basePhase + spread) & 0xFFFFU;
  float px = 0.0f;
  float py = 0.0f;
  if (shape == ORB_SHAPE_SQUARE || shape == ORB_SHAPE_DIAMOND) {
    const int side = phase >> 14;
    const float along = (phase & 0x3FFFU) / 16384.0f;
    float sx, sy;
    if (side == 0) {
      sx = -0.82f + 1.64f * along;
      sy = -0.82f;
    } else if (side == 1) {
      sx = 0.82f;
      sy = -0.82f + 1.64f * along;
    } else if (side == 2) {
      sx = 0.82f - 1.64f * along;
      sy = 0.82f;
    } else {
      sx = -0.82f;
      sy = 0.82f - 1.64f * along;
    }
    if (shape == ORB_SHAPE_DIAMOND) {
      px = (sx - sy) * 0.82f;
      py = (sx + sy) * 0.82f;
    } else {
      px = sx;
      py = sy;
    }
  } else { // Infinity loop.
    orbInfinityPoint(phase, elapsedMs, motion, centerX, centerY,
                     radiusX, radiusY, x, y);
    *depth = 65.0f + orbWave(phase) * 30.0f / 127.0f;
    return;
  }
  const float scale = 0.85f + motion->scale / 500.0f;
  const float tilt = 0.92f + motion->tilt / 1000.0f;
  *x = centerX + px * radiusX * scale;
  *y = centerY + py * radiusY * scale * tilt;
  *depth = 65.0f + orbWave(phase) * 30.0f / 127.0f;
}

static void orbFormationPosition(const OrbFormation *formation, int index,
                                  uint32_t sampleMs, uint32_t animationMs,
                                  const OrbMotion *motion,
                                  int centerX, int centerY, int radiusX,
                                  int radiusY, float *x, float *y,
                                  float *depth, float *opacity) {
  const uint32_t cycleStart = formation->cycle * ORB_FORMATION_PERIOD_MS;
  float blend = sampleMs <= cycleStart ? 0.0f :
                (sampleMs - cycleStart) / (float)ORB_FORMATION_MORPH_MS;
  if (blend > 1.0f)
    blend = 1.0f;
  blend = blend * blend * (3.0f - 2.0f * blend);
  if (blend <= 0.0f) {
    orbPatternPosition(formation->from, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY, x, y, depth);
  } else if (blend >= 1.0f) {
    orbPatternPosition(formation->to, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY, x, y, depth);
  } else {
    float fromX, fromY, fromDepth, toX, toY, toDepth;
    orbPatternPosition(formation->from, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY,
                       &fromX, &fromY, &fromDepth);
    orbPatternPosition(formation->to, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY,
                       &toX, &toY, &toDepth);
    *x = fromX + (toX - fromX) * blend;
    *y = fromY + (toY - fromY) * blend;
    *depth = fromDepth + (toDepth - fromDepth) * blend;
  }
  const float fromOpacity = index < ORB_ORBIT_COUNT ||
                            formation->from != ORB_SHAPE_ORBIT ? 1.0f : 0.0f;
  const float toOpacity = index < ORB_ORBIT_COUNT ||
                          formation->to != ORB_SHAPE_ORBIT ? 1.0f : 0.0f;
  *opacity = fromOpacity + (toOpacity - fromOpacity) * blend;
}

static void drawOrbTrailSegment(float oldX, float oldY, float newX, float newY,
                                float oldWidth, float newWidth,
                                int z, uint64_t oldColor, uint64_t newColor) {
  const float dx = newX - oldX;
  const float dy = newY - oldY;
  const float absDx = dx < 0 ? -dx : dx;
  const float absDy = dy < 0 ? -dy : dy;
  const float longer = absDx > absDy ? absDx : absDy;
  const float shorter = absDx > absDy ? absDy : absDx;
  const float length = longer + shorter * 0.375f;
  if (length < 0.01f)
    return;
  const float oldOffsetX = -dy * oldWidth / (2.0f * length);
  const float oldOffsetY = dx * oldWidth / (2.0f * length);
  const float newOffsetX = -dy * newWidth / (2.0f * length);
  const float newOffsetY = dx * newWidth / (2.0f * length);
  gsKit_prim_quad_gouraud(gsGlobal,
                          oldX + oldOffsetX, oldY + oldOffsetY,
                          newX + newOffsetX, newY + newOffsetY,
                          oldX - oldOffsetX, oldY - oldOffsetY,
                          newX - newOffsetX, newY - newOffsetY, z,
                          oldColor, newColor, oldColor, newColor);
}

// Fade both width and light along the stroke so a formation reads as an orb
// afterimage rather than a solid bar joining two points.
static void drawOrbTailStroke(float x1, float y1, float x2, float y2,
                              float extent, float width, int alpha, int z) {
  if (extent <= 0.0f || alpha <= 0)
    return;
  if (extent > 1.0f)
    extent = 1.0f;
  const float tipX = x1 + (x2 - x1) * extent;
  const float tipY = y1 + (y2 - y1) * extent;
  for (int piece = 0; piece < 4; piece++) {
    const float from = piece / 4.0f;
    const float to = (piece + 1) / 4.0f;
    const float fromFade = (1.0f - from) * (1.0f - from);
    const float toFade = (1.0f - to) * (1.0f - to);
    const float ax = x1 + (tipX - x1) * from;
    const float ay = y1 + (tipY - y1) * from;
    const float bx = x1 + (tipX - x1) * to;
    const float by = y1 + (tipY - y1) * to;
    drawOrbTrailSegment(ax, ay, bx, by,
                        (width + 4.0f) * fromFade,
                        (width + 4.0f) * toFade, z,
                        glassPresetColor(0x40, 0x78, 0xC8,
                                       (int)(alpha * fromFade * 0.45f)),
                        glassPresetColor(0x40, 0x78, 0xC8,
                                       (int)(alpha * toFade * 0.45f)));
    drawOrbTrailSegment(ax, ay, bx, by,
                        width * fromFade, width * toFade, z,
                        glassPresetColor(0xA0, 0xD8, 0xFF,
                                       (int)(alpha * fromFade)),
                        glassPresetColor(0xA0, 0xD8, 0xFF,
                                       (int)(alpha * toFade)));
  }
}

// Keep the stronger, continuous stroke only for the LUNA wordmark.
static void drawOrbLogoStroke(float x1, float y1, float x2, float y2,
                               float extent, float width, int alpha, int z) {
  if (extent <= 0.0f || alpha <= 0)
    return;
  if (extent > 1.0f)
    extent = 1.0f;
  const float tipX = x1 + (x2 - x1) * extent;
  const float tipY = y1 + (y2 - y1) * extent;
  drawOrbTrailSegment(x1, y1, tipX, tipY, width + 4.0f, width + 2.0f, z,
                      glassPresetColor(0x40, 0x78, 0xC8, alpha / 2),
                      glassPresetColor(0x40, 0x78, 0xC8, alpha / 3));
  drawOrbTrailSegment(x1, y1, tipX, tipY, width, width * 0.65f, z,
                      glassPresetColor(0xA0, 0xD8, 0xFF, alpha),
                      glassPresetColor(0xE0, 0xF0, 0xFF, alpha / 2));
}

static void drawOrbFormationEdges(OrbShape shape, float extent,
                                   int centerX, int centerY, int radiusX,
                                   int radiusY, uint32_t animationMs,
                                   const OrbMotion *motion, int z, int alpha) {
  static const int cubeEdges[][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 8}, {8, 4}, {1, 9}, {9, 5},
    {2, 10}, {10, 6}, {3, 11}, {11, 7}
  };
  static const int octaEdges[][2] = {
    {0, 2}, {0, 3}, {0, 4}, {0, 5},
    {1, 2}, {1, 3}, {1, 4}, {1, 5},
    {2, 4}, {4, 3}, {3, 5}, {5, 2}
  };
  // Their moving afterimages already trace most of the diamond and infinity.
  if (extent <= 0.0f || shape == ORB_SHAPE_ORBIT ||
      shape == ORB_SHAPE_DIAMOND || shape == ORB_SHAPE_INFINITY ||
      shape == ORB_SHAPE_SPHERE || shape == ORB_SHAPE_LUNA)
    return;
  float x[ORB_COUNT], y[ORB_COUNT], depth;
  for (int i = 0; i < ORB_COUNT; i++)
    orbPatternPosition(shape, i, animationMs, motion, centerX, centerY,
                       radiusX, radiusY, &x[i], &y[i], &depth);
  if (shape == ORB_SHAPE_CUBE || shape == ORB_SHAPE_OCTAHEDRON) {
    const int (*edges)[2] = shape == ORB_SHAPE_CUBE ? cubeEdges : octaEdges;
    const int edgeCount = shape == ORB_SHAPE_CUBE ?
                          (int)(sizeof(cubeEdges) / sizeof(cubeEdges[0])) :
                          (int)(sizeof(octaEdges) / sizeof(octaEdges[0]));
    for (int i = 0; i < edgeCount; i++) {
      const int a = edges[i][0], b = edges[i][1];
      drawOrbTailStroke(x[a], y[a], x[b], y[b], extent, 3.5f, alpha, z);
    }
  } else {
    // In the slower square, each moving orb leaves a tail toward the one behind.
    for (int i = 0; i < ORB_COUNT; i++) {
      const int previous = (i + ORB_COUNT - 1) % ORB_COUNT;
      drawOrbTailStroke(x[i], y[i], x[previous], y[previous], extent, 3.5f,
                        alpha, z);
    }
  }
}

static void orbSphereGuidePoint(GlassPoint *point, int ring, int step,
                                 int centerX, int centerY, int size,
                                 uint32_t yaw, uint32_t pitch, uint32_t roll) {
  const uint32_t angle = (uint32_t)step * 4096U;
  const int sine = (int)orbWave(angle);
  const int cosine = (int)orbWave(angle + 16384);
  projectCrystalPoint(point, centerX, centerY, size, yaw, pitch, roll,
                      ring == 0 ? cosine : 0,
                      ring == 0 ? 0 : sine,
                      ring == 0 ? sine : cosine);
}

static void drawOrbSphereGuides(int centerX, int centerY, int radiusX,
                                 int radiusY, uint32_t animationMs,
                                 const OrbMotion *motion, int z, int alpha,
                                 float extent) {
  const uint32_t yaw = glassPhase(animationMs, ORB_SPHERE_ROTATION_MS, 0);
  const uint32_t pitch = ((yaw * 3) / 7) + (4 << 11);
  const uint32_t roll = ((yaw * 2) / 9) + (2 << 11);
  const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
  const int size = smallerRadius * 77 / 100;
  const float breath = 0.85f + motion->scale / 500.0f;
  for (int ring = 0; ring < 2; ring++) {
    GlassPoint a;
    orbSphereGuidePoint(&a, ring, 0, centerX, centerY, size,
                        yaw, pitch, roll);
    for (int step = 1; step <= 16; step++) {
      GlassPoint b;
      orbSphereGuidePoint(&b, ring, step, centerX, centerY, size,
                          yaw, pitch, roll);
      int edgeAlpha = (int)(alpha *
                            (0.55f + (a.depth + b.depth + 254) / 800.0f));
      if (edgeAlpha > 0x50)
        edgeAlpha = 0x50;
      float sectionExtent = extent * 16.0f - (step - 1);
      drawOrbTailStroke(centerX + (a.x - centerX) * breath,
                        centerY + (a.y - centerY) * breath,
                        centerX + (b.x - centerX) * breath,
                        centerY + (b.y - centerY) * breath,
                        sectionExtent, 3.0f, edgeAlpha, z);
      a = b;
    }
  }
}

static void drawOrbLogoWordmark(int centerX, int centerY, int radiusX,
                                 const OrbMotion *motion, int z, int alpha,
                                 float extent) {
  const float scale = orbLogoScale(radiusX, motion);
  const float left = centerX - 355.0f * scale;
  const float top = centerY - 54.5f * scale;
  for (int i = 0; i < ORB_COUNT; i++) {
    const OrbLogoStroke *stroke = &orbLogoStrokes[i];
    const float x1 = left + stroke->x1 * scale;
    const float y1 = top + stroke->y1 * scale;
    const float x2 = left + stroke->x2 * scale;
    const float y2 = top + stroke->y2 * scale;
    const float width = 11.0f * scale;
    if (stroke->x2 - stroke->x1 > stroke->y2 - stroke->y1)
      drawOrbLogoStroke(x1, (y1 + y2) * 0.5f,
                        x2, (y1 + y2) * 0.5f, extent, width, alpha, z);
    else
      drawOrbLogoStroke((x1 + x2) * 0.5f, y1,
                        (x1 + x2) * 0.5f, y2, extent, width, alpha, z);
  }
}

static void drawFormationOrbs(int centerX, int centerY, int radiusX, int radiusY,
                              uint32_t elapsedMs, int glowScale, int trailZ) {
  OrbFormation *formation = orbFormationAt(elapsedMs);
  const float selectionPulse = orbSelectionPulse(elapsedMs);
  OrbMotion motion[ORB_TRAIL_SEGMENTS + 1];
  uint32_t animationTime[ORB_TRAIL_SEGMENTS + 1];
  float positionsX[ORB_COUNT], positionsY[ORB_COUNT];
  float depths[ORB_COUNT], opacities[ORB_COUNT];
  for (int sample = 0; sample <= ORB_TRAIL_SEGMENTS; sample++) {
    const uint32_t age = sample * ORB_TRAIL_STEP_MS;
    animationTime[sample] = orbAnimationMs(elapsedMs > age ? elapsedMs - age : 0);
    motion[sample] = orbMotion(animationTime[sample]);
  }
  for (int i = 0; i < ORB_COUNT; i++)
    orbFormationPosition(formation, i, elapsedMs, animationTime[0], &motion[0],
                         centerX, centerY, radiusX, radiusY,
                         &positionsX[i], &positionsY[i],
                         &depths[i], &opacities[i]);

  // (Cs - 0) * As + Cd: overlapping halos merge into a brighter light.
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  const uint32_t cycleStart = formation->cycle * ORB_FORMATION_PERIOD_MS;
  float blend = elapsedMs <= cycleStart ? 0.0f :
                (elapsedMs - cycleStart) / (float)ORB_FORMATION_MORPH_MS;
  if (blend > 1.0f)
    blend = 1.0f;
  blend = blend * blend * (3.0f - 2.0f * blend);
  // Existing tails show motion. These longer tails grow along each shape's
  // contours and retract during the transition to the next formation.
  const float fromExtent = 1.0f - blend;
  const float toExtent = blend;
  const int edgeAlpha = (int)(0x40 * (1.0f + selectionPulse * 0.25f));
  const int logoAlpha = (int)(0x50 * (1.0f + selectionPulse * 0.25f));
  drawOrbFormationEdges(formation->from, fromExtent,
                        centerX, centerY, radiusX, radiusY,
                        animationTime[0], &motion[0], trailZ,
                        (int)(edgeAlpha * fromExtent));
  drawOrbFormationEdges(formation->to, toExtent,
                        centerX, centerY, radiusX, radiusY,
                        animationTime[0], &motion[0], trailZ,
                        (int)(edgeAlpha * toExtent));
  if (formation->from == ORB_SHAPE_SPHERE)
    drawOrbSphereGuides(centerX, centerY, radiusX, radiusY,
                        animationTime[0], &motion[0], trailZ,
                        (int)(edgeAlpha * fromExtent), fromExtent);
  if (formation->to == ORB_SHAPE_SPHERE)
    drawOrbSphereGuides(centerX, centerY, radiusX, radiusY,
                        animationTime[0], &motion[0], trailZ,
                        (int)(edgeAlpha * toExtent), toExtent);
  if (formation->from == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, radiusX, &motion[0], trailZ,
                        (int)(logoAlpha * fromExtent), fromExtent);
  if (formation->to == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, radiusX, &motion[0], trailZ,
                        (int)(logoAlpha * toExtent), toExtent);
  const float logoStrength =
      (formation->from == ORB_SHAPE_LUNA ? fromExtent : 0.0f) +
      (formation->to == ORB_SHAPE_LUNA ? toExtent : 0.0f);
  for (int i = 0; i < ORB_COUNT; i++) {
    const float x = positionsX[i];
    const float y = positionsY[i];
    const float depth = depths[i];
    const float opacity = opacities[i];
    float newX = x;
    float newY = y;
    float newOpacity = opacity;
    for (int segment = 1; segment <= ORB_TRAIL_SEGMENTS; segment++) {
      float oldX, oldY, oldDepth, oldOpacity;
      const uint32_t age = segment * ORB_TRAIL_STEP_MS;
      orbFormationPosition(formation, i, elapsedMs > age ? elapsedMs - age : 0,
                           animationTime[segment],
                           &motion[segment], centerX, centerY, radiusX, radiusY,
                           &oldX, &oldY, &oldDepth, &oldOpacity);
      const int oldOuterAlpha = (int)((4 + (ORB_TRAIL_SEGMENTS - segment) * 28 /
                                      ORB_TRAIL_SEGMENTS) * oldOpacity);
      const int newOuterAlpha = (int)((4 + (ORB_TRAIL_SEGMENTS - segment + 1) * 28 /
                                      ORB_TRAIL_SEGMENTS) * newOpacity);
      const int oldInnerAlpha = (int)((5 + (ORB_TRAIL_SEGMENTS - segment) * 45 /
                                      ORB_TRAIL_SEGMENTS) * oldOpacity);
      const int newInnerAlpha = (int)((5 + (ORB_TRAIL_SEGMENTS - segment + 1) * 45 /
                                      ORB_TRAIL_SEGMENTS) * newOpacity);
      const int oldOuterWidth = 3 + (ORB_TRAIL_SEGMENTS - segment) * 6 / ORB_TRAIL_SEGMENTS;
      const int newOuterWidth = 3 + (ORB_TRAIL_SEGMENTS - segment + 1) * 6 / ORB_TRAIL_SEGMENTS;
      const int oldInnerWidth = 2 + (ORB_TRAIL_SEGMENTS - segment) * 3 / ORB_TRAIL_SEGMENTS;
      const int newInnerWidth = 2 + (ORB_TRAIL_SEGMENTS - segment + 1) * 3 / ORB_TRAIL_SEGMENTS;
      const float trailScale = 1.0f - logoStrength * 0.4f;
      if (oldOuterAlpha || newOuterAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldOuterWidth * trailScale, newOuterWidth * trailScale, trailZ,
                            glassPresetColor(0x40, 0x78, 0xC8, oldOuterAlpha),
                            glassPresetColor(0x40, 0x78, 0xC8, newOuterAlpha));
      if (oldInnerAlpha || newInnerAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldInnerWidth * trailScale, newInnerWidth * trailScale, trailZ,
                            glassPresetColor(0xA0, 0xD8, 0xFF, oldInnerAlpha),
                            glassPresetColor(0xA0, 0xD8, 0xFF, newInnerAlpha));
      newX = oldX;
      newY = oldY;
      newOpacity = oldOpacity;
    }
    if (opacity <= 0.0f)
      continue;
    const float logoOrbScale = 1.0f - logoStrength * 0.5f;
    const float sizePulse = 1.0f +
        orbWave(glassPhase(elapsedMs,
                           ORB_SIZE_PULSE_MS + (uint32_t)(i % 4) * 170U,
                           (uint32_t)i * 311U)) * 0.32f / 127.0f;
    const float haloRadius = (18.0f + depth / 14.0f) * glowScale / 100.0f *
                             1.12f *
                             logoOrbScale * sizePulse *
                             (1.0f + selectionPulse * 0.22f);
    const float coreRadius = (5.6f + depth / 52.0f) * glowScale / 100.0f *
                             1.12f *
                             logoOrbScale * sizePulse *
                             (1.0f + selectionPulse * 0.12f);
    const int haloAlpha = (int)((0x13 + depth / 13.0f) * 1.20f * opacity *
                                (1.0f + selectionPulse * 0.45f));
    int coreAlpha = (int)((0x50 + depth / 3.0f) * 1.10f * opacity *
                          (1.0f + selectionPulse * 0.22f));
    if (coreAlpha > 0x80)
      coreAlpha = 0x80;

    drawOrbGlowDisc(x, y, haloRadius, trailZ + 1,
                    glassPresetColor(0x70, 0xA8, 0xE8, haloAlpha),
                    glassPresetColor(0x70, 0xA8, 0xE8, 0));
    drawOrbGlowDisc(x, y, coreRadius, trailZ + 1,
                    glassPresetColor(0xE0, 0xF0, 0xFF, coreAlpha),
                    glassPresetColor(0xE0, 0xF0, 0xFF, 0));
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawOrbitalStars(int width, int height, uint32_t elapsedMs) {
  static const int speedPixelsPerSecond[3] = {4, 8, 13};
  const int edgeFadeWidth = 18;
  float scroll[3];

  for (int layer = 0; layer < 3; layer++)
    scroll[layer] = (float)(((uint64_t)elapsedMs * speedPixelsPerSecond[layer]) %
                            ((uint64_t)width * 1000)) / 1000.0f;

  gsKit_TexManager_bind(gsGlobal, &glassStarAtlas);
  for (int i = 0; i < 58; i++) {
    int layer = i % 3;
    int baseX = (i * 97 + i * i * 13 + 31) % width;
    int baseY = (i * 53 + i * i * 7 + 19) % height;
    float y = baseY + orbWave(glassPhase(elapsedMs, 7000 + layer * 1300, i * 113)) * (layer + 1) / 160.0f;
    float x = baseX + scroll[layer];
    if (x >= width)
      x -= width;
    int twinkle = (int)(orbWave(glassPhase(elapsedMs, 2600 + layer * 900, i * 173)) / 14.0f);
    int size = ((layer == 2) && ((i % 5) == 0)) ? 2 : 1;
    int alpha = 0x20 + layer * 0x12;
    int edgeAlpha = (alpha * glassStarEdgeAlpha(x, width, edgeFadeWidth)) / 255;
    drawGlassBackgroundStarSprite(x, y, size, layer, edgeAlpha + twinkle);

    // Cross-fade a duplicate through the opposite edge during wraparound.
    if (x < edgeFadeWidth)
      drawGlassBackgroundStarSprite(x + width, y, size, layer,
                                    (alpha * glassStarEdgeAlpha(x + width, width, edgeFadeWidth)) / 255 + twinkle);
    else if (x >= width - edgeFadeWidth)
      drawGlassBackgroundStarSprite(x - width, y, size, layer,
                                    (alpha * glassStarEdgeAlpha(x - width, width, edgeFadeWidth)) / 255 + twinkle);
  }
}

static void drawColorStarBackground(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  const uint64_t black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);

  gsKit_prim_quad_gouraud(gsGlobal, 0, 0, width, 0, 0, height, width, height, 0, black, black, black, black);

  drawOrbitalStars(width, height, elapsedMs);
}

static void drawGlassBackground(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  const int orbitX = width * 65 / 100;
  const int orbitY = height * 52 / 100;

  drawColorStarBackground(frameNowMs);

  // Two floating glass cubes retain the PS2 BIOS geometry without crowding the library.
  // Share one orbital phase and keep the crystals half a revolution apart.
  // The nested ellipses retain at least 96 pixels of center separation, so
  // their paths cannot collide even at their closest vertical alignment.
  uint32_t orbitPhase = glassPhase(elapsedMs, 36000, 0);
  uint32_t oppositePhase = orbitPhase + (16 << 11);
  float nearX = orbitX + orbWave(orbitPhase + (8 << 11)) * (146.0f / 127.0f);
  float nearY = orbitY + orbWave(orbitPhase) * (56.0f / 127.0f);
  float farX = orbitX + orbWave(oppositePhase + (8 << 11)) * (96.0f / 127.0f);
  float farY = orbitY + orbWave(oppositePhase) * (40.0f / 127.0f);
  drawGlassCube(nearX, nearY, 9, glassPhase(elapsedMs, 18000, 3000), 0x38, 0x98, 0xD8, 1);
  drawGlassCube(farX, farY, 7, glassPhase(elapsedMs, 26000, 12000), 0x78, 0x68, 0xC8, 1);
}

void drawSplashGlassBackground(uint32_t now) {
  const uint32_t elapsedMs = glassElapsedMs(now);
  // Keep the transparent logo over a visibly blue-purple field.
  const uint64_t topLeft = GS_SETREG_RGBA(0x04, 0x0B, 0x26, 0x80);
  const uint64_t topRight = GS_SETREG_RGBA(0x20, 0x10, 0x48, 0x80);
  const uint64_t bottomLeft = GS_SETREG_RGBA(0x08, 0x18, 0x40, 0x80);
  const uint64_t bottomRight = GS_SETREG_RGBA(0x34, 0x18, 0x60, 0x80);
  gsKit_prim_quad_gouraud(gsGlobal, 0, 0, gsGlobal->Width, 0, 0, gsGlobal->Height, gsGlobal->Width, gsGlobal->Height, 0,
              topLeft, topRight, bottomLeft, bottomRight);
  drawGlassCube(gsGlobal->Width / 2, gsGlobal->Height * 57 / 100, 30,
          glassPhase(elapsedMs, 9000, 0), 0x38, 0xA8, 0xE0, 1);
}


void setOrbsBackgroundStyle(int enabled) {
  orbsBackgroundStyle = enabled != 0;
}

void drawSharedLibraryBackground(uint32_t frameNowMs) {
  if (orbsBackgroundStyle) {
    const int width = gsGlobal->Width;
    const int height = gsGlobal->Height;
    const uint64_t black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
    gsKit_prim_quad_gouraud(gsGlobal, 0, 0, width, 0, 0, height,
                            width, height, 0, black, black, black, black);
    drawFormationOrbs(width * 65 / 100, height * 52 / 100,
                  width * 20 / 100, height * 30 / 100,
                  glassElapsedMs(frameNowMs), 100, 0);
  } else {
    drawGlassBackground(frameNowMs);
  }
}

int orbsVisualCacheIndex(int flowOffset) {
  int closest = ORBS_LOGO_CACHE_FOCUS;
  int closestDistance = 0x7FFFFFFF;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    int position = (i - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    if (distance < closestDistance) {
      closest = i;
      closestDistance = distance;
    }
  }
  return closest;
}

static void drawOrbsLogo(GSTEXTURE *texture, float x, float y,
                         float width, float height, int brightness) {
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
  gsKit_prim_sprite_texture(gsGlobal, texture, x, y, 0.0f, 0.0f,
                            x + width, y + height,
                            texture->Width - 1, texture->Height - 1, 6,
                            GS_SETREG_RGBA(brightness, brightness, brightness, 0x80));
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void drawOrbsView(TargetList *titles, int selectedTitleIdx,
                  int flowOffset, int visualFocus, uint32_t now) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int listTop = headerHeight + 12;
  const int listBottom = height - footerHeight - 12;
  const int centerY = (listTop + listBottom) / 2;
  const int rowPitch = (listBottom - listTop) / 4;
  const int logoCenterX = width - 153;
  const int visualTitleIdx = lunaNavWrap(titles->total,
      selectedTitleIdx + visualFocus - ORBS_LOGO_CACHE_FOCUS);
  char title[255];

  if (orbsBackgroundLoaded) {
    GSTEXTURE *background = orbsBackgroundTexture;
    gsKit_TexManager_bind(gsGlobal, background);
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_prim_sprite_texture(gsGlobal, background, 0.0f, 0.0f,
                              0.0f, 0.0f, (float)width, (float)height,
                              background->Width - 1, background->Height - 1, 0,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  }

  // Keep the orbit visible over the art and make a quiet area for the logos.
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 1,
                    GS_SETREG_RGBA(0x00, 0x02, 0x0C, 0x20));
  gsKit_prim_sprite(gsGlobal, 0, 0, width / 2, height, 2,
                    GS_SETREG_RGBA(0x02, 0x06, 0x12, 0x26));
  gsKit_prim_quad_gouraud(gsGlobal, width - 350, 0, width, 0,
                          width - 350, height, width, height, 2,
                          GS_SETREG_RGBA(0x02, 0x06, 0x12, 0x08),
                          GS_SETREG_RGBA(0x02, 0x06, 0x12, 0x72),
                          GS_SETREG_RGBA(0x02, 0x06, 0x12, 0x08),
                          GS_SETREG_RGBA(0x02, 0x06, 0x12, 0x72));
  gsKit_prim_sprite(gsGlobal, 0, 0, width, headerHeight + 3, 3,
                    GS_SETREG_RGBA(0x02, 0x05, 0x10, 0x4A));
  gsKit_prim_sprite(gsGlobal, 0, height - footerHeight, width, height, 3,
                    GS_SETREG_RGBA(0x02, 0x05, 0x10, 0x62));

  drawTextWindow(keepoutArea + 10, headerHeight - getFontLineHeight(),
                 width - keepoutArea, 0, 7, FontMainColor, ALIGN_LEFT, "ORBS");
  snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", visualTitleIdx + 1,
           titles->total);
  drawTextWindow(width - 116, headerHeight - getFontLineHeight(),
                 width - keepoutArea - 8, 0, 7, FontMainColor,
                 ALIGN_RIGHT, lineBuffer);

  gsKit_prim_sprite(gsGlobal, width - 283, centerY - 37,
                    width - 35, centerY + 37, 4,
                    GS_SETREG_RGBA(0x18, 0x34, 0x58, 0x56));
  drawOrbitalDisc(width - 20, centerY, 10, 6,
                  GS_SETREG_RGBA(0xD0, 0xEB, 0xFF, 0x80),
                  GS_SETREG_RGBA(0x30, 0x8C, 0xD8, 0));
  drawFormationOrbs(width * 27 / 100, centerY, width * 20 / 100,
                (listBottom - listTop) * 36 / 100,
                glassElapsedMs(now), 100, 5);

  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    int position = (i - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    int targetIdx = lunaNavWrap(titles->total,
        selectedTitleIdx + i - ORBS_LOGO_CACHE_FOCUS);
    int duplicate = 0;
    int proximity;
    float logoWidth;
    float logoHeight;
    float rowY;
    if (distance > 1350)
      continue;
    for (int j = 0; j < ORBS_LOGO_CACHE_COUNT; j++) {
      int otherPosition = (j - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
      int otherDistance = otherPosition < 0 ? -otherPosition : otherPosition;
      int otherTarget = lunaNavWrap(titles->total,
          selectedTitleIdx + j - ORBS_LOGO_CACHE_FOCUS);
      if (j != i && otherTarget == targetIdx &&
          (otherDistance < distance || (otherDistance == distance && j < i))) {
        duplicate = 1;
        break;
      }
    }
    if (duplicate)
      continue;
    proximity = distance < 1000 ? 1000 - distance : 0;
    logoWidth = 188.0f + 44.0f * lunaNavEase(proximity) / 1000.0f;
    logoHeight = 62.0f + 14.0f * lunaNavEase(proximity) / 1000.0f;
    rowY = centerY + position * rowPitch / 1000.0f;
    if (orbsLogoLoaded[i])
      drawOrbsLogo(orbsLogoTextures[i], logoCenterX - logoWidth / 2.0f,
                   rowY - logoHeight / 2.0f, logoWidth, logoHeight,
                   i == visualFocus ? 0x80 : 0x58);
    else {
      formatPSBBNTitle(getTargetByIdx(titles, targetIdx)->name, title, 190);
      drawTextWindow(width - 270, (int)rowY - getFontLineHeight() / 2,
                     width - 35, (int)rowY + getFontLineHeight() / 2, 6,
                     i == visualFocus ? FontMainColor : HeaderTextColor,
                     ALIGN_CENTER, title);
    }
  }

  const int footerY = height - footerHeight + 8;
  const int circleX = 26;
  const int crossX = width * 39 / 100;
  const int triangleX = width * 69 / 100;
  drawIconWindow(circleX, footerY, 0, height, 8, FontMainColor,
                 ALIGN_CENTER, ICON_CIRCLE);
  drawTextWindow(circleX + getIconWidth(ICON_CIRCLE) + 6, footerY,
                 crossX - 8, height, 8, FontMainColor, ALIGN_VCENTER, "Views");
  drawIconWindow(crossX, footerY, 0, height, 8, FontMainColor,
                 ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(crossX + getIconWidth(ICON_CROSS) + 6, footerY,
                 triangleX - 8, height, 8, FontMainColor, ALIGN_VCENTER, "Launch");
  drawIconWindow(triangleX, footerY, 0, height, 8, FontMainColor,
                 ALIGN_CENTER, ICON_TRIANGLE);
  drawTextWindow(triangleX + getIconWidth(ICON_TRIANGLE) + 6, footerY,
                 width - keepoutArea, height, 8, FontMainColor,
                 ALIGN_VCENTER, "Options");
}
