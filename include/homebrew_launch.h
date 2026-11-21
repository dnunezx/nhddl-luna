#ifndef LUNA_HOMEBREW_LAUNCH_H
#define LUNA_HOMEBREW_LAUNCH_H
#include <stddef.h>

typedef int (*LunaHomebrewProgress)(size_t completed, size_t total, void *context);
// Preparation retains the UI and all workers on failure. The selected path is argv[0].
const char *lunaHomebrewPrepare(const char *path, LunaHomebrewProgress progress, void *context);
const char *lunaHomebrewPrepareWithArgument(const char *path,const char *argument,
                                          LunaHomebrewProgress progress,void *context);
void lunaHomebrewDiscard(void);
void lunaHomebrewExecute(void) __attribute__((noreturn));
#endif
