#ifndef LUNA_UI_GAME_OPTIONS_H
#define LUNA_UI_GAME_OPTIONS_H

#include "options.h"
#include <stdint.h>

#define LUNA_GAME_COMPAT_COUNT 5
#define LUNA_GAME_ROW_COUNT 10

typedef enum {
  LUNA_GAME_FAST_READS,
  LUNA_GAME_SYNC_READS,
  LUNA_GAME_UNHOOK_SYSCALLS,
  LUNA_GAME_DVD_DL,
  LUNA_GAME_BUFFER_OVERRUN,
  LUNA_GAME_LAUNCH_ARGUMENTS,
  LUNA_GAME_VIDEO_MODE,
  LUNA_GAME_FIELD_FLIP,
  LUNA_GAME_PS2_LOGO,
  LUNA_GAME_DEBUG_COLORS
} LunaGameRow;

typedef struct {
  uint8_t compat;
  int videoMode; // 0 = default, 1..5 = forced modes, -1 = unrecognized value
  int fieldFlip; // 0 = off, 1..3 = field flipping modes
  int ps2Logo;
  int debugColors;
} LunaGameOptions;

void lunaGameOptionsRead(LunaGameOptions *options, ArgumentList *arguments);
// Returns 1 when an option changed, 0 otherwise. Launch arguments use a separate page.
int lunaGameOptionsChange(LunaGameOptions *options, ArgumentList *arguments,
                          LunaGameRow row, int direction);
const char *lunaGameOptionsValue(const LunaGameOptions *options, LunaGameRow row);

#endif
