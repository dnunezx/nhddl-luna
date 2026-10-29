// LUNA 2026
#ifndef _UI_GRAPHICS_H_
#define _UI_GRAPHICS_H_

#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>
#include "ui/texture_budget.h"

// Predefined colors
// static const uint64_t ColorWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
static const uint64_t ColorBlack = GS_SETREG_RGBA(0x04, 0x08, 0x16, 0x80);
static const uint64_t ColorSelected = GS_SETREG_RGBA(0x33, 0xB5, 0xE5, 0x80);
static const uint64_t ColorSelectedText = GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80);
static const uint64_t ColorGrey = GS_SETREG_RGBA(0xC8, 0xD4, 0xE8, 0x80);
static const uint64_t ColorPanel = GS_SETREG_RGBA(0x0A, 0x14, 0x2A, 0x80);
static const uint64_t ColorPanelEdge = GS_SETREG_RGBA(0x29, 0x4E, 0x7A, 0x80);
static const uint64_t ColorHighlight = GS_SETREG_RGBA(0x12, 0x35, 0x5D, 0x80);

static const uint64_t FontMainColor = ColorGrey;
static const uint64_t BGColor = ColorBlack;
static const uint64_t HeaderTextColor = GS_SETREG_RGBA(0x74, 0x98, 0xC8, 0x80);
static const uint64_t WarnTextColor = GS_SETREG_RGBA(0xF0, 0xB8, 0x42, 0x80);
static const uint64_t ErrorTextColor = GS_SETREG_RGBA(0xF0, 0x58, 0x78, 0x80);

// Initialized in gui.c
extern GSGLOBAL *gsGlobal;
extern GSTEXTURE *gridSelector;

// Loads a PNG file through LUNA's RGBA-normalizing decoder.
int loadPNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path);

// Decodes a PNG into EE memory without uploading it to GS VRAM.
int decodePNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path);
// Collections can abandon obsolete work between file-read and whole-image stages.
int decodeCollectionPNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture,
                                   const char *path, const volatile int *cancelled);

#define ALIGN_LEFT 0 << 0
#define ALIGN_RIGHT 1 << 0
#define ALIGN_TOP 0 << 1
#define ALIGN_BOTTOM 1 << 1
#define ALIGN_VCENTER 1 << 2
#define ALIGN_HCENTER 1 << 3
#define ALIGN_NONE (ALIGN_TOP | ALIGN_LEFT)
#define ALIGN_CENTER (ALIGN_VCENTER | ALIGN_HCENTER)

// Icon types, must match ICONS array index in gui_icons.h
typedef enum {
  ICON_CIRCLE,
  ICON_CROSS,
  ICON_SQUARE,
  ICON_TRIANGLE,
  ICON_L1,
  ICON_R1,
  ICON_SELECT,
  ICON_START,
  ICON_ENABLED,
  ICON_L2,
  ICON_R2,
  ICON_DPAD,
  ICON_L3,
  ICON_R3,
} IconType;

typedef struct {
  IconType icon;
  const char *label;
} ButtonPrompt;

typedef struct PromptBar {
  const char *note;
  const ButtonPrompt *items;
  int count;
} PromptBar;

#define MENU_PROMPT_MAX 8
typedef struct {
  ButtonPrompt items[MENU_PROMPT_MAX];
  char labels[MENU_PROMPT_MAX][96],note[256];
  int count,hasNote;
} MenuPromptSnapshot;
typedef struct {
  MenuPromptSnapshot from,to;
  uint32_t started,lastDraw;
  int initialized;
} MenuPromptTransition;

typedef enum {
  UI_FONT_DEJAVU,
  UI_FONT_PSBBN,
  UI_FONT_COUNT,
} UIFont;

// Initializes and uploads graphics resources to GS VRAM
int initGraphics();

// Reserve a stable VRAM slot for the active font before managed textures load.
int reserveUIFontVRAM(void);

// Switches the UI bitmap font; leaves the current font selected on failure.
int setUIFont(UIFont selection);

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawText(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text);

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text);

// A single compact line, clipped to the right edge; uses the active font atlas.
void drawTextLineScaled(int x, int y, int right, int z, float scale,
                        uint64_t color, const char *text);

// Menu text: uniform scale, pixel-aligned glyphs and matching atlas padding.
void drawTextLineScaledCrisp(int x, int y, int right, int z, float scale,
                           uint64_t color, const char *text);

// Draws a single line shifted left within a clipped horizontal window.
int drawTextMarquee(int x1, int y, int x2, int z, uint64_t color, const char *text, int scrollX);

// Frees memory used by the font
void closeFont();

// Returns line height for used font
uint8_t getFontLineHeight();

// Gets the line width for the first line in text
float getLineWidth(const char *text);
// Includes glyph overhang and the crisp renderer's extra atlas texel.
float getLineDrawWidth(const char *text);

// Returns icon width
int getIconWidth(IconType iconType);

// Draws the icon at specified coordinates
void drawIcon(float x, float y, int z, uint64_t color, IconType iconType);
// Official wordmark; pixel coordinates, original aspect, standard color/opacity.
void drawLunaLogo(float centerX, float top, float width, int z, uint64_t color);
void drawIconScaled(float x, float y, int z, uint64_t color, IconType iconType, float scale);

// Draws the icon in [x1,y1],[x2,y2] window.
void drawIconWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, IconType iconType);

// Centers prompts within equal-width slots; an optional note occupies the left half.
void drawPromptBar(int left, int top, int right, int bottom, int z,
                   uint64_t labelColor, PromptBar bar);
// Morph prompt-only bars: shared buttons move once, changed labels crossfade.
void drawPromptBarTransition(int left, int top, int right, int bottom, int z,
                            uint64_t labelColor, PromptBar from, PromptBar to,
                            float progress, float opacity);
// Menu-owned state keeps temporary prompt arrays safe across rendered frames.
void drawMenuPromptBar(MenuPromptTransition *state, int left, int top, int right,
                       int bottom, int z, uint64_t color, PromptBar bar, float opacity);

// Quiet selection plate with the Options menu's leading accent.
void drawMenuRowSelector(int left, int top, int right);

// Proportional glass rail for list viewports; hidden when all rows fit.
void drawListScrollbar(int x, int top, int bottom, int total, int first,
                       int visible, int z);

#include "ui/camera_draw.h"

#endif
