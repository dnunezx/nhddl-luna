#ifndef LUNA_CHEAT_STORAGE_H
#define LUNA_CHEAT_STORAGE_H
#include "cheats.h"
#include "target.h"
// Paths use the game's metadata device, including the mounted HDL/PFS root.
int lunaCheatsLoad(Target *target, LunaCheatFile *file, char *path, size_t pathSize,
                   char *error, size_t size);
int lunaCheatsLoadSettings(Target *target, LunaCheatSettings *settings);
int lunaCheatsSaveSettings(Target *target, const LunaCheatSettings *settings);
// NULL settings means load saved choices. Disabled cheats never open a .cht.
int lunaCheatsPrepare(Target *target, const LunaCheatSettings *settings,
                      char **payload, char *error, size_t size);
#endif
