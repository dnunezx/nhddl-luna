// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_HANDOFF_H
#define LUNA_UI_HANDOFF_H

#include "neutrino.h"
#include "target.h"

// Fades the library away, shows the moving background, then fades to black.
void uiPlayLaunchTransition(void);

// Keeps the final black frame through loader progress.
void uiLaunchHandoffProgress(LaunchStage stage, void *userdata);

#endif
