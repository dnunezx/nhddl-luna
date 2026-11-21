#ifndef LUNA_QUICK_PLAY_H
#define LUNA_QUICK_PLAY_H

#include "target.h"

void quickPlayConfigure(const char *directory, const char *bootPath);
int quickPlayEnabled(void);
// -1 means the setting could not be saved, -2 means the old record could not be cleared.
int quickPlaySetEnabled(int enabled);
int quickPlayIsBoot(void);
int quickPlayExport(Target *target);
Target *quickPlayFindTarget(TargetList *titles);

#endif
