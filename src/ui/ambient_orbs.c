// Ambient Orbs: reusable animated formation asset.
#include "ui/view_internal.h"
#include "ui/ambient_orbs.h"
#include "ui/ps2_menu_scene.h"
#include "options.h"
#include <stdio.h>
#include <string.h>

// LUNA formations use twelve lights; the original PS2 behavior uses seven.
#define ORB_ORBIT_COUNT 7
#define ORB_COUNT 12
#define ORB_SPREAD_PERIOD_MS 4300
#define ORB_CONTRACT_PERIOD_MS 24000
#define ORB_PULSE_PERIOD_MS 1800
#define ORB_TRAIL_STEP_MS 40
#define ORB_TRAIL_SEGMENTS 6
#define ORB_PLAYTIME_TRAIL_STEP_MS 120
#define ORB_PLAYTIME_TRAIL_SEGMENTS 18
#define ORB_FORMATION_PERIOD_MS 4600
#define ORB_SPLASH_FORMATION_PERIOD_MS 1800
#define ORB_FORMATION_MORPH_MS 1000
#define ORB_SPLASH_FORMATION_MORPH_MS 550
#define ORB_PLAYTIME_MS 17000
#define ORB_PLAYTIME_ENTRY_MS 400
#define ORB_PLAYTIME_SWEEP_X_MS 12000
#define ORB_PLAYTIME_SWEEP_Y_MS 16000
#define ORB_DIAMOND_ROTATION_MS 7200
#define ORB_CUBE_ROTATION_MS 8000
#define ORB_OCTAHEDRON_ROTATION_MS 7600
#define ORB_SPHERE_ROTATION_MS 8200
#define ORB_SPEED_SWELL_PERIOD_MS 12000
#define ORB_SELECTION_PULSE_MS 1100
#define ORB_SIZE_PULSE_MS 2400
#define ORB_SELECTION_EVENT_COUNT 12
#define SCROLL_LETTER_MORPH_MS 420
#define SCROLL_GLYPH_MORPH_MS 140
#define SCROLL_GLYPH_DEPTH_X 9.0f
#define SCROLL_GLYPH_DEPTH_Y 8.0f

static uint32_t glassStartMs;
static uint32_t orbitStartMs;
static uint32_t splashStartMs;
static int ambientOrbsBackgroundStyle;
static AmbientOrbsTheme ambientOrbsTheme = ORBS_THEME_LUNA;
static AmbientOrbsAppearance ambientOrbsAppearance = ORBS_APPEARANCE_LUNA;
static AmbientOrbsColor ambientOrbsColor = ORBS_COLOR_ORIGINAL;
static AmbientOrbsColor ambientTailsColor = ORBS_COLOR_ORIGINAL;
static uint32_t enabledOrbShapes = ORBS_SHAPES_ALL_MASK;
static int orbBackgroundColorsActive;
static const uint8_t orbPalette[ORBS_COLOR_COUNT][3] = {
    {0, 0, 0}, {0x52, 0xE0, 0xFF}, {0xB0, 0x82, 0xFF},
    {0xFF, 0x7A, 0xC6}, {0x72, 0xEC, 0xA0},
    {0xFF, 0xC4, 0x66}, {0xFF, 0xFF, 0xFF}};
static u32 originalHaloPixels[64 * 64] __attribute__((aligned(64)));
static u32 originalCorePixels[64 * 64] __attribute__((aligned(64)));
static GSTEXTURE originalHaloTexture;
static GSTEXTURE originalCoreTexture;
static int originalMasksLoaded;
void resetAmbientOrbsTextures(void) {
  originalMasksLoaded = 0;
  originalHaloTexture.Vram = originalCoreTexture.Vram = 0;
  originalHaloTexture.VramClut = originalCoreTexture.VramClut = 0;
}
static uint32_t originalClockAnchorMs;
static uint32_t originalClockStartMs;
static uint32_t originalScatterPhase;
static int systemConfigClockReady;
static uint32_t systemConfigFrame;
static float systemConfigRadius;
static int16_t systemConfigTilt, systemConfigSpin;

typedef struct {
  uint64_t clockMs;
  uint32_t elapsedMs;
  float radius;
  int16_t tilt, spin;
} SystemConfigOrbScene;
static uint32_t orbSelectionEvents[ORB_SELECTION_EVENT_COUNT];
static uint32_t orbLastSelectionEventMs;
static int orbSelectionEventNext;
static int orbSelectionEventCount;
static int orbObservedSelection = -1;

static uint32_t scrollOrbClockMs;
static uint32_t scrollFormationClockMs;
static uint32_t scrollOrbLastFrameMs;
static int scrollOrbClockInitialized;
static uint32_t scrollLetterStartMs;
static float scrollLetterFrom;
static int scrollLetterTarget;
static int scrollLetterInitialized;
static char scrollLetterGlyph;
static char scrollLetterPreviousGlyph;
static uint32_t scrollGlyphStartMs;

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

static float orbWave(uint32_t phase);
static void resetScrollFormation(void);

