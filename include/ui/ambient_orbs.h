#ifndef LUNA_UI_AMBIENT_ORBS_H
#define LUNA_UI_AMBIENT_ORBS_H

#include <stdint.h>

typedef enum {
  ORBS_THEME_LUNA,
  ORBS_THEME_PS2_ORIGINAL
} AmbientOrbsTheme;

typedef enum {
  ORBS_APPEARANCE_LUNA,
  ORBS_APPEARANCE_PS2_ORIGINAL
} AmbientOrbsAppearance;

typedef enum {
  ORBS_COLOR_ORIGINAL,
  ORBS_COLOR_CYAN,
  ORBS_COLOR_VIOLET,
  ORBS_COLOR_ROSE,
  ORBS_COLOR_GREEN,
  ORBS_COLOR_GOLD,
  ORBS_COLOR_WHITE,
  ORBS_COLOR_COUNT
} AmbientOrbsColor;

typedef enum {
  ORBS_COLOR_PART_ORBS,
  ORBS_COLOR_PART_TAILS
} AmbientOrbsColorPart;

// Ambient Orbs own their default background, placement, and animation.
void resetAmbientOrbs(uint32_t startMs);
void observeAmbientOrbsSelection(int selectedTitleIdx, uint32_t now);
void setAmbientOrbsBackgroundStyle(int enabled);
void setAmbientOrbsTheme(AmbientOrbsTheme theme, uint32_t now);
int setAmbientOrbsAppearance(AmbientOrbsAppearance appearance);
void setAmbientOrbsColor(AmbientOrbsColorPart part, AmbientOrbsColor color);
// Returns 1 when the Ambient Orbs background was selected and drawn.
int drawAmbientOrbsBackground(uint32_t now);

// Orbit view cycles its centered cube, octahedron, orbit, and LUNA formations.
void resetAmbientOrbsOrbit(uint32_t now);
void drawAmbientOrbsOrbit(int centerX, int centerY, int radiusX,
                          int radiusY, uint32_t now, int trailZ);

// Boot splash alternates centered LUNA and rotating cube formations.
// Its orb sprites use the bright LUNA appearance.
void resetAmbientOrbsSplash(uint32_t now);
void drawAmbientOrbsSplash(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t now, int trailZ);

// The alphabet formation and navigation reaction belong only to Scroll view.
void resetAmbientOrbsScroll(void);
void triggerAmbientOrbsScrollReaction(int direction, uint32_t now);
void drawAmbientOrbsScroll(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t elapsedMs, int fastScroll,
                           const char *title, int trailZ, int scrollFocusX);

#endif
