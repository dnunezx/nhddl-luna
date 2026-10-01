// Minimal ATA game handoff derived from the pinned Open PS2 Loader fork.
// Source reference: opl/src/{bdmsupport,supportbase,system,ioprp}.c.
#include "opl_ata.h"
#include "opl_ata_abi.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/ambient.h"
#include "ui/game_options.h"
#include <errno.h>
#include <fcntl.h>
#include <kernel.h>
#include <libcdvd.h>
#include <loadfile.h>
#include <osd_config.h>
#include <ps2sdkapi.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#define OPL_STORAGE_START 0x00097000u
#define OPL_STORAGE_LIMIT 0x000D0000u // Match OPL's reserved module region.
#define OPL_CORE_START 0x00084000u
#define OPL_CORE_LIMIT OPL_STORAGE_START
#define OPL_CLEAR_END 0x00100000u
#define OPL_PAYLOAD_LIMIT (128 * 1024)
#define OPL_MODULE_INFO(id, size) (((id) << 24) | (size))
#define ATA_DEVCTL_IS_48BIT 0x6840
#define OPL_COMPAT_ALT_READ 0x0001
#define OPL_COMPAT_SKIP_VIDEOS 0x0002
#define OPL_COMPAT_DVD_DL 0x0004
#define OPL_COMPAT_ACCURATE_READS 0x0008
#define OPL_COMPAT_ENABLE_POFF 0x0100
#define OPL_COMPAT_IOP_POFF 0x80000000u

typedef struct {
  unsigned char ident[16];
  uint16_t type, machine;
  uint32_t version, entry, phoff, shoff, flags;
  uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} OplElfHeader;

typedef struct {
  uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} OplElfProgram;

typedef struct {
  char name[10];
  uint16_t extinfoSize;
  uint32_t fileSize;
} OplRomdirEntry;

typedef struct {
  const char *name;
  unsigned char *data;
  size_t size;
} OplPayload;

enum { PAYLOAD_CORE, PAYLOAD_CDVDMAN, PAYLOAD_CDVDFSV, PAYLOAD_EESYNC,
       PAYLOAD_IOPRP, PAYLOAD_UDNL, PAYLOAD_IMGDRV, PAYLOAD_RESETSPU,
       PAYLOAD_COUNT };

static OplPayload payloads[PAYLOAD_COUNT] = {
    {"ee_core.elf", NULL, 0}, {"bdm_ata_cdvdman.irx", NULL, 0},
    {"cdvdfsv.irx", NULL, 0}, {"eesync-nano.irx", NULL, 0},
    {"IOPRP.img", NULL, 0}, {"udnl.irx", NULL, 0},
    {"imgdrv.irx", NULL, 0}, {"resetspu.irx", NULL, 0},
};

static void freePayloads(void) {
  for (int i = 0; i < PAYLOAD_COUNT; i++) {
    free(payloads[i].data);
    payloads[i].data = NULL;
    payloads[i].size = 0;
  }
}

static int loadPayloads(void) {
  const char *slash = strrchr(NEUTRINO_ELF_PATH, '/');
  if (slash == NULL)
    return -ENOENT;
  size_t directoryLength = (size_t)(slash - NEUTRINO_ELF_PATH);
  for (int i = 0; i < PAYLOAD_COUNT; i++) {
    char path[PATH_MAX + 1];
    if (snprintf(path, sizeof(path), "%.*s/opl/%s", (int)directoryLength,
                 NEUTRINO_ELF_PATH, payloads[i].name) >= sizeof(path))
      return -ENAMETOOLONG;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
      DPRINTF("OPL: missing %s\n", path);
      return -ENOENT;
    }
    if (fseek(file, 0, SEEK_END) ||
        (payloads[i].size = ftell(file)) == 0 ||
        payloads[i].size > OPL_PAYLOAD_LIMIT || fseek(file, 0, SEEK_SET)) {
      fclose(file);
      return -EINVAL;
    }
    payloads[i].data = malloc(payloads[i].size);
    if (payloads[i].data == NULL ||
        fread(payloads[i].data, 1, payloads[i].size, file) != payloads[i].size) {
      fclose(file);
      return -EIO;
    }
    fclose(file);
  }
  return 0;
}

