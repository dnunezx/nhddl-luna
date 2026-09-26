// LUNA 2026
#ifndef _UI_GRAPHICS_H_
#define _UI_GRAPHICS_H_

#include <gsKit.h>
#include <stdint.h>

// Predefined colors
// static const uint64_t ColorWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
static const uint64_t ColorBlack = GS_SETREG_RGBA(0x04, 0x08, 0x16, 0x80);
static const uint64_t ColorSelected = GS_SETREG_RGBA(0x22, 0xA8, 0xFF, 0x80);
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
} IconType;

// Initializes and uploads graphics resources to GS VRAM
int initGraphics();

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawText(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text);

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text);

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

// Draws the embedded Classic scrollbar PNG at a chosen height.
void drawClassicScrollbar(float x, float y, float height, int z);

// Draws the user-supplied boot logo centered at x and scaled to width
void drawBootLogo(float centerX, float y, float width, int z);

// Releases the boot-only logo from GS VRAM after the splash screen closes.
void releaseBootLogo();

#endif
