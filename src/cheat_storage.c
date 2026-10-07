#include "cheat_storage.h"
#include "ui/language.h"
#include "devices/devices.h"
#include "options.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *metadataRoot(Target *target) {
  if (!target || !target->device) return NULL;
  struct DeviceMapEntry *device = target->device;
  if (device->metadev) device = device->metadev;
  return device->mountpoint && device->mountpoint[0] ? device->mountpoint : NULL;
}

static int validID(const char *id) {
  if (!id || strlen(id) != 11 || id[4] != '_' || id[8] != '.') return 0;
  for (int i = 0; i < 4; i++) if (!isalpha((unsigned char)id[i])) return 0;
  for (int i = 5; i < 11; i++) if (i != 8 && !isdigit((unsigned char)id[i])) return 0;
  return 1;
}

static int settingsPath(Target *target, char *path, size_t size) {
  const char *root = metadataRoot(target);
  if (!root || !validID(target->id)) return -EINVAL;
  char filename[64];
  snprintf(filename, sizeof(filename), "cheats-%s.cfg", target->id);
  for (char *p = filename + 7; *p && *p != '.'; p++) *p = (char)toupper((unsigned char)*p);
  return buildConfigFilePath(path, size, root, filename);
}

static int missingSettingsFile(const char *root, const char *path) {
  // PS2 file drivers do not consistently translate missing fopen paths into
  // POSIX ENOENT. Check directory entries instead of trusting a stale errno.
  char directory[PATH_MAX + 1];
  snprintf(directory, sizeof(directory), "%s", path);
  char *slash = strrchr(directory, '/');
  if (!slash) return -EIO;
  *slash = 0;
  const char *filename = strrchr(path, '/') + 1;
  DIR *dir = opendir(directory);
  if (dir) {
    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
      if (!strcasecmp(entry->d_name, filename)) { found = 1; break; }
    closedir(dir);
    return found ? -EIO : -ENOENT;
  }
  // The entire /LUNA directory may be absent on a first-time installation.
  char rootPath[PATH_MAX + 1];
  int n = snprintf(rootPath, sizeof(rootPath), "%s%s", root,
                   root[strlen(root) - 1] == '/' ? "" : "/");
  if (n < 0 || (size_t)n >= sizeof(rootPath)) return -ENAMETOOLONG;
  dir = opendir(rootPath);
  if (!dir) return -EIO;
  const char *folder = strrchr(directory, '/');
  folder = folder ? folder + 1 : directory;
  int found = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL)
    if (!strcasecmp(entry->d_name, folder)) { found = 1; break; }
  closedir(dir);
  return found ? -EIO : -ENOENT;
}

static int readSettingsPath(const char *root, const char *path,
                             LunaCheatSettings *settings) {
  FILE *stream = fopen(path, "rb");
  if (!stream) return missingSettingsFile(root, path);
  int result = lunaCheatSettingsRead(stream, settings);
  fclose(stream);
  return result;
}

