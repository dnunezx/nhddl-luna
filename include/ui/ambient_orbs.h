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

// Ambient Orbs own their default background, placement, and animation.
void resetAmbientOrbs(uint32_t startMs);
void observeAmbientOrbsSelection(int selectedTitleIdx, uint32_t now);
void setAmbientOrbsBackgroundStyle(int enabled);
void setAmbientOrbsTheme(AmbientOrbsTheme theme, uint32_t now);
int setAmbientOrbsAppearance(AmbientOrbsAppearance appearance);
// Returns 1 when the Ambient Orbs background was selected and drawn.
int drawAmbientOrbsBackground(uint32_t now);

// Orbit view cycles its centered cube, octahedron, orbit, and LUNA formations.
void resetAmbientOrbsOrbit(uint32_t now);
void drawAmbientOrbsOrbit(int centerX, int centerY, int radiusX,
                          int radiusY, uint32_t now, int trailZ);

// Boot splash alternates LUNA and the rotating cube, beginning with LUNA.
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