static uint32_t glassPhase(uint32_t elapsedMs, uint32_t periodMs, uint32_t offsetMs) {
  return (uint32_t)((((uint64_t)(elapsedMs + offsetMs)) << 16) / periodMs);
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

void resetAmbientOrbs(uint32_t startMs) {
  glassStartMs = startMs;
  systemConfigClockReady = 0;
  orbitStartMs = 0;
  splashStartMs = 0;
  resetAmbientOrbsScroll();
}

void setAmbientOrbsShapes(uint32_t enabledShapes, uint32_t now) {
  enabledShapes &= ORBS_SHAPES_ALL_MASK;
  if (enabledOrbShapes != enabledShapes) {
    enabledOrbShapes = enabledShapes;
    resetAmbientOrbs(now);
  }
}

void setAmbientOrbsTheme(AmbientOrbsTheme theme, uint32_t now) {
  if (theme != ORBS_THEME_PS2_ORIGINAL)
    theme = ORBS_THEME_LUNA;
  if (ambientOrbsTheme == theme)
    return;
  ambientOrbsTheme = theme;
  if (theme == ORBS_THEME_PS2_ORIGINAL) {
    // Sample the console clock once; the frame timer supplies its millisecond
    // fraction without asking the CDVD RPC service on every frame.
    const uint32_t stamp = getTimestamp();
    const uint32_t hour = (stamp >> 12) & 31U;
    const uint32_t minute = (stamp >> 6) & 63U;
    const uint32_t second = stamp & 63U;
    originalClockStartMs =
        ((hour < 24 ? hour : 0) * 3600U +
         (minute < 60 ? minute : 0) * 60U +
         (second < 60 ? second : 0)) * 1000U;
    originalClockAnchorMs = now;
    originalScatterPhase = stamp & 0xFFFFU;
  }
}

static uint32_t originalReadLE32(const unsigned char *bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

// TEXIMAGE is a ROMDIR inside the console BIOS. Its file offsets are
// relative to the start of TEXIMAGE and rounded up to 16-byte boundaries.
static int originalFindResource(FILE *rom, const char *name,
                                long *offset, uint32_t *size) {
  unsigned char entry[16];
  long fileOffset = 0;
  if (fseek(rom, 0, SEEK_SET))
    return -1;
  for (int index = 0; index < 256; index++) {
    if (fread(entry, 1, sizeof(entry), rom) != sizeof(entry) || !entry[0])
      break;
    const uint32_t fileSize = originalReadLE32(entry + 12);
    if (!memcmp(entry, name, strlen(name)) && entry[strlen(name)] == 0) {
      *offset = fileOffset;
      *size = fileSize;
      return 0;
    }
    fileOffset += (fileSize + 15U) & ~15U;
  }
  return -1;
}

static int originalExpandMask(FILE *rom, long offset, uint32_t size,
                              u32 *pixels) {
  // The retail TEXCNAVI and TEXCBLUR entries are smaller than 2 KiB.
  unsigned char source[4096];
  unsigned char mask[64 * 64];
  if (size < 8 || size > sizeof(source) ||
      fseek(rom, offset, SEEK_SET) ||
      fread(source, 1, size, rom) != size ||
      originalReadLE32(source) != sizeof(mask))
    return -1;
  uint32_t descriptor = 0;
  uint32_t position = 4;
  int flagsLeft = 0;
  int shift = 0, backMask = 0;
  for (int output = 0; output < sizeof(mask);) {
    if (!flagsLeft) {
      if (position + 4 > size)
        return -1;
      descriptor = ((uint32_t)source[position] << 24) |
                   ((uint32_t)source[position + 1] << 16) |
                   ((uint32_t)source[position + 2] << 8) |
                   source[position + 3];
      position += 4;
      int mode = descriptor & 3;
      shift = 14 - mode;
      backMask = 0x3fff >> mode;
      flagsLeft = 30;
    }
    if (position >= size)
      return -1;
    int value = source[position++];
    if (descriptor & 0x80000000U) {
      if (position >= size)
        return -1;
      int code = (value << 8) | source[position++];
      int back = 1 + (code & backMask);
      int count = 3 + (code >> shift);
      if (back > output || count > (int)sizeof(mask) - output)
        return -1;
      while (count--) {
        mask[output] = mask[output - back];
        output++;
      }
    } else {
      mask[output++] = value;
    }
    descriptor <<= 1;
    flagsLeft--;
  }
  for (int i = 0; i < sizeof(mask); i++)
    pixels[i] = 0xffffffU | (uint32_t)mask[i] << 24;
  return 0;
}

static void originalInitTexture(GSTEXTURE *texture, u32 *pixels) {
  memset(texture, 0, sizeof(*texture));
  texture->Width = 64;
  texture->Height = 64;
  texture->PSM = GS_PSM_CT32;
  texture->Mem = pixels;
  texture->Filter = GS_FILTER_LINEAR;
  texture->Delayed = GS_SETTING_ON;
}

static int originalLoadMasks(void) {
  if (originalMasksLoaded)
    return 0;
  FILE *rom = fopen("rom0:TEXIMAGE", "rb");
  if (rom == NULL)
    return -1;
  long haloOffset, coreOffset;
  uint32_t haloSize, coreSize;
  int result = originalFindResource(rom, "TEXCBLUR", &haloOffset, &haloSize) ||
               originalFindResource(rom, "TEXCNAVI", &coreOffset, &coreSize) ||
               originalExpandMask(rom, haloOffset, haloSize, originalHaloPixels) ||
               originalExpandMask(rom, coreOffset, coreSize, originalCorePixels);
  fclose(rom);
  if (result)
    return -1;
  originalInitTexture(&originalHaloTexture, originalHaloPixels);
  originalInitTexture(&originalCoreTexture, originalCorePixels);
  originalMasksLoaded = 1;
  return 0;
}

int loadAmbientOrbsSystemConfigAssets(void) {
  return originalLoadMasks();
}

int setAmbientOrbsAppearance(AmbientOrbsAppearance appearance) {
  if (appearance == ORBS_APPEARANCE_PS2_ORIGINAL && originalLoadMasks())
    return -1;
  ambientOrbsAppearance = appearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                          appearance : ORBS_APPEARANCE_LUNA;
  return 0;
}

void setAmbientOrbsColor(AmbientOrbsColorPart part, AmbientOrbsColor color) {
  if (color < ORBS_COLOR_ORIGINAL || color >= ORBS_COLOR_COUNT)
    color = ORBS_COLOR_ORIGINAL;
  if (part == ORBS_COLOR_PART_ORBS)
    ambientOrbsColor = color;
  else if (part == ORBS_COLOR_PART_TAILS)
    ambientTailsColor = color;
}

static void orbTintColor(int *red, int *green, int *blue,
                         AmbientOrbsColorPart part) {
  const AmbientOrbsColor color = part == ORBS_COLOR_PART_ORBS ?
                                  ambientOrbsColor : ambientTailsColor;
  if (!orbBackgroundColorsActive ||
      ambientOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL ||
      color == ORBS_COLOR_ORIGINAL)
    return;
  int brightness = *red;
  if (*green > brightness)
    brightness = *green;
  if (*blue > brightness)
    brightness = *blue;
  *red = orbPalette[color][0] * brightness / 255;
  *green = orbPalette[color][1] * brightness / 255;
  *blue = orbPalette[color][2] * brightness / 255;
}

static uint64_t orbLightColor(int red, int green, int blue, int alpha,
                              AmbientOrbsColorPart part) {
  const AmbientOrbsColor color = part == ORBS_COLOR_PART_ORBS ?
                                  ambientOrbsColor : ambientTailsColor;
  if (ambientOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL)
    return GS_SETREG_RGBA(red, green, blue, alpha);
  if (!orbBackgroundColorsActive || color == ORBS_COLOR_ORIGINAL)
    return glassLightColor(red, green, blue, alpha);
  orbTintColor(&red, &green, &blue, part);
  return GS_SETREG_RGBA(red, green, blue, alpha);
}

void resetAmbientOrbsOrbit(uint32_t now) {
  orbitStartMs = now - glassStartMs;
}

void resetAmbientOrbsSplash(uint32_t now) {
  splashStartMs = now - glassStartMs;
}

void resetAmbientOrbsScroll(void) {
  scrollOrbClockInitialized = 0;
  scrollFormationClockMs = 0;
  resetScrollFormation();
  scrollLetterInitialized = 0;
  scrollLetterGlyph = 0;
  scrollLetterPreviousGlyph = 0;
  orbObservedSelection = -1;
  orbSelectionEventNext = 0;
  orbSelectionEventCount = 0;
}

static uint32_t glassElapsedMs(uint32_t now) {
  return now - glassStartMs;
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

void observeAmbientOrbsSelection(int selectedTitleIdx, uint32_t now) {
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

static float scrollOrbEase(float progress) {
  if (progress <= 0.0f)
    return 0.0f;
  if (progress >= 1.0f)
    return 1.0f;
  return progress * progress * (3.0f - 2.0f * progress);
}

static float scrollLetterBlendAt(uint32_t elapsedMs) {
  if (!scrollLetterInitialized)
    return 0.0f;
  if (elapsedMs < scrollLetterStartMs)
    return scrollLetterFrom;
  const float progress = (elapsedMs - scrollLetterStartMs) /
                         (float)SCROLL_LETTER_MORPH_MS;
  return scrollLetterFrom +
         (scrollLetterTarget - scrollLetterFrom) * scrollOrbEase(progress);
}

static float scrollGlyphBlendAt(uint32_t elapsedMs) {
  if (elapsedMs < scrollGlyphStartMs)
    return 0.0f;
  return scrollOrbEase((elapsedMs - scrollGlyphStartMs) /
                       (float)SCROLL_GLYPH_MORPH_MS);
}

static char scrollTitleInitial(const char *title) {
  while (*title) {
    const char c = *title++;
    if (c >= 'A' && c <= 'Z')
      return c;
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 'A';
    if (c >= '0' && c <= '9')
      return '#';
  }
  return '#';
}

static void setScrollLetterMode(int fastScroll, char glyph,
                                uint32_t elapsedMs) {
  if (!scrollLetterInitialized) {
    scrollLetterStartMs = elapsedMs;
    scrollLetterFrom = 0.0f;
    scrollLetterTarget = 0;
    scrollLetterGlyph = glyph;
    scrollLetterPreviousGlyph = glyph;
    scrollGlyphStartMs = elapsedMs;
    scrollLetterInitialized = 1;
  }
  if (glyph != scrollLetterGlyph) {
    scrollLetterPreviousGlyph = scrollLetterGlyph;
    scrollLetterGlyph = glyph;
    scrollGlyphStartMs = elapsedMs;
  }
  if (fastScroll != scrollLetterTarget) {
    scrollLetterFrom = scrollLetterBlendAt(elapsedMs);
    scrollLetterStartMs = elapsedMs;
    scrollLetterTarget = fastScroll;
  }
}

static uint32_t orbAnimationMs(uint32_t elapsedMs, uint32_t movementMs) {
  // A slow phase swell changes speed smoothly without jumping between paths.
  float animated = (float)movementMs +
                   orbWave(glassPhase(movementMs, ORB_SPEED_SWELL_PERIOD_MS, 0)) *
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
  float breath;
  float scale;
} OrbMotion;

static OrbMotion orbMotion(uint32_t elapsedMs) {
  OrbMotion motion;
  motion.breath = orbWave(glassPhase(elapsedMs, ORB_SPREAD_PERIOD_MS, 0));
  // Fast grouping rides on a slower swell, as in the reference animation.
  const float slowScale = 80.0f + orbWave(glassPhase(elapsedMs,
                                  ORB_CONTRACT_PERIOD_MS, 0) + (8 << 11)) * 20.0f / 127.0f;
  const float pulseScale = 92.0f + orbWave(glassPhase(elapsedMs,
                                   ORB_PULSE_PERIOD_MS, 0) + (8 << 11)) * 8.0f / 127.0f;
  motion.scale = slowScale * pulseScale / 100.0f;
  return motion;
}

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

// Each group of four digits is one stroke on a 5 by 7 letter grid.
static const char *const scrollAlphabetPaths[27] = {
    "0620 2046 1343",                         // A
    "0006 0030 3041 4133 3303 3345 4536 3606", // B
    "4130 3010 1001 0105 0516 1636 3645", // C
    "0006 0030 3041 4145 4536 3606",       // D
    "0006 0040 0333 0646",                   // E
    "0006 0040 0333",                        // F
    "4130 3010 1001 0105 0516 1636 3645 4543 4323", // G
    "0006 4046 0343",                        // H
    "0040 2026 0646",                        // I
    "0040 4045 4536 3616 1605",            // J
    "0006 4003 0346",                        // K
    "0006 0646",                             // L
    "0600 0023 2340 4046",                  // M
    "0600 0046 4640",                       // N
    "0130 3041 4145 4536 3616 1605 0501", // O
    "0006 0030 3041 4133 3303",            // P
    "0130 3041 4145 4536 3616 1605 0501 2446", // Q
    "0006 0030 3041 4133 3303 2346",       // R
    "4130 3010 1001 0102 0213 1333 3344 4445 4536 3616 1605", // S
    "0040 2026",                             // T
    "0005 0516 1636 3645 4045",            // U
    "0026 2640",                             // V
    "0006 0614 1426 2634 3446 4640",       // W
    "0046 4006",                             // X
    "0023 4023 2326",                        // Y
    "0040 4006 0646",                        // Z
    "1016 3036 0242 0444"                   // #
};

static const char *scrollGlyphPath(char glyph) {
  return scrollAlphabetPaths[glyph >= 'A' && glyph <= 'Z' ?
                             glyph - 'A' : 26];
}

static int nextScrollGlyphStroke(const char **cursor, int *x1, int *y1,
                                  int *x2, int *y2) {
  while (**cursor == ' ')
    (*cursor)++;
  if (!**cursor)
    return 0;
  *x1 = (*cursor)[0] - '0';
  *y1 = (*cursor)[1] - '0';
  *x2 = (*cursor)[2] - '0';
  *y2 = (*cursor)[3] - '0';
  *cursor += 4;
  return 1;
}

typedef struct {
  uint32_t cycle;
  uint32_t phaseStartMs;
  uint32_t morphMs;
  uint32_t previousPhaseStartMs;
  uint32_t previousMorphMs;
  uint32_t random;
  uint32_t startMs;
  uint32_t epochMs;
  OrbShape from;
  OrbShape to;
  OrbShape previousFrom;
  OrbShape previousTo;
  OrbShape lastShape;
  int initialized;
} OrbFormation;

static OrbFormation scrollFormation;

static void resetScrollFormation(void) {
  scrollFormation.initialized = 0;
}

static OrbShape pickEnabledOrbShape(uint32_t mask, OrbShape previous,
                                     uint32_t choice) {
  mask &= ORBS_SHAPES_ALL_MASK;
  const uint32_t previousBit = previous < ORB_SHAPE_PLAYTIME ? 1U << previous : 0;
  if (mask & ~previousBit)
    mask &= ~previousBit;
  OrbShape candidates[ORB_SHAPE_PLAYTIME];
  int count = 0;
  for (int shape = 0; shape < ORB_SHAPE_PLAYTIME; shape++)
    if (mask & (1U << shape))
      candidates[count++] = (OrbShape)shape;
  return count ? candidates[choice % count] : ORB_SHAPE_PLAYTIME;
}

static OrbShape nextEnabledOrbitShape(OrbShape previous) {
  static const OrbShape order[] = {
    ORB_SHAPE_CUBE, ORB_SHAPE_OCTAHEDRON, ORB_SHAPE_LUNA,
    ORB_SHAPE_SKULL, ORB_SHAPE_ATOM, ORB_SHAPE_DIAMOND, ORB_SHAPE_SPHERE
  };
  int start = -1;
  for (int i = 0; i < ORB_SHAPE_PLAYTIME; i++)
    if (order[i] == previous)
      start = i;
  for (int offset = 1; offset <= ORB_SHAPE_PLAYTIME; offset++) {
    const OrbShape shape = order[(start + offset) % ORB_SHAPE_PLAYTIME];
    if (enabledOrbShapes & (1U << shape))
      return shape;
  }
  return ORB_SHAPE_PLAYTIME;
}

static OrbFormation *orbFormationAt(uint32_t elapsedMs, int formationMode) {
  static OrbFormation sharedFormation;
  static OrbFormation orbitFormation;
  static OrbFormation splashFormation;
  OrbFormation *formation = formationMode == 3 ? &scrollFormation :
                            formationMode == 2 ? &splashFormation :
                            formationMode == 1 ? &orbitFormation : &sharedFormation;
  const uint32_t epochMs = formationMode == 2 ? splashStartMs :
                           formationMode == 1 ? orbitStartMs : 0;
  const uint32_t shapeMs = ORB_FORMATION_PERIOD_MS;
  const uint32_t pairMs = shapeMs + ORB_PLAYTIME_MS;
  const uint32_t relativeMs = elapsedMs >= epochMs ? elapsedMs - epochMs : 0;
  const uint32_t cycle = formationMode == 2 ?
                         relativeMs / ORB_SPLASH_FORMATION_PERIOD_MS :
                         (relativeMs / pairMs) * 2U +
                         (relativeMs % pairMs >= shapeMs ? 1U : 0U);
  if (!formation->initialized || formation->startMs != glassStartMs ||
      formation->epochMs != epochMs || cycle < formation->cycle) {
    formation->cycle = 0;
    formation->phaseStartMs = epochMs;
    formation->morphMs = 0;
    formation->previousPhaseStartMs = epochMs;
    formation->previousMorphMs = 0;
    formation->random = glassStartMs ^ 0xA341316CU ^
                        ((uint32_t)formationMode * 0x9E3779B9U);
    formation->startMs = glassStartMs;
    formation->epochMs = epochMs;
    formation->from = formationMode == 2 ? ORB_SHAPE_LUNA :
                      formationMode == 1 ? nextEnabledOrbitShape(ORB_SHAPE_PLAYTIME) :
                      pickEnabledOrbShape(enabledOrbShapes, ORB_SHAPE_PLAYTIME, 0);
    formation->to = formation->from;
    formation->previousFrom = formation->from;
    formation->previousTo = formation->to;
    formation->lastShape = formation->to;
    formation->initialized = 1;
  }
  while (formation->cycle < cycle) {
    formation->previousFrom = formation->from;
    formation->previousTo = formation->to;
    formation->previousPhaseStartMs = formation->phaseStartMs;
    formation->previousMorphMs = formation->morphMs;
    formation->from = formation->to;
    formation->cycle++;
    if (formationMode == 2) {
      // Loading owns a fixed LUNA <-> Cube loop, separate from Playtime.
      formation->phaseStartMs = formation->epochMs +
          formation->cycle * ORB_SPLASH_FORMATION_PERIOD_MS;
      formation->to = formation->from == ORB_SHAPE_LUNA ?
                       ORB_SHAPE_CUBE : ORB_SHAPE_LUNA;
      formation->morphMs = ORB_SPLASH_FORMATION_MORPH_MS;
      continue;
    }
    formation->phaseStartMs = formation->epochMs +
        (formation->cycle / 2U) * pairMs +
        (formation->cycle & 1U ? shapeMs : 0U);
    if (formation->cycle & 1U) {
      formation->lastShape = formation->from;
      formation->to = ORB_SHAPE_PLAYTIME;
      formation->morphMs = ORB_PLAYTIME_ENTRY_MS;
    } else if (formationMode == 1) {
      formation->to = nextEnabledOrbitShape(formation->lastShape);
      formation->morphMs = ORB_FORMATION_MORPH_MS;
    } else {
      formation->random = formation->random * 1664525U + 1013904223U;
      formation->to = pickEnabledOrbShape(enabledOrbShapes, formation->lastShape,
                                           formation->random);
      formation->morphMs = ORB_FORMATION_MORPH_MS;
    }
  }
  return formation;
}

static float orbLogoScale(int radiusX, const OrbMotion *motion) {
  const float breath = 0.85f + motion->scale / 500.0f;
  return radiusX * 2.4f * breath / 710.0f;
}

static void orbPlaytimePosition(int index, uint32_t animationMs,
                                const OrbMotion *motion,
                                float *x, float *y, float *depth) {
  // Three staggered groups roam the screen on different horizontal and
  // vertical cycles. Each group's followers weave around their leader.
  const int group = index / 4;
  const int follower = index % 4;
  const uint32_t travelMs = animationMs + 1600U - follower * 420U;
  const uint32_t phaseX = glassPhase(travelMs, ORB_PLAYTIME_SWEEP_X_MS,
                                      (uint32_t)group * 4000U);
  const uint32_t phaseY = glassPhase(travelMs, ORB_PLAYTIME_SWEEP_Y_MS,
                                      (uint32_t)group * 5300U);
  const float weave = orbWave(phaseX * 2U +
                              (group & 1 ? 8192U : 0U)) / 127.0f;
  const float breath = 1.0f + motion->breath * 0.02f / 127.0f;
  const float width = gsGlobal->Width;
  const float height = gsGlobal->Height;
  *x = width * (0.5f + breath *
                (orbWave(phaseX + 16384U) / 127.0f * 0.41f +
                 weave * 0.012f));
  *y = height * (0.5f + breath *
                 (orbWave(phaseY) / 127.0f * 0.36f +
                  weave * (follower & 1 ? 0.018f : -0.018f)));
  *depth = 65.0f + orbWave(phaseX) * 24.0f / 127.0f;
}

// Share the projected curves between the lights and their formation outlines.
static void orbCurvedShapePoint(GlassPoint *point, OrbShape shape, int ring,
                                 uint32_t phase, uint32_t elapsedMs,
                                 const OrbMotion *motion, int centerX, int centerY,
                                 int radiusX, int radiusY) {
  float sourceX, sourceY, sourceZ;
  uint32_t yaw, pitch, roll;
  if (shape == ORB_SHAPE_SKULL) {
    phase &= 65535U;
    if (ring == 0 && phase <= 32768U) {
      // A round cranium joins the cheekbones and narrower squared jaw.
      sourceX = orbWave(phase + 16384U);
      sourceY = 25.0f + orbWave(phase) * 105.0f / 127.0f;
    } else if (ring == 0) {
      static const int jaw[10][2] = {
        {-127, 25}, {-105, -5}, {-75, -18}, {-65, -48}, {-65, -85},
        {65, -85}, {65, -48}, {75, -18}, {105, -5}, {127, 25}
      };
      const float along = (phase - 32768U) * 9.0f / 32768.0f;
      const int segment = (int)along;
      const float blend = along - segment;
      sourceX = jaw[segment][0] +
                (jaw[segment + 1][0] - jaw[segment][0]) * blend;
      sourceY = jaw[segment][1] +
                (jaw[segment + 1][1] - jaw[segment][1]) * blend;
    } else if (ring <= 2) {
      sourceX = (ring == 1 ? -48.0f : 48.0f) +
                orbWave(phase + 16384U) * 28.0f / 127.0f;
      sourceY = 35.0f + orbWave(phase) * 32.0f / 127.0f;
    } else if (ring == 3) {
      static const int nose[4][2] = {
        {-15, -15}, {0, 9}, {15, -15}, {-15, -15}
      };
      const float along = phase * 3.0f / 65536.0f;
      const int segment = (int)along;
      const float blend = along - segment;
      sourceX = nose[segment][0] +
                (nose[segment + 1][0] - nose[segment][0]) * blend;
      sourceY = nose[segment][1] +
                (nose[segment + 1][1] - nose[segment][1]) * blend;
    } else {
      // One continuous route lets a light draw the mouth and teeth.
      static const int teeth[10][2] = {
        {-65, -48}, {-30, -48}, {-30, -85}, {0, -85}, {0, -48},
        {30, -48}, {30, -85}, {65, -85}, {65, -48}, {-65, -48}
      };
      const float along = phase * 9.0f / 65536.0f;
      const int segment = (int)along;
      const float blend = along - segment;
      sourceX = teeth[segment][0] +
                (teeth[segment + 1][0] - teeth[segment][0]) * blend;
      sourceY = teeth[segment][1] +
                (teeth[segment + 1][1] - teeth[segment][1]) * blend;
    }
    sourceZ = ring == 0 ? 0.0f : 10.0f;
    yaw = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 11000, 0)) * 18.0f);
    pitch = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 9700, 0)) * 7.0f);
    roll = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 8600, 0)) * 9.0f);
  } else {
    // Three narrow ellipses tilted sixty degrees apart make an atom silhouette.
    const float alongX = orbWave(phase + 16384U);
    const float alongY = orbWave(phase) * 0.34f;
    const uint32_t tilt = (uint32_t)ring * 10923U;
    const float sine = orbWave(tilt) / 127.0f;
    const float cosine = orbWave(tilt + 16384U) / 127.0f;
    sourceX = alongX * cosine - alongY * sine;
    sourceY = alongX * sine + alongY * cosine;
    sourceZ = orbWave(phase) * 0.65f;
    yaw = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 12000, 0)) * 14.0f);
    pitch = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 10300, 0)) * 8.0f);
    roll = (uint32_t)(int)(orbWave(glassPhase(elapsedMs, 14000, 0)) * 7.0f);
  }
  const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
  projectCrystalPoint(point, centerX, centerY, smallerRadius * 83 / 100,
                      yaw, pitch, roll, (int)sourceX, (int)sourceY, (int)sourceZ);
  const float breath = 0.85f + motion->scale / 500.0f;
  point->x = centerX + (point->x - centerX) * breath;
  point->y = centerY + (point->y - centerY) * breath;
}

