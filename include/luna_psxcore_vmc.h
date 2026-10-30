#ifndef LUNA_PSXCORE_VMC_H
#define LUNA_PSXCORE_VMC_H
#include "target.h"
#include "psxcore_file.h"
#include <stddef.h>
#include <stdint.h>

// Preferences select cards; they never authorize a game to use them.
typedef struct LunaPsxVmcSettings {
  unsigned char volume[16];
  char key[49]; // Empty selects Sony's default pair on the game drive.
  char gameRoot[8], gamePath[NP_FILE_PATH_BYTES];
  unsigned mode;
  char cards[2][NP_FILE_PATH_BYTES]; // Independent paths on one save volume.
} LunaPsxVmcSettings;
enum { LUNA_PS1_CARDS_DEFAULT, LUNA_PS1_CARDS_CUSTOM, LUNA_PS1_CARDS_SLOTS };
typedef int (*LunaPsxVmcProgress)(uint64_t completed, uint64_t total);
struct DeviceMapEntry;
int lunaPsxVmcDevice(const struct DeviceMapEntry *device, char root[8]);
int lunaPsxVmcKeyValid(const char *key);
const char *lunaPsxVmcVolume(const char *root, unsigned char id[16], int create);
const char *lunaPsxVmcFindVolume(const unsigned char id[16], char root[8]);
const char *lunaPsxVmcLoad(Target *target, LunaPsxVmcSettings *settings, LunaPsxVmcProgress progress);
const char *lunaPsxVmcSave(const LunaPsxVmcSettings *settings);
// Archive damaged/interrupted preferences and return to defaults; cards untouched.
const char *lunaPsxVmcReset(Target *target);
const char *lunaPsxVmcBase(const LunaPsxVmcSettings *settings, char base[128]);
const char *lunaPsxVmcPaths(const char *base, char cards[2][NP_FILE_PATH_BYTES]);
const char *lunaPsxVmcResolve(const LunaPsxVmcSettings *settings, char root[8], char cards[2][NP_FILE_PATH_BYTES]);
const char *lunaPsxVmcPrepare(const LunaPsxVmcSettings *settings, int create);
const char *lunaPsxVmcCreate(const char *root, const char *name, char key[49]);
const char *lunaPsxVmcCard(const char *path, unsigned char hash[32]);
const char *lunaPsxVmcBackup(const char *base, char out[256]);
const char *lunaPsxVmcReplace(const char *base, const char *source, int slot);
const char *lunaPsxVmcRecover(const char *base, int slot, const char *suffix);
#endif
