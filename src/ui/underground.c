// Underground's approved artwork, packed as indexed textures with GS palettes.
#include "ui/underground.h"
#include "ui/graphics.h"
#include "ui/view_scroll.h"
#include <dmaKit.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

extern const unsigned char undergroundBackgroundData[], undergroundFrameData[], undergroundLogoData[];
extern const unsigned int undergroundBackgroundSize, undergroundFrameSize, undergroundLogoSize;
static GSTEXTURE background, frame, logo;
static int interfaceFailed;
static uint32_t motionPrevious, motionElapsed;
static int motionInitialized;
enum { TONE_RED, TONE_BLUE, TONE_WHITE };
static int backgroundTone = TONE_RED;

int uiUndergroundIsBackground(LibraryBackground value) {
  return value == LIBRARY_BACKGROUND_UNDERGROUND ||
         value == LIBRARY_BACKGROUND_STATIC_X || value == LIBRARY_BACKGROUND_PHANTOM;
}

int uiUndergroundIsGlass(GlassColorPreset value) {
  return value == GLASS_COLOR_UNDERGROUND || value == GLASS_COLOR_STATIC_X ||
         value == GLASS_COLOR_PHANTOM;
}

static int glassTone(void) {
  return getGlassColorPreset() == GLASS_COLOR_STATIC_X ? TONE_BLUE :
         getGlassColorPreset() == GLASS_COLOR_PHANTOM ? TONE_WHITE : TONE_RED;
}

static uint64_t toneColor(int tone, int red, int green, int blue, int alpha) {
  // Move only red chroma; retain neutral metal, black, texture detail and alpha.
  const int neutral = green > blue ? green : blue;
  if (tone != TONE_RED && red > neutral) {
    const int accent = red - neutral;
    if (tone == TONE_WHITE) {
      green = blue = red;
    } else {
      const int peak = red;
      red = neutral + accent * 48 / 255;
      green += accent * 136 / 255;
      blue = peak;
    }
  }
  return GS_SETREG_RGBA(red, green, blue, alpha);
}

uint64_t uiUndergroundTintColor(int red, int green, int blue, int alpha) {
  return toneColor(glassTone(), red, green, blue, alpha);
}

uint64_t uiUndergroundTextColor(uint64_t color) {
  int light = color & 255;
  const int green = (color >> 8) & 255;
  const int blue = (color >> 16) & 255;
  if (green > light) light = green;
  if (blue > light) light = blue;
  const int tone = glassTone();
  return GS_SETREG_RGBA(tone == TONE_BLUE ? light * 238 / 255 : light,
      tone == TONE_RED ? light * 246 / 255 : tone == TONE_BLUE ? light * 247 / 255 : light,
      tone == TONE_RED ? light * 248 / 255 : light, (color >> 24) & 255);
}

typedef struct { GSTEST test; uint64_t blend; int alpha, pabe; } UndergroundState;

static UndergroundState beginDraw(void) {
  UndergroundState saved = {*gsGlobal->Test, gsGlobal->PrimAlpha,
                            gsGlobal->PrimAlphaEnable, gsGlobal->PABE};
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  // Transparent art must not write depth over the live text and cover layers.
  gsGlobal->Test->ATST = 6;
  gsGlobal->Test->AREF = 0;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  return saved;
}

static void endDraw(UndergroundState saved) {
  gsGlobal->PrimAlphaEnable = saved.alpha;
  gsKit_set_primalpha(gsGlobal, saved.blend, saved.pabe);
  *gsGlobal->Test = saved.test;
  gsKit_set_test(gsGlobal, saved.test.ATE ? GS_ATEST_ON : GS_ATEST_OFF);
}

static int loadTexture(GSTEXTURE *texture, const unsigned char *data,
                        unsigned int size, int width, int height, int tone) {
  if (texture->Mem) return 0;
  unsigned long bytes = (unsigned long)width * height + 1024;
  unsigned char *pixels = memalign(128, bytes);
  if (!pixels) return -1;
  unsigned long decoded = bytes;
  if (uncompress(pixels, &decoded, data, size) != Z_OK || decoded != bytes) {
    free(pixels);
    return -1;
  }
  if (tone != TONE_RED) {
    unsigned char *palette = pixels + width * height;
    for (int i = 0; i < 256; i++, palette += 4) {
      const uint64_t ink = toneColor(tone, palette[0], palette[1], palette[2], palette[3]);
      palette[0] = ink & 255;
      palette[1] = (ink >> 8) & 255;
      palette[2] = (ink >> 16) & 255;
    }
  }
  memset(texture, 0, sizeof(*texture));
  texture->Width = width;
  texture->Height = height;
  texture->PSM = GS_PSM_T8;
  texture->Mem = (u32 *)pixels;
  texture->Clut = (u32 *)(pixels + width * height);
  texture->ClutPSM = GS_PSM_CT32;
  texture->ClutStorageMode = GS_CLUT_STORAGE_CSM1;
  texture->Filter = GS_FILTER_LINEAR;
  texture->Delayed = GS_SETTING_ON;
  return 0;
}

