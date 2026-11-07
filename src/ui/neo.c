// Neo: a bounded, single-atlas digital rain background.
#include "ui/neo.h"
#include "ui/view_internal.h"
#include <string.h>

#define NEO_COLUMNS 60
#define NEO_GLYPHS 48
#define NEO_ATLAS_SIZE 64
#define NEO_CELL 8
#define NEO_ROW_HEIGHT 18.0f
#define NEO_HEIGHT 448.0f

// Original 5x7 bitmap drawings: katakana, digits, then punctuation.
// These glyphs are decorative atlas indices, independent of menu font encoding.
static const uint8_t glyphRows[NEO_GLYPHS][7] = {
    {31, 1, 5, 6, 4, 8, 16},       // ア
    {1, 2, 4, 12, 20, 4, 4},       // イ
    {4, 31, 17, 1, 2, 4, 8},       // ウ
    {0, 31, 4, 4, 4, 4, 31},       // エ
    {2, 31, 6, 10, 18, 2, 6},      // オ
    {8, 31, 9, 9, 9, 17, 2},       // カ
    {4, 30, 4, 31, 4, 4, 4},       // キ
    {8, 15, 17, 1, 2, 4, 8},       // ク
    {8, 16, 31, 9, 1, 2, 4},       // ケ
    {0, 31, 1, 1, 1, 1, 31},       // コ
    {10, 31, 10, 10, 2, 4, 8},     // サ
    {16, 8, 17, 9, 2, 4, 24},      // シ
    {0, 30, 2, 4, 4, 10, 17},      // ス
    {8, 8, 31, 9, 10, 8, 7},       // セ
    {16, 9, 9, 1, 2, 4, 8},        // ソ
    {8, 15, 17, 9, 6, 4, 8},       // タ
    {2, 28, 4, 31, 4, 8, 16},      // チ
    {0, 21, 21, 1, 2, 4, 8},       // ツ
    {14, 0, 31, 4, 4, 8, 16},      // テ
    {8, 8, 12, 10, 8, 8, 8},       // ト
    {4, 4, 31, 4, 4, 8, 16},       // ナ
    {0, 14, 0, 0, 0, 31, 0},       // ニ
    {31, 1, 9, 6, 6, 9, 16},       // ヌ
    {4, 31, 2, 4, 14, 21, 4},      // ネ
    {1, 1, 2, 2, 4, 8, 16},        // ノ
    {0, 10, 10, 10, 17, 17, 17},   // ハ
    {16, 16, 30, 16, 16, 16, 15},  // ヒ
    {0, 31, 1, 1, 2, 4, 8},        // フ
    {0, 0, 8, 20, 2, 1, 0},        // ヘ
    {4, 31, 4, 4, 21, 21, 4},      // ホ
    {4, 8, 8, 16, 18, 31, 1},      // ム
    {1, 1, 10, 4, 6, 9, 16},       // メ
    {14, 17, 19, 21, 25, 17, 14},  // 0
    {4, 12, 4, 4, 4, 4, 14},        // 1
    {14, 17, 1, 2, 4, 8, 31},      // 2
    {30, 1, 1, 14, 1, 1, 30},      // 3
    {2, 6, 10, 18, 31, 2, 2},      // 4
    {31, 16, 16, 30, 1, 1, 30},    // 5
    {14, 16, 16, 30, 17, 17, 14},  // 6
    {31, 1, 2, 4, 8, 8, 8},        // 7
    {14, 17, 17, 14, 17, 17, 14},  // 8
    {14, 17, 17, 15, 1, 1, 14},    // 9
    {10, 10, 31, 10, 31, 10, 10},  // #
    {0, 4, 4, 31, 4, 4, 0},        // +
    {0, 17, 10, 4, 10, 17, 0},     // *
    {0, 0, 31, 0, 31, 0, 0},       // =
    {3, 4, 8, 8, 8, 4, 3},         // (
    {24, 4, 2, 2, 2, 4, 24},       // )
};

typedef struct {
  float head;
  unsigned speed, length;
  uint32_t seed;
} NeoColumn;

static NeoColumn columns[NEO_COLUMNS];
static u32 atlasPixels[NEO_ATLAS_SIZE * NEO_ATLAS_SIZE] __attribute__((aligned(128)));
static GSTEXTURE atlas;
static int ready;
static uint32_t previousMs, animationMs;
static unsigned neoSpeed = NEO_SPEED_NORMAL, neoDensity = NEO_DENSITY_NORMAL;
static unsigned neoColor = NEO_COLOR_GREEN;
typedef struct { uint8_t head[3], base[3], glow[3], background[3]; } NeoPalette;
static const NeoPalette palettes[NEO_COLOR_COUNT] = {
    {{160,224,176},{4,14,12},{0,128,0},{0,2,0}},
    {{160,224,224},{4,14,14},{0,128,128},{0,2,2}},
    {{176,200,240},{4,8,14},{24,72,128},{0,0,2}},
    {{216,176,240},{10,4,14},{80,24,128},{1,0,2}},
    {{240,176,176},{14,4,8},{128,0,16},{2,0,0}},
    {{240,216,160},{14,10,4},{128,88,0},{2,1,0}},
    {{224,224,224},{12,12,12},{128,128,128},{1,1,1}}
};

