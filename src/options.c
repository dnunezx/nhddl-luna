// LUNA 2026
#include "options.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/game_options.h"
#include "storage.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libcdvd.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath);
int parseOptionsFile(ArgumentList *result, FILE *file, struct DeviceMapEntry *device);
void appendArgument(ArgumentList *target, Argument *arg);
uint32_t getTimestamp();

const char BASE_CONFIG_PATH[] = "/LUNA";
const size_t BASE_CONFIG_PATH_LEN = sizeof(BASE_CONFIG_PATH) / sizeof(char);
const char LEGACY_BASE_CONFIG_PATH[] = "/nhddl";

const char globalOptionsPath[] = "/global.yaml";
const char lastTitlePath[] = "/lastTitle.bin";
static const char lastTitleTempPath[] = "/lastTitle.bin.tmp";
static const char ps2LogoPath[] = "/ps2Logo.txt";
static const char ps2LogoTempPath[] = "/ps2Logo.txt.tmp";
static const char gameCorePath[] = "/gameCore.txt";
static const char gameCoreTempPath[] = "/gameCore.txt.tmp";

static int buildConfigFilePathAtBase(char *targetPath, size_t targetSize,
                                     const char *targetMountpoint, const char *basePath,
                                     const char *targetFileName) {
  int length = snprintf(targetPath, targetSize, "%s%s%s%s", targetMountpoint, basePath,
                        (targetFileName != NULL && targetFileName[0] != '/') ? "/" : "",
                        targetFileName != NULL ? targetFileName : "");
  if (length < 0 || (size_t)length >= targetSize) {
    if (targetSize > 0)
      targetPath[0] = '\0';
    return -ENAMETOOLONG;
  }
  return 0;
}

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
int buildConfigFilePath(char *targetPath, size_t targetSize, const char *targetMountpoint,
                        const char *targetFileName) {
  return buildConfigFilePathAtBase(targetPath, targetSize, targetMountpoint, BASE_CONFIG_PATH,
                                   targetFileName);
}

int buildLegacyConfigFilePath(char *targetPath, size_t targetSize, const char *targetMountpoint,
                              const char *targetFileName) {
  return buildConfigFilePathAtBase(targetPath, targetSize, targetMountpoint,
                                   LEGACY_BASE_CONFIG_PATH, targetFileName);
}

static int readAll(int fd, void *buffer, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    int result = read(fd, (char *)buffer + offset, length - offset);
    if (result <= 0)
      return -EIO;
    offset += (size_t)result;
  }
  return 0;
}

static int writeAll(int fd, const void *buffer, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    int result = write(fd, (const char *)buffer + offset, length - offset);
    if (result <= 0)
      return -EIO;
    offset += (size_t)result;
  }
  return 0;
}

int commitConfigFile(const char *tempPath, const char *path) {
  if (!strcmp(tempPath, path))
    return -EINVAL;
  // Keep the complete staged record until the destination is closed. On the
  // ATA filesystem, remove/rename has left neither file available after a
  // failed commit. Readers accept the temporary record if a copy is interrupted.
  int source = open(tempPath, O_RDONLY);
  if (source < 0) {
    DPRINTF("ERROR: Could not open staged config (%d): %s\n", errno, tempPath);
    return -EIO;
  }
  int destination = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (destination < 0) {
    close(source);
    DPRINTF("ERROR: Could not open config destination (%d): %s\n", errno, path);
    return -EIO;
  }
  char buffer[256];
  int result = 0;
  int count;
  while ((count = read(source, buffer, sizeof(buffer))) > 0) {
    if (writeAll(destination, buffer, (size_t)count)) {
      result = -EIO;
      break;
    }
  }
  if (count < 0)
    result = -EIO;
  if (close(destination) < 0)
    result = -EIO;
  close(source);
  if (result) {
    DPRINTF("ERROR: Config copy failed: %s (staged record retained)\n", path);
    return result;
  }
  remove(tempPath);
  DPRINTF("Saved config: %s\n", path);
  return 0;
}

