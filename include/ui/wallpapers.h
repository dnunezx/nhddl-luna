#ifndef LUNA_UI_WALLPAPERS_H
#define LUNA_UI_WALLPAPERS_H

#include "ui/view_scroll.h"

void resetAnimatedWallpapers(void);
void drawAnimatedWallpaper(LibraryBackground background, uint32_t nowMs);
void drawUndergroundWallpaper(uint32_t nowMs);

#endif
