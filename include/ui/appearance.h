#ifndef LUNA_UI_APPEARANCE_H
#define LUNA_UI_APPEARANCE_H

#include "target.h"
#include <stdint.h>

// Storage values have explicit names; enum ordinals are never serialized.
typedef struct {
  uint32_t background, glassColor, font, ambientSound, enabledViews;
  uint32_t classicOverlap, collectionGameList, ps1CaseStyle;
  uint32_t orbsTheme, orbsAppearance, orbsColor, tailsColor, orbShapes;
  uint32_t ringColor;
  uint32_t spaceSpeed, spaceDepth;
  uint32_t ringEffectsColor;
} LunaAppearance;

// Always fills settings, including legacy/default fallback on read errors.
int loadAppearance(Target *target, LunaAppearance *settings);
int saveAppearance(Target *target, const LunaAppearance *settings);
// Device maps and mounted metadata locations can change during a rescan.
void resetAppearanceCache(void);
// Internal migration reader: preserves the individual files' existing rules.
void loadLegacyAppearance(Target *target, LunaAppearance *settings);

#endif