static int readLastTitleFile(const char *path, char *titlePath, size_t titlePathSize,
                             uint32_t *timestamp) {
  char candidate[PATH_MAX + 1];
  int fd = open(path, O_RDONLY);
  int fileSize;
  size_t pathLength;
  uint32_t candidateTimestamp;

  if (fd < 0)
    return fd;
  fileSize = lseek(fd, 0, SEEK_END);
  if (fileSize < (int)(sizeof(candidateTimestamp) + 1) ||
      fileSize > (int)(sizeof(candidateTimestamp) + PATH_MAX)) {
    close(fd);
    return -EFBIG;
  }
  pathLength = (size_t)fileSize - sizeof(candidateTimestamp);
  if (pathLength >= titlePathSize || lseek(fd, 0, SEEK_SET) < 0 ||
      readAll(fd, &candidateTimestamp, sizeof(candidateTimestamp)) ||
      readAll(fd, candidate, pathLength)) {
    close(fd);
    return -EIO;
  }
  close(fd);
  candidate[pathLength] = '\0';
  memcpy(titlePath, candidate, pathLength + 1);
  *timestamp = candidateTimestamp;
  return 0;
}

// Gets last launched title path into titlePath
// Searches for the latest file across all mounted BDM devices
int getLastLaunchedTitle(char *titlePath, size_t titlePathSize) {
  DPRINTF("Reading last launched title\n");
  char targetPath[PATH_MAX];
  uint32_t maxTimestamp = 0;
  int found = 0;

  if (titlePath == NULL || titlePathSize == 0)
    return -EINVAL;
  titlePath[0] = '\0';
  for (int i = 0; i < MAX_DEVICES; i++) {
    struct DeviceMapEntry *device;
    const char *paths[] = {lastTitlePath, lastTitleTempPath, lastTitlePath};
    if (deviceModeMap[i].mode == MODE_NONE || deviceModeMap[i].mountpoint == NULL) {
      break;
    }
    device = deviceModeMap[i].metadev ? deviceModeMap[i].metadev : &deviceModeMap[i];

    for (int pathIndex = 0; pathIndex < 3; pathIndex++) {
      char candidate[PATH_MAX + 1];
      uint32_t timestamp;
      int pathResult = (pathIndex < 2)
                           ? buildConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint,
                                                 paths[pathIndex])
                           : buildLegacyConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint,
                                                       paths[pathIndex]);
      if (pathResult || readLastTitleFile(targetPath, candidate, sizeof(candidate), &timestamp))
        continue;
      if (!found || timestamp >= maxTimestamp) {
        strlcpy(titlePath, candidate, titlePathSize);
        maxTimestamp = timestamp;
        found = 1;
      }
    }
  }
  return found ? 0 : -ENOENT;
}

// Writes last launched title path into lastTitle file on title mountpoint
int updateLastLaunchedTitle(struct DeviceMapEntry *device, char *titlePath) {
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  DPRINTF("Writing last launched title as %s\n", titlePath);
  char targetPath[PATH_MAX];
  char tempPath[PATH_MAX];
  if (buildConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint, NULL))
    return -ENAMETOOLONG;

  // Make sure config directory exists
  struct stat st;
  if (stat(targetPath, &st) == -1) {
    DPRINTF("Creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }

  // Append last title file path
  if (buildConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint, lastTitlePath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, lastTitleTempPath))
    return -ENAMETOOLONG;

  // Stage the complete record before replacing the prior known-good file.
  int fd = open(tempPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open last launched title file: %d\n", fd);
    return -ENOENT;
  }

  // Write timestamp
  uint32_t timestamp = getTimestamp();
  if (writeAll(fd, &timestamp, sizeof(timestamp))) {
    DPRINTF("ERROR: Failed to write last launched title timestamp\n");
    close(fd);
    remove(tempPath);
    return -EIO;
  }

  // Write path without the mountpoint
  int mountpointLen = getRelativePathIdx(titlePath);
  if (mountpointLen < 0)
    mountpointLen = 0; // Write path as-is

  size_t writeLen = strlen(titlePath) + 1 - mountpointLen;
  if (writeLen > PATH_MAX || writeAll(fd, titlePath + mountpointLen, writeLen)) {
    DPRINTF("ERROR: Failed to write last launched title\n");
    close(fd);
    remove(tempPath);
    return -EIO;
  }
  close(fd);

  // Readers also accept the temporary path, so interrupted replacement cannot
  // discard the newly completed record even on filesystems without overwrite-rename.
  remove(targetPath);
  if (rename(tempPath, targetPath)) {
    DPRINTF("ERROR: Failed to commit last launched title: %d\n", errno);
    return -EIO;
  }
  return 0;
}

