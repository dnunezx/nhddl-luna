// LUNA modifications: Danny Nunez (dnunezx) 2026
#ifndef _NEUTRINO_H_
#define _NEUTRINO_H_

#include "devices/init.h"
#include "options.h"
#include "target.h"

typedef enum {
  LAUNCH_STAGE_PREPARING = 0,
  LAUNCH_STAGE_SYNCING,
  LAUNCH_STAGE_STARTING,
} LaunchStage;

typedef void (*LaunchProgressCallback)(LaunchStage stage, void *userdata);

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath);
// Reads version.txt from NEUTRINO_ELF_PATH
// Returns empty string if the file could not be read
char *getNeutrinoVersion();
// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
void launchTitle(Target *target, ArgumentList *arguments);
// Launches a target while reporting completed handoff boundaries to the UI.
// The callback must not block or introduce an artificial launch delay.
void launchTitleWithProgress(Target *target, ArgumentList *arguments,
                             LaunchProgressCallback progress, void *userdata);

#endif
