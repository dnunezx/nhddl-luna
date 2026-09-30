#ifndef _DEVICES_INIT_H_
#define _DEVICES_INIT_H_

#include "common.h"

// Initializes IOP modules
int initModules(ModeType modeType);
// Best-effort optional drivers; returns modes whose initialization failed.
ModeType initStorageModules(ModeType modes, int restart);
int parseIPConfig(void);

// Flush storage and power off the console.
void powerOffConsole(void);

#endif