static OplCdvdSettingsBdm *findCdvdSettings(void) {
  static const OplCdvdSettingsCommon marker = {
      0x68, 0x68, 0x1234, 0x39393939, "DSKID", 16, 8, 16};
  OplPayload *module = &payloads[PAYLOAD_CDVDMAN];
  for (size_t offset = 0; offset + sizeof(OplCdvdSettingsBdm) <= module->size;
       offset += 4) {
    if (!memcmp(module->data + offset, &marker, sizeof(marker)))
      return (OplCdvdSettingsBdm *)(module->data + offset);
  }
  return NULL;
}

static int prepareDisc(Target *target, uint8_t compat) {
  if (target->device->mode != MODE_ATA || target->id == NULL ||
      strlen(target->id) > 15)
    return -EINVAL;
  OplCdvdSettingsBdm *settings = findCdvdSettings();
  if (settings == NULL)
    return -EINVAL;

  int fd = open(target->fullPath, O_RDONLY);
  if (fd < 0)
    return -ENOENT;
  int iopFd = ps2sdk_get_iop_fd(fd);
  int fragments = fileXioIoctl2(iopFd, USBMASS_IOCTL_GET_FRAGLIST,
                                NULL, 0, settings->frags,
                                sizeof(settings->frags));
  if (fragments <= 0 || fragments > OPL_ATA_MAX_FRAGS) {
    close(fd);
    DPRINTF("OPL: invalid ISO fragment count: %d\n", fragments);
    return -EFBIG;
  }

  uint32_t maxLba = 0;
  unsigned char descriptor[6] = {0};
  if (lseek(fd, 16 * 2048, SEEK_SET) >= 0 &&
      read(fd, descriptor, sizeof(descriptor)) == sizeof(descriptor) &&
      descriptor[0] == 1 && !memcmp(descriptor + 1, "CD001", 5)) {
    lseek(fd, 16 * 2048 + 80, SEEK_SET);
    read(fd, &maxLba, sizeof(maxLba));
  }
  if (maxLba > 16 && lseek(fd, (off_t)maxLba * 2048, SEEK_SET) >= 0 &&
      read(fd, descriptor, sizeof(descriptor)) == sizeof(descriptor) &&
      descriptor[0] == 1 && !memcmp(descriptor + 1, "CD001", 5))
    settings->common.layer1_start = maxLba - 16;
  close(fd);

  settings->common.NumParts = 1;
  settings->common.media = strstr(target->fullPath, "/CD/") != NULL
                               ? SCECdPS2CD : SCECdPS2DVD;
  settings->common.flags =
      ((compat & (1U << 0)) ? OPL_COMPAT_ACCURATE_READS : 0) |
      ((compat & (1U << 1)) ? OPL_COMPAT_ALT_READ : 0) |
      ((compat & (1U << 3)) ? OPL_COMPAT_SKIP_VIDEOS : 0) |
      ((compat & (1U << 4)) ? OPL_COMPAT_DVD_DL : 0) |
      OPL_COMPAT_ENABLE_POFF;
  memset(settings->common.DiscID, 0, sizeof(settings->common.DiscID));
  settings->common.zso_cache = 0;
  settings->common.fakemodule_flags = (1 << 0) | (1 << 3) | (1 << 4) | (1 << 5);
  settings->fragfile[0].frag_start = 0;
  settings->fragfile[0].frag_count = fragments;
  settings->bdDeviceId = target->device->index;
  int lba48 = fileXioDevctl("xhdd0:", ATA_DEVCTL_IS_48BIT,
                            NULL, 0, NULL, 0);
  settings->hddIsLBA48 = lba48 > 0;
  DPRINTF("OPL: %s, %d fragments, device %u, LBA48 %u\n", target->id,
          fragments, target->device->index, settings->hddIsLBA48);
  return 0;
}

