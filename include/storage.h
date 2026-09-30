// LUNA storage preferences and manual library refresh.
#ifndef LUNA_STORAGE_H
#define LUNA_STORAGE_H
#include "common.h"
#include "target.h"

#define STORAGE_SOURCE_COUNT 7
#define STORAGE_SUPPORTED (MODE_ATA | MODE_HDL | MODE_USB | MODE_MX4SIO | MODE_MMCE | MODE_ILINK | MODE_UDPFS)
#define STORAGE_UI_REFRESH 2
typedef struct {
  ModeType enabled;
  char ip[16];
} StorageSettings;
typedef struct {
  int devices;
  int games;
  int error;
} StorageStatus;
extern StorageSettings STORAGE_SETTINGS;
extern StorageStatus STORAGE_STATUS[STORAGE_SOURCE_COUNT];
ModeType storageSourceMode(int index);
const char *storageSourceName(int index);
int storageValidIP(const char *ip);
int storageValidSettings(const StorageSettings *settings);
void storageConfigure(const char *directory, ModeType defaults, ModeType required,
                      int useSaved);
int storageSave(const StorageSettings *settings);
int storageNeedsRestart(const StorageSettings *settings);
int storageRequest(const StorageSettings *settings, ModeType scan);
// Call only after artwork, audio and pad workers have stopped.
TargetList *storageRefresh(TargetList *previous);
void storageRememberSelection(Target *target, int view);
int storageRestoreSelection(TargetList *titles, int *view);
int storageRequiredConflict(const StorageSettings *settings);
void storageProtectRuntime(const char *path);
// File VMCs use the game's enabled local drive, or its mounted PFS metadata.
const char *storageVMCRoot(const struct DeviceMapEntry *device);
// Save card paths relative to the metadata root, including pfs0:/OPL.
const char *storageVMCRelativePath(const struct DeviceMapEntry *device,
                                 const char *path);
#endif
