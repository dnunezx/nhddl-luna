#ifndef LUNA_UI_VIEW_STATE_H
#define LUNA_UI_VIEW_STATE_H

#include "target.h"
#include "ui/navigation.h"
#include "ui/view_scroll.h"

// The selected view is stored with the selected title's metadata on its game drive.
// Missing or invalid state falls back to Classic.
UILibraryView loadLastLibraryView(Target *target);
int saveLastLibraryView(Target *target, UILibraryView view);

// All five views are enabled when no valid selection has been saved.
uint32_t loadEnabledLibraryViews(Target *target);
int saveEnabledLibraryViews(Target *target, uint32_t enabledViews);

// Classic artwork layout is a library-wide preference on the metadata drive.
// Missing or invalid state keeps the original separate cover and disc layout.
int loadClassicArtOverlap(Target *target);
int saveClassicArtOverlap(Target *target, int overlap);

// The shared library background defaults to stars and cubes.
int loadAmbientOrbsBackground(Target *target);
int saveAmbientOrbsBackground(Target *target, int enabled);

// Glass color is a library-wide preference; invalid or absent data uses Original.
GlassColorPreset loadGlassColorPreset(Target *target);
int saveGlassColorPreset(Target *target, GlassColorPreset preset);

// Ambient sound is enabled when no valid preference has been saved.
int loadAmbientSoundEnabled(Target *target);
int saveAmbientSoundEnabled(Target *target, int enabled);

#endif