// OPL's IOPRP replacement format is a ROMDIR header followed by aligned files.
static int patchIoprp(unsigned char **result, size_t *resultSize) {
  const OplPayload *base = &payloads[PAYLOAD_IOPRP];
  size_t capacity = base->size + payloads[PAYLOAD_CDVDMAN].size +
                    payloads[PAYLOAD_CDVDFSV].size +
                    payloads[PAYLOAD_EESYNC].size + 256;
  unsigned char *out = calloc(1, capacity);
  if (out == NULL)
    return -ENOMEM;
  size_t srcOffset = 0, dstOffset = 0;
  const OplRomdirEntry *source = (const OplRomdirEntry *)base->data;
  OplRomdirEntry *dest = (OplRomdirEntry *)out;
  size_t maxEntries = base->size / sizeof(*source);
  int terminated = 0;
  for (size_t i = 0; i < maxEntries; i++) {
    if (!source[i].name[0]) {
      terminated = 1;
      break;
    }
    const unsigned char *replacement = base->data +
                                       (srcOffset < base->size ? srcOffset : base->size);
    size_t size = source[i].fileSize;
    int isReplacement = 0;
    if (!memcmp(source[i].name, "CDVDMAN", 8)) {
      replacement = payloads[PAYLOAD_CDVDMAN].data;
      size = payloads[PAYLOAD_CDVDMAN].size;
      isReplacement = 1;
    } else if (!memcmp(source[i].name, "CDVDFSV", 8)) {
      replacement = payloads[PAYLOAD_CDVDFSV].data;
      size = payloads[PAYLOAD_CDVDFSV].size;
      isReplacement = 1;
    } else if (!memcmp(source[i].name, "EESYNC", 7)) {
      replacement = payloads[PAYLOAD_EESYNC].data;
      size = payloads[PAYLOAD_EESYNC].size;
      isReplacement = 1;
    }
    // OPL's 244-byte IOPRP template declares 248 bytes of ROMDIR/EXTINFO.
    // Its final four padding bytes are implicit zeroes in the embedded image.
    if (srcOffset > base->size + 15 ||
        srcOffset + source[i].fileSize > base->size + 15 ||
        dstOffset + size > capacity || size > UINT32_MAX) {
      free(out);
      return -EINVAL;
    }
    size_t copySize = isReplacement ? size : source[i].fileSize;
    if (!isReplacement && copySize > (srcOffset < base->size ? base->size - srcOffset : 0))
      copySize = srcOffset < base->size ? base->size - srcOffset : 0;
    memcpy(out + dstOffset, replacement, copySize);
    dest[i] = source[i];
    dest[i].fileSize = size;
    srcOffset += (source[i].fileSize + 15) & ~15u;
    dstOffset += (size + 15) & ~15u;
  }
  if (!terminated) {
    free(out);
    return -EINVAL;
  }
  *result = out;
  *resultSize = dstOffset;
  return 0;
}

static OplEeCoreConfig *findCoreConfig(void) {
  OplPayload *core = &payloads[PAYLOAD_CORE];
  for (size_t i = 0; i + sizeof(OplEeCoreConfig) <= core->size; i += 4) {
    uint32_t *magic = (uint32_t *)(core->data + i);
    if (magic[0] == OPL_ATA_CORE_MAGIC_0 && magic[1] == OPL_ATA_CORE_MAGIC_1)
      return (OplEeCoreConfig *)(core->data + i);
  }
  return NULL;
}

static int validateCore(void) {
  OplPayload *core = &payloads[PAYLOAD_CORE];
  if (core->size < sizeof(OplElfHeader))
    return -EINVAL;
  OplElfHeader *elf = (OplElfHeader *)core->data;
  if (memcmp(elf->ident, "\177ELF", 4) || elf->ident[4] != 1 ||
      elf->phentsize != sizeof(OplElfProgram) ||
      elf->phnum == 0 || elf->phnum > 16 ||
      elf->phoff + elf->phnum * sizeof(OplElfProgram) > core->size ||
      elf->entry < OPL_CORE_START || elf->entry >= OPL_CORE_LIMIT)
    return -EINVAL;
  OplElfProgram *segments = (OplElfProgram *)(core->data + elf->phoff);
  for (int i = 0; i < elf->phnum; i++) {
    if (segments[i].type != 1)
      continue;
    if (segments[i].offset + segments[i].filesz > core->size ||
        segments[i].memsz < segments[i].filesz ||
        segments[i].vaddr < OPL_CORE_START ||
        segments[i].vaddr + segments[i].memsz > OPL_CORE_LIMIT)
      return -EINVAL;
  }
  return findCoreConfig() != NULL ? 0 : -EINVAL;
}

