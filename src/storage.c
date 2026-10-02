// LUNA 2026: all supported storage sources share one refresh lifecycle.
#include "storage.h"
#include "genres.h"
#include "devices/devices.h"
#include "devices/init.h"
#include "options.h"
#include "ui/ui.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

StorageSettings STORAGE_SETTINGS;
StorageStatus STORAGE_STATUS[STORAGE_SOURCE_COUNT];
static const ModeType modes[] = {MODE_ATA, MODE_HDL, MODE_USB, MODE_MX4SIO,
                                MODE_MMCE, MODE_ILINK, MODE_UDPFS};
static const char *names[] = {"Internal HDD (exFAT)", "Internal HDD (APA / HDL)",
    "USB Storage", "MX4SIO", "MMCE", "iLink Storage", "UDPFS Network Storage"};
static char settingsPath[PATH_MAX + 1];
static ModeType requiredModes;
static StorageSettings pending;
static ModeType pendingScan;
static int requestPending;
static char selectedPath[PATH_MAX + 1];
static ModeType selectedMode;
static int selectedDevice, selectedView, restoreSelection;

ModeType storageSourceMode(int index) { return modes[index]; }
const char *storageSourceName(int index) { return names[index]; }

const char *storageVMCRoot(const struct DeviceMapEntry *device) {
  if (!device || !(STORAGE_SETTINGS.enabled & device->mode)) return NULL;
  switch (device->mode) {
  case MODE_ATA: case MODE_USB: case MODE_MX4SIO: case MODE_ILINK:
    return device->mountpoint;
  case MODE_HDL:
    return device->metadev ? device->metadev->mountpoint : NULL;
  default:
    return NULL; // MMCE switches cards in hardware; UDPFS has no file VMC UI.
  }
}

const char *storageVMCRelativePath(const struct DeviceMapEntry *device,
                                 const char *path) {
  if (!device || !path) return path;
  if (device->metadev) device = device->metadev;
  if (!device->mountpoint) return path;
  size_t length = strlen(device->mountpoint);
  if (length && device->mountpoint[length - 1] == '/') length--;
  if (!strncmp(path, device->mountpoint, length) &&
      (path[length] == '/' || path[length] == '\\')) return path + length;
  return path;
}

void storageProtectRuntime(const char *path) {
  // A runtime found on a library drive must remain readable after driver resets.
  if (!strncmp(path, "pfs", 3)) requiredModes |= MODE_HDL;
  else if (!strncmp(path, "mmce", 4)) requiredModes |= MODE_MMCE;
  else for (int i = 0; i < MAX_DEVICES; i++) {
    const char *mount = deviceModeMap[i].mountpoint;
    if (mount && !strncmp(path, mount, strlen(mount))) {
      requiredModes |= deviceModeMap[i].mode;
      break;
    }
  }
}

int storageValidIP(const char *ip) {
  unsigned a, b, c, d;
  char extra;
  if (ip[0] == '\0') return 1; // Use memory-card IPCONFIG.DAT.
  for (const char *p = ip; *p; p++)
    if ((*p < '0' || *p > '9') && *p != '.') return 0;
  return sscanf(ip, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) == 4 &&
         a <= 255 && b <= 255 && c <= 255 && d <= 255 &&
         a != 0 && a < 224 && !(a == 255 && b == 255 && c == 255 && d == 255);
}

int storageValidSettings(const StorageSettings *settings) {
  return !(settings->enabled & ~STORAGE_SUPPORTED) &&
      !((settings->enabled & MODE_MX4SIO) && (settings->enabled & MODE_MMCE)) &&
      storageValidIP(settings->ip);
}

int storageRequiredConflict(const StorageSettings *settings) {
  return ((settings->enabled & MODE_MX4SIO) && (requiredModes & MODE_MMCE)) ||
         ((settings->enabled & MODE_MMCE) && (requiredModes & MODE_MX4SIO));
}

static int readSettings(const char *path, StorageSettings *saved) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  memset(saved, 0, sizeof(*saved));
  char line[96];
  unsigned mask;
  int version = 0, haveMask = 0;
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = '\0';
    if (!strcmp(line, "LUNA_STORAGE_1")) version = 1;
    else if (sscanf(line, "enabled=%u", &mask) == 1) {
      saved->enabled = mask;
      haveMask = 1;
    } else if (!strncmp(line, "ip=", 3)) {
      if (strlen(line + 3) >= sizeof(saved->ip)) { version = 0; break; }
      strcpy(saved->ip, line + 3);
    }
  }
  int bad = ferror(file);
  fclose(file);
  return !bad && version && haveMask && storageValidSettings(saved) &&
         !storageRequiredConflict(saved);
}

static int writeSettings(const char *path, const StorageSettings *settings) {
  FILE *file = fopen(path, "wb");
  if (!file) return -EIO;
  int bad = fprintf(file, "LUNA_STORAGE_1\nenabled=%u\nip=%s\n",
                    (unsigned)settings->enabled, settings->ip) < 0;
  if (fflush(file)) bad = 1;
  if (fclose(file)) bad = 1;
  return bad ? -EIO : 0;
}

