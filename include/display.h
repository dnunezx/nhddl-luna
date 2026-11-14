#ifndef LUNA_DISPLAY_H
#define LUNA_DISPLAY_H
#include "common.h"

typedef enum {
  LUNA_DISPLAY_AUTO, LUNA_DISPLAY_NTSC, LUNA_DISPLAY_PAL,
  LUNA_DISPLAY_480P, LUNA_DISPLAY_COUNT
} LunaDisplayMode;

// Menu output only. Existing YAML/argv video options remain the fallback.
void lunaDisplayConfigure(const char *directory);
LunaDisplayMode lunaDisplayMode(void);
VModeType lunaDisplayVMode(LunaDisplayMode mode);
const char *lunaDisplayLabel(LunaDisplayMode mode);
// Call only after the user confirms a working output mode.
int lunaDisplaySave(LunaDisplayMode mode);
#endif
