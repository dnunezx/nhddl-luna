// Procedural library wallpapers. Fixed draw counts, no textures or heap work.
#include "ui/wallpapers.h"
#include "ui/graphics.h"
#include "ui/underground.h"
#include <math.h>
#include <string.h>

#define WALLPAPER_COUNT (LIBRARY_BACKGROUND_TEMPEST - LIBRARY_BACKGROUND_AURORA + 1)
#define WP_TURN 65536U

typedef struct { float x, y; } WallpaperPoint;
typedef struct { uint32_t previous, elapsed; int initialized; } WallpaperClock;

static WallpaperClock clocks[WALLPAPER_COUNT];
static float scaleX, scaleY;
static LibraryBackground activeWallpaper = LIBRARY_BACKGROUND_COUNT;
static const int16_t sineTable[32] = {
    0, 25, 49, 71, 90, 106, 117, 125, 127, 125, 117, 106, 90, 71, 49, 25,
    0, -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

static uint32_t hash(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  return value ^ (value >> 16);
}

// Cubic interpolation avoids trigonometric calls in the per-frame loops.
static float wave(uint32_t phase) {
  const unsigned index = (phase >> 11) & 31U;
  const float p0 = sineTable[(index + 31) & 31];
  const float p1 = sineTable[index];
  const float p2 = sineTable[(index + 1) & 31];
  const float p3 = sineTable[(index + 2) & 31];
  const float t = (phase & 2047U) / 2048.0f;
  return (0.5f / 127.0f) * (2 * p1 + t * (p2 - p0 +
      t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (-p0 + 3 * p1 - 3 * p2 + p3))));
}

static float cycle(uint32_t time, uint32_t period, uint32_t offset) {
  return ((time % period + offset % period) % period) / (float)period;
}

static uint32_t phase(uint32_t time, uint32_t period) {
  return (uint32_t)(cycle(time, period, 0) * WP_TURN);
}

static int clamp(int value, int maximum) {
  return value < 0 ? 0 : value > maximum ? maximum : value;
}

static uint64_t color(int r, int g, int b, int a) {
  return GS_SETREG_RGBA(clamp(r, 255), clamp(g, 255), clamp(b, 255), clamp(a, 128));
}

static WallpaperPoint point(float x, float y) {
  const WallpaperPoint result = {x, y};
  return result;
}

static void quad(WallpaperPoint a, WallpaperPoint b, WallpaperPoint c, WallpaperPoint d,
                 uint64_t ca, uint64_t cb, uint64_t cc, uint64_t cd) {
  gsKit_prim_quad_gouraud(gsGlobal, a.x * scaleX, a.y * scaleY,
      b.x * scaleX, b.y * scaleY, c.x * scaleX, c.y * scaleY,
      d.x * scaleX, d.y * scaleY, 0, ca, cb, cc, cd);
}

static void triangle(WallpaperPoint a, WallpaperPoint b, WallpaperPoint c,
                     uint64_t ca, uint64_t cb, uint64_t cc) {
  gsKit_prim_triangle_gouraud(gsGlobal, a.x * scaleX, a.y * scaleY,
      b.x * scaleX, b.y * scaleY, c.x * scaleX, c.y * scaleY, 0, ca, cb, cc);
}

static void line(float x1, float y1, float x2, float y2, uint64_t tint) {
  gsKit_prim_line(gsGlobal, x1 * scaleX, y1 * scaleY, x2 * scaleX, y2 * scaleY, 0, tint);
}

static void rectangle(float x1, float y1, float x2, float y2, uint64_t tint) {
  gsKit_prim_sprite(gsGlobal, x1 * scaleX, y1 * scaleY, x2 * scaleX, y2 * scaleY, 0, tint);
}

static void gradient(uint64_t top, uint64_t bottom) {
  quad(point(0, 0), point(640, 0), point(0, 448), point(640, 448),
       top, top, bottom, bottom);
}

static WallpaperPoint polar(float x, float y, float rx, float ry, uint32_t angle) {
  return point(x + wave(angle + WP_TURN / 4) * rx, y + wave(angle) * ry);
}

static void glow(float x, float y, float rx, float ry, uint64_t center, uint64_t edge) {
  for (unsigned i = 0; i < 24; i++)
    triangle(point(x, y), polar(x, y, rx, ry, i * WP_TURN / 24),
             polar(x, y, rx, ry, (i + 1) * WP_TURN / 24), center, edge, edge);
}

static void stars(uint32_t time, int count, int r, int g, int b) {
  for (int i = 0; i < count; i++) {
    const uint32_t seed = hash((uint32_t)i + 771U);
    const float x = 8 + seed % 624U + wave(phase(time, 60000) + seed) * 3;
    const float y = 8 + (seed >> 10) % 384U + wave(phase(time, 47000) + seed) * 2;
    const int alpha = 24 + (int)(wave(phase(time, 4000 + seed % 5000U) + seed) * 12);
    const float size = (seed & 3U) == 0 ? 2 : 1;
    rectangle(x, y, x + size, y + size, color(r, g, b, alpha));
  }
}