int lunaCheatsLoad(Target *target, LunaCheatFile *file, char *path, size_t pathSize,
                   char *error, size_t size) {
  memset(file, 0, sizeof(*file));
  const char *root = metadataRoot(target);
  if (!root || !validID(target->id)) {
    snprintf(error, size, lunaText("Cheats require a valid game title ID and metadata drive"));
    return -EINVAL;
  }
  char folder[PATH_MAX + 1], filename[32];
  snprintf(filename, sizeof(filename), "%s.cht", target->id);
  int n = snprintf(folder, sizeof(folder), "%s%sCHT", root,
                   root[strlen(root) - 1] == '/' ? "" : "/");
  if (n < 0 || (size_t)n >= sizeof(folder)) return -ENAMETOOLONG;
  n = snprintf(path, pathSize, "%s/%s", folder, filename);
  if (n < 0 || (size_t)n >= pathSize) return -ENAMETOOLONG;
  DIR *dir = opendir(folder);
  if (!dir) {
    snprintf(error, size, lunaText("No cheat file found: CHT/%s"), filename);
    return -ENOENT;
  }
  char match[32] = "";
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcasecmp(entry->d_name, filename)) continue;
    if (match[0]) {
      closedir(dir);
      snprintf(error, size, lunaText("Multiple .cht files match this title ID"));
      return -EINVAL;
    }
    strcpy(match, entry->d_name); // Matching filename has the same bounded length.
  }
  closedir(dir);
  if (!match[0]) {
    snprintf(error, size, lunaText("No cheat file found: CHT/%s"), filename);
    return -ENOENT;
  }
  snprintf(path, pathSize, "%s/%s", folder, match);
  FILE *stream = fopen(path, "rb");
  if (!stream) {
    snprintf(error, size, lunaText("Could not read CHT/%s"), match);
    return -EIO;
  }
  int result = lunaCheatParse(stream, file, error, size);
  fclose(stream);
  return result;
}

int lunaCheatsLoadSettings(Target *target, LunaCheatSettings *settings) {
  memset(settings, 0, sizeof(*settings));
  // Nonstandard homebrew IDs have no cheat settings; their launches are unchanged.
  if (!target || !metadataRoot(target) || !validID(target->id)) return 0;
  char path[PATH_MAX + 1];
  int result = settingsPath(target, path, sizeof(path));
  if (result) return result;
  const char *root = metadataRoot(target);
  result = readSettingsPath(root, path, settings);
  if (!result) return 0;
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", path);
  int fallback = readSettingsPath(root, backup, settings);
  if (!fallback) return 0;
  return result == -ENOENT && fallback == -ENOENT ? 0 : result;
}

int lunaCheatsSaveSettings(Target *target, const LunaCheatSettings *settings) {
  char path[PATH_MAX + 1], directory[PATH_MAX + 1], temp[PATH_MAX + 8];
  int result = settingsPath(target, path, sizeof(path));
  if (result) return result;
  result = buildConfigFilePath(directory, sizeof(directory), metadataRoot(target), NULL);
  if (result) return result;
  struct stat st;
  if (stat(directory, &st) && mkdir(directory, 0777)) return -EIO;
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  FILE *stream = fopen(temp, "wb");
  if (!stream) return -EIO;
  result = lunaCheatSettingsWrite(stream, settings);
  if (fclose(stream)) result = -EIO;
  if (result) { remove(temp); return result; }
  // Recover the previous complete selection file if replace/copy is interrupted.
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", path);
  LunaCheatSettings previous = {0};
  lunaCheatsLoadSettings(target, &previous);
  stream = fopen(backup, "wb");
  if (!stream) { remove(temp); return -EIO; }
  result = lunaCheatSettingsWrite(stream, &previous);
  if (fclose(stream)) result = -EIO;
  if (result) { remove(temp); return result; }
  result = commitConfigFile(temp, path);
  if (!result) remove(backup);
  return result;
}

int lunaCheatsPrepare(Target *target, const LunaCheatSettings *settings,
                      char **payload, char *error, size_t size) {
  *payload = NULL;
  error[0] = 0;
  LunaCheatSettings saved;
  if (!settings) {
    int result = lunaCheatsLoadSettings(target, &saved);
    if (result) {
      snprintf(error, size, lunaText("Could not read cheat settings. Open Cheats and save again."));
      return result;
    }
    settings = &saved;
  }
  // No .cht discovery, allocation or engine payload on this path.
  if (!settings->enabled || !settings->count) return 0;
  LunaCheatFile file;
  char path[PATH_MAX + 1];
  int result = lunaCheatsLoad(target, &file, path, sizeof(path), error, size);
  if (result) return result;
  result = lunaCheatPayload(&file, settings, payload, error, size);
  lunaCheatFileFree(&file);
  return result;
}