static void orbSkullLightPath(int index, uint32_t elapsedMs,
                              int *ring, uint32_t *phase) {
  if (index < 6) {
    *ring = 0;
    *phase = glassPhase(elapsedMs, 3300, 0) + (uint32_t)index * 65536U / 6U;
  } else if (index < 10) {
    *ring = 1 + (index - 6) / 2;
    *phase = glassPhase(elapsedMs, 1600U + (uint32_t)*ring * 180U, 0) +
             (uint32_t)(index & 1) * 32768U;
  } else {
    *ring = index == 10 ? 3 : 4;
    *phase = glassPhase(elapsedMs, index == 10 ? 1300U : 2400U, 0);
  }
}

static void orbPatternPosition(OrbShape shape, int index, uint32_t elapsedMs,
                               const OrbMotion *motion, int centerX, int centerY,
                               int radiusX, int radiusY, float *x, float *y,
                               float *depth) {
  if (shape == ORB_SHAPE_PLAYTIME) {
    orbPlaytimePosition(index, elapsedMs, motion, x, y, depth);
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
  if (shape == ORB_SHAPE_SKULL || shape == ORB_SHAPE_ATOM) {
    int ring = index / 4;
    uint32_t phase;
    if (shape == ORB_SHAPE_SKULL) {
      // Every light moves: six trace the outline, four the eyes, two the face.
      orbSkullLightPath(index, elapsedMs, &ring, &phase);
    } else {
      phase = glassPhase(elapsedMs, 2500U + (uint32_t)ring * 280U, 0);
      if (ring == 1)
        phase = 0U - phase;
      phase += (uint32_t)(index & 3) * 16384U + (uint32_t)ring * 8192U;
    }
    GlassPoint point;
    orbCurvedShapePoint(&point, shape, ring, phase, elapsedMs, motion,
                        centerX, centerY, radiusX, radiusY);
    *x = point.x;
    *y = point.y;
    *depth = 65.0f + point.depth / 5.0f;
    return;
  }
  if (shape == ORB_SHAPE_DIAMOND) {
    // A cut gem: four table corners, four wider girdle points, and a culet.
    // Three extra lights travel along facets to use the full orb set.
    static const int vertices[9][3] = {
      {-55, 80, -55}, {55, 80, -55},
      {55, 80, 55}, {-55, 80, 55},
      {0, 0, -127}, {127, 0, 0},
      {0, 0, 127}, {-127, 0, 0},
      {0, -127, 0}
    };
    static const int accentEdges[3][2] = {
      {0, 4}, {2, 6}, {5, 8}
    };
    int source[3];
    if (index < 9) {
      for (int axis = 0; axis < 3; axis++)
        source[axis] = vertices[index][axis];
    } else {
      const int accent = index - 9;
      const int a = accentEdges[accent][0];
      const int b = accentEdges[accent][1];
      const uint32_t phase = glassPhase(elapsedMs, 2600,
                                        (uint32_t)accent * 370U);
      const float along = 0.5f + orbWave(phase) * 0.30f / 127.0f;
      for (int axis = 0; axis < 3; axis++)
        source[axis] = vertices[a][axis] +
                       (int)((vertices[b][axis] - vertices[a][axis]) * along);
    }
    const uint32_t yaw = glassPhase(elapsedMs, ORB_DIAMOND_ROTATION_MS, 0);
    const uint32_t pitch = (4 << 11) +
        (int)(orbWave(glassPhase(elapsedMs, 11000, 0)) * (2 << 11) / 127.0f);
    const uint32_t roll =
        (int)(orbWave(glassPhase(elapsedMs, 9700, 1900)) * (1 << 11) / 127.0f);
    const int smallerRadius = radiusX < radiusY ? radiusX : radiusY;
    GlassPoint point;
    projectCrystalPoint(&point, centerX, centerY,
                        smallerRadius * 82 / 100, yaw, pitch, roll,
                        source[0], source[1], source[2]);
    const float breath = 0.85f + motion->scale / 500.0f;
    *x = centerX + (point.x - centerX) * breath;
    *y = centerY + (point.y - centerY) * breath;
    *depth = 65.0f + point.depth / 5.0f;
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

  *x = centerX;
  *y = centerY;
  *depth = 65.0f;
}

static void orbFormationPosition(const OrbFormation *formation, int index,
                                  uint32_t sampleMs, uint32_t animationMs,
                                  const OrbMotion *motion,
                                  int centerX, int centerY, int radiusX,
                                  int radiusY, float *x, float *y,
                                  float *depth, float *opacity) {
  // Trail samples can cross a phase boundary; retain the preceding phase so
  // they follow the path actually traveled instead of stretching to a new one.
  const int previous = formation->cycle > 0 &&
                       sampleMs < formation->phaseStartMs;
  const OrbShape from = previous ? formation->previousFrom : formation->from;
  const OrbShape to = previous ? formation->previousTo : formation->to;
  const uint32_t phaseStartMs = previous ? formation->previousPhaseStartMs :
                                 formation->phaseStartMs;
  const uint32_t morphMs = previous ? formation->previousMorphMs :
                           formation->morphMs;
  float blend = !morphMs || sampleMs <= phaseStartMs ? 0.0f :
                (sampleMs - phaseStartMs) / (float)morphMs;
  if (blend > 1.0f)
    blend = 1.0f;
  blend = blend * blend * (3.0f - 2.0f * blend);
  if (blend <= 0.0f) {
    orbPatternPosition(from, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY, x, y, depth);
  } else if (blend >= 1.0f) {
    orbPatternPosition(to, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY, x, y, depth);
  } else {
    float fromX, fromY, fromDepth, toX, toY, toDepth;
    orbPatternPosition(from, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY,
                       &fromX, &fromY, &fromDepth);
    orbPatternPosition(to, index, animationMs, motion,
                       centerX, centerY, radiusX, radiusY,
                       &toX, &toY, &toDepth);
    *x = fromX + (toX - fromX) * blend;
    *y = fromY + (toY - fromY) * blend;
    *depth = fromDepth + (toDepth - fromDepth) * blend;
  }
  *opacity = 1.0f;
}

static void scrollGlyphPoint(char glyph, int index, uint32_t animationMs,
                              int centerX, int centerY, int radiusX,
                              int radiusY, float *x, float *y, float *depth) {
  const char *cursor = scrollGlyphPath(glyph);
  int x1, y1, x2, y2;
  int strokeCount = 0;
  while (nextScrollGlyphStroke(&cursor, &x1, &y1, &x2, &y2))
    strokeCount++;
  const int wantedStroke = index % strokeCount;
  cursor = scrollGlyphPath(glyph);
  for (int stroke = 0; nextScrollGlyphStroke(&cursor, &x1, &y1, &x2, &y2);
       stroke++) {
    if (stroke == wantedStroke)
      break;
  }
  // Each orb sweeps one stroke back and forth; the short afterimages write
  // the letter without leaving a permanent geometric outline.
  const uint32_t phase = glassPhase(animationMs,
                                    1080U + (uint32_t)(index % 3) * 90U,
                                    (uint32_t)index * 211U);
  const float along = 0.5f + orbWave(phase) / 254.0f;
  const float gridX = x1 + (x2 - x1) * along;
  const float gridY = y1 + (y2 - y1) * along;
  *x = centerX + (gridX - 2.0f) * radiusX * 1.35f / 4.0f;
  *y = centerY + (gridY - 3.0f) * radiusY * 1.45f / 6.0f;
  const float near = 0.5f + orbWave(glassPhase(animationMs, 1800,
                                               (uint32_t)index * 173U)) /
                                254.0f;
  *x += (1.0f - near) * SCROLL_GLYPH_DEPTH_X;
  *y += (1.0f - near) * SCROLL_GLYPH_DEPTH_Y;
  *depth = 70.0f + (near * 2.0f - 1.0f) * 18.0f;
}

static void blendScrollLetterOrb(int index, float letterBlend,
                                 float glyphBlend, uint32_t animationMs,
                                 int centerX, int centerY, int radiusX,
                                 int radiusY, float *x, float *y,
                                 float *depth, float *opacity) {
  if (letterBlend <= 0.0f)
    return;
  float letterX, letterY, letterDepth;
  scrollGlyphPoint(scrollLetterGlyph, index, animationMs,
                   centerX, centerY, radiusX, radiusY,
                   &letterX, &letterY, &letterDepth);
  if (scrollLetterPreviousGlyph != scrollLetterGlyph && glyphBlend < 1.0f) {
    float oldX, oldY, oldDepth;
    scrollGlyphPoint(scrollLetterPreviousGlyph, index, animationMs,
                     centerX, centerY, radiusX, radiusY,
                     &oldX, &oldY, &oldDepth);
    letterX = oldX + (letterX - oldX) * glyphBlend;
    letterY = oldY + (letterY - oldY) * glyphBlend;
    letterDepth = oldDepth + (letterDepth - oldDepth) * glyphBlend;
  }
  *x += (letterX - *x) * letterBlend;
  *y += (letterY - *y) * letterBlend;
  *depth += (letterDepth - *depth) * letterBlend;
  *opacity += (1.0f - *opacity) * letterBlend;
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
                        orbLightColor(0x40, 0x78, 0xC8,
                                      (int)(alpha * fromFade * 0.45f),
                                      ORBS_COLOR_PART_TAILS),
                        orbLightColor(0x40, 0x78, 0xC8,
                                      (int)(alpha * toFade * 0.45f),
                                      ORBS_COLOR_PART_TAILS));
    drawOrbTrailSegment(ax, ay, bx, by,
                        width * fromFade, width * toFade, z,
                        orbLightColor(0xA0, 0xD8, 0xFF,
                                      (int)(alpha * fromFade),
                                      ORBS_COLOR_PART_TAILS),
                        orbLightColor(0xA0, 0xD8, 0xFF,
                                      (int)(alpha * toFade),
                                      ORBS_COLOR_PART_TAILS));
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
                      orbLightColor(0x40, 0x78, 0xC8, alpha / 2,
                                    ORBS_COLOR_PART_TAILS),
                      orbLightColor(0x40, 0x78, 0xC8, alpha / 3,
                                    ORBS_COLOR_PART_TAILS));
  drawOrbTrailSegment(x1, y1, tipX, tipY, width, width * 0.65f, z,
                      orbLightColor(0xA0, 0xD8, 0xFF, alpha,
                                    ORBS_COLOR_PART_TAILS),
                      orbLightColor(0xE0, 0xF0, 0xFF, alpha / 2,
                                    ORBS_COLOR_PART_TAILS));
}