// Generates ArgumentList from global config file located at targetMounpoint (usually ISO full path)
int getGlobalLaunchArguments(ArgumentList *result, struct DeviceMapEntry *device) {
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  char targetPath[PATH_MAX];
  if (buildConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint, globalOptionsPath))
    return -ENAMETOOLONG;
  int ret = loadArgumentList(result, device, targetPath);
  if (ret == -ENOENT &&
      !buildLegacyConfigFilePath(targetPath, sizeof(targetPath), device->mountpoint,
                                 globalOptionsPath))
    ret = loadArgumentList(result, device, targetPath);
  Argument *curArg = result->first;
  while (curArg != NULL) {
    curArg->isGlobal = 1;
    curArg = curArg->next;
  }
  return ret;
}

int loadPS2LogoEnabled(Target *target) {
  if (target == NULL || target->device == NULL)
    return 1;
  struct DeviceMapEntry *device = target->device->metadev ?
                                  target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return 1;
  const char *const paths[] = {ps2LogoTempPath, ps2LogoPath};
  char path[PATH_MAX];
  char value[16];
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL)
      continue;
    int readable = fgets(value, sizeof(value), file) != NULL;
    fclose(file);
    if (!readable)
      continue;
    value[strcspn(value, "\r\n")] = '\0';
    if (!strcmp(value, "off"))
      return 0;
    if (!strcmp(value, "on"))
      return 1;
  }
  return 1;
}

int savePS2LogoEnabled(Target *target, int enabled) {
  if (target == NULL || target->device == NULL)
    return -EINVAL;
  struct DeviceMapEntry *device = target->device->metadev ?
                                  target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -EINVAL;
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, ps2LogoPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint,
                          ps2LogoTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", enabled ? "on" : "off");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  return commitConfigFile(tempPath, path);
}

// -1 means no valid saved choice, so existing global.yaml settings can apply.
static int readGameCorePreference(Target *target) {
  if (target == NULL || target->device == NULL)
    return -1;
  struct DeviceMapEntry *device = target->device->metadev ?
                                  target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -1;
  const char *const paths[] = {gameCoreTempPath, gameCorePath};
  char path[PATH_MAX];
  char value[16];
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL)
      continue;
    int readable = fgets(value, sizeof(value), file) != NULL;
    fclose(file);
    if (!readable)
      continue;
    value[strcspn(value, "\r\n")] = '\0';
    if (!strcmp(value, "opl"))
      return 1;
    if (!strcmp(value, "neutrino"))
      return 0;
  }
  return -1;
}

int loadGameCoreOpl(Target *target) {
  int saved = readGameCorePreference(target);
  if (saved >= 0)
    return saved;
  if (target == NULL || target->device == NULL)
    return 0;
  ArgumentList *global = calloc(1, sizeof(*global));
  if (global == NULL)
    return 0;
  int result = getGlobalLaunchArguments(global, target->device);
  if (result) {
    freeArgumentList(global);
    return 0;
  }
  Argument *core = getArgument(global, "luna_core");
  int opl = core != NULL && !core->isDisabled && core->value != NULL &&
            !strcmp(core->value, "opl");
  freeArgumentList(global);
  return opl;
}

