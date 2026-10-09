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

// Loads an embedded PNG and uploads it to GS VRAM.
int loadPNGTextureRGBAMemory(GSGLOBAL *gsGlobal, GSTEXTURE *texture,
                             const unsigned char *data, size_t size);

// Decodes a PNG into EE memory without uploading it to GS VRAM.
int decodePNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path);
int decodePNGTextureRGBATimed(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path,
                              uint32_t *readMs, uint32_t *decodeMs);

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

typedef struct {
  const char *note;
  const ButtonPrompt *items;
  int count;
} PromptBar;

typedef enum {
  CARD_ART_NONE = -1,
  CARD_ART_VIRTUAL,
  CARD_ART_SLOT_1,
  CARD_ART_SLOT_2,
  CARD_ART_MEMORY_CARD_MENU,
  CARD_ART_FILE_EXPLORER,
  CARD_ART_COUNT,
} CardArtType;

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
UIFont getUIFont(void);

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

// Draws a single line shifted left within a clipped horizontal window.
int drawTextMarquee(int x1, int y, int x2, int z, uint64_t color, const char *text, int scrollX);

// Frees memory used by the font
void closeFont();

// Returns line height for used font
uint8_t getFontLineHeight();

// Gets the line width for the first line in text
float getLineWidth(const char *text);

// Returns icon width
int getIconWidth(IconType iconType);

// Draws the icon at specified coordinates
void drawIcon(float x, float y, int z, uint64_t color, IconType iconType);

// Draws the icon in [x1,y1],[x2,y2] window.
void drawIconWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, IconType iconType);

// Centers prompts within equal-width slots; an optional note occupies the left half.
void drawPromptBar(int left, int top, int right, int bottom, int z,
                   uint64_t labelColor, PromptBar bar);

// Quiet selection plate with the Options menu's leading accent.
void drawMenuRowSelector(int left, int top, int right);

// Draws bundled memory-card artwork at the requested square size.
void drawCardArt(CardArtType card, float x, float y, float size);

// Draws the embedded Classic scrollbar PNG at a chosen height.

#endif
