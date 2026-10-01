#ifndef LUNA_UI_GAME_OPTIONS_H
#define LUNA_UI_GAME_OPTIONS_H

#include "options.h"
#include <stdint.h>

#define LUNA_GAME_COMPAT_COUNT 5
#define LUNA_GAME_ROW_COUNT 13

typedef enum {
  LUNA_GAME_FAST_READS,
  LUNA_GAME_SYNC_READS,
  LUNA_GAME_UNHOOK_SYSCALLS,
  LUNA_GAME_DVD_DL,
  LUNA_GAME_BUFFER_OVERRUN,
  LUNA_GAME_LAUNCH_ARGUMENTS,
  LUNA_GAME_VMC_SLOT1,
  LUNA_GAME_VMC_SLOT2,
  LUNA_GAME_VIDEO_MODE,
  LUNA_GAME_FIELD_FLIP,
  LUNA_GAME_PS2_LOGO,
  LUNA_GAME_DEBUG_COLORS,
  LUNA_GAME_CORE
} LunaGameRow;

typedef struct {
  uint8_t compat;
  int videoMode; // 0 = default, 1..5 = forced modes, -1 = unrecognized value
  int fieldFlip; // 0 = off, 1..3 = field flipping modes
  int ps2Logo;
  int debugColors;
  int oplCore;
  char vmcSlotLabel[2][25];
} LunaGameOptions;

void lunaGameOptionsRead(LunaGameOptions *options, ArgumentList *arguments);
// Returns 1 when an option changed, 0 otherwise. Launch arguments use a separate page.
int lunaGameOptionsChange(LunaGameOptions *options, ArgumentList *arguments,
                          LunaGameRow row, int direction);
// Cycles Inherit, the opposite of the Global default, and an explicit match.
int lunaGameOptionsCyclePS2Logo(LunaGameOptions *options,
                                ArgumentList *arguments,
                                int globalEnabled, int direction);
int lunaGameOptionsSetVMC(LunaGameOptions *options, ArgumentList *arguments,
                          int slot, const char *path);
const char *lunaGameOptionsValue(const LunaGameOptions *options, LunaGameRow row);
// Applies the library default without replacing a title-specific logo choice.
int lunaApplyGlobalPS2Logo(ArgumentList *arguments, int enabled);

#endif