int saveGameCoreOpl(Target *target, int opl) {
  if (target == NULL || target->device == NULL)
    return -EINVAL;
  struct DeviceMapEntry *device = target->device->metadev ?
                                  target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -EINVAL;
  char directory[PATH_MAX], path[PATH_MAX], tempPath[PATH_MAX];
  struct stat st;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, gameCorePath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint,
                          gameCoreTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", opl ? "opl" : "neutrino");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  return commitConfigFile(tempPath, path);
}

// Generates ArgumentList from global and title-specific config file
int getTitleLaunchArguments(ArgumentList *result, Target *target) {
  struct DeviceMapEntry *device = target->device;
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  DPRINTF("Looking for title-specific config for %s (%s)\n", target->name, target->id);
  char directoryPath[PATH_MAX + 1];
  char targetPath[PATH_MAX + 1];
  // Prefer the current config directory, then older installations. Keep the
  // directory in the path: a bare "/<game>.yaml" cannot be loaded from the device.
  for (int base = 0; base < 2; base++) {
    int pathResult = base == 0
                         ? buildConfigFilePath(directoryPath, sizeof(directoryPath), device->mountpoint, NULL)
                         : buildLegacyConfigFilePath(directoryPath, sizeof(directoryPath), device->mountpoint, NULL);
    if (pathResult)
      return pathResult;
    DIR *directory = opendir(directoryPath);
    if (directory == NULL)
      continue;

    // Older configs may use a longer prefix than the displayed ISO name.
    // Prefer the exact filename written by the Save action when both exist.
    char filename[PATH_MAX + 1] = "";
    struct dirent *entry;
    size_t nameLength = strlen(target->name);
    while ((entry = readdir(directory)) != NULL) {
      size_t entryLength = strlen(entry->d_name);
      if (entry->d_type == DT_DIR || entryLength < nameLength + 5 ||
          strncmp(entry->d_name, target->name, nameLength) ||
          strcmp(entry->d_name + entryLength - 5, ".yaml"))
        continue;
      if (filename[0] == '\0' ||
          (entryLength == nameLength + 5 && entry->d_name[nameLength] == '.'))
        strlcpy(filename, entry->d_name, sizeof(filename));
      if (entryLength == nameLength + 5 && entry->d_name[nameLength] == '.')
        break;
    }
    closedir(directory);
    if (filename[0] == '\0')
      continue;
    if (snprintf(targetPath, sizeof(targetPath), "%s/%s", directoryPath, filename) >= sizeof(targetPath))
      return -ENAMETOOLONG;

    DPRINTF("Loading title-specific config from %s\n", targetPath);
    int ret = loadArgumentList(result, device, targetPath);
    if (ret)
      DPRINTF("ERROR: Failed to load argument list: %d\n", ret);
    return ret;
  }
  DPRINTF("Title-specific config not found\n");
  return 0;
}

