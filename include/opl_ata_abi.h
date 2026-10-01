// ABI shared with the pinned OPL ee_core and BDM ATA CDVDMAN payloads.
// Keep these layouts in sync with opl/ee_core/include/{modules,coreconfig}.h
// and opl/modules/iopcore/common/cdvd_config.h when updating the submodule.
#ifndef LUNA_OPL_ATA_ABI_H
#define LUNA_OPL_ATA_ABI_H

#include <osd_config.h>
#include <tamtypes.h>
#include <usbhdfsd-common.h>

#define OPL_ATA_MAX_FRAGS 64
#define OPL_ATA_CORE_MAGIC_0 0x4D614730
#define OPL_ATA_CORE_MAGIC_1 0x4D616731

typedef struct {
  void *ptr;
  unsigned int info;
} OplModule;

typedef struct {
  OplModule *modules;
  int count;
} OplModuleTable;

typedef struct {
  u8 NumParts;
  u8 media;
  u16 flags;
  u32 layer1_start;
  u8 DiscID[5];
  u8 zso_cache;
  u8 fakemodule_flags;
  u8 padding;
} __attribute__((packed)) OplCdvdSettingsCommon;

typedef struct {
  u8 frag_start;
  u8 frag_count;
} __attribute__((packed)) OplFragFile;

typedef struct {
  OplCdvdSettingsCommon common;
  OplFragFile fragfile[1];
  u32 bdDeviceId;
  u32 hddIsLBA48;
  bd_fragment_t frags[OPL_ATA_MAX_FRAGS];
} __attribute__((packed)) OplCdvdSettingsBdm;

typedef struct {
  s16 interlace, mode, ffmd;
  u32 dx_offset, dy_offset;
  u64 display, syncv, smode2;
  int k576P_fix, kGsDxDyOffsetSupported, FIELD_fix;
} OplGsmConfig;

typedef struct {
  u32 magic[2];
  char GameMode;
  char GameModeDesc[16];
  int EnableDebug;
  char ExitPath[256];
  int HDDSpindown;
  char g_ps2_ip[16], g_ps2_netmask[16], g_ps2_gateway[16];
  unsigned char g_ps2_ETHOpMode;
  u32 *gCheatList;
  void *eeloadCopy, *initUserMemory;
  void *ModStorageStart, *ModStorageEnd;
  char GameID[16];
  u32 _CompatMask;
  ConfigParam CustomOSDConfigParam;
  int enforceLanguage;
  int EnablePadEmuOp, PadEmuSettings, PadMacroSettings;
  int EnableGSMOp;
  OplGsmConfig GsmConfig;
} OplEeCoreConfig;

#endif
