// Exercise source replacement, driver conflicts and real preference persistence.
#include "storage.h"
#include "devices/devices.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

LauncherOptions LAUNCHER_OPTIONS;
struct DeviceMapEntry deviceModeMap[MAX_DEVICES];
static int scans[2], usbFailure, driverFailure, resets, usbPresent = 1;
static int commitFailure;
int commitConfigFile(const char *temp, const char *path) {
  if (commitFailure) {
    FILE *file = fopen(path, "wb");
    assert(file); fputs("partial", file); fclose(file);
    return -5;
  }
  return rename(temp, path);
}
void uiSplashLogString(int level, const char *format, ...) { (void)level; (void)format; }
void syncDeviceMap(void) {}
int parseIPConfig(void) { strcpy(LAUNCHER_OPTIONS.udpfsIp, "192.168.1.20"); return 12; }
ModeType initStorageModules(ModeType modes, int reset) {
  resets += reset != 0;
  LAUNCHER_OPTIONS.mode = modes & ~(driverFailure ? MODE_USB : 0);
  return driverFailure ? MODE_USB : 0;
}
int getRelativePathIdx(char *path) {
  char *colon = strchr(path, ':');
  return colon ? (int)(colon + 1 - path) : -1;
}
void freeDeviceMapEntries(struct DeviceMapEntry *map) {
  for (int i = 0; i < MAX_DEVICES; i++) { free(map[i].mountpoint); memset(&map[i], 0, sizeof(map[i])); }
}
static int scan(TargetList *list, struct DeviceMapEntry *dev) {
  int usb = dev->mode == MODE_USB;
  scans[usb]++;
  if (usb && usbFailure) return -5;
  Target *t = calloc(1, sizeof(*t));
  t->fullPath = strdup(usb ? "mass1:/DVD/USB.iso" : "mass0:/DVD/HDD.iso");
  t->name = strdup(usb ? "USB" : "HDD");
  t->id = strdup(usb ? "SLUS_000.02" : "SLUS_000.01");
  t->device = dev;
  list->first = list->last = t;
  list->total = 1;
  return 0;
}
int initDeviceMap(void) {
  int count = 0;
  for (int i = 0; i < 2; i++) {
    ModeType mode = i ? MODE_USB : MODE_ATA;
    if (!(LAUNCHER_OPTIONS.mode & mode) || (i && !usbPresent)) continue;
    deviceModeMap[count].mode = mode;
    deviceModeMap[count].mountpoint = strdup(i ? "mass1:" : "mass0:");
    deviceModeMap[count].scan = scan;
    count++;
  }
  return count;
}
int main(void) {
  char directory[] = "/tmp/luna-storage-XXXXXX";
  assert(mkdtemp(directory));
  assert(storageValidIP("192.168.1.10"));
  assert(storageValidIP(""));
  assert(!storageValidIP("192.168.1.256"));
  assert(!storageValidIP("192.168.1.1junk"));
  assert(!storageValidIP("224.1.1.1"));
  for (int i = 0; i < STORAGE_SOURCE_COUNT; i++) {
    StorageSettings one = {.enabled = storageSourceMode(i)};
    assert(storageValidSettings(&one));
    assert(storageSourceName(i)[0]);
    struct DeviceMapEntry metadata = {.mountpoint = "pfs0:/OPL"};
    struct DeviceMapEntry device = {.mode = one.enabled, .mountpoint = "mass3:"};
    if (device.mode == MODE_HDL) device.metadev = &metadata;
    STORAGE_SETTINGS = one;
    int local = device.mode != MODE_MMCE && device.mode != MODE_UDPFS;
    assert((storageVMCRoot(&device) != NULL) == local);
    if (local) {
      assert(!strcmp(storageVMCRoot(&device), device.mode == MODE_HDL ?
                     "pfs0:/OPL" : "mass3:"));
      const char *card = device.mode == MODE_HDL ?
                         "pfs0:/OPL/VMC/card.bin" : "mass3:/VMC/card.bin";
      assert(!strcmp(storageVMCRelativePath(&device, card), "/VMC/card.bin"));
    }
    // A loaded runtime driver must not expose a disabled VMC library drive.
    STORAGE_SETTINGS.enabled = MODE_NONE;
    LAUNCHER_OPTIONS.mode = one.enabled | MODE_BASIC;
    assert(storageVMCRoot(&device) == NULL);
  }
  struct DeviceMapEntry missingMetadata = {.mode = MODE_HDL, .mountpoint = "hdd0:"};
  STORAGE_SETTINGS.enabled = MODE_HDL;
  assert(storageVMCRoot(&missingMetadata) == NULL);
  assert(storageVMCRoot(NULL) == NULL);
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 1);
  TargetList *list = storageRefresh(NULL);
  assert(list && list->total == 1 && scans[0] == 1 && scans[1] == 0);
  StorageSettings settings = STORAGE_SETTINGS;
  settings.enabled |= MODE_USB;
  assert(storageSave(&settings) == 0);
  assert(storageRequest(&settings, MODE_USB) == 0);
  list = storageRefresh(list);
  assert(list && list->total == 2 && scans[0] == 1 && scans[1] == 1);
  assert(STORAGE_STATUS[0].games == 1 && STORAGE_STATUS[2].games == 1);
  storageRememberSelection(list->last, 3);
  for (int i = 0; i < 12; i++) {
    assert(storageRequest(&settings, MODE_USB) == 0);
    list = storageRefresh(list);
    assert(list && list->total == 2);
    assert(scans[0] == 1);
  }
  int view = 0;
  assert(storageRestoreSelection(list, &view) == 1 && view == 3);
  usbFailure = 1;
  storageRequest(&settings, MODE_USB);
  list = storageRefresh(list);
  assert(list->total == 2 && STORAGE_STATUS[2].error == -5);
  usbFailure = 0;
  settings.enabled &= ~MODE_USB;
  storageRequest(&settings, MODE_NONE);
  list = storageRefresh(list);
  assert(list->total == 1 && scans[0] == 1);
  settings.enabled |= MODE_USB;
  driverFailure = 1;
  storageRequest(&settings, MODE_USB);
  list = storageRefresh(list);
  assert(list->total == 1 && STORAGE_STATUS[2].error && STORAGE_STATUS[0].games == 1);
  driverFailure = 0;
  usbPresent = 0;
  storageRequest(&settings, MODE_USB);
  list = storageRefresh(list);
  assert(list->total == 1 && STORAGE_STATUS[2].devices == 0);
  settings.enabled |= MODE_MX4SIO | MODE_MMCE;
  assert(storageRequest(&settings, settings.enabled) < 0);
  settings.enabled &= ~MODE_MMCE;
  assert(storageNeedsRestart(&settings));
  storageRequest(&settings, settings.enabled);
  list = storageRefresh(list);
  assert(resets == 1);
  settings.enabled = MODE_NONE;
  storageRequest(&settings, MODE_NONE);
  list = storageRefresh(list);
  assert(list && list->total == 0);
  settings.enabled = MODE_ATA;
  storageRequest(&settings, MODE_ATA);
  list = storageRefresh(list);
  assert(list && list->total == 1);
  freeTargetList(list);
  freeDeviceMapEntries(deviceModeMap);
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 1);
  assert(STORAGE_SETTINGS.enabled == (MODE_ATA | MODE_USB));
  // Explicit startup arguments ignore the saved device mask.
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 0);
  assert(STORAGE_SETTINGS.enabled == MODE_ATA);
  // Startup on MMCE cannot switch its driver to MX4SIO.
  storageConfigure(directory, MODE_MMCE, MODE_MMCE, 0);
  settings.enabled = MODE_MX4SIO;
  assert(storageRequiredConflict(&settings));
  char path[PATH_MAX + 16];
  snprintf(path, sizeof(path), "%s/storage.cfg", directory);
  char backup[PATH_MAX + 16];
  snprintf(backup, sizeof(backup), "%s/storage.cfg.bak", directory);
  assert(rename(path, backup) == 0);
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 1);
  assert(STORAGE_SETTINGS.enabled == (MODE_ATA | MODE_USB));
  assert(rename(backup, path) == 0);
  commitFailure = 1;
  settings = STORAGE_SETTINGS;
  settings.enabled |= MODE_ILINK;
  assert(storageSave(&settings) < 0);
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 1);
  assert(STORAGE_SETTINGS.enabled == (MODE_ATA | MODE_USB));
  commitFailure = 0;
  assert(storageSave(&STORAGE_SETTINGS) == 0);
  FILE *file = fopen(path, "wb"); assert(file);
  fputs("LUNA_STORAGE_1\nenabled=34\nip=192.168.1.10\n", file); fclose(file);
  storageConfigure(directory, MODE_ATA, MODE_BASIC, 1);
  assert(STORAGE_SETTINGS.enabled == MODE_ATA); // Conflicting saved mask rejected.
  remove(path);
  rmdir(directory);
  puts("storage lifecycle and persistence tests passed");
  return 0;
}
