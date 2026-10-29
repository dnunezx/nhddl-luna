#ifndef LUNA_UI_VIEW_STATE_H
#define LUNA_UI_VIEW_STATE_H

#include "target.h"
#include "ui/appearance.h"
#include "ui/navigation.h"
#include "ui/view_scroll.h"
#include "ui/ambient_orbs.h"
#include "ui/graphics.h"
#include "ui/views.h"

// PS1 covers in List and 3D use this preference; absent/invalid state is Tall.
Ps1CaseStyle loadPs1CaseStyle(Target *target);

// Explicit startup preference on the metadata drive; never saved by navigation.
// Missing or invalid state falls back to List. Callers skip disabled views.
UILibraryView loadStartupLibraryView(Target *target);
int saveStartupLibraryView(Target *target, UILibraryView view);

// Game mode uses the same metadata drive; absent/invalid state defaults to All.
TargetFilter loadLastGameMode(Target *target);
int saveLastGameMode(Target *target, TargetFilter filter);

// List, Collections, Orbit, and 3D are enabled without a saved selection.
uint32_t loadEnabledLibraryViews(Target *target);

// Classic artwork layout is a library-wide preference on the metadata drive.
// Missing or invalid state keeps the original separate cover and disc layout.
int loadClassicArtOverlap(Target *target);

// Collections' optional game list defaults to off.
int loadCollectionGameList(Target *target);

// The shared library background defaults to stars and cubes.
LibraryBackground loadLibraryBackground(Target *target);

// Orb behavior defaults to LUNA when no valid choice has been saved.
AmbientOrbsTheme loadAmbientOrbsTheme(Target *target);

uint32_t loadEnabledOrbShapes(Target *target);

// Appearance is independent of behavior and defaults to LUNA.
AmbientOrbsAppearance loadAmbientOrbsAppearance(Target *target);

// Background orb and tail colors are independent; absent settings keep the original colors.
AmbientOrbsColor loadAmbientOrbsColor(Target *target, AmbientOrbsColorPart part);

// Glass color is a library-wide preference; invalid or absent data uses Original.
GlassColorPreset loadGlassColorPreset(Target *target);

// The UI font defaults to DejaVu Sans when no valid preference exists.
UIFont loadUIFont(Target *target);

// Ambient sound is disabled when no valid preference has been saved.
int loadAmbientSoundEnabled(Target *target);

#endif
