#ifndef LUNA_UI_VIEW_STATE_H
#define LUNA_UI_VIEW_STATE_H

#include "target.h"
#include "ui/navigation.h"
#include "ui/view_scroll.h"
#include "ui/ambient_orbs.h"
#include "ui/graphics.h"

// The selected view is stored with the selected title's metadata on its game drive.
// Missing or invalid state falls back to Classic.
UILibraryView loadLastLibraryView(Target *target);
int saveLastLibraryView(Target *target, UILibraryView view);

// List, Collections, Orbit, and 3D are enabled without a saved selection.
uint32_t loadEnabledLibraryViews(Target *target);
int saveEnabledLibraryViews(Target *target, uint32_t enabledViews);

// Classic artwork layout is a library-wide preference on the metadata drive.
// Missing or invalid state keeps the original separate cover and disc layout.
int loadClassicArtOverlap(Target *target);
int saveClassicArtOverlap(Target *target, int overlap);

// The shared library background defaults to stars and cubes.
LibraryBackground loadLibraryBackground(Target *target);
int saveLibraryBackground(Target *target, LibraryBackground background);

// Orb behavior defaults to LUNA when no valid choice has been saved.
AmbientOrbsTheme loadAmbientOrbsTheme(Target *target);
int saveAmbientOrbsTheme(Target *target, AmbientOrbsTheme theme);

uint32_t loadEnabledOrbShapes(Target *target);
int saveEnabledOrbShapes(Target *target, uint32_t enabledShapes);

// Appearance is independent of behavior and defaults to LUNA.
AmbientOrbsAppearance loadAmbientOrbsAppearance(Target *target);
int saveAmbientOrbsAppearance(Target *target, AmbientOrbsAppearance appearance);

// Background orb and tail colors are independent; absent settings keep the original colors.
AmbientOrbsColor loadAmbientOrbsColor(Target *target, AmbientOrbsColorPart part);
int saveAmbientOrbsColor(Target *target, AmbientOrbsColorPart part,
                         AmbientOrbsColor color);

// Glass color is a library-wide preference; invalid or absent data uses Original.
GlassColorPreset loadGlassColorPreset(Target *target);
int saveGlassColorPreset(Target *target, GlassColorPreset preset);

// The UI font defaults to DejaVu Sans when no valid preference exists.
UIFont loadUIFont(Target *target);
int saveUIFont(Target *target, UIFont selection);

// Ambient sound is disabled when no valid preference has been saved.
int loadAmbientSoundEnabled(Target *target);
int saveAmbientSoundEnabled(Target *target, int enabled);

#endif