// These two kernel patterns and replacements are from OPL src/system.c.
static int patchKernel(void *entry, void *storageEnd, void **eeloadCopy,
                       void **initUserMemory) {
  static const uint32_t eeloadPattern[] = {
      0x8FA30010, 0x0240302D, 0x8FA50014, 0x8C67000C, 0x18E00009};
  uint32_t *eeload = NULL, *memory = NULL;
  DI();
  ee_kmode_enter();
  for (uint32_t *p = (uint32_t *)0x80001000; p < (uint32_t *)0x80030000; p++) {
    if (!eeload && !memcmp(p, eeloadPattern, sizeof(eeloadPattern)))
      eeload = p;
    if (!memory && p[0] == 0x3c040008 &&
        (p[1] & 0xfc000000) == 0x0c000000 && p[2] == 0x34842000)
      memory = p;
  }
  if (eeload && memory) {
    eeload[1] = 0x3C120000 | ((uint32_t)entry >> 16);
    eeload[2] = 0x36520000 | ((uint32_t)entry & 0xffff);
    eeload[3] = 0x24070000;
    memory[0] = 0x3c040000 | ((uint32_t)storageEnd >> 16);
    memory[2] = 0x34840000 | ((uint32_t)storageEnd & 0xffff);
  }
  ee_kmode_exit();
  EI();
  *eeloadCopy = eeload;
  *initUserMemory = memory;
  return eeload && memory ? 0 : -ENOTSUP;
}

