#ifndef LUNA_UI_VIEW_ORBS_H
#define LUNA_UI_VIEW_ORBS_H

#include "target.h"
#include <stdint.h>

typedef enum {
  GLASS_COLOR_ORIGINAL,
  GLASS_COLOR_WHITE_GRAY,
  GLASS_COLOR_BLACK,
  GLASS_COLOR_COUNT
} GlassColorPreset;

void resetGlassVisuals(uint32_t startMs);
void initGlassStarAtlas(void);
void setGlassColorPreset(GlassColorPreset preset);
GlassColorPreset getGlassColorPreset(void);
uint64_t glassPresetColor(int red, int green, int blue, int alpha);
uint64_t glassCoverAccentColor(int alpha);
uint64_t glassMissingCoverColor(int alpha);
uint64_t glassMissingCoverTextColor(void);
uint64_t glassMissingCoverDiamondColor(int alpha);
void observeOrbSelection(int selectedTitleIdx, uint32_t now);
void drawSplashGlassBackground(uint32_t now);
void setOrbsBackgroundStyle(int enabled);
int orbsVisualCacheIndex(int flowOffset);
void drawOrbsView(TargetList *titles, int selectedTitleIdx,
                  int flowOffset, int visualFocus, uint32_t now);

#endif
