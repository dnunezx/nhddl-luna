#ifndef LUNA_OPL_ATA_H
#define LUNA_OPL_ATA_H

#include "neutrino.h"

// Returns on a preflight failure; a successful launch does not return.
int launchOplAta(Target *target, LaunchProgressCallback progress, void *userdata);

#endif
