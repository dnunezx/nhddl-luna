#ifndef LUNA_UI_FILE_MANAGER_H
#define LUNA_UI_FILE_MANAGER_H

// Returns 1 for Exit, 0 for the library, or STORAGE_UI_REFRESH for a rescan.
// With no library, the menu remains open until Exit is chosen.
int uiMainMenuLoop(int hasLibrary);
#ifdef LUNA_ENABLE_PSXCORE
#include "luna_psxcore_vmc.h"
const char *uiPs1VmcLoad(Target *target, LunaPsxVmcSettings *settings);
int uiPs1VmcRetry(const char *error);
int uiPs1VmcChooseSaves(const char *const *labels, unsigned count);
int uiPs1VmcPolicy(Target *target, LunaPsxVmcSettings *settings);
const char *uiPs1VmcPrepare(Target *target, LunaPsxVmcSettings *settings);
const char *uiPs1VmcCreatePrivate(const LunaPsxVmcSettings *settings);
// Picker/creation return 1 after changing the pending per-game assignment.
int uiPs1VmcPicker(Target *target, LunaPsxVmcSettings *settings);
int uiPs1VmcCreate(Target *target, LunaPsxVmcSettings *settings);
void uiPs1VmcManage(const LunaPsxVmcSettings *settings, int slot);
void uiPs1VmcDescribe(const LunaPsxVmcSettings *settings, char name[64], char slots[2][32]);
#endif

#endif