static void drawAurora(uint32_t time) {
  static const int palette[3][3] = {{45, 190, 115}, {120, 65, 205}, {45, 125, 200}};
  gradient(color(2, 5, 16, 128), color(6, 12, 24, 128));
  stars(time, 32, 150, 175, 195);
  const uint32_t drift = phase(time, 36000);
  for (unsigned ribbon = 0; ribbon < 3; ribbon++) {
    const uint64_t edge = color(palette[ribbon][0], palette[ribbon][1], palette[ribbon][2], 0);
    for (unsigned i = 0; i < 32; i++) {
      float y[2], height[2];
      uint64_t middle[2];
      for (unsigned j = 0; j < 2; j++) {
        const uint32_t p = (i + j) * 2100U + ribbon * 18000U;
        y[j] = 102 + ribbon * 46 + wave(p + drift) * 33 + wave(p / 2 - drift) * 20;
        height[j] = 28 + (wave(p * 2 + drift) + 1) * 20;
        middle[j] = color(palette[ribbon][0], palette[ribbon][1], palette[ribbon][2],
                           25 + (int)((wave(p * 3 - drift) + 1) * 9));
      }
      const float left = i * 20, right = left + 20;
      quad(point(left, y[0] - height[0]), point(right, y[1] - height[1]),
           point(left, y[0]), point(right, y[1]), edge, edge, middle[0], middle[1]);
      quad(point(left, y[0]), point(right, y[1]),
           point(left, y[0] + 40), point(right, y[1] + 40), middle[0], middle[1], edge, edge);
    }
  }
}

static void drawRonin(uint32_t time) {
  gradient(color(8, 9, 29, 128), color(22, 13, 35, 128));
  glow(512, 90, 70, 68, color(95, 68, 110, 22), color(60, 35, 75, 0));
  glow(512, 90, 38, 37, color(180, 150, 166, 90), color(110, 86, 124, 72));
  for (unsigned band = 0; band < 3; band++) {
    const float y = 280 + band * 60 + wave(phase(time, 23000) + band * 15000U) * 14;
    glow(170 + band * 160, y, 290, 34, color(105, 70, 115, 9), color(70, 45, 85, 0));
  }
  for (unsigned i = 0; i < 52; i++) {
    const uint32_t seed = hash(i + 0x524f4eU);
    const float fall = cycle(time, 12000 + seed % 16000U, seed >> 4);
    const uint32_t spinPeriod = 3800 + seed % 4300U;
    const uint32_t spin = phase(time, spinPeriod) + seed;
    const float x = fmodf(seed % 704U + fall * 170 +
                          wave(phase(time, spinPeriod * 2) + seed) * 20, 704) - 32;
    const float y = -24 + fall * 496;
    const float size = 3 + (seed >> 16) % 5U;
    const float flutter = 0.22f + fabsf(wave(spin)) * 0.6f;
    const float tilt = wave(spin + WP_TURN / 4);
    const WallpaperPoint a = point(x - tilt * size * .4f, y - size);
    const WallpaperPoint b = point(x + tilt * size * .4f, y + size);
    const WallpaperPoint left = point(x - size * flutter, y - size * .15f);
    const WallpaperPoint right = point(x + size * flutter, y + size * .15f);
    const uint64_t pale = color(235, 168, 195, 62 + seed % 30U);
    const uint64_t pink = color(145, 66, 115, 56);
    triangle(a, left, b, pale, pink, pale);
    triangle(a, b, right, pale, pale, pink);
  }
}

static void drawTorch(uint32_t time) {
  gradient(color(9, 4, 8, 128), color(40, 9, 7, 128));
  const int breath = 20 + (int)(wave(phase(time, 9000)) * 5);
  glow(320, 480, 430, 155, color(210, 47, 12, breath), color(85, 12, 4, 0));
  for (unsigned i = 0; i < 64; i++) {
    const uint32_t seed = hash(i + 0x454d42U);
    const float up = cycle(time, 4700 + seed % 6500U, seed);
    const float fade = wave((uint32_t)(up * (WP_TURN / 2)));
    const float x = 16 + seed % 608U + wave(phase(time, 4800 + seed % 3000U) + seed) * 13 +
                    (up - .5f) * ((int)(seed % 61U) - 30);
    const float y = 476 - up * 500;
    const float size = (seed & 7U) == 0 ? 2.5f : 1.0f;
    if ((seed & 7U) == 0)
      glow(x, y, 9, 12, color(255, 85, 18, (int)(fade * 28)), color(120, 28, 4, 0));
    line(x, y + size, x + wave(seed) * 2, y + 5 + size * 2,
         color(220, 70, 12, (int)(fade * 25)));
    rectangle(x, y, x + size, y + size * 1.5f,
              color(255, 135 + seed % 65U, 45, (int)(fade * 98)));
  }
}

static float tempestFlash(uint32_t time) {
  const float t = time % 18000U;
  const float first = fmaxf(0, 1 - fabsf(t - 12100) / 140);
  const float second = fmaxf(0, 1 - fabsf(t - 12460) / 110) * .65f;
  const float third = fmaxf(0, 1 - fabsf(t - 12800) / 240) * .8f;
  return fmaxf(first, fmaxf(second, third));
}

