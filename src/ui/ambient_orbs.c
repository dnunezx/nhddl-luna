// Ambient Orbs: reusable animated formation asset.
#include "ui/view_internal.h"
#include "ui/ambient_orbs.h"

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
#define ORB_SPLASH_FORMATION_PERIOD_MS 1500
#define ORB_FORMATION_MORPH_MS 1000
#define ORB_SPLASH_FORMATION_MORPH_MS 550
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
#define SCROLL_ORB_EVENT_COUNT 12
#define SCROLL_ORB_REACTION_MS 1050
#define SCROLL_LETTER_MORPH_MS 420
#define SCROLL_GLYPH_MORPH_MS 140
#define SCROLL_GLYPH_DEPTH_X 9.0f
#define SCROLL_GLYPH_DEPTH_Y 8.0f

static uint32_t glassStartMs;
static uint32_t orbitStartMs;
static uint32_t splashStartMs;
static int ambientOrbsBackgroundStyle;
static uint32_t orbSelectionEvents[ORB_SELECTION_EVENT_COUNT];
static uint32_t orbLastSelectionEventMs;
static int orbSelectionEventNext;
static int orbSelectionEventCount;
static int orbObservedSelection = -1;

typedef struct {
  uint32_t startMs;
  int direction;
} ScrollOrbEvent;

typedef struct {
  float reach;
  float direction;
} ScrollOrbMotion;

static ScrollOrbEvent scrollOrbEvents[SCROLL_ORB_EVENT_COUNT];
static int scrollOrbEventNext;
static int scrollOrbEventCount;
static uint32_t scrollOrbClockMs;
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
  orbitStartMs = 0;
  splashStartMs = 0;
  resetAmbientOrbsScroll();
}

void resetAmbientOrbsOrbit(uint32_t now) {
  orbitStartMs = now - glassStartMs;
}

void resetAmbientOrbsSplash(uint32_t now) {
  splashStartMs = now - glassStartMs;
}

void resetAmbientOrbsScroll(void) {
  scrollOrbEventNext = 0;
  scrollOrbEventCount = 0;
  scrollOrbClockInitialized = 0;
  scrollLetterInitialized = 0;
  scrollLetterGlyph = 0;
  scrollLetterPreviousGlyph = 0;
  orbObservedSelection = -1;
  orbSelectionEventNext = 0;
  orbSelectionEventCount = 0;
}

