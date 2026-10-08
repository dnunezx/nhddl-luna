#ifndef LUNA_UI_AMBIENT_ORBS_H
#define LUNA_UI_AMBIENT_ORBS_H
void resetAmbientOrbsTextures(void);

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

typedef enum {
  ORB_SHAPE_DIAMOND,
  ORB_SHAPE_CUBE,
  ORB_SHAPE_OCTAHEDRON,
  ORB_SHAPE_SPHERE,
  ORB_SHAPE_LUNA,
  ORB_SHAPE_SKULL,
  ORB_SHAPE_ATOM,
  ORB_SHAPE_PLAYTIME,
  ORB_SHAPE_COUNT
} OrbShape;

#define ORBS_SHAPES_ALL_MASK ((1U << ORB_SHAPE_PLAYTIME) - 1U)
void setAmbientOrbsShapes(uint32_t enabledShapes, uint32_t now);

// Ambient Orbs own their default background, placement, and animation.
void resetAmbientOrbs(uint32_t startMs);
void observeAmbientOrbsSelection(int selectedTitleIdx, uint32_t now);
void setAmbientOrbsBackgroundStyle(int enabled);
void setAmbientOrbsTheme(AmbientOrbsTheme theme, uint32_t now);
int setAmbientOrbsAppearance(AmbientOrbsAppearance appearance);
void setAmbientOrbsColor(AmbientOrbsColorPart part, AmbientOrbsColor color);
// Returns 1 when the Ambient Orbs background was selected and drawn.
int drawAmbientOrbsBackground(uint32_t now);

// Orbit view cycles enabled centered formations.
void resetAmbientOrbsOrbit(uint32_t now);
void drawAmbientOrbsOrbit(int centerX, int centerY, int radiusX,
                          int radiusY, uint32_t now, int trailZ);
// The System Configuration scene always uses the BIOS orb masks and the
// retail seven-orb clock motion, independent of LUNA's orb preferences.
int loadAmbientOrbsSystemConfigAssets(void);
// Shares the rod scene's clock, world origin and camera; no screen ellipse.
void drawAmbientOrbsSystemConfig(uint64_t clockMs, uint32_t now,
                                 uint32_t elapsedMs, int trailZ);

// Boot splash alternates centered LUNA and rotating cube formations.
// Its orb sprites use the bright LUNA appearance.
void resetAmbientOrbsSplash(uint32_t now);
void drawAmbientOrbsSplash(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t now, int trailZ);

#endif
