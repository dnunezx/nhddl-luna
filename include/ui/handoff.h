// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_HANDOFF_H
#define LUNA_UI_HANDOFF_H

#include "neutrino.h"
#include "target.h"
#include <gsKit.h>

typedef struct {
  Target *target;
  GSTEXTURE *cover;
} UILaunchHandoff;

// Draws and presents the launch handoff immediately. There is deliberately no
// minimum display duration; launch work resumes as soon as the frame is shown.
void uiPresentLaunchHandoff(Target *target, GSTEXTURE *cover, LaunchStage stage);

// LaunchProgressCallback adapter used by the Neutrino handoff.
void uiLaunchHandoffProgress(LaunchStage stage, void *userdata);

#endif