void storageConfigure(const char *directory, ModeType defaults, ModeType required,
                      int useSaved) {
  requiredModes = required;
  STORAGE_SETTINGS.enabled = defaults & STORAGE_SUPPORTED;
  snprintf(STORAGE_SETTINGS.ip, sizeof(STORAGE_SETTINGS.ip), "%s", LAUNCHER_OPTIONS.udpfsIp);
  if ((STORAGE_SETTINGS.enabled & MODE_MX4SIO) && (STORAGE_SETTINGS.enabled & MODE_MMCE))
    STORAGE_SETTINGS.enabled &= ~MODE_MMCE;
  if (required & MODE_MX4SIO) STORAGE_SETTINGS.enabled &= ~MODE_MMCE;
  if (required & MODE_MMCE) STORAGE_SETTINGS.enabled &= ~MODE_MX4SIO;
  settingsPath[0] = '\0';
  if (directory && directory[0]) {
    int n = snprintf(settingsPath, sizeof(settingsPath), "%s%sstorage.cfg", directory,
                     directory[strlen(directory) - 1] == '/' ? "" : "/");
    if (n < 0 || (size_t)n >= sizeof(settingsPath)) settingsPath[0] = '\0';
  }
  if (!useSaved || !settingsPath[0]) return;
  StorageSettings saved = {0};
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", settingsPath);
  if (readSettings(settingsPath, &saved) || readSettings(backup, &saved))
    STORAGE_SETTINGS = saved;
  snprintf(LAUNCHER_OPTIONS.udpfsIp, sizeof(LAUNCHER_OPTIONS.udpfsIp), "%s", STORAGE_SETTINGS.ip);
}