// Saves title launch arguments to title-specific config file.
// '$' before the argument name is used as 'disabled' flag.
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options) {
  struct DeviceMapEntry *device = target->device;
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  // Build file path
  char lineBuffer[PATH_MAX + 1];
  if (buildConfigFilePath(lineBuffer, sizeof(lineBuffer), device->mountpoint, NULL))
    return -ENAMETOOLONG;
  struct stat st;
  if (stat(lineBuffer, &st) == -1 && mkdir(lineBuffer, 0777))
    return -EIO;
  char filename[PATH_MAX + 1];
  if (snprintf(filename, sizeof(filename), "%s.yaml", target->name) >= sizeof(filename))
    return -ENAMETOOLONG;
  if (buildConfigFilePath(lineBuffer, sizeof(lineBuffer), device->mountpoint, filename))
    return -ENAMETOOLONG;
  DPRINTF("Saving title-specific config to %s\n", lineBuffer);

  // Open file, truncating it
  int fd = open(lineBuffer, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open file\n");
    return -EIO;
  }

  // Write each argument into the file
  lineBuffer[0] = '\0'; // reuse buffer
  int len = 0;
  int ret = 0;
  Argument *tArg = options->first;
  while (tArg != NULL) {
    len = 0;
    // Skip enabled global arguments
    // Write disabled global arguments as disabled empty arguments
    if (!tArg->isGlobal) {
      // Check if arg is a file path and trim mountpoint
      const char *value = tArg->value;
      if (!strcmp(tArg->arg, "mc0") || !strcmp(tArg->arg, "mc1"))
        value = storageVMCRelativePath(target->device, value);
      else {
        len = getRelativePathIdx(tArg->value);
        if (len > 0) value += len;
      }
      len = snprintf(lineBuffer, sizeof(lineBuffer), "%s%s: %s\n", (tArg->isDisabled) ? "$" : "", tArg->arg,
                     value);
    } else if (tArg->isDisabled && strcmp(tArg->arg, "logo")) {
      // The disabled library logo default is not a per-game override.
      len = snprintf(lineBuffer, sizeof(lineBuffer), "$%s:\n", tArg->arg);
    }
    if (len > 0) {
      if ((size_t)len >= sizeof(lineBuffer)) {
        ret = -ENAMETOOLONG;
        goto out;
      }
      if (writeAll(fd, lineBuffer, len)) {
        DPRINTF("ERROR: Failed to write to file\n");
        ret = -EIO;
        goto out;
      }
    }
    tArg = tArg->next;
  }
out:
  if (close(fd) < 0 && !ret)
    ret = -EIO;
  return ret;
}

// Parses options file into ArgumentList
int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath) {
  // Open options file
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open %s\n", filePath);
    return -ENOENT;
  }

  // Initialize ArgumentList
  options->total = 0;
  options->first = NULL;
  options->last = NULL;

  // Parse options file
  if (parseOptionsFile(options, file, device)) {
    fclose(file);
    Argument *argument = options->last;
    while (argument != NULL) {
      Argument *previous = argument->prev;
      free(argument->arg);
      free(argument->value);
      free(argument);
      argument = previous;
    }
    options->first = NULL;
    options->last = NULL;
    options->total = 0;
    return -EIO;
  }

  fclose(file);
  return 0;
}

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
// Adds mountpoint with the deviceNumber to arguments values that start with \ or /
int parseOptionsFile(ArgumentList *result, FILE *file, struct DeviceMapEntry *device) {
  // Our lines will mostly consist of file paths, which aren't likely to exceed 300 characters due to 255 character limit in exFAT path component
  char lineBuffer[PATH_MAX + 1];
  lineBuffer[0] = '\0';
  int isDisabled = 0;
  char *valuePtr = NULL;
  char *argPtr = NULL;

  while (fgets(lineBuffer, PATH_MAX, file)) { // fgets reutrns NULL if EOF or an error occurs
    argPtr = lineBuffer;
    while (isspace((int)*argPtr))
      argPtr++; // Advance argument until the first non-whitespace character

    if (argPtr[0] == '#') // Ignore commented lines
      continue;

    // Find the start of the value
    valuePtr = strchr(lineBuffer, ':');
    if (!valuePtr)
      continue;

    // Terminate the string argPtr points to at the argument name
    *valuePtr = '\0';

    // Trim whitespace and terminate the value
    do {
      valuePtr++;
    } while (isspace((int)*valuePtr));
    valuePtr[strcspn(valuePtr, "#\r\n")] = '\0'; // Terminate the value at the line end or comment token

    // Trim whitespace at the end of argument and value strings
    char *tempPtr = argPtr + strlen(argPtr) - 1;
    while (isspace((int)*tempPtr)) {
      *tempPtr = '\0';
      tempPtr--;
    }
    tempPtr = valuePtr + strlen(valuePtr) - 1;
    while (isspace((int)*tempPtr)) {
      *tempPtr = '\0';
      tempPtr--;
    }

    // Admit only the supported frontend metadata; it is filtered at handoff.
    const char *name = argPtr[0] == '$' ? argPtr + 1 : argPtr;
    if (!strncmp(name, "luna_", 5) &&
        strcmp(name, "luna_neutrino_disable_igr") &&
        strcmp(name, "luna_core") && strcmp(name, "luna_opl_compat") &&
        strcmp(name, "luna_opl_gsm") && strcmp(name, "luna_opl_field_flip"))
      continue;

    char *newValue = NULL;
    if (device && (valuePtr[0] == '/' || valuePtr[0] == '\\')) {
      // Add device mountpoint to argument value if path starts with \ or /
      newValue = calloc(sizeof(char), strlen(valuePtr) + 1 + strlen(device->mountpoint));
      // Replace current mountpoint with device number.
      strcpy(newValue, device->mountpoint);
      strcat(newValue, valuePtr);
    }

    if (argPtr[0] == '$') {
      argPtr++;
      isDisabled = 1;
    } else
      isDisabled = 0;

    Argument *arg = NULL;
    if (newValue) {
      arg = newArgument(argPtr, newValue);
      free(newValue);
    } else
      arg = newArgument(argPtr, valuePtr);

    arg->isDisabled = isDisabled;
    appendArgument(result, arg);
  }
  if (ferror(file) || !feof(file)) {
    DPRINTF("ERROR: Failed to read config file\n");
    return -EIO;
  }

  return 0;
}