static void tempestBolt(uint32_t time, float flash) {
  if (flash <= 0)
    return;
  const uint32_t seed = hash(time / 18000U + 0x53544dU);
  float x = 150 + seed % 340U, y = 18;
  for (unsigned segment = 0; segment < 11; segment++) {
    const uint32_t bend = hash(seed + segment);
    const float xx = x + (int)(bend % 47U) - 23, yy = y + 24;
    line(x - 1, y, xx - 1, yy, color(95, 105, 225, flash * 35));
    line(x, y, xx, yy, color(180, 190, 245, flash * 100));
    line(x + 1, y, xx + 1, yy, color(95, 105, 225, flash * 35));
    if (segment == 3 || segment == 6) {
      float bx = x, by = y;
      for (unsigned branch = 0; branch < 3; branch++) {
        const float ex = bx + (segment == 3 ? -23 : 25);
        const float ey = by + 11 + hash(bend + branch) % 15U;
        line(bx, by, ex, ey, color(135, 145, 225, flash * 55));
        bx = ex;
        by = ey;
      }
    }
    x = xx;
    y = yy;
  }
}

static void drawTempest(uint32_t time) {
  const float flash = tempestFlash(time);
  const float afterglow = fmaxf(0, 1 - fabsf((float)(time % 18000U) - 12900) / 1500) * .25f;
  gradient(color(5 + flash * 14, 7 + flash * 16, 20 + flash * 30, 128), color(10, 12, 29, 128));
  tempestBolt(time, flash);
  const uint32_t drift = phase(time, 32000);
  for (unsigned layer = 0; layer < 3; layer++) {
    for (unsigned i = 0; i < 24; i++) {
      float y[2], thickness[2];
      for (unsigned j = 0; j < 2; j++) {
        const uint32_t p = (i + j) * 3300U + layer * 19000U;
        y[j] = 55 + layer * 78 + wave(p + drift) * 22 + wave(p * 2 - drift) * 10;
        thickness[j] = 37 + (wave(p + drift + 16000U) + 1) * 18;
      }
      const float light = flash * .6f + afterglow;
      const uint64_t clear = color(20, 25, 55, 0);
      const uint64_t middle = color(24 + light * 55, 29 + light * 55, 50 + light * 85, 90);
      const float left = i * (640.0f / 24), right = (i + 1) * (640.0f / 24);
      quad(point(left, y[0] - thickness[0]), point(right, y[1] - thickness[1]),
           point(left, y[0]), point(right, y[1]), clear, clear, middle, middle);
      quad(point(left, y[0]), point(right, y[1]), point(left, y[0] + thickness[0]),
           point(right, y[1] + thickness[1]), middle, middle, clear, clear);
    }
  }
  for (unsigned i = 0; i < 96; i++) {
    const uint32_t seed = hash(i + 0x524149U);
    const float fall = cycle(time, 1100 + seed % 1200U, seed);
    const float x = fmodf(seed % 720U + 720 - fall * 95, 720) - 40;
    const float y = -30 + fall * 510;
    const float length = 9 + seed % 15U;
    line(x, y, x - length * .2f, y + length,
         color(100 + flash * 45, 125 + flash * 45, 170 + flash * 45, 14 + seed % 16U));
  }
}

void resetAnimatedWallpapers(void) {
  memset(clocks, 0, sizeof(clocks));
  activeWallpaper = LIBRARY_BACKGROUND_COUNT;
}

// The approved Underground artwork is shared across all library views.
void drawUndergroundWallpaper(uint32_t nowMs) {
  uiUndergroundBackgroundDraw(nowMs);
}

void drawAnimatedWallpaper(LibraryBackground background, uint32_t nowMs) {
  if (background < LIBRARY_BACKGROUND_AURORA || background > LIBRARY_BACKGROUND_TEMPEST)
    return;
  WallpaperClock *clock = &clocks[background - LIBRARY_BACKGROUND_AURORA];
  if (!clock->initialized || activeWallpaper != background) {
    clock->previous = nowMs;
    clock->initialized = 1;
  }
  const uint32_t delta = nowMs - clock->previous;
  clock->previous = nowMs;
  if (delta <= 250U)
    clock->elapsed += delta;
  activeWallpaper = background;
  scaleX = gsGlobal->Width / 640.0f;
  scaleY = gsGlobal->Height / 448.0f;
  const int oldAlpha = gsGlobal->PrimAlphaEnable;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  switch (background) {
    case LIBRARY_BACKGROUND_AURORA: drawAurora(clock->elapsed); break;
    case LIBRARY_BACKGROUND_RONIN: drawRonin(clock->elapsed); break;
    case LIBRARY_BACKGROUND_TORCH: drawTorch(clock->elapsed); break;
    case LIBRARY_BACKGROUND_TEMPEST: drawTempest(clock->elapsed); break;
    default: break;
  }
  gsGlobal->PrimAlphaEnable = oldAlpha;
}