int storageSave(const StorageSettings *settings) {
  if (!storageValidSettings(settings) || storageRequiredConflict(settings)) return -EINVAL;
  if (!settingsPath[0]) return -EROFS;
  char temp[PATH_MAX + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", settingsPath);
  if (writeSettings(temp, settings)) { remove(temp); return -EIO; }
  // Keep the previous complete file recoverable until installation succeeds.
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", settingsPath);
  if (writeSettings(backup, &STORAGE_SETTINGS)) { remove(temp); return -EIO; }
  // Existing helper supports drivers (including MC) without replace-by-rename.
  int result = commitConfigFile(temp, settingsPath);
  if (result) return result; // Keep the backup for missing or partial primaries.
  remove(backup);
  return 0;
}

int storageNeedsRestart(const StorageSettings *settings) {
  return ((settings->enabled ^ STORAGE_SETTINGS.enabled) & MODE_MX4SIO) ||
         strcmp(settings->ip, STORAGE_SETTINGS.ip);
}

int storageRequest(const StorageSettings *settings, ModeType scan) {
  if (!storageValidSettings(settings) || storageRequiredConflict(settings)) return -EINVAL;
  pending = *settings;
  pendingScan = scan & settings->enabled;
  pendingScan |= settings->enabled & ~STORAGE_SETTINGS.enabled;
  requestPending = 1;
  return 0;
}

static struct DeviceMapEntry *matchDevice(struct DeviceMapEntry *old) {
  for (int i = 0; i < MAX_DEVICES; i++)
    if (deviceModeMap[i].mountpoint && deviceModeMap[i].mode == old->mode &&
        deviceModeMap[i].index == old->index &&
        !strcmp(deviceModeMap[i].mountpoint, old->mountpoint)) return &deviceModeMap[i];
  for (int i = 0; i < MAX_DEVICES; i++)
    if (deviceModeMap[i].mountpoint && deviceModeMap[i].mode == old->mode &&
        deviceModeMap[i].index == old->index &&
        !strncmp(deviceModeMap[i].mountpoint, "mass", 4) &&
        !strncmp(old->mountpoint, "mass", 4)) return &deviceModeMap[i];
  return NULL;
}

static void mergeTitles(TargetList *destination, TargetList *source) {
  Target *title = source->first;
  while (title) {
    Target *next = title->next;
    title->prev = title->next = NULL;
    destination->total++;
    if (!destination->first) destination->first = destination->last = title;
    else insertIntoTargetList(destination, title);
    title = next;
  }
  free(source);
}

TargetList *storageRefresh(TargetList *previous) {
  TargetList *next = calloc(1, sizeof(*next));
  if (!next) return NULL;
  StorageSettings desired = requestPending ? pending : STORAGE_SETTINGS;
  ModeType scan = requestPending ? pendingScan : desired.enabled;
  int restart = requestPending && storageNeedsRestart(&desired);
  if (restart) scan = desired.enabled;
  struct DeviceMapEntry oldMap[MAX_DEVICES];
  syncDeviceMap();
  memcpy(oldMap, deviceModeMap, sizeof(oldMap));
  // The old map owns its allocations until the replacement is ready.
  if (previous)
    for (Target *t = previous->first; t; t = t->next)
      t->device = &oldMap[t->device - deviceModeMap];
  memset(deviceModeMap, 0, sizeof(oldMap));
  snprintf(LAUNCHER_OPTIONS.udpfsIp, sizeof(LAUNCHER_OPTIONS.udpfsIp), "%s", desired.ip);
  if ((desired.enabled & MODE_UDPFS) && !desired.ip[0]) parseIPConfig();
  ModeType wanted = desired.enabled | requiredModes | MODE_BASIC;
  StorageStatus previousStatus[STORAGE_SOURCE_COUNT];
  memcpy(previousStatus, STORAGE_STATUS, sizeof(previousStatus));
  ModeType failed = initStorageModules(wanted, restart);
  ModeType loaded = LAUNCHER_OPTIONS.mode;
  // Disabled library sources must not incur device readiness probes.
  LAUNCHER_OPTIONS.mode = wanted & ~failed;
  initDeviceMap();
  LAUNCHER_OPTIONS.mode = loaded;
  memset(STORAGE_STATUS, 0, sizeof(STORAGE_STATUS));
  for (int s = 0; s < STORAGE_SOURCE_COUNT; s++) {
    ModeType mode = modes[s];
    if (!(desired.enabled & mode)) continue;
    STORAGE_STATUS[s].error = (failed & mode) ? -EIO : 0;
    if (!(scan & mode) && !STORAGE_STATUS[s].error)
      STORAGE_STATUS[s].error = previousStatus[s].error;
    for (int i = 0; i < MAX_DEVICES; i++) {
      struct DeviceMapEntry *dev = &deviceModeMap[i];
      if (!dev->mountpoint || dev->mode != mode || !dev->scan) continue;
      STORAGE_STATUS[s].devices++;
      TargetList *part = calloc(1, sizeof(*part));
      if (!part) goto fail;
      int result = 0;
      if (scan & mode) {
        uiSplashLogString(LEVEL_INFO_NODELAY, "Scanning %s...\n", names[s]);
        result = dev->scan(part, dev);
      }
      if (!(scan & mode) || (result && result != -ENOENT)) {
        if (result) {
          freeTargetList(part);
          part = calloc(1, sizeof(*part));
          if (!part) goto fail;
          STORAGE_STATUS[s].error = result;
        }
        if (previous) for (Target *t = previous->first; t; t = t->next) {
          if (matchDevice(t->device) != dev) continue;
          Target *copy = copyTarget(t);
          if (!copy) { freeTargetList(part); goto fail; }
          copy->device = dev;
          int relative = getRelativePathIdx(t->fullPath);
          if (relative >= 0 && strcmp(t->device->mountpoint, dev->mountpoint)) {
            char path[PATH_MAX + 1];
            snprintf(path, sizeof(path), "%s%s", dev->mountpoint, t->fullPath + relative);
            free(copy->fullPath);
            copy->fullPath = strdup(path);
            if (!copy->fullPath) {
              free(copy->name); free(copy->id); free(copy);
              freeTargetList(part); goto fail;
            }
          }
          part->total++;
          if (!part->first) part->first = part->last = copy;
          else insertIntoTargetList(part, copy);
        }
      }
      STORAGE_STATUS[s].games += part->total;
      mergeTitles(next, part);
    }
  }
  int index = 0;
  for (Target *t = next->first; t; t = t->next) t->idx = index++;
  // Read once per refresh, including retained sources whose CFG changed on PC.
  // Metadata failure must never prevent games from being launched.
  lunaLoadLibraryGenres(next);
  STORAGE_SETTINGS = desired;
  requestPending = 0;
  if (previous) freeTargetList(previous);
  freeDeviceMapEntries(oldMap);
  return next;
fail:
  freeTargetList(next);
  freeDeviceMapEntries(deviceModeMap);
  memcpy(deviceModeMap, oldMap, sizeof(oldMap));
  if (previous) for (Target *t = previous->first; t; t = t->next)
    t->device = &deviceModeMap[t->device - oldMap];
  requestPending = 0;
  for (int s = 0; s < STORAGE_SOURCE_COUNT; s++) {
    STORAGE_STATUS[s] = previousStatus[s];
    if (desired.enabled & modes[s]) STORAGE_STATUS[s].error = -ENOMEM;
  }
  return NULL;
}

void storageRememberSelection(Target *target, int view) {
  restoreSelection = target != NULL;
  if (!target) return;
  int relative = getRelativePathIdx(target->fullPath);
  snprintf(selectedPath, sizeof(selectedPath), "%s", target->fullPath + (relative < 0 ? 0 : relative));
  selectedMode = target->device->mode;
  selectedDevice = target->device->index;
  selectedView = view;
}

int storageRestoreSelection(TargetList *titles, int *view) {
  if (!restoreSelection) return -1;
  restoreSelection = 0;
  *view = selectedView;
  for (Target *t = titles->first; t; t = t->next) {
    int relative = getRelativePathIdx(t->fullPath);
    if (t->device->mode == selectedMode && t->device->index == selectedDevice &&
        !strcmp(t->fullPath + (relative < 0 ? 0 : relative), selectedPath)) return t->idx;
  }
  return -1;
}
