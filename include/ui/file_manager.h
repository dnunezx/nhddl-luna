#ifndef LUNA_UI_FILE_MANAGER_H
#define LUNA_UI_FILE_MANAGER_H

// Returns 1 for Exit, 0 for the library, or STORAGE_UI_REFRESH for a rescan.
// With no library, the menu remains open until Exit is chosen.
#include "target.h"
int uiMainMenuLoop(int hasLibrary, Target *target);
#include "ui/game_options.h"
void uiVmcSetLibrary(TargetList *library);
// Draw-only backdrop for pickers opened by another workspace; clear on return.
void uiFileManagerSetBackdrop(void (*draw)(void *), void *context);
// Opt in only while Game Options owns the active crystal page; NULL on return.
void uiFileManagerSetPageSection(const char *sectionTitle);
int uiVmcPs2Slot(Target *target, ArgumentList *arguments, LunaGameOptions *options, int slot);
int uiVmcPs2Policy(Target *target, ArgumentList *arguments, LunaGameOptions *options);
const char *uiVmcCheckPs2(Target *target, ArgumentList *arguments);
#ifdef LUNA_ENABLE_PSXCORE
#include "luna_psxcore_vmc.h"
const char *uiPs1VmcLoad(Target *target, LunaPsxVmcSettings *settings);
int uiPs1VmcRetry(Target *target, const char *error);
int uiPs1VmcPolicy(Target *target, LunaPsxVmcSettings *settings);
// Returns 1 after changing the pending per-game assignment.
int uiPs1VmcPicker(Target *target, LunaPsxVmcSettings *settings);
int uiPs1VmcSlot(Target *target, LunaPsxVmcSettings *settings, int slot);
void uiPs1VmcManage(const LunaPsxVmcSettings *settings, int slot);
void uiPs1VmcDescribe(const LunaPsxVmcSettings *settings, char name[64], char slots[2][32]);
// Returns STORAGE_UI_REFRESH only after saving a playlist and requesting a scan.
int uiPs1DiscManage(Target *target);
#endif

#endif