int launchOplAta(Target *target, ArgumentList *arguments,
                 LaunchProgressCallback progress, void *userdata) {
  uint8_t compat = lunaGetOplCompatMask(arguments);
  int result = loadPayloads();
  if (!result)
    result = prepareDisc(target, compat);
  if (!result)
    result = validateCore();
  if (result) {
    DPRINTF("OPL: launch preflight failed: %d\n", result);
    freePayloads();
    return result;
  }
  unsigned char *ioprp = NULL;
  size_t ioprpSize = 0;
  result = patchIoprp(&ioprp, &ioprpSize);
  if (result) {
    freePayloads();
    return result;
  }

  if (progress)
    progress(LAUNCH_STAGE_SYNCING, userdata);
  if (updateLastLaunchedTitle(target->device, target->fullPath))
    DPRINTF("OPL: failed to save last launched title\n");
  if (target->device->sync)
    target->device->sync();

  OplModuleTable *table = (OplModuleTable *)OPL_STORAGE_START;
  OplModule *modules = (OplModule *)(OPL_STORAGE_START + sizeof(*table));
  const int ids[] = {1, 2, 3, 4};
  const unsigned char *data[] = {payloads[PAYLOAD_UDNL].data, ioprp,
                                 payloads[PAYLOAD_IMGDRV].data,
                                 payloads[PAYLOAD_RESETSPU].data};
  const size_t sizes[] = {payloads[PAYLOAD_UDNL].size, ioprpSize,
                          payloads[PAYLOAD_IMGDRV].size,
                          payloads[PAYLOAD_RESETSPU].size};
  uintptr_t cursor = ((uintptr_t)(modules + 4) + 15) & ~15u;
  uintptr_t moduleAddresses[4];
  for (int i = 0; i < 4; i++) {
    if (cursor + sizes[i] >= OPL_STORAGE_LIMIT) {
      free(ioprp);
      freePayloads();
      return -EFBIG;
    }
    moduleAddresses[i] = cursor;
    cursor += (sizes[i] + 15) & ~15u;
  }
  void *storageEnd = (void *)((cursor + 63) & ~63u);

  OplElfHeader *elf = (OplElfHeader *)payloads[PAYLOAD_CORE].data;
  OplEeCoreConfig *config = findCoreConfig();
  memset(config, 0, sizeof(*config));
  config->magic[0] = OPL_ATA_CORE_MAGIC_0;
  config->magic[1] = OPL_ATA_CORE_MAGIC_1;
  strcpy(config->GameModeDesc, "BDM_ATA_MODE");
  Argument *debug = getArgument(arguments, "dbc");
  config->EnableDebug = debug != NULL && !debug->isDisabled;
  // The HDD return target uses the console's browser boot chain, which
  // routes the held START button back into LUNA. Other configured targets
  // are launcher ELFs that OPL can load directly after IGR.
  const char *returnPath = LAUNCHER_OPTIONS.returnPath;
  if (returnPath[0] == '\0' || strcmp(returnPath, "hdd") == 0)
    strcpy(config->ExitPath, "Browser");
  else if (strlen(returnPath) < sizeof(config->ExitPath))
    strcpy(config->ExitPath, returnPath);
  else {
    DPRINTF("OPL: return path exceeds core limit\n");
    free(ioprp);
    freePayloads();
    return -ENAMETOOLONG;
  }
  DPRINTF("OPL: IGR return target %s\n", config->ExitPath);
  strlcpy(config->GameID, target->id, sizeof(config->GameID));
  // Leave controller IGR configurable, but always hand a physical power press
  // to CDVDMAN's IOP shutdown thread, even before a game opens its pad.
  config->_CompatMask = compat | OPL_COMPAT_IOP_POFF;
  config->ModStorageStart = table;
  config->ModStorageEnd = storageEnd;
  GetOsdConfigParam(&config->CustomOSDConfigParam);

  result = patchKernel((void *)elf->entry, storageEnd,
                       &config->eeloadCopy, &config->initUserMemory);
  if (result) {
    DPRINTF("OPL: unsupported kernel; handoff cancelled\n");
    free(ioprp);
    freePayloads();
    return result;
  }

  // Commit the handoff only after all checks that can fall back to Neutrino.
  // OPL clears this range before installing its EE core and IOP modules.
  ambientStop();
  memset((void *)OPL_CORE_START, 0, OPL_CLEAR_END - OPL_CORE_START);
  for (int i = 0; i < 4; i++) {
    memcpy((void *)moduleAddresses[i], data[i], sizes[i]);
    modules[i].ptr = (void *)moduleAddresses[i];
    modules[i].info = OPL_MODULE_INFO(ids[i], sizes[i]);
  }
  table->modules = modules;
  table->count = 4;
  free(ioprp);

  OplElfProgram *segments = (OplElfProgram *)(payloads[PAYLOAD_CORE].data + elf->phoff);
  for (int i = 0; i < elf->phnum; i++) {
    if (segments[i].type != 1)
      continue;
    memcpy((void *)segments[i].vaddr,
           payloads[PAYLOAD_CORE].data + segments[i].offset,
           segments[i].filesz);
    if (segments[i].memsz > segments[i].filesz)
      memset((void *)(segments[i].vaddr + segments[i].filesz), 0,
             segments[i].memsz - segments[i].filesz);
  }
  char bootPath[32];
  snprintf(bootPath, sizeof(bootPath), "cdrom0:\\%s;1", target->id);
  char *argv[] = {bootPath};
  if (progress)
    progress(LAUNCH_STAGE_STARTING, userdata);
  DPRINTF("OPL: starting EE core at %08x with %s\n", elf->entry, bootPath);
  FlushCache(WRITEBACK_DCACHE);
  FlushCache(INVALIDATE_ICACHE);
  fileXioExit();
  SifExitRpc();
  ExecPS2((void *)elf->entry, NULL, 1, argv);
  DPRINTF("OPL: ExecPS2 returned after handoff\n");
  __builtin_trap();
}
