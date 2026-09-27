// LUNA visual rendering extracted from gui.c.
#include "ui/view_internal.h"
#include "ui/view_scroll.h"
#include "ui/ambient_orbs.h"
#include <stdio.h>

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
  resetAmbientOrbs(startMs);
}

static uint32_t glassElapsedMs(uint32_t frameNowMs) {
  return frameNowMs - glassStartMs;
}

uint64_t glassLightColor(int red, int green, int blue, int alpha) {
  if (glassColorPreset == GLASS_COLOR_BLACK) {
    int brightness = red;
    if (green > brightness)
      brightness = green;
    if (blue > brightness)
      brightness = blue;
    brightness = 0xC8 * brightness / 255;
    return GS_SETREG_RGBA(brightness, brightness, brightness, alpha);
  }
  return glassPresetColor(red, green, blue, alpha);
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
  return glassLightColor(clampColor(red + brightness), clampColor(green + brightness),
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

void drawSharedLibraryBackground(uint32_t frameNowMs) {
  if (!drawAmbientOrbsBackground(frameNowMs))
    drawGlassBackground(frameNowMs);
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
                  int flowOffset, int visualFocus, int fastScroll,
                  uint32_t now) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int listTop = headerHeight + 12;
  const int listBottom = height - footerHeight - 12;
  const int centerY = (listTop + listBottom) / 2;
  const int rowPitch = (listBottom - listTop) / 4;
  const int logoCenterX = width - 153;
  const int visualTitleIdx = lunaNavWrap(titles->total,
      selectedTitleIdx + visualFocus - ORBS_LOGO_CACHE_FOCUS);
  const uint32_t elapsedMs = glassElapsedMs(now);
  char title[255];

  drawSharedLibraryBackground(now);

  if (orbsBackgroundLoaded && !fastScroll) {
    GSTEXTURE *background = orbsBackgroundTexture;
    gsKit_TexManager_bind(gsGlobal, background);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_prim_sprite_texture(gsGlobal, background, 0.0f, 0.0f,
                              0.0f, 0.0f, (float)width, (float)height,
                              background->Width - 1, background->Height - 1, 0,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x50));
  }

  // Keep the orbit visible over the art and make a quiet area for the logos.
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 1,
                    glassPresetColor(0x00, 0x02, 0x0C, 0x20));
  gsKit_prim_sprite(gsGlobal, 0, 0, width / 2, height, 2,
                    glassPresetColor(0x02, 0x06, 0x12, 0x26));
  gsKit_prim_quad_gouraud(gsGlobal, width - 350, 0, width, 0,
                          width - 350, height, width, height, 2,
                          glassPresetColor(0x02, 0x06, 0x12, 0x08),
                          glassPresetColor(0x02, 0x06, 0x12, 0x72),
                          glassPresetColor(0x02, 0x06, 0x12, 0x08),
                          glassPresetColor(0x02, 0x06, 0x12, 0x72));
  gsKit_prim_sprite(gsGlobal, 0, 0, width, headerHeight + 3, 3,
                    glassPresetColor(0x02, 0x05, 0x10, 0x4A));
  gsKit_prim_sprite(gsGlobal, 0, height - footerHeight, width, height, 3,
                    glassPresetColor(0x02, 0x05, 0x10, 0x62));

  drawTextWindow(keepoutArea + 10, headerHeight - getFontLineHeight(),
                 width - keepoutArea, 0, 7, FontMainColor, ALIGN_LEFT, "SCROLL");
  snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", visualTitleIdx + 1,
           titles->total);
  drawTextWindow(width - 116, headerHeight - getFontLineHeight(),
                 width - keepoutArea - 8, 0, 7, FontMainColor,
                 ALIGN_RIGHT, lineBuffer);

  gsKit_prim_sprite(gsGlobal, width - 283, centerY - 37,
                    width - 35, centerY + 37, 4,
                    glassPresetColor(0x18, 0x34, 0x58, 0x56));
  drawOrbitalDisc(width - 20, centerY, 10, 6,
                  glassLightColor(0xD0, 0xEB, 0xFF, 0x80),
                  glassLightColor(0x30, 0x8C, 0xD8, 0));
  drawAmbientOrbsScroll(width * 27 / 100, centerY, width * 20 / 100,
                        (listBottom - listTop) * 36 / 100, elapsedMs,
                        fastScroll, getTargetByIdx(titles, visualTitleIdx)->name,
                        5, width - 290);

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
    if (orbsLogoLoaded[i] && !fastScroll)
      drawOrbsLogo(orbsLogoTextures[i], logoCenterX - logoWidth / 2.0f,
                   rowY - logoHeight / 2.0f, logoWidth, logoHeight,
                   i == visualFocus ? 0x80 : 0x58);
    else {
      formatPSBBNTitle(getTargetByIdx(titles, targetIdx)->name, title, 190);
      drawTextWindow(width - 270, (int)rowY - getFontLineHeight() / 2,
                     width - 35, (int)rowY + getFontLineHeight() / 2, 6,
                     i == visualFocus ? FontMainColor : glassMissingCoverTextColor(),
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
