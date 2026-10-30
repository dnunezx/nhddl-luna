#ifndef LUNA_UI_VIEW_SCROLL_H
#define LUNA_UI_VIEW_SCROLL_H

#include "target.h"
#include <stdint.h>

typedef enum {
  GLASS_COLOR_ORIGINAL,
  GLASS_COLOR_WHITE_GRAY,
  GLASS_COLOR_BLACK,
  GLASS_COLOR_CYBER,
  GLASS_COLOR_COUNT
} GlassColorPreset;

typedef enum {
  LIBRARY_BACKGROUND_STARS,
  LIBRARY_BACKGROUND_ORBS,
  LIBRARY_BACKGROUND_RED_CLOUDS,
  LIBRARY_BACKGROUND_MIDNIGHT_CUBES,
  LIBRARY_BACKGROUND_SYSTEM_CONFIG,
  LIBRARY_BACKGROUND_NEO,
  LIBRARY_BACKGROUND_AURORA,
  LIBRARY_BACKGROUND_RONIN,
  LIBRARY_BACKGROUND_TORCH,
  LIBRARY_BACKGROUND_TEMPEST,
  LIBRARY_BACKGROUND_SPACE_FLIGHT,
  LIBRARY_BACKGROUND_COUNT
} LibraryBackground;

void resetGlassVisuals(uint32_t startMs);
void initSystemConfigCapture(void);
void initGlassStarAtlas(void);
int setLibraryBackground(LibraryBackground background);
void setGlassColorPreset(GlassColorPreset preset);
GlassColorPreset getGlassColorPreset(void);
uint64_t glassPresetColor(int red, int green, int blue, int alpha);
uint64_t glassLightColor(int red, int green, int blue, int alpha);
uint64_t glassCoverAccentColor(int alpha);
uint64_t glassMissingCoverColor(int alpha);
uint64_t glassMissingCoverTextColor(void);
uint64_t glassMissingCoverDiamondColor(int alpha);
int orbsVisualCacheIndex(int flowOffset);
void drawOrbsView(TargetList *titles, int selectedTitleIdx,
                  int flowOffset, int visualFocus, int fastScroll,
                  int entryProgress, uint32_t now,
                  const char *nextViewLabel);

void drawOrbitLibraryBackground(uint32_t nowMs);
// Game Options draws the live background once, independently of frozen artwork.
void beginOptionsLibraryBackground(uint32_t nowMs, int orbit);
void endOptionsLibraryBackground(void);

#endif
