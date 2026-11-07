#ifndef LUNA_UI_UNDERGROUND_H
#define LUNA_UI_UNDERGROUND_H
#include <stdint.h>
#include "ui/view_scroll.h"

int uiUndergroundIsBackground(LibraryBackground background);
int uiUndergroundIsGlass(GlassColorPreset preset);
uint64_t uiUndergroundTintColor(int red, int green, int blue, int alpha);
uint64_t uiUndergroundTextColor(uint64_t color);
int uiUndergroundPrepareBackground(LibraryBackground background);
void uiUndergroundReleaseBackground(void);
void uiUndergroundReleaseInterface(void);
void uiUndergroundClose(void);
void uiUndergroundBackgroundDraw(uint32_t nowMs);
// Underground, Static-X and Phantom share artwork with distinct palettes.
void uiUndergroundFrameDraw(float left, float top, float right, float bottom,
                            int z, float opacity);
void uiUndergroundPanelDraw(float left, float top, float right, float bottom,
                            int z, float opacity);
int uiUndergroundLogoDraw(float centerX, float top, float width, int z, float opacity);

#endif
