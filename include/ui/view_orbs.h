#ifndef LUNA_UI_VIEW_ORBS_H
#define LUNA_UI_VIEW_ORBS_H

#include "target.h"
#include <stdint.h>

void resetGlassVisuals(uint32_t startMs);
void initGlassStarAtlas(void);
void observeOrbSelection(int selectedTitleIdx, uint32_t now);
void drawSplashGlassBackground(uint32_t now);
void setOrbsBackgroundStyle(int enabled);
int orbsVisualCacheIndex(int flowOffset);
void drawOrbsView(TargetList *titles, int selectedTitleIdx,
                  int flowOffset, int visualFocus, uint32_t now);

#endif