static void drawOrbCurvedGuides(OrbShape shape, float extent,
                                int centerX, int centerY, int radiusX,
                                int radiusY, uint32_t animationMs,
                                const OrbMotion *motion, int z, int alpha) {
  if (shape == ORB_SHAPE_SKULL) {
    // Moving, fading strokes follow each light instead of a fixed skull guide.
    for (int index = 0; index < ORB_COUNT; index++) {
      int ring;
      uint32_t phase;
      orbSkullLightPath(index, animationMs, &ring, &phase);
      GlassPoint a;
      orbCurvedShapePoint(&a, shape, ring, phase, animationMs, motion,
                          centerX, centerY, radiusX, radiusY);
      const uint32_t historyMs = index < 6 ? 650U : index < 10 ? 600U :
                                 index == 10 ? 650U : 900U;
      for (int step = 1; step <= 12; step++) {
        const uint32_t age = (uint32_t)(step * historyMs * extent / 12.0f);
        const uint32_t sampleMs = animationMs > age ? animationMs - age : 0;
        orbSkullLightPath(index, sampleMs, &ring, &phase);
        GlassPoint b;
        orbCurvedShapePoint(&b, shape, ring, phase, sampleMs, motion,
                            centerX, centerY, radiusX, radiusY);
        const float headFade = (13 - step) / 12.0f;
        const float tailFade = (12 - step) / 12.0f;
        drawOrbTrailSegment(a.x, a.y, b.x, b.y,
                            6.0f * headFade, 6.0f * tailFade, z,
                            orbLightColor(0x40, 0x78, 0xC8,
                                          (int)(alpha * headFade * 0.45f),
                                          ORBS_COLOR_PART_TAILS),
                            orbLightColor(0x40, 0x78, 0xC8,
                                          (int)(alpha * tailFade * 0.45f),
                                          ORBS_COLOR_PART_TAILS));
        drawOrbTrailSegment(a.x, a.y, b.x, b.y,
                            2.5f * headFade, 2.5f * tailFade, z,
                            orbLightColor(0xA0, 0xD8, 0xFF,
                                          (int)(alpha * headFade),
                                          ORBS_COLOR_PART_TAILS),
                            orbLightColor(0xA0, 0xD8, 0xFF,
                                          (int)(alpha * tailFade),
                                          ORBS_COLOR_PART_TAILS));
        a = b;
      }
    }
    return;
  }
  const int steps = 16;
  for (int ring = 0; ring < 3; ring++) {
    GlassPoint a;
    orbCurvedShapePoint(&a, shape, ring, 0, animationMs, motion,
                        centerX, centerY, radiusX, radiusY);
    for (int step = 1; step <= steps; step++) {
      GlassPoint b;
      orbCurvedShapePoint(&b, shape, ring, (uint32_t)step * 65536U / steps,
                          animationMs, motion, centerX, centerY, radiusX, radiusY);
      int edgeAlpha = (int)(alpha *
                            (0.55f + (a.depth + b.depth + 254) / 800.0f));
      if (edgeAlpha > 0x50)
        edgeAlpha = 0x50;
      drawOrbTailStroke(a.x, a.y, b.x, b.y, extent * steps - (step - 1),
                        2.5f, edgeAlpha, z);
      a = b;
    }
  }
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
  static const int diamondEdges[][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {0, 7}, {1, 4}, {1, 5},
    {2, 5}, {2, 6}, {3, 6}, {3, 7},
    {4, 8}, {5, 8}, {6, 8}, {7, 8}
  };
  if (extent <= 0.0f || shape == ORB_SHAPE_PLAYTIME ||
      shape == ORB_SHAPE_SPHERE || shape == ORB_SHAPE_LUNA)
    return;
  if (shape == ORB_SHAPE_SKULL || shape == ORB_SHAPE_ATOM) {
    drawOrbCurvedGuides(shape, extent, centerX, centerY, radiusX, radiusY,
                        animationMs, motion, z, alpha);
    return;
  }
  float x[ORB_COUNT], y[ORB_COUNT], depth;
  for (int i = 0; i < ORB_COUNT; i++)
    orbPatternPosition(shape, i, animationMs, motion, centerX, centerY,
                       radiusX, radiusY, &x[i], &y[i], &depth);
  const int (*edges)[2] = shape == ORB_SHAPE_DIAMOND ? diamondEdges :
                          shape == ORB_SHAPE_CUBE ? cubeEdges : octaEdges;
  const int edgeCount = shape == ORB_SHAPE_CUBE ?
                         (int)(sizeof(cubeEdges) / sizeof(cubeEdges[0])) :
                         shape == ORB_SHAPE_DIAMOND ?
                         (int)(sizeof(diamondEdges) / sizeof(diamondEdges[0])) :
                         (int)(sizeof(octaEdges) / sizeof(octaEdges[0]));
  for (int i = 0; i < edgeCount; i++) {
    const int a = edges[i][0], b = edges[i][1];
    drawOrbTailStroke(x[a], y[a], x[b], y[b], extent, 3.5f, alpha, z);
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

static void drawOrbLogoWordmark(int centerX, int centerY, float scale,
                                 int z, int alpha, float extent) {
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

// The retail OSDSYS menu uses seven lights on a single clock-driven 3D ring.
// Seconds give each light 21..27 laps per minute, while minutes turn the ring
// 1100 times per hour. The second hand spins the whole ring, the hour hand
// sets its tilt, and the minute hand sets its size.
static void originalOrbPoint(int index, uint32_t now,
                             int centerX, int centerY, int radiusX,
                             int radiusY, float *x, float *y, float *depth) {
  const uint32_t elapsed = now >= originalClockAnchorMs ?
                           now - originalClockAnchorMs : 0;
  const uint64_t clockMs = (uint64_t)originalClockStartMs + elapsed;
  const uint32_t secondsPhase = (uint32_t)(((clockMs % 60000U) << 16) / 60000U);
  const uint32_t minutePhase = (uint32_t)(((clockMs % 3600000U) *
                                           1100U * 65536U) / 3600000U);
  const uint32_t hourPhase = (uint32_t)(((clockMs % 43200000U) << 16) /
                                        43200000U);
  const uint32_t phase = secondsPhase * (uint32_t)(index + 21);
  const float orbitSine = orbWave(phase) / 127.0f;
  const float orbitCosine = orbWave(phase + 16384U) / 127.0f;
  // RotY(second), RotZ(180), RotY(minute*1100) reduce to this phase for
  // the translated point. The ROM eases the second angle toward the clock;
  // sampling it directly keeps this stateless trail path continuous.
  const uint32_t ringPhase = secondsPhase - minutePhase;
  const float tumbleSine = orbWave(ringPhase) / 127.0f;
  const float tumbleCosine = orbWave(ringPhase + 16384U) / 127.0f;
  const float tiltSine = orbWave(hourPhase) / 127.0f;
  const float tiltCosine = orbWave(hourPhase + 16384U) / 127.0f;
  const float side = orbitSine * tumbleSine;
  const float forward = orbitSine * tumbleCosine;
  const float up = -orbitCosine;
  const float radius = (10.0f + 7.25f *
                       (clockMs % 3600000U) / 3600000.0f) / 13.625f;
  const float perspective = 1.0f + forward * 0.23f;
  *x = centerX + (side * tiltCosine - up * tiltSine) *
                   radiusX * radius / perspective;
  *y = centerY + (side * tiltSine + up * tiltCosine) *
                   radiusY * radius / perspective;
  *depth = forward;

  // The first menu entry scatters the lights across the screen before they
  // settle. Use the same fade window as the ROM's 128-frame entry.
  const float progress = elapsed >= 2135U ? 1.0f : elapsed / 2135.0f;
  const float scatter = 1.0f - progress * progress * (3.0f - 2.0f * progress);
  const uint32_t scatterPhase = originalScatterPhase + (uint32_t)index * 9362U;
  *x += orbWave(scatterPhase + 16384U) / 127.0f * radiusX * 1.6f * scatter;
  *y += orbWave(scatterPhase) / 127.0f * radiusY * 1.3f * scatter;
}

static void drawOriginalOrbDisc(float x, float y, float radiusX,
                                float radiusY, int z, uint64_t centerColor,
                                uint64_t edgeColor) {
  for (int i = 0; i < 16; i++) {
    const uint32_t phase = (uint32_t)i * 4096U;
    const uint32_t next = phase + 4096U;
    gsKit_prim_triangle_gouraud(gsGlobal, x, y,
        x + orbWave(phase + 16384U) * radiusX / 127.0f,
        y + orbWave(phase) * radiusY / 127.0f,
        x + orbWave(next + 16384U) * radiusX / 127.0f,
        y + orbWave(next) * radiusY / 127.0f,
        z, centerColor, edgeColor, edgeColor);
  }
}

static void drawOriginalMask(GSTEXTURE *texture, float x, float y,
                             float radiusX, float radiusY, int z,
                             int red, int green, int blue, int alpha) {
  if (alpha <= 0)
    return;
  if (alpha > 0x80)
    alpha = 0x80;
  if (!bindTextureSafe(gsGlobal, texture))
    return;
  gsKit_prim_sprite_texture(gsGlobal, texture,
                            x - radiusX, y - radiusY, 0.0f, 0.0f,
                            x + radiusX, y + radiusY, 63.0f, 63.0f, z,
                            GS_SETREG_RGBA(red, green, blue, alpha));
}

static void drawOriginalOrbSprite(float x, float y, float size, int z,
                                  int red, int green, int blue, float opacity,
                                  int softenMasks) {
  const float haloX = 30.0f * size;
  const float haloY = 15.0f * size;
  const int haloAlpha = (int)(0x3c * opacity);
  const int coreAlpha = (int)(0x80 * opacity);
  int coreRed = 0x80, coreGreen = 0x80, coreBlue = 0x80;
  orbTintColor(&red, &green, &blue, ORBS_COLOR_PART_ORBS);
  orbTintColor(&coreRed, &coreGreen, &coreBlue, ORBS_COLOR_PART_ORBS);
  // Other orb backgrounds approximate the ROM's softness within the mask.
  // System Configuration now has the real scene-wide five-pass blur, so
  // draw its halo once at the source alpha and let that postprocess soften it.
  static const int offsets[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
  for (int tap = 0; softenMasks && tap < 4; tap++)
    drawOriginalMask(&originalHaloTexture, x + offsets[tap][0] * size,
                     y + offsets[tap][1] * size, haloX * 1.05f,
                     haloY * 1.05f, z, red, green, blue, haloAlpha / 10);
  drawOriginalMask(&originalHaloTexture, x, y, haloX, haloY,
                   z, red, green, blue, softenMasks ? haloAlpha * 3 / 4 : haloAlpha);
  drawOriginalMask(&originalCoreTexture, x, y, 4.5f * size, 2.25f * size,
                   z, coreRed, coreGreen, coreBlue, coreAlpha);
}

static void systemConfigOrbPoint(const SystemConfigOrbScene *scene,
                                 int index, uint32_t age,
                                 float *x, float *y, float *depth) {
  if (age > scene->elapsedMs) age = scene->elapsedMs;
  const uint64_t clockMs = scene->clockMs - age;
  const uint32_t spin = (uint16_t)scene->spin -
                         (uint32_t)((uint64_t)age * 65536U / 60000U);
  const uint32_t tilt = (uint16_t)scene->tilt -
                         (uint32_t)((uint64_t)age * 65536U / 43200000U);
  float wx, wy, wz;
  ps2MenuOrbWorld(index, clockMs, scene->radius, tilt, spin, &wx, &wy, &wz);
  ps2MenuCamera(&wx, &wy, &wz, 0);
  ps2MenuProject(wx, wy, wz, gsGlobal->Width, gsGlobal->Height, x, y);
  // The shared sprite renderer's size becomes exactly 103 / camera Z.
  *depth = (wz / 103.0f - 1.0f) / 0.23f;
}

static void drawOriginalOrbs(int centerX, int centerY, int radiusX,
                              int radiusY, uint32_t now, int trailZ,
                              int forceBiosMasks, const SystemConfigOrbScene *scene) {
  static const int entryColor[ORB_ORBIT_COUNT][3] = {
      {0x00, 0x00, 0x80}, {0x00, 0x80, 0x00},
      {0x00, 0x80, 0x80}, {0x80, 0x00, 0x00},
      {0x80, 0x00, 0x44}, {0x80, 0x44, 0x00},
      {0x80, 0x80, 0x80}};
  const uint32_t elapsed = scene ? scene->elapsedMs : now >= originalClockAnchorMs ?
                           now - originalClockAnchorMs : 0;
  const float entry = scene || elapsed >= 2135U ? 0.0f :
                      1.0f - elapsed / 2135.0f;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  for (int i = 0; i < ORB_ORBIT_COUNT; i++) {
    const int red = (int)(0x30 + (entryColor[i][0] - 0x30) * entry);
    const int green = (int)(0x62 + (entryColor[i][1] - 0x62) * entry);
    const int blue = (int)(0x80 + (entryColor[i][2] - 0x80) * entry);
    int tailRed = red, tailGreen = green, tailBlue = blue;
    orbTintColor(&tailRed, &tailGreen, &tailBlue, ORBS_COLOR_PART_TAILS);
    float headX, headY, headDepth;
    if (scene)
      systemConfigOrbPoint(scene, i, 0, &headX, &headY, &headDepth);
    else
      originalOrbPoint(i, now, centerX, centerY, radiusX, radiusY,
                       &headX, &headY, &headDepth);
    float newerX = headX, newerY = headY;
    for (int segment = 1; segment <= 43; segment++) {
      const uint32_t age = (uint32_t)segment * 50U;
      const uint32_t sampleNow = age < elapsed ? now - age :
                                  originalClockAnchorMs;
      float olderX, olderY, olderDepth;
      if (scene)
        systemConfigOrbPoint(scene, i, age, &olderX, &olderY, &olderDepth);
      else
        originalOrbPoint(i, sampleNow, centerX, centerY, radiusX, radiusY,
                         &olderX, &olderY, &olderDepth);
      const float oldFade = segment < 43 ? 1.0f - segment / 43.0f : 0.0f;
      const float newFade = 1.0f - (segment - 1) / 43.0f;
      const int oldRed = (int)(tailRed * oldFade * oldFade * oldFade * oldFade);
      const int oldGreen = (int)(tailGreen * oldFade * oldFade);
      const int oldBlue = (int)(tailBlue * oldFade);
      const int newRed = (int)(tailRed * newFade * newFade * newFade * newFade);
      const int newGreen = (int)(tailGreen * newFade * newFade);
      const int newBlue = (int)(tailBlue * newFade);
      drawOrbTrailSegment(olderX, olderY, newerX, newerY,
                          1.2f, 1.6f, trailZ,
                          GS_SETREG_RGBA(oldRed, oldGreen, oldBlue,
                                          (int)(0x40 * oldFade)),
                          GS_SETREG_RGBA(newRed, newGreen, newBlue,
                                          (int)(0x40 * newFade)));
      newerX = olderX;
      newerY = olderY;
    }
    const float size = 1.0f / (1.0f + headDepth * 0.23f);
    if ((forceBiosMasks || ambientOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL) &&
        originalMasksLoaded) {
      drawOriginalOrbSprite(headX, headY, size, trailZ + 1,
                            red, green, blue, 1.0f, scene == NULL);
    } else {
      drawOriginalOrbDisc(headX, headY, 30.0f * size, 15.0f * size,
                           trailZ + 1,
                           orbLightColor(red, green, blue, 0x3C,
                                         ORBS_COLOR_PART_ORBS),
                           orbLightColor(red, green, blue, 0,
                                         ORBS_COLOR_PART_ORBS));
      drawOriginalOrbDisc(headX, headY, 4.5f * size, 2.25f * size,
                           trailZ + 1,
                           orbLightColor(0x80, 0x80, 0x80, 0x80,
                                         ORBS_COLOR_PART_ORBS),
                           orbLightColor(0x80, 0x80, 0x80, 0,
                                         ORBS_COLOR_PART_ORBS));
    }
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawAmbientOrbs(int centerX, int centerY, int radiusX, int radiusY,
                               uint32_t elapsedMs, uint32_t formationMs,
                               uint32_t movementMs, int formationSpeed,
                               int movementSpeed, int glowScale, int trailZ,
                               int scrollFocusX, int formationMode) {
  orbBackgroundColorsActive = formationMode == 0;
  if (formationMode != 2 && ambientOrbsTheme == ORBS_THEME_PS2_ORIGINAL) {
    drawOriginalOrbs(centerX, centerY, radiusX, radiusY,
                     glassStartMs + elapsedMs, trailZ, 0, NULL);
    return;
  }
  OrbFormation *formation = orbFormationAt(formationMs, formationMode);
  const float selectionPulse = orbSelectionPulse(elapsedMs);
  float blend = !formation->morphMs || formationMs <= formation->phaseStartMs ?
                0.0f : (formationMs - formation->phaseStartMs) /
                         (float)formation->morphMs;
  if (blend > 1.0f)
    blend = 1.0f;
  blend = blend * blend * (3.0f - 2.0f * blend);
  const float playtimeWeight =
      (formation->from == ORB_SHAPE_PLAYTIME ? 1.0f - blend : 0.0f) +
      (formation->to == ORB_SHAPE_PLAYTIME ? blend : 0.0f);
  const float trailWeight = playtimeWeight *
      (scrollFocusX ? 1.0f - scrollLetterBlendAt(elapsedMs) : 1.0f);
  const int trailSegments = ORB_TRAIL_SEGMENTS +
      (int)((ORB_PLAYTIME_TRAIL_SEGMENTS - ORB_TRAIL_SEGMENTS) *
            trailWeight + 0.5f);
  const uint32_t trailStepMs = ORB_TRAIL_STEP_MS +
      (uint32_t)((ORB_PLAYTIME_TRAIL_STEP_MS - ORB_TRAIL_STEP_MS) *
                 trailWeight);
  OrbMotion motion[ORB_PLAYTIME_TRAIL_SEGMENTS + 1];
  float letterBlend[ORB_PLAYTIME_TRAIL_SEGMENTS + 1] = {0};
  float glyphBlend[ORB_PLAYTIME_TRAIL_SEGMENTS + 1] = {0};
  uint32_t animationTime[ORB_PLAYTIME_TRAIL_SEGMENTS + 1];
  float positionsX[ORB_COUNT], positionsY[ORB_COUNT];
  float depths[ORB_COUNT], opacities[ORB_COUNT];
  for (int sample = 0; sample <= trailSegments; sample++) {
    const uint32_t age = sample * trailStepMs;
    const uint32_t sampleMs = elapsedMs > age ? elapsedMs - age : 0;
    const uint32_t movementAge = age * movementSpeed;
    const uint32_t sampleMovementMs = movementMs > movementAge ?
                                      movementMs - movementAge : 0;
    animationTime[sample] = orbAnimationMs(sampleMs, sampleMovementMs);
    motion[sample] = orbMotion(animationTime[sample]);
    if (scrollFocusX) {
      letterBlend[sample] = scrollLetterBlendAt(sampleMs);
      glyphBlend[sample] = scrollGlyphBlendAt(sampleMs);
    }
  }
  for (int i = 0; i < ORB_COUNT; i++) {
    orbFormationPosition(formation, i, formationMs, animationTime[0], &motion[0],
                         centerX, centerY, radiusX, radiusY,
                         &positionsX[i], &positionsY[i],
                         &depths[i], &opacities[i]);
    if (scrollFocusX) {
      blendScrollLetterOrb(i, letterBlend[0], glyphBlend[0], animationTime[0],
                           centerX, centerY, radiusX, radiusY,
                           &positionsX[i], &positionsY[i],
                           &depths[i], &opacities[i]);
    }
  }

  // (Cs - 0) * As + Cd: overlapping halos merge into a brighter light.
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  // Existing tails show motion. These longer tails grow along each shape's
  // contours and retract during the transition to the next formation.
  const float baseStrength = 1.0f - letterBlend[0];
  const float fromExtent = (1.0f - blend) * baseStrength;
  const float toExtent = blend * baseStrength;
  const int edgeAlpha = (int)(0x40 * (1.0f + selectionPulse * 0.25f));
  const int logoAlpha = (int)((formationMode == 2 ? 0x18 : 0x50) *
                              (1.0f + selectionPulse * 0.25f));
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
  const float logoScale = orbLogoScale(radiusX, &motion[0]);
  if (formation->from == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, logoScale, trailZ,
                        (int)(logoAlpha * fromExtent), fromExtent);
  if (formation->to == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, logoScale, trailZ,
                        (int)(logoAlpha * toExtent), toExtent);
  const float logoStrength = letterBlend[0] +
      (formation->from == ORB_SHAPE_LUNA ? fromExtent : 0.0f) +
      (formation->to == ORB_SHAPE_LUNA ? toExtent : 0.0f);
  const float baseTrailVisibility = 1.0f - letterBlend[0];
  float letterTrailVisibility = letterBlend[0];
  if (scrollLetterPreviousGlyph != scrollLetterGlyph)
    letterTrailVisibility *= glyphBlend[0];
  for (int i = 0; i < ORB_COUNT; i++) {
    const float x = positionsX[i];
    const float y = positionsY[i];
    const float depth = depths[i];
    const float opacity = opacities[i];
    float newX = x;
    float newY = y;
    float newOpacity = opacity;
    for (int segment = 1; segment <= trailSegments &&
                          (baseTrailVisibility > 0.01f ||
                           letterTrailVisibility > 0.01f); segment++) {
      float oldX, oldY, oldDepth, oldOpacity;
      const uint32_t age = segment * trailStepMs * formationSpeed;
      orbFormationPosition(formation, i,
                            formationMs > age ? formationMs - age : 0,
                           animationTime[segment],
                           &motion[segment], centerX, centerY, radiusX, radiusY,
                           &oldX, &oldY, &oldDepth, &oldOpacity);
      if (scrollFocusX) {
        blendScrollLetterOrb(i, letterBlend[segment], glyphBlend[segment],
                             animationTime[segment], centerX, centerY,
                             radiusX, radiusY,
                             &oldX, &oldY, &oldDepth, &oldOpacity);
      }
      const int oldOuterAlpha = (int)((4 + (trailSegments - segment) * 28 /
                                      trailSegments) * oldOpacity *
                                      baseTrailVisibility);
      const int newOuterAlpha = (int)((4 + (trailSegments - segment + 1) * 28 /
                                      trailSegments) * newOpacity *
                                      baseTrailVisibility);
      const int oldInnerAlpha = (int)((5 + (trailSegments - segment) * 45 /
                                      trailSegments) * oldOpacity *
                                      baseTrailVisibility);
      const int newInnerAlpha = (int)((5 + (trailSegments - segment + 1) * 45 /
                                      trailSegments) * newOpacity *
                                      baseTrailVisibility);
      const int oldOuterWidth = 3 + (trailSegments - segment) * 6 / trailSegments;
      const int newOuterWidth = 3 + (trailSegments - segment + 1) * 6 / trailSegments;
      const int oldInnerWidth = 2 + (trailSegments - segment) * 3 / trailSegments;
      const int newInnerWidth = 2 + (trailSegments - segment + 1) * 3 / trailSegments;
      const float trailScale = (1.0f - logoStrength * 0.4f) *
                               (1.0f - playtimeWeight * 0.15f);
      if (oldOuterAlpha || newOuterAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldOuterWidth * trailScale, newOuterWidth * trailScale, trailZ,
                            orbLightColor(0x40, 0x78, 0xC8, oldOuterAlpha,
                                          ORBS_COLOR_PART_TAILS),
                            orbLightColor(0x40, 0x78, 0xC8, newOuterAlpha,
                                          ORBS_COLOR_PART_TAILS));
      if (oldInnerAlpha || newInnerAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldInnerWidth * trailScale, newInnerWidth * trailScale, trailZ,
                            orbLightColor(0xA0, 0xD8, 0xFF, oldInnerAlpha,
                                          ORBS_COLOR_PART_TAILS),
                            orbLightColor(0xA0, 0xD8, 0xFF, newInnerAlpha,
                                          ORBS_COLOR_PART_TAILS));
      if (letterTrailVisibility > 0.01f) {
        const float fade = (trailSegments - segment + 1) /
                           (float)trailSegments;
        const int outerAlpha = (int)(0x1E * fade * letterTrailVisibility *
                                     oldOpacity);
        const int innerAlpha = (int)(0x42 * fade * letterTrailVisibility *
                                     newOpacity);
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            3.0f * fade, 4.0f * fade, trailZ,
                            orbLightColor(0x48, 0x90, 0xD8, outerAlpha,
                                          ORBS_COLOR_PART_TAILS),
                            orbLightColor(0x70, 0xB8, 0xF0, innerAlpha,
                                          ORBS_COLOR_PART_TAILS));
        drawOrbGlowDisc(newX, newY, 4.0f + fade * 3.0f, trailZ,
                        orbLightColor(0x98, 0xD8, 0xFF,
                                      (int)(0x24 * fade *
                                            letterTrailVisibility * newOpacity),
                                      ORBS_COLOR_PART_ORBS),
                        orbLightColor(0x98, 0xD8, 0xFF, 0,
                                      ORBS_COLOR_PART_ORBS));
      }
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
                           (uint32_t)i * 311U)) *
        (0.32f - playtimeWeight * 0.20f) / 127.0f;
    const float haloRadius = (18.0f + depth / 14.0f) * glowScale / 100.0f *
                             1.12f *
                             logoOrbScale * sizePulse *
                              (1.0f + selectionPulse * 0.22f) *
                              (1.0f + playtimeWeight * 0.10f);
    const float coreRadius = (5.6f + depth / 52.0f) * glowScale / 100.0f *
                             1.12f *
                             logoOrbScale * sizePulse *
                             (1.0f + selectionPulse * 0.12f);
    const int haloAlpha = (int)((0x13 + depth / 13.0f) * 1.20f * opacity *
                                 (1.0f + selectionPulse * 0.45f) *
                                 (1.0f - playtimeWeight * 0.08f));
    int coreAlpha = (int)((0x50 + depth / 3.0f) * 1.10f * opacity *
                           (1.0f + selectionPulse * 0.22f) *
                           (1.0f - playtimeWeight * 0.18f));
    if (coreAlpha > 0x80)
      coreAlpha = 0x80;

    if (letterBlend[0] > 0.0f)
      drawOrbGlowDisc(x + SCROLL_GLYPH_DEPTH_X * 0.75f,
                      y + SCROLL_GLYPH_DEPTH_Y * 0.75f,
                      haloRadius * 0.62f, trailZ,
                      orbLightColor(0x30, 0x68, 0xA8,
                                    (int)(coreAlpha * letterBlend[0] * 0.28f),
                                    ORBS_COLOR_PART_ORBS),
                      orbLightColor(0x30, 0x68, 0xA8, 0,
                                    ORBS_COLOR_PART_ORBS));
    if (ambientOrbsAppearance == ORBS_APPEARANCE_PS2_ORIGINAL &&
        originalMasksLoaded) {
      const float originalSize = 1.0f / (1.0f + depth / 640.0f);
      drawOriginalOrbSprite(x, y, originalSize, trailZ + 1,
                            0x30, 0x62, 0x80, opacity, 1);
    } else {
      drawOrbGlowDisc(x, y, haloRadius, trailZ + 1,
                      orbLightColor(0x70, 0xA8, 0xE8, haloAlpha,
                                    ORBS_COLOR_PART_ORBS),
                      orbLightColor(0x70, 0xA8, 0xE8, 0,
                                    ORBS_COLOR_PART_ORBS));
      drawOrbGlowDisc(x, y, coreRadius, trailZ + 1,
                      orbLightColor(0xE0, 0xF0, 0xFF, coreAlpha,
                                    ORBS_COLOR_PART_ORBS),
                      orbLightColor(0xE0, 0xF0, 0xFF, 0,
                                    ORBS_COLOR_PART_ORBS));
    }
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}


void setAmbientOrbsBackgroundStyle(int enabled) {
  ambientOrbsBackgroundStyle = enabled != 0;
}

int drawAmbientOrbsBackground(uint32_t now) {
  if (!ambientOrbsBackgroundStyle)
    return 0;
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(now);
  const uint64_t black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
  gsKit_prim_quad_gouraud(gsGlobal, 0, 0, width, 0, 0, height,
                          width, height, 0, black, black, black, black);
  drawAmbientOrbs(width * 65 / 100, height * 52 / 100,
                  width * 20 / 100, height * 30 / 100,
                   elapsedMs, elapsedMs, elapsedMs, 1, 1, 100, 0, 0, 0);
  return 1;
}

void drawAmbientOrbsOrbit(int centerX, int centerY, int radiusX,
                          int radiusY, uint32_t now, int trailZ) {
  const uint32_t elapsedMs = glassElapsedMs(now);
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                   elapsedMs, elapsedMs, 1, 1, 100, trailZ, 0, 1);
}

void drawAmbientOrbsSystemConfig(uint64_t clockMs, uint32_t now,
                                 uint32_t elapsedMs, int trailZ) {
  const uint32_t frame = (uint32_t)((uint64_t)elapsedMs * 60U / 1000U);
  const int16_t tiltTarget = (int16_t)((clockMs % 43200000U) * 65536U / 43200000U);
  const int16_t spinTarget = (int16_t)((clockMs % 60000U) * 65536U / 60000U);
  if (!systemConfigClockReady) {
    systemConfigRadius = 10.0f;
    systemConfigTilt = tiltTarget;
    systemConfigSpin = spinTarget;
    systemConfigFrame = frame;
    systemConfigClockReady = 1;
  }
  // UpdateOrbs / CarouselClock: minute-driven radius and signed angle easing.
  // Bound catch-up after another background has been displayed for a while.
  const uint32_t missed = frame - systemConfigFrame;
  const uint32_t steps = missed < 120U ? missed : 120U;
  const float radiusTarget = 10.0f + 7.25f *
                             (clockMs % 3600000U) / 3600000.0f;
  for (uint32_t i = 0; i < steps; i++) {
    systemConfigRadius += (radiusTarget - systemConfigRadius) * 0.005f;
    if (systemConfigSpin > -201 && systemConfigSpin < 201)
      systemConfigTilt = tiltTarget;
    else
      systemConfigTilt = (int16_t)(systemConfigTilt +
                                   (int16_t)(tiltTarget - systemConfigTilt) * 0.1f);
    systemConfigSpin = (int16_t)(systemConfigSpin +
                                 (int16_t)(spinTarget - systemConfigSpin) * 0.1f);
  }
  systemConfigFrame = frame;
  const SystemConfigOrbScene scene = {
      clockMs, elapsedMs, systemConfigRadius, systemConfigTilt, systemConfigSpin};
  const int oldBackgroundColors = orbBackgroundColorsActive;
  orbBackgroundColorsActive = 0;
  drawOriginalOrbs(0, 0, 0, 0, now, trailZ, 1, &scene);
  orbBackgroundColorsActive = oldBackgroundColors;
}

void drawAmbientOrbsSplash(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t now, int trailZ) {
  const uint32_t elapsedMs = glassElapsedMs(now);
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                   elapsedMs, elapsedMs, 1, 1, 100, trailZ, 0, 2);
}

void drawAmbientOrbsScroll(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t elapsedMs,
                           int fastScroll, const char *title,
                           int trailZ, int scrollFocusX) {
  setScrollLetterMode(fastScroll, scrollTitleInitial(title), elapsedMs);
  if (!scrollOrbClockInitialized) {
    scrollOrbClockMs = elapsedMs;
    scrollFormationClockMs = 0;
    scrollOrbClockInitialized = 1;
  } else {
    scrollOrbClockMs += (elapsedMs - scrollOrbLastFrameMs) *
                        (fastScroll ? 3U : 1U);
    if (!fastScroll)
      scrollFormationClockMs += elapsedMs - scrollOrbLastFrameMs;
  }
  scrollOrbLastFrameMs = elapsedMs;
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                   scrollFormationClockMs, scrollOrbClockMs,
                   fastScroll ? 0 : 1, fastScroll ? 3 : 1, 100,
                   trailZ, scrollFocusX, 3);
}