void triggerAmbientOrbsScrollReaction(int direction, uint32_t now) {
  if (!direction)
    return;
  scrollOrbEvents[scrollOrbEventNext].startMs = now - glassStartMs;
  scrollOrbEvents[scrollOrbEventNext].direction = direction;
  scrollOrbEventNext = (scrollOrbEventNext + 1) % SCROLL_ORB_EVENT_COUNT;
  if (scrollOrbEventCount < SCROLL_ORB_EVENT_COUNT)
    scrollOrbEventCount++;
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

static ScrollOrbMotion scrollOrbMotion(uint32_t elapsedMs) {
  ScrollOrbMotion motion = {0.0f, 0.0f};
  for (int i = 0; i < scrollOrbEventCount; i++) {
    if (elapsedMs < scrollOrbEvents[i].startMs)
      continue;
    const uint32_t age = elapsedMs - scrollOrbEvents[i].startMs;
    if (age >= SCROLL_ORB_REACTION_MS)
      continue;
    float weight;
    if (age < 180)
      weight = scrollOrbEase(age / 180.0f);
    else if (age < 340)
      weight = 1.0f;
    else if (age < 850)
      weight = 1.0f - scrollOrbEase((age - 340) / 510.0f);
    else if (age < 950)
      weight = -0.10f * scrollOrbEase((age - 850) / 100.0f);
    else
      weight = -0.10f * (1.0f - scrollOrbEase((age - 950) / 100.0f));
    motion.reach += weight;
    motion.direction += weight * scrollOrbEvents[i].direction;
  }
  if (motion.reach > 1.0f)
    motion.reach = 1.0f;
  if (motion.reach < -0.10f)
    motion.reach = -0.10f;
  if (motion.direction > 1.0f)
    motion.direction = 1.0f;
  if (motion.direction < -1.0f)
    motion.direction = -1.0f;
  return motion;
}

static void applyScrollOrbMotion(int index, const ScrollOrbMotion *motion,
                                 float letterBlend, int focusX, int focusY,
                                 float *x, float *y) {
  const float reach = motion->reach * (1.0f - letterBlend);
  const float direction = motion->direction * (1.0f - letterBlend);
  if (index == 0 || index == 2 || index == 4) {
    const int slot = index / 2 - 1;
    const float targetX = focusX - (slot == 0 ? 0.0f : 23.0f);
    const float targetY = focusY + slot * 18.0f;
    *x += (targetX - *x) * reach;
    *y += (targetY - *y) * reach +
          direction * (1.0f - reach) * 28.0f;
  } else {
    *x += reach * 6.0f;
    *y += direction * 3.0f;
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
  uint32_t periodMs;
  uint32_t morphMs;
  uint32_t random;
  uint32_t startMs;
  uint32_t epochMs;
  OrbShape from;
  OrbShape to;
  int initialized;
} OrbFormation;

static OrbFormation *orbFormationAt(uint32_t elapsedMs, int formationMode) {
  static OrbFormation sharedFormation;
  static OrbFormation orbitFormation;
  static OrbFormation splashFormation;
  static const OrbShape orbitShapes[] = {
    ORB_SHAPE_CUBE, ORB_SHAPE_OCTAHEDRON,
    ORB_SHAPE_ORBIT, ORB_SHAPE_LUNA
  };
  OrbFormation *formation = formationMode == 2 ? &splashFormation :
                            formationMode == 1 ? &orbitFormation : &sharedFormation;
  const uint32_t epochMs = formationMode == 2 ? splashStartMs :
                           formationMode == 1 ? orbitStartMs : 0;
  const uint32_t periodMs = formationMode == 2 ?
                            ORB_SPLASH_FORMATION_PERIOD_MS : ORB_FORMATION_PERIOD_MS;
  const uint32_t morphMs = formationMode == 2 ?
                           ORB_SPLASH_FORMATION_MORPH_MS : ORB_FORMATION_MORPH_MS;
  const uint32_t cycle = (elapsedMs >= epochMs ? elapsedMs - epochMs : 0) /
      periodMs;
  if (!formation->initialized || formation->startMs != glassStartMs ||
      formation->epochMs != epochMs || cycle < formation->cycle) {
    formation->cycle = 0;
    formation->periodMs = periodMs;
    formation->morphMs = morphMs;
    formation->random = glassStartMs ^ 0xA341316CU;
    formation->startMs = glassStartMs;
    formation->epochMs = epochMs;
    formation->from = formationMode == 2 ? ORB_SHAPE_LUNA :
                      formationMode == 1 ? ORB_SHAPE_CUBE : ORB_SHAPE_ORBIT;
    formation->to = formation->from;
    formation->initialized = 1;
  }
  while (formation->cycle < cycle) {
    formation->from = formation->to;
    if (formationMode == 2) {
      formation->to = formation->to == ORB_SHAPE_LUNA ?
                      ORB_SHAPE_CUBE : ORB_SHAPE_LUNA;
    } else if (formationMode == 1) {
      formation->to = orbitShapes[(formation->cycle + 1) %
                                  (sizeof(orbitShapes) / sizeof(orbitShapes[0]))];
    } else {
      formation->random = formation->random * 1664525U + 1013904223U;
      int next = (int)(formation->random % (ORB_SHAPE_COUNT - 1));
      if (next >= formation->to)
        next++;
      formation->to = (OrbShape)next;
    }
    formation->cycle++;
  }
  return formation;
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
  const uint32_t cycleStart = formation->epochMs +
      formation->cycle * formation->periodMs;
  float blend = sampleMs <= cycleStart ? 0.0f :
                (sampleMs - cycleStart) / (float)formation->morphMs;
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
                        glassLightColor(0x40, 0x78, 0xC8,
                                       (int)(alpha * fromFade * 0.45f)),
                        glassLightColor(0x40, 0x78, 0xC8,
                                       (int)(alpha * toFade * 0.45f)));
    drawOrbTrailSegment(ax, ay, bx, by,
                        width * fromFade, width * toFade, z,
                        glassLightColor(0xA0, 0xD8, 0xFF,
                                       (int)(alpha * fromFade)),
                        glassLightColor(0xA0, 0xD8, 0xFF,
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
                      glassLightColor(0x40, 0x78, 0xC8, alpha / 2),
                      glassLightColor(0x40, 0x78, 0xC8, alpha / 3));
  drawOrbTrailSegment(x1, y1, tipX, tipY, width, width * 0.65f, z,
                      glassLightColor(0xA0, 0xD8, 0xFF, alpha),
                      glassLightColor(0xE0, 0xF0, 0xFF, alpha / 2));
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

static void drawAmbientOrbs(int centerX, int centerY, int radiusX, int radiusY,
                              uint32_t elapsedMs, uint32_t movementMs,
                              int movementSpeed, int glowScale, int trailZ,
                              int scrollFocusX, int formationMode) {
  OrbFormation *formation = orbFormationAt(elapsedMs, formationMode);
  const float selectionPulse = orbSelectionPulse(elapsedMs);
  OrbMotion motion[ORB_TRAIL_SEGMENTS + 1];
  ScrollOrbMotion scrollMotion[ORB_TRAIL_SEGMENTS + 1];
  float letterBlend[ORB_TRAIL_SEGMENTS + 1] = {0};
  float glyphBlend[ORB_TRAIL_SEGMENTS + 1] = {0};
  uint32_t animationTime[ORB_TRAIL_SEGMENTS + 1];
  float positionsX[ORB_COUNT], positionsY[ORB_COUNT];
  float depths[ORB_COUNT], opacities[ORB_COUNT];
  for (int sample = 0; sample <= ORB_TRAIL_SEGMENTS; sample++) {
    const uint32_t age = sample * ORB_TRAIL_STEP_MS;
    const uint32_t sampleMs = elapsedMs > age ? elapsedMs - age : 0;
    const uint32_t movementAge = age * movementSpeed;
    const uint32_t sampleMovementMs = movementMs > movementAge ?
                                      movementMs - movementAge : 0;
    animationTime[sample] = orbAnimationMs(sampleMs, sampleMovementMs);
    motion[sample] = orbMotion(animationTime[sample]);
    if (scrollFocusX) {
      scrollMotion[sample] = scrollOrbMotion(sampleMs);
      letterBlend[sample] = scrollLetterBlendAt(sampleMs);
      glyphBlend[sample] = scrollGlyphBlendAt(sampleMs);
    }
  }
  for (int i = 0; i < ORB_COUNT; i++) {
    orbFormationPosition(formation, i, elapsedMs, animationTime[0], &motion[0],
                         centerX, centerY, radiusX, radiusY,
                         &positionsX[i], &positionsY[i],
                         &depths[i], &opacities[i]);
    if (scrollFocusX) {
      blendScrollLetterOrb(i, letterBlend[0], glyphBlend[0], animationTime[0],
                           centerX, centerY, radiusX, radiusY,
                           &positionsX[i], &positionsY[i],
                           &depths[i], &opacities[i]);
      applyScrollOrbMotion(i, &scrollMotion[0], letterBlend[0],
                           scrollFocusX, centerY,
                           &positionsX[i], &positionsY[i]);
    }
  }

  // (Cs - 0) * As + Cd: overlapping halos merge into a brighter light.
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  const uint32_t cycleStart = formation->epochMs +
      formation->cycle * formation->periodMs;
  float blend = elapsedMs <= cycleStart ? 0.0f :
                (elapsedMs - cycleStart) / (float)formation->morphMs;
  if (blend > 1.0f)
    blend = 1.0f;
  blend = blend * blend * (3.0f - 2.0f * blend);
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
  if (formation->from == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, radiusX, &motion[0], trailZ,
                        (int)(logoAlpha * fromExtent), fromExtent);
  if (formation->to == ORB_SHAPE_LUNA)
    drawOrbLogoWordmark(centerX, centerY, radiusX, &motion[0], trailZ,
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
    for (int segment = 1; segment <= ORB_TRAIL_SEGMENTS &&
                          (baseTrailVisibility > 0.01f ||
                           letterTrailVisibility > 0.01f); segment++) {
      float oldX, oldY, oldDepth, oldOpacity;
      const uint32_t age = segment * ORB_TRAIL_STEP_MS;
      orbFormationPosition(formation, i, elapsedMs > age ? elapsedMs - age : 0,
                           animationTime[segment],
                           &motion[segment], centerX, centerY, radiusX, radiusY,
                           &oldX, &oldY, &oldDepth, &oldOpacity);
      if (scrollFocusX) {
        blendScrollLetterOrb(i, letterBlend[segment], glyphBlend[segment],
                             animationTime[segment], centerX, centerY,
                             radiusX, radiusY,
                             &oldX, &oldY, &oldDepth, &oldOpacity);
        applyScrollOrbMotion(i, &scrollMotion[segment],
                             letterBlend[segment], scrollFocusX, centerY,
                             &oldX, &oldY);
      }
      const int oldOuterAlpha = (int)((4 + (ORB_TRAIL_SEGMENTS - segment) * 28 /
                                      ORB_TRAIL_SEGMENTS) * oldOpacity *
                                      baseTrailVisibility);
      const int newOuterAlpha = (int)((4 + (ORB_TRAIL_SEGMENTS - segment + 1) * 28 /
                                      ORB_TRAIL_SEGMENTS) * newOpacity *
                                      baseTrailVisibility);
      const int oldInnerAlpha = (int)((5 + (ORB_TRAIL_SEGMENTS - segment) * 45 /
                                      ORB_TRAIL_SEGMENTS) * oldOpacity *
                                      baseTrailVisibility);
      const int newInnerAlpha = (int)((5 + (ORB_TRAIL_SEGMENTS - segment + 1) * 45 /
                                      ORB_TRAIL_SEGMENTS) * newOpacity *
                                      baseTrailVisibility);
      const int oldOuterWidth = 3 + (ORB_TRAIL_SEGMENTS - segment) * 6 / ORB_TRAIL_SEGMENTS;
      const int newOuterWidth = 3 + (ORB_TRAIL_SEGMENTS - segment + 1) * 6 / ORB_TRAIL_SEGMENTS;
      const int oldInnerWidth = 2 + (ORB_TRAIL_SEGMENTS - segment) * 3 / ORB_TRAIL_SEGMENTS;
      const int newInnerWidth = 2 + (ORB_TRAIL_SEGMENTS - segment + 1) * 3 / ORB_TRAIL_SEGMENTS;
      const float trailScale = 1.0f - logoStrength * 0.4f;
      if (oldOuterAlpha || newOuterAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldOuterWidth * trailScale, newOuterWidth * trailScale, trailZ,
                            glassLightColor(0x40, 0x78, 0xC8, oldOuterAlpha),
                            glassLightColor(0x40, 0x78, 0xC8, newOuterAlpha));
      if (oldInnerAlpha || newInnerAlpha)
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            oldInnerWidth * trailScale, newInnerWidth * trailScale, trailZ,
                            glassLightColor(0xA0, 0xD8, 0xFF, oldInnerAlpha),
                            glassLightColor(0xA0, 0xD8, 0xFF, newInnerAlpha));
      if (letterTrailVisibility > 0.01f) {
        const float fade = (ORB_TRAIL_SEGMENTS - segment + 1) /
                           (float)ORB_TRAIL_SEGMENTS;
        const int outerAlpha = (int)(0x1E * fade * letterTrailVisibility *
                                     oldOpacity);
        const int innerAlpha = (int)(0x42 * fade * letterTrailVisibility *
                                     newOpacity);
        drawOrbTrailSegment(oldX, oldY, newX, newY,
                            3.0f * fade, 4.0f * fade, trailZ,
                            glassLightColor(0x48, 0x90, 0xD8, outerAlpha),
                            glassLightColor(0x70, 0xB8, 0xF0, innerAlpha));
        drawOrbGlowDisc(newX, newY, 4.0f + fade * 3.0f, trailZ,
                        glassLightColor(0x98, 0xD8, 0xFF,
                                         (int)(0x24 * fade *
                                               letterTrailVisibility * newOpacity)),
                        glassLightColor(0x98, 0xD8, 0xFF, 0));
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

    if (letterBlend[0] > 0.0f)
      drawOrbGlowDisc(x + SCROLL_GLYPH_DEPTH_X * 0.75f,
                      y + SCROLL_GLYPH_DEPTH_Y * 0.75f,
                      haloRadius * 0.62f, trailZ,
                      glassLightColor(0x30, 0x68, 0xA8,
                                       (int)(coreAlpha * letterBlend[0] * 0.28f)),
                      glassLightColor(0x30, 0x68, 0xA8, 0));
    drawOrbGlowDisc(x, y, haloRadius, trailZ + 1,
                    glassLightColor(0x70, 0xA8, 0xE8, haloAlpha),
                    glassLightColor(0x70, 0xA8, 0xE8, 0));
    drawOrbGlowDisc(x, y, coreRadius, trailZ + 1,
                    glassLightColor(0xE0, 0xF0, 0xFF, coreAlpha),
                    glassLightColor(0xE0, 0xF0, 0xFF, 0));
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
                  elapsedMs, elapsedMs, 1, 100, 0, 0, 0);
  return 1;
}

void drawAmbientOrbsOrbit(int centerX, int centerY, int radiusX,
                          int radiusY, uint32_t now, int trailZ) {
  const uint32_t elapsedMs = glassElapsedMs(now);
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                  elapsedMs, 1, 100, trailZ, 0, 1);
}

void drawAmbientOrbsSplash(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t now, int trailZ) {
  const uint32_t elapsedMs = glassElapsedMs(now);
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                  elapsedMs, 1, 100, trailZ, 0, 2);
}

void drawAmbientOrbsScroll(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t elapsedMs,
                           int fastScroll, const char *title,
                           int trailZ, int scrollFocusX) {
  setScrollLetterMode(fastScroll, scrollTitleInitial(title), elapsedMs);
  if (!scrollOrbClockInitialized) {
    scrollOrbClockMs = elapsedMs;
    scrollOrbClockInitialized = 1;
  } else {
    scrollOrbClockMs += (elapsedMs - scrollOrbLastFrameMs) *
                        (fastScroll ? 3U : 1U);
  }
  scrollOrbLastFrameMs = elapsedMs;
  drawAmbientOrbs(centerX, centerY, radiusX, radiusY, elapsedMs,
                  scrollOrbClockMs, fastScroll ? 3 : 1, 100,
                  trailZ, scrollFocusX, 0);
}