// Completely frees Argument and returns pointer to a previous argument in the list
Argument *freeArgument(Argument *arg) {
  Argument *prev = NULL;
  if (arg->arg)
    free(arg->arg);
  if (arg->value)
    free(arg->value);
  if (arg->prev)
    prev = arg->prev;

  free(arg);
  return prev;
}

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(ArgumentList *result) {
  Argument *tArg = result->last;
  while (tArg != NULL) {
    tArg = freeArgument(tArg);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Makes and returns a deep copy of src without prev/next pointers.
Argument *copyArgument(Argument *src) {
  // Do a deep copy for argument and value
  Argument *copy = calloc(sizeof(Argument), 1);
  copy->isGlobal = src->isGlobal;
  copy->isDisabled = src->isDisabled;
  if (src->arg)
    copy->arg = strdup(src->arg);
  if (src->value)
    copy->value = strdup(src->value);
  return copy;
}

// Replaces argument and value in dst, freeing arg and value.
// Keeps next and prev pointers.
void replaceArgument(Argument *dst, Argument *src) {
  // Do a deep copy for argument and value
  if (dst->arg)
    free(dst->arg);
  if (dst->value)
    free(dst->value);
  dst->isGlobal = src->isGlobal;
  dst->isDisabled = src->isDisabled;
  if (src->arg)
    dst->arg = strdup(src->arg);
  if (src->value)
    dst->value = strdup(src->value);
}

// Creates new Argument with passed argName and value.
// Copies both argName and value
Argument *newArgument(const char *argName, char *value) {
  Argument *arg = malloc(sizeof(Argument));
  arg->isDisabled = 0;
  arg->isGlobal = 0;
  arg->prev = NULL;
  arg->next = NULL;
  if (argName)
    arg->arg = strdup(argName);
  if (value)
    arg->value = strdup(value);

  return arg;
}

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg) {
  target->total++;

  if (!target->first) {
    target->first = arg;
  } else {
    target->last->next = arg;
    arg->prev = target->last;
  }
  target->last = arg;
}

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list
void appendArgumentCopy(ArgumentList *target, Argument *arg) {
  // Do a deep copy for argument and value
  Argument *copy = copyArgument(arg);
  appendArgument(target, copy);
}

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// All arguments merged from the second list are a deep copy of arguments in source lists.
// Expects both lists to be initialized.
void mergeArgumentLists(ArgumentList *list1, ArgumentList *list2) {
  Argument *curArg1;
  Argument *curArg2 = list2->first;
  int isDuplicate = 0;

  // Copy arguments from the second list into result
  while (curArg2 != NULL) {
    isDuplicate = 0;
    // Look for duplicate arguments in the first list
    curArg1 = list1->first;
    while (curArg1 != NULL) {
      // If result already contains argument with the same name, skip it
      if (!strcmp(curArg2->arg, curArg1->arg)) {
        isDuplicate = 1;
        // If argument is disabled and has no value
        if (curArg1->isDisabled && (curArg1->value[0] == '\0')) {
          // Replace element in list1 with disabled element from list2
          replaceArgument(curArg1, curArg2);
          curArg1->isDisabled = 1;
        }
        break;
      }
      curArg1 = curArg1->next;
    }
    // If no duplicate was found, insert the argument
    if (!isDuplicate) {
      appendArgumentCopy(list1, curArg2);
    }
    curArg2 = curArg2->next;
  }
}

// Retrieves argument from the list
Argument *getArgument(ArgumentList *target, const char *argumentName) {
  Argument *arg = target->first;
  while (arg != NULL) {
    if (!strcmp(arg->arg, argumentName)) {
      return arg;
    }
    arg = arg->next;
  }
  return NULL;
}

// Creates new argument and inserts it into the list
Argument *insertArgument(ArgumentList *target, const char *argumentName, char *value) {
  Argument *arg = newArgument(argumentName, value);
  appendArgument(target, arg);
  return arg;
}

// Loads both global and title launch arguments, returning pointer to a merged list
ArgumentList *loadLaunchArgumentLists(Target *target) {
  int res = 0;
  // Initialize global argument list
  ArgumentList *globalArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getGlobalLaunchArguments(globalArguments, target->device))) {
    DPRINTF("WARN: Failed to load global launch arguments: %d\n", res);
  }
  int globalOpl = readGameCorePreference(target);
  if (globalOpl < 0) {
    Argument *core = getArgument(globalArguments, "luna_core");
    globalOpl = core != NULL && !core->isDisabled && core->value != NULL &&
                !strcmp(core->value, "opl");
  }
  // Initialize title list and merge global into it
  ArgumentList *titleArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getTitleLaunchArguments(titleArguments, target))) {
    DPRINTF("WARN: Failed to load title arguments: %d\n", res);
  }
  int titleLogoOverride = getArgument(titleArguments, "logo") != NULL;
  int titleCoreOverride = getArgument(titleArguments, "luna_core") != NULL;
  ArgumentList *result;
  if (titleArguments->total != 0) {
    // Merge lists
    mergeArgumentLists(titleArguments, globalArguments);
    freeArgumentList(globalArguments);
    result = titleArguments;
  } else {
    // If there are no title arguments, use global arguments directly.
    free(titleArguments);
    result = globalArguments;
  }
  Argument *logo = getArgument(result, "logo");
  if (titleLogoOverride && logo != NULL)
    logo->isGlobal = 0;
  Argument *core = getArgument(result, "luna_core");
  if (titleCoreOverride && core != NULL)
    core->isGlobal = 0;
  if (!lunaApplyGlobalGameCore(result, globalOpl))
    DPRINTF("WARN: Failed to add default game core launch argument\n");
  if (!lunaApplyGlobalPS2Logo(result, loadPS2LogoEnabled(target)))
    DPRINTF("WARN: Failed to add default PS2 logo launch argument\n");
  return result;
}

// Generates 32-bit timestamp from RTC.
// Will wrap around every 64th year
uint32_t getTimestamp() {
  // Initialize libcdvd to get timestamp
  if (sceCdInit(SCECdINoD)) {
    // Read clock
    sceCdCLOCK time;
    sceCdReadClock(&time);
    sceCdInit(SCECdEXIT);

    // Pack date into 32-bit timestamp
    // Y   26 M 22 D  17 H  12 M    6 S    0
    // 111111 1111 11111 11111 111111 111111
    uint32_t sum = ((uint32_t)btoi(time.year)) << 26 |        // Year
                   ((uint32_t)btoi(time.month) & 0xF) << 22 | // Month
                   ((uint32_t)btoi(time.day)) << 17 |         // Day
                   ((uint32_t)btoi(time.hour)) << 12 |        // Hour
                   ((uint32_t)btoi(time.minute)) << 6 |       // Minute
                   (btoi(time.second) & 0x3F);                // Second
    return sum;
  }
  return 0;
}