static void releaseTexture(GSTEXTURE *texture) {
  if (!texture->Mem) return;
  gsKit_TexManager_free(gsGlobal, texture);
  free(texture->Mem); // The CLUT shares this allocation.
  memset(texture, 0, sizeof(*texture));
}

static void finishDraws(void) {
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  dmaKit_wait_fast();
}

int uiUndergroundPrepareBackground(LibraryBackground value) {
  if (!gsGlobal) return -1;
  const int tone = value == LIBRARY_BACKGROUND_STATIC_X ? TONE_BLUE :
                   value == LIBRARY_BACKGROUND_PHANTOM ? TONE_WHITE : TONE_RED;
  if (background.Mem && tone != backgroundTone) {
    // Flush queued users before replacing the palette, keeping motion continuous.
    finishDraws();
    releaseTexture(&background);
  }
  if (loadTexture(&background, undergroundBackgroundData,
                  undergroundBackgroundSize, 512, 512, tone)) return -1;
  backgroundTone = tone;
  return 0;
}

void uiUndergroundReleaseBackground(void) {
  if (!background.Mem) return;
  finishDraws();
  releaseTexture(&background);
  motionInitialized = 0;
  motionElapsed = 0;
}

void uiUndergroundClose(void) {
  if (background.Mem || frame.Mem || logo.Mem) {
    finishDraws();
    releaseTexture(&background);
    releaseTexture(&frame);
    releaseTexture(&logo);
  }
  interfaceFailed = 0;
  motionInitialized = 0;
  motionElapsed = 0;
}

void uiUndergroundReleaseInterface(void) {
  if (frame.Mem || logo.Mem) {
    finishDraws();
    releaseTexture(&frame);
    releaseTexture(&logo);
  }
  interfaceFailed = 0;
}

static int prepareInterface(void) {
  if (!uiUndergroundIsGlass(getGlassColorPreset()) || interfaceFailed) return 0;
  const int tone = glassTone();
  if (loadTexture(&frame, undergroundFrameData, undergroundFrameSize, 256, 128, tone) ||
      loadTexture(&logo, undergroundLogoData, undergroundLogoSize, 256, 64, tone)) {
    interfaceFailed = 1;
    return 0;
  }
  return 1;
}

static void sprite(GSTEXTURE *texture, float x1, float y1, float x2, float y2,
                    float u1, float v1, float u2, float v2, int z, float opacity) {
  const uint64_t ink = GS_SETREG_RGBA(128, 128, 128, (int)(128 * opacity));
  gsKit_prim_sprite_texture(gsGlobal, texture, x1, y1, u1, v1,
                            x2, y2, u2, v2, z, ink);
}

typedef struct { float x, y; } CircuitPoint;
static const CircuitPoint circuitRoutes[6][5] = {
  {{0,38},{112,38},{136,62},{194,62},{224,92}},
  {{512,96},{452,96},{426,122},{392,122},{376,138}},
  {{0,308},{64,308},{88,332},{140,332},{154,346}},
  {{512,458},{426,458},{398,430},{342,430},{318,406}},
  {{264,0},{264,30},{292,58},{326,58},{348,80}},
  {{44,512},{44,470},{70,444},{112,444},{132,424}}
};

static CircuitPoint circuitPosition(int route, float progress) {
  const float position = fminf(3.99999f, progress * 4);
  const int segment = (int)position;
  const float t = position - segment;
  const CircuitPoint a = circuitRoutes[route][segment];
  const CircuitPoint b = circuitRoutes[route][segment + 1];
  return (CircuitPoint){(a.x + (b.x - a.x) * t) * gsGlobal->Width / 512,
                         (a.y + (b.y - a.y) * t) * gsGlobal->Height / 512};
}

static void drawCircuitCurrents(uint32_t elapsed) {
  // Small lights follow routed tracks; no flashing layer over the game list.
  const int light = backgroundTone == TONE_WHITE ? 162 : 255;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  for (int route = 0; route < 6; route++) {
    const uint32_t period = 10500U + route * 1700U;
    const float head = ((elapsed + route * 1931U) % period) / (float)period;
    for (int tail = 0; tail < 6; tail++) {
      const float near = head - (tail + 1) * .018f;
      if (near < 0) continue;
      CircuitPoint a = circuitPosition(route, head - tail * .018f);
      CircuitPoint b = circuitPosition(route, near);
      const uint64_t ink = toneColor(backgroundTone, light, 28 * light / 255,
                                     46 * light / 255, (6 - tail) * 5);
      gsKit_prim_line(gsGlobal, a.x, a.y, b.x, b.y, 0, ink);
      gsKit_prim_line(gsGlobal, a.x, a.y + 1, b.x, b.y + 1, 0, ink);
    }
    CircuitPoint tip = circuitPosition(route, head);
    gsKit_prim_sprite(gsGlobal, tip.x - 1, tip.y - 1, tip.x + 2, tip.y + 1,
                        0, toneColor(backgroundTone, light, 100 * light / 255,
                                      110 * light / 255, 36));
  }
}

