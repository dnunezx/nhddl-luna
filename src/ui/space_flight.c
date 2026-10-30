/* LUNA Space Flight. Normalized moving-field wrapping follows the researched
   Cloud behavior; projection, presets, colors and trails are LUNA designs. */
#include "ui/space_flight.h"
#include <math.h>
#ifndef LUNA_SPACE_MODEL_ONLY
#include "ui/view_internal.h"
#endif

typedef struct { float x, y, z, vx, vy, vz; } SpaceParticle;
static SpaceParticle particles[LUNA_SPACE_POINTS];
static int speedSetting = 4, depthSetting = 2, initialized;
static uint32_t lastMs, randomState = 0x4c554e41U;
static float currentDepth = 3.0f;
static const float speeds[] = {0, 0.15f, 0.3f, 0.6f, 1, 1.7f, 2.8f, 4.2f, 6};
static const float depths[] = {1.4f, 2.1f, 3.0f, 4.5f, 6.5f};
static float randomUnit(void) {
  randomState = randomState * 1664525U + 1013904223U;
  return (randomState >> 8) / 16777216.0f;
}
static int clampSetting(int value, int maximum) {
  return value < 0 ? 0 : value > maximum ? maximum : value;
}
int lunaSpaceSpeed(void) { return speedSetting; }
int lunaSpaceDepth(void) { return depthSetting; }
void lunaSpaceSetSpeed(int setting) { speedSetting = clampSetting(setting, 8); }
void lunaSpaceSetDepth(int setting) { depthSetting = clampSetting(setting, 4); }
static void initSpace(uint32_t nowMs) {
  for (int i = 0; i < LUNA_SPACE_POINTS; ++i) {
    SpaceParticle *p = &particles[i];
    p->x = randomUnit() * 3.0f - 1.5f;
    p->y = randomUnit() - 0.5f;
    p->z = randomUnit() - 0.5f;
    p->vx = (randomUnit() - 0.5f) * 0.008f;
    p->vy = (randomUnit() - 0.5f) * 0.003f;
    p->vz = -(0.14f + randomUnit() * 0.09f);
  }
  lastMs = nowMs;
  currentDepth = depths[depthSetting];
  initialized = 1;
}
void lunaSpaceStep(uint32_t nowMs) {
  if (!initialized) { initSpace(nowMs); return; }
  uint32_t elapsed = nowMs - lastMs;
  lastMs = nowMs;
  if (elapsed > 250U) return;
  const float dt = elapsed * 0.001f;
  const float blend = fminf(1.0f, dt * 5.0f);
  currentDepth += (depths[depthSetting] - currentDepth) * blend;
  const float movement = dt * speeds[speedSetting];
  for (int i = 0; i < LUNA_SPACE_POINTS; ++i) {
    SpaceParticle *p = &particles[i];
    p->x += p->vx * movement;
    p->y += p->vy * movement;
    p->z += p->vz * movement;
    if (p->x < -1.5f) p->x += 3;
    if (p->x > 1.5f) p->x -= 3;
    if (p->y < -0.5f) p->y += 1;
    if (p->y > 0.5f) p->y -= 1;
    while (p->z < -0.5f) p->z += 1;
  }
}
int lunaSpaceProject(int index, int width, int height, LunaSpacePoint *point) {
  if (!point || index < 0 || index >= LUNA_SPACE_POINTS || width <= 0 || height <= 0) return 0;
  if (!initialized) initSpace(0);
  const SpaceParticle *p = &particles[index];
  const float near = 0.14f;
  const float depth = near + (p->z + 0.5f) * currentDepth;
  const float tailDepth = depth - p->vz * speeds[speedSetting] * 0.055f * currentDepth;
  const float focalX = width * 0.38f, focalY = height * 0.82f;
  point->x = width * 0.5f + p->x * focalX / depth;
  point->y = height * 0.46f + p->y * focalY / depth;
  point->tailX = width * 0.5f + p->x * focalX / tailDepth;
  point->tailY = height * 0.46f + p->y * focalY / tailDepth;
  const float dx = point->tailX - point->x, dy = point->tailY - point->y;
  const float length = sqrtf(dx * dx + dy * dy);
  if (length > 44) {
    point->tailX = point->x + dx * 44 / length;
    point->tailY = point->y + dy * 44 / length;
  }
  point->size = fminf(2.8f, 0.55f + 0.7f / depth) * width / 640.0f;
  const float nearFade = fminf(1.0f, (depth - near) / 0.18f);
  point->light = nearFade * fminf(1.0f, 0.25f + 0.9f / (0.7f + depth));
  return point->x >= -48 && point->x <= width + 48 && point->y >= -48 && point->y <= height + 48;
}
#ifndef LUNA_SPACE_MODEL_ONLY
/* One small alpha mask keeps every star round without extra geometry. */
enum { SPACE_STAR_SIZE = 32 };
static u32 spaceStarPixels[SPACE_STAR_SIZE * SPACE_STAR_SIZE] __attribute__((aligned(128)));
static GSTEXTURE spaceStarTexture;
void resetSpaceFlightTexture(void) {
  // The UI has discarded the previous GS context and its texture residency.
  spaceStarTexture = (GSTEXTURE){0};
}
static int prepareSpaceStarTexture(void) {
  if (!spaceStarTexture.Mem) {
    for (int y = 0; y < SPACE_STAR_SIZE; ++y) {
      for (int x = 0; x < SPACE_STAR_SIZE; ++x) {
        const float dx = (x - 15.5f) / 14.5f;
        const float dy = (y - 15.5f) / 14.5f;
        const float radius = sqrtf(dx * dx + dy * dy);
        const float fade = fmaxf(0, 1 - radius);
        const uint32_t alpha = (uint32_t)(128 * fade * fade * (3 - 2 * fade));
        // Neutral RGB preserves the existing tints, even at transparent edges.
        spaceStarPixels[y * SPACE_STAR_SIZE + x] = 0x00808080U | (alpha << 24);
      }
    }
    spaceStarTexture.Width = spaceStarTexture.Height = SPACE_STAR_SIZE;
    spaceStarTexture.PSM = GS_PSM_CT32;
    spaceStarTexture.Mem = spaceStarPixels;
    spaceStarTexture.Filter = GS_FILTER_LINEAR;
    spaceStarTexture.Delayed = GS_SETTING_ON;
  }
  return bindTextureSafe(gsGlobal, &spaceStarTexture);
}
static void drawSpaceStar(float x, float y, float radius, uint64_t color) {
  gsKit_prim_sprite_texture(gsGlobal, &spaceStarTexture,
      x - radius, y - radius, 0.5f, 0.5f,
      x + radius, y + radius, SPACE_STAR_SIZE - 0.5f, SPACE_STAR_SIZE - 0.5f,
      0, color);
}
enum { SPACE_GLOW_SEGMENTS = 24 };
static struct { float x, y; } spaceGlow[SPACE_GLOW_SEGMENTS + 1];
static int spaceGlowWidth, spaceGlowHeight;
static void prepareSpaceGlow(int width, int height) {
  if (spaceGlowWidth == width && spaceGlowHeight == height) return;
  // Preserve both endpoints and the original arithmetic at each screen size.
  for (int point = 0; point <= SPACE_GLOW_SEGMENTS; ++point) {
    const float angle = point * 6.2831853f / SPACE_GLOW_SEGMENTS;
    spaceGlow[point].x = width * 0.5f + cosf(angle) * width * 0.45f;
    spaceGlow[point].y = height * 0.46f + sinf(angle) * height * 0.35f;
  }
  spaceGlowWidth = width;
  spaceGlowHeight = height;
}
void drawSpaceFlight(uint32_t nowMs) {
  lunaSpaceStep(nowMs);
  const int w = gsGlobal->Width, h = gsGlobal->Height;
  prepareSpaceGlow(w, h);
  const int previousAlpha = gsGlobal->PrimAlphaEnable;
  const GSTEST previousTest = *gsGlobal->Test;
  const uint64_t previousBlend = gsGlobal->PrimAlpha;
  const int previousPabe = gsGlobal->PABE;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  const uint64_t dark = GS_SETREG_RGBA(0x01, 0x03, 0x0B, 0x80);
  const uint64_t blue = GS_SETREG_RGBA(0x04, 0x0C, 0x22, 0x80);
  gsKit_prim_quad_gouraud(gsGlobal, 0, 0, w, 0, 0, h, w, h,
      0, dark, blue, blue, dark);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  /* Original procedural glow: no Sony textures or native runtime are used. */
  for (int segment = 0; segment < SPACE_GLOW_SEGMENTS; ++segment) {
    gsKit_prim_triangle_gouraud(gsGlobal, w * 0.5f, h * 0.46f,
        spaceGlow[segment].x, spaceGlow[segment].y,
        spaceGlow[segment + 1].x, spaceGlow[segment + 1].y,
        0, GS_SETREG_RGBA(0x12, 0x38, 0x68, 0x12),
        GS_SETREG_RGBA(0x12, 0x38, 0x68, 0), GS_SETREG_RGBA(0x12, 0x38, 0x68, 0));
  }
  const int starsReady = prepareSpaceStarTexture();
  for (int i = 0; i < LUNA_SPACE_POINTS; ++i) {
    LunaSpacePoint p;
    if (!lunaSpaceProject(i, w, h, &p)) continue;
    const int alpha = (int)(p.light * 112);
    const int warm = i % 7 == 0;
    const int r = warm ? 0xC0 : 0x60, g = warm ? 0xB0 : 0xA0;
    gsKit_prim_line_goraud(gsGlobal, p.tailX, p.tailY, p.x, p.y, 0,
        GS_SETREG_RGBA(r, g, 0xE0, alpha / 6), GS_SETREG_RGBA(r, g, 0xE0, alpha));
    if (starsReady) {
      drawSpaceStar(p.x, p.y, p.size * 2.5f, GS_SETREG_RGBA(r, g, 0xE0, alpha / 9));
      drawSpaceStar(p.x, p.y, p.size * 0.5f, GS_SETREG_RGBA(0xD0, 0xE0, 0xF0, alpha));
    }
  }
  /* Options composes text/panels with depth testing disabled. Restore the
     complete caller TEST state instead of imposing library defaults. */
  *gsGlobal->Test = previousTest;
  gsKit_set_test(gsGlobal, previousTest.ATE ? GS_ATEST_ON : GS_ATEST_OFF);
  gsKit_set_primalpha(gsGlobal, previousBlend, previousPabe);
  gsGlobal->PrimAlphaEnable = previousAlpha;
}
#endif
