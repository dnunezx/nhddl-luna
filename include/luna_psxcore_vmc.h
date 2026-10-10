#ifndef LUNA_PSXCORE_VMC_H
#define LUNA_PSXCORE_VMC_H

#include "target.h"
#include "psxcore_shared.h"
#include <stddef.h>
#include <stdint.h>

typedef struct LunaPsxVmcSettings {
  unsigned char disc[32], volume[16];
  char key[33]; // Empty selects the existing private card pair.
  char gameRoot[8]; // Transient mount name; never persisted as media identity.
  unsigned mode; // Automatic is the default; explicit legacy choices stay manual.
} LunaPsxVmcSettings;
enum { LUNA_PS1_CARDS_AUTO, LUNA_PS1_CARDS_PRIVATE, LUNA_PS1_CARDS_SHARED };

typedef int (*LunaPsxVmcProgress)(uint64_t completed, uint64_t total);
// Return a source-card index, or -1 to cancel. Called only for conflicting saves.
typedef int (*LunaPsxVmcChoose)(const char *const *labels, unsigned count);
void lunaPsxVmcLibrary(const TargetList *library);
const char *lunaPsxVmcGroup(const char *discId, char key[33]);
const char *lunaPsxVmcAutomatic(Target *target, LunaPsxVmcSettings *settings,
    int create, LunaPsxVmcProgress progress, LunaPsxVmcChoose choose);
struct DeviceMapEntry;
int lunaPsxVmcDevice(const struct DeviceMapEntry *device, char root[8]);
const char *lunaPsxVmcVolume(const char *root, unsigned char id[16], int create);
const char *lunaPsxVmcFindVolume(const unsigned char id[16], char root[8]);
const char *lunaPsxVmcLoad(Target *target, LunaPsxVmcSettings *settings,
                          LunaPsxVmcProgress progress);
const char *lunaPsxVmcSave(const LunaPsxVmcSettings *settings);
const char *lunaPsxVmcBase(const LunaPsxVmcSettings *settings, char base[128]);
const char *lunaPsxVmcPrivate(const LunaPsxVmcSettings *settings, int create);
const char *lunaPsxVmcCreate(const char *root, const char *name,
                            const LunaPsxVmcSettings *source, NpSharedSet *set);
const char *lunaPsxVmcEnroll(const char *root, const char *key,
                            const unsigned char disc[32], int enroll);
const char *lunaPsxVmcRename(const char *base, const char *name);
const char *lunaPsxVmcCard(const char *path, unsigned char hash[32]);
const char *lunaPsxVmcBackup(const char *base, char out[256]);
// Restore a pair, or import one raw card. Existing cards are backed up first.
const char *lunaPsxVmcReplace(const char *base, const char *source, int slot);
const char *lunaPsxVmcExport(const char *source, const char *destination);
void lunaPsxVmcDiscName(const char *root, const unsigned char disc[32], char *name, size_t size);
void lunaPsxVmcRememberDisc(const char *root, const unsigned char disc[32], const char *name);

#endif