void uiUndergroundBackgroundDraw(uint32_t nowMs) {
  if (!background.Mem || !bindTextureSafe(gsGlobal, &background)) return;
  if (!motionInitialized) {
    motionPrevious = nowMs;
    motionInitialized = 1;
  }
  const uint32_t delta = nowMs - motionPrevious;
  motionPrevious = nowMs;
  // Resume after a modal/view change without jumping the wallpaper forward.
  if (delta <= 250U) motionElapsed += delta;
  const float phase = (motionElapsed % 18000U) * (6.2831853f / 18000);
  const float slowPhase = (motionElapsed % 27000U) * (6.2831853f / 27000);
  float u[17], v[17];
  for (int row = 0; row <= 16; row++) {
    u[row] = 1.3f * sinf(phase + row * .42f) + .5f * sinf(slowPhase + row * .77f);
    v[row] = .5f + 511.0f * row / 16 +
        .8f * sinf(slowPhase + row * .65f) * sinf(row * (3.14159265f / 16));
  }
  UndergroundState saved = beginDraw();
  // Shared UVs at strip boundaries make a continuous slow ripple in the art.
  // Screen geometry and all foreground animation clocks stay unchanged.
  // Phantom's wallpaper is gray and subdued; interface art keeps its brightness.
  const int brightness = backgroundTone == TONE_WHITE ? 82 : 128;
  for (int row = 0; row < 16; row++) {
    const float top = gsGlobal->Height * row / 16.0f;
    const float bottom = gsGlobal->Height * (row + 1) / 16.0f;
    gsKit_prim_quad_texture(gsGlobal, &background,
        0, top, 3 + u[row], v[row],
        gsGlobal->Width, top, 508 + u[row], v[row],
        0, bottom, 3 + u[row + 1], v[row + 1],
        gsGlobal->Width, bottom, 508 + u[row + 1], v[row + 1],
        0, GS_SETREG_RGBA(brightness, brightness, brightness, 128));
  }
  drawCircuitCurrents(motionElapsed);
  endDraw(saved);
}

void uiUndergroundFrameDraw(float left, float top, float right, float bottom,
                            int z, float opacity) {
  if (right <= left || bottom <= top || opacity <= 0 || !prepareInterface() ||
      !bindTextureSafe(gsGlobal, &frame)) return;
  opacity = fminf(1, opacity);
  // Fixed corner sizes preserve the distressed artwork on narrow rows and PAL.
  const float horizontal = fminf(14, (right - left) * .25f);
  const float vertical = fminf(14, (bottom - top) * .4f);
  const float x[] = {left, left + horizontal, right - horizontal, right};
  const float y[] = {top, top + vertical, bottom - vertical, bottom};
  const float u[] = {.5f, 32, 224, 255.5f};
  const float v[] = {.5f, 32, 96, 127.5f};
  UndergroundState saved = beginDraw();
  for (int row = 0; row < 3; row++)
    for (int column = 0; column < 3; column++) {
      if (row == 1 && column == 1) continue;
      sprite(&frame, x[column], y[row], x[column + 1], y[row + 1],
               u[column], v[row], u[column + 1], v[row + 1], z, opacity);
    }
  endDraw(saved);
}

static void undergroundPanelFillDraw(float left, float top, float right, float bottom,
                                      int z, float opacity) {
  if (!uiUndergroundIsGlass(getGlassColorPreset()) || opacity <= 0) return;
  opacity = fminf(1, opacity);
  UndergroundState saved = beginDraw();
  gsKit_prim_sprite(gsGlobal, left, top, right, bottom, z,
                      uiUndergroundTintColor(5, 2, 5, (int)(76 * opacity)));
  endDraw(saved);
}

void uiUndergroundPanelDraw(float left, float top, float right, float bottom,
                            int z, float opacity) {
  undergroundPanelFillDraw(left, top, right, bottom, z, opacity);
  uiUndergroundFrameDraw(left - 4, top - 4, right + 4, bottom + 4, z + 1, opacity);
}

int uiUndergroundLogoDraw(float centerX, float top, float width, int z, float opacity) {
  if (width <= 0 || opacity <= 0 || !prepareInterface() ||
      !bindTextureSafe(gsGlobal, &logo)) return 0;
  UndergroundState saved = beginDraw();
  sprite(&logo, centerX - width * .5f, top, centerX + width * .5f, top + width * .25f,
           .5f, .5f, 255.5f, 63.5f, z, fminf(1, opacity));
  endDraw(saved);
  return 1;
}
