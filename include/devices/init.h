#ifndef _DEVICES_INIT_H_
#define _DEVICES_INIT_H_

#include "common.h"

// Initializes IOP modules
int initModules(ModeType modeType);

// Flush storage and power off the console.
void powerOffConsole(void);

#endif