void setNeoAppearance(unsigned speed, unsigned density, unsigned color) {
  neoSpeed = speed < NEO_SPEED_COUNT ? speed : NEO_SPEED_NORMAL;
  neoDensity = density < NEO_DENSITY_COUNT ? density : NEO_DENSITY_NORMAL;
  neoColor = color < NEO_COLOR_COUNT ? color : NEO_COLOR_GREEN;
}

static uint32_t neoHash(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  return value ^ (value >> 16);
}

static void restartColumn(NeoColumn *column, int initial) {
  column->seed = neoHash(column->seed + 1U);
  column->speed = 48U + column->seed % 65U;
  column->length = 9U + (column->seed >> 8) % 11U;
  column->head = initial ? (float)((column->seed >> 16) % 800U) - 160.0f :
                          -(float)(20U + (column->seed >> 16) % 260U);
}

void resetNeoBackground(void) {
  // A UI refresh destroys the GS context and its texture-manager residency.
  memset(&atlas, 0, sizeof(atlas));
  ready = 0;
}

static void initNeo(uint32_t nowMs) {
  memset(atlasPixels, 0, sizeof(atlasPixels));
  for (int glyph = 0; glyph < NEO_GLYPHS; glyph++) {
    const int left = (glyph % 8) * NEO_CELL + 1;
    const int top = (glyph / 8) * NEO_CELL;
    for (int y = 0; y < 7; y++)
      for (int x = 0; x < 5; x++)
        if (glyphRows[glyph][y] & (1U << (4 - x)))
          atlasPixels[(top + y) * NEO_ATLAS_SIZE + left + x] = 0x80808080U;
  }
  atlas.Width = atlas.Height = NEO_ATLAS_SIZE;
  atlas.PSM = GS_PSM_CT32;
  atlas.Mem = atlasPixels;
  atlas.Filter = GS_FILTER_NEAREST;
  atlas.Delayed = GS_SETTING_ON;
  for (int i = 0; i < NEO_COLUMNS; i++) {
    columns[i].seed = neoHash((uint32_t)i + 0x4e454fU);
    restartColumn(&columns[i], 1);
  }
  previousMs = nowMs;
  animationMs = 0;
  ready = 1;
}

void drawNeoBackground(uint32_t nowMs) {
  const NeoPalette *palette = &palettes[neoColor];
  static const unsigned columnCounts[NEO_DENSITY_COUNT] = {20,40,60};
  static const float speeds[NEO_SPEED_COUNT] = {.5f,1,1.5f};
  const unsigned count = columnCounts[neoDensity];
  const float spacing = 640.0f / count;
  const float glyphWidth = spacing < 16.0f ? spacing - 2.0f : 14.0f;
  // Draw behind all artwork and menu panels, using their existing GS state.
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(palette->background[0],palette->background[1],palette->background[2],0x80));
  if (!ready)
    initNeo(nowMs);
  uint32_t delta = nowMs - previousMs;
  previousMs = nowMs;
  // Resume gently after another background, a game, or a long UI stall.
  if (delta > 250U)
    delta = 0;
  animationMs += delta;
  const float scaleX = gsGlobal->Width / 640.0f;
  const float scaleY = gsGlobal->Height / NEO_HEIGHT;
  const int bound = bindTextureSafe(gsGlobal, &atlas);
  for (int i = 0; i < NEO_COLUMNS; i++) {
    NeoColumn *column = &columns[i];
    column->head += column->speed * delta / 1000.0f * speeds[neoSpeed];
    const float end = NEO_HEIGHT + column->length * NEO_ROW_HEIGHT;
    if (column->head > end) {
      const float overshoot = column->head - end;
      const unsigned oldSpeed = column->speed;
      restartColumn(column, 0);
      column->head += overshoot * column->speed / oldSpeed;
    }
    if (!bound || (unsigned)i >= count)
      continue;
    const float x = (i * spacing + 1.0f) * scaleX;
    for (unsigned trail = 0; trail < column->length; trail++) {
      const float y = (column->head - trail * NEO_ROW_HEIGHT) * scaleY;
      if (y >= gsGlobal->Height || y + 16.0f * scaleY <= 0)
        continue;
      const uint32_t pattern = neoHash(column->seed + trail * 131U);
      // Only one quarter of glyphs mutate, at a staggered, quiet cadence.
      const uint32_t mutation = (pattern & 3U) == 0 ?
          (animationMs + (pattern & 1023U)) / 420U : 0;
      const unsigned glyph = neoHash(pattern + mutation) % NEO_GLYPHS;
      float u1 = (glyph % 8) * NEO_CELL;
      float u2 = u1 + NEO_CELL;
      const float v = (glyph / 8) * NEO_CELL;
      if (pattern & 0x100U) {
        const float swap = u1;
        u1 = u2;
        u2 = swap;
      }
      const int fade = (column->length - trail) * 100 / column->length;
      const int glow = fade * fade / 85;
      const uint64_t color = trail == 0 ?
          GS_SETREG_RGBA(palette->head[0],palette->head[1],palette->head[2],0x70) :
          GS_SETREG_RGBA(palette->base[0]+glow*palette->glow[0]/128,
                        palette->base[1]+glow*palette->glow[1]/128,
                        palette->base[2]+glow*palette->glow[2]/128,0x68);
      gsKit_prim_sprite_texture(gsGlobal, &atlas, x, y, u1, v,
          x + glyphWidth * scaleX, y + 16.0f * scaleY, u2, v + NEO_CELL, 0, color);
    }
  }
}
