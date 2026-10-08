// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_OPL_VMC_H
#define LUNA_OPL_VMC_H
#include "opl_ata_abi.h"
#include "opl_devices.h"
#include "options.h"
#include "target.h"
// Returns the active slot count or a negative error; caller closes retained fds on failure.
int oplPrepareVmcs(Target *target, ArgumentList *arguments, const LunaOplDevice *device,
                   unsigned char *module, size_t moduleSize,
                   OplCdvdSettingsBdm *disc, OplFhiSettings *fhi, int fds[2]);
#endif
