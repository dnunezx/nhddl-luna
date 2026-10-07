#ifndef _UI_H_
#define _UI_H_

#include "target.h"
#include "options.h"
#include "cheats.h"
#include <gsKit.h>
#include <stdint.h>

int uiInit();
uint32_t uiNowMs(void);
int uiLoop(TargetList *titles, int preparedCollectionIdx);
void uiLaunchTitle(Target *target, ArgumentList *arguments);
// Returns 0 after a recoverable preflight error, -1 if execution returned.
int uiLaunchTitleWithCheats(Target *target, ArgumentList *arguments,
                           const LunaCheatSettings *cheats);

// Splash screen log level types
typedef enum {
  LEVEL_INFO_NODELAY, // Prints text without delay
  LEVEL_INFO,         // Prints in regular color and waits for a second
  LEVEL_WARN,         // Prints in warning color and waits for two seconds
  LEVEL_ERROR,        // Prints in error color and waits for two seconds
} UILogLevelType;

// Initializes and starts UI splash thread
int startSplashScreen();

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...);

// Sets Neutrino version on the splash screen
void uiSplashSetNeutrinoVersion(const char *str);

// Stops UI splash thread
void stopUISplashThread();

#endif
