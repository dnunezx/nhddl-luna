#include "igr_return.h"
#include "homebrew_elf.h"
#include "options.h"
#include <stdio.h>
#include <strings.h>
#include <string.h>

static char configPath[PATH_MAX + 1];
static char customPath[LUNA_IGR_PATH_SIZE];
static int commandLineOverride;

int lunaIgrDeviceSupported(const char *path) {
#ifdef LUNA_EMULATOR_BUILD
  if (!strncmp(path, "host:/", 6)) return 1;
#endif
  return !strncmp(path, "mc0:/", 5) || !strncmp(path, "mc1:/", 5);
}

static int customPathValid(const char *path) {
  size_t length = strlen(path);
  if (length < 10 || length >= LUNA_IGR_PATH_SIZE ||
      !lunaIgrDeviceSupported(path) || strcasecmp(path + length - 4, ".elf"))
    return 0;
  for (const unsigned char *p = (const unsigned char *)strchr(path, ':') + 2; *p; p++)
    if (*p < 32 || *p == 127 || *p == ':' || *p == '\\') return 0;
  return 1;
}

static int readPath(const char *filePath, char *path) {
  FILE *file = fopen(filePath, "rb");
  if (!file) return 0;
  char line[LUNA_IGR_PATH_SIZE + 1];
  size_t count = fread(line, 1, sizeof(line), file);
  int valid = count > 0 && count < sizeof(line) && line[count - 1] == '\n' &&
              !memchr(line, '\0', count) && !ferror(file);
  if (fclose(file)) valid = 0;
  if (!valid) return 0;
  line[count - 1] = '\0';
  if (*line && !customPathValid(line)) return 0;
  strcpy(path, line);
  return 1;
}

void lunaIgrConfigure(const char *directory, int explicitPath) {
  commandLineOverride = explicitPath;
  *configPath = *customPath = '\0';
  if (!directory || !*directory) return;
  int length = snprintf(configPath, sizeof(configPath), "%s%sigr.cfg", directory,
      directory[strlen(directory) - 1] == '/' ? "" : "/");
  if (length < 0 || (size_t)length >= sizeof(configPath)) {
    *configPath = '\0';
    return;
  }
  char backup[PATH_MAX + 5];
  snprintf(backup, sizeof(backup), "%s.bak", configPath);
  if (!readPath(backup, customPath))
    readPath(configPath, customPath);
}

const char *lunaIgrCustomPath(void) {
  return customPath;
}

static int readableElf(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  unsigned char header[LUNA_HOMEBREW_ELF_HEADER_SIZE];
  int valid = fread(header, 1, sizeof(header), file) == sizeof(header) &&
              lunaHomebrewElfHeaderValid(header, sizeof(header));
  if (valid) {
    long size = fseek(file, 0, SEEK_END) ? -1 : ftell(file);
    valid = size >= (long)sizeof(header) && lunaHomebrewElfProgramTableFits(header, (size_t)size);
  }
  if (fclose(file)) valid = 0;
  return valid;
}

const char *lunaIgrReturnPath(void) {
  if (!commandLineOverride && *customPath && readableElf(customPath))
    return customPath;
  return LAUNCHER_OPTIONS.returnPath;
}

static int writePath(const char *filePath, const char *path) {
  FILE *file = fopen(filePath, "wb");
  if (!file) return 0;
  int failed = fprintf(file, "%s\n", path) < 0;
  if (fclose(file)) failed = 1;
  if (failed) remove(filePath);
  return !failed;
}

const char *lunaIgrSavePath(const char *path) {
  const char *saveError = "Could not save the IGR return path.";
  if (commandLineOverride) return "The launch arguments specify the IGR return path.";
  if (*path && !customPathValid(path)) return "Choose an ELF on a supported device (up to 127 bytes).";
  if (*path && !readableElf(path)) return "Not a supported PS2 ELF executable.";
  if (!*configPath) return saveError;

  char backup[PATH_MAX + 5], staged[PATH_MAX + 5], previous[LUNA_IGR_PATH_SIZE];
  snprintf(backup, sizeof(backup), "%s.bak", configPath);
  snprintf(staged, sizeof(staged), "%s.tmp", configPath);
  if (!writePath(staged, path)) return saveError;
  // Keep the last confirmed choice if this device's copy fails midway.
  if ((!readPath(backup, previous) && !writePath(backup, customPath)) ||
      commitConfigFile(staged, configPath) || remove(backup)) {
    remove(staged);
    return saveError;
  }
  strcpy(customPath, path);
  return NULL;
}
