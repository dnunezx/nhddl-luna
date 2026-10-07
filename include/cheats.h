// LUNA's raw PS2RD file model; independent of the renderer and PS2 runtime.
#ifndef LUNA_CHEATS_H
#define LUNA_CHEATS_H
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#define LUNA_CHEAT_MAX_ENTRIES 512
#define LUNA_CHEAT_MAX_PAIRS 8192
#define LUNA_CHEAT_NAME_MAX 127
#define LUNA_CHEAT_MAX_HOOKS 5
#define LUNA_CHEAT_MAX_CODES 250
#define LUNA_CHEAT_PAYLOAD_MAX (3 + 16 * (LUNA_CHEAT_MAX_HOOKS + LUNA_CHEAT_MAX_CODES))

typedef struct { uint32_t address, value; } LunaCheatPair;
typedef struct {
  char name[LUNA_CHEAT_NAME_MAX + 1];
  uint64_t id;
  int firstPair, pairCount, required, sourceLine;
} LunaCheatEntry;
typedef struct {
  LunaCheatEntry *entries;
  LunaCheatPair *pairs;
  int entryCount, pairCount;
} LunaCheatFile;
typedef struct {
  int enabled, count;
  uint64_t ids[LUNA_CHEAT_MAX_ENTRIES];
} LunaCheatSettings;

void lunaCheatFileFree(LunaCheatFile *file);
int lunaCheatParse(FILE *stream, LunaCheatFile *file, char *error, size_t size);
int lunaCheatSelected(const LunaCheatSettings *settings, uint64_t id);
int lunaCheatToggle(LunaCheatSettings *settings, uint64_t id);
// Reject missing selections and overflows; never emit a partial code block.
int lunaCheatPayload(const LunaCheatFile *file, const LunaCheatSettings *settings,
                     char **payload, char *error, size_t size);
int lunaCheatSettingsRead(FILE *stream, LunaCheatSettings *settings);
int lunaCheatSettingsWrite(FILE *stream, const LunaCheatSettings *settings);
#endif
