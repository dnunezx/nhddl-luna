#include "ui/view_state.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char lastViewPath[] = "/lastView.txt";
static const char lastViewTempPath[] = "/lastView.txt.tmp";
static const char enabledViewsPath[] = "/enabledViews.txt";
static const char enabledViewsTempPath[] = "/enabledViews.txt.tmp";
static const char classicLayoutPath[] = "/classicLayout.txt";
static const char classicLayoutTempPath[] = "/classicLayout.txt.tmp";
static const char backgroundPath[] = "/background.txt";
static const char backgroundTempPath[] = "/background.txt.tmp";
static const char orbsThemePath[] = "/orbsTheme.txt";
static const char orbsThemeTempPath[] = "/orbsTheme.txt.tmp";
static const char orbsAppearancePath[] = "/orbsAppearance.txt";
static const char orbsAppearanceTempPath[] = "/orbsAppearance.txt.tmp";
static const char glassColorPath[] = "/glassColor.txt";
static const char glassColorTempPath[] = "/glassColor.txt.tmp";
static const char uiFontPath[] = "/uiFont.txt";
static const char uiFontTempPath[] = "/uiFont.txt.tmp";
static const char ambientSoundPath[] = "/ambientSound.txt";
static const char ambientSoundTempPath[] = "/ambientSound.txt.tmp";
static const char *const viewNames[] = {
    "classic", "collection", "grid", "orbit", "orbs"};
static const char *const glassColorNames[GLASS_COLOR_COUNT] = {
    "original", "white-gray", "black"};
static const char *const uiFontNames[UI_FONT_COUNT] = {
    "dejavu", "psbbn"};

static struct DeviceMapEntry *viewDevice(Target *target) {
  if (target == NULL || target->device == NULL)
    return NULL;
  return target->device->metadev ? target->device->metadev : target->device;
}

static int readViewFile(const char *path, UILibraryView *view) {
  char name[24];
  FILE *file = fopen(path, "r");
  if (file == NULL)
    return -ENOENT;
  if (fgets(name, sizeof(name), file) == NULL) {
    fclose(file);
    return -EINVAL;
  }
  fclose(file);
  size_t nameLength = strcspn(name, "\r\n");
  if (name[nameLength] == '\0')
    return -EINVAL;
  name[nameLength] = '\0';
  for (int i = UI_VIEW_CLASSIC; i <= UI_VIEW_ORBS; i++) {
    if (!strcmp(name, viewNames[i])) {
      *view = (UILibraryView)i;
      return 0;
    }
  }
  return -EINVAL;
}

UILibraryView loadLastLibraryView(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  char path[PATH_MAX];
  UILibraryView view;

  if (device == NULL || device->mountpoint == NULL)
    return UI_VIEW_CLASSIC;
  // A completed temporary file is the latest state if power was lost between
  // removing the previous file and committing the replacement.
  if (!buildConfigFilePath(path, sizeof(path), device->mountpoint, lastViewTempPath) &&
      !readViewFile(path, &view)) {
    DPRINTF("Restored library view %s from %s\n", viewNames[view], path);
    return view;
  }
  if (!buildConfigFilePath(path, sizeof(path), device->mountpoint, lastViewPath) &&
      !readViewFile(path, &view)) {
    DPRINTF("Restored library view %s from %s\n", viewNames[view], path);
    return view;
  }
  return UI_VIEW_CLASSIC;
}

int saveLastLibraryView(Target *target, UILibraryView view) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL ||
      view < UI_VIEW_CLASSIC || view > UI_VIEW_ORBS)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, lastViewPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, lastViewTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777)) {
    DPRINTF("ERROR: Failed to create view state directory: %d\n", errno);
    return -EIO;
  }
  file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", viewNames[view]);
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path)) {
    DPRINTF("ERROR: Failed to commit last view: %d\n", errno);
    return -EIO;
  }
  DPRINTF("Saved library view %s to %s\n", viewNames[view], path);
  return 0;
}

int loadClassicArtOverlap(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {classicLayoutTempPath, classicLayoutPath};
  char path[PATH_MAX];
  char name[24];

  if (device == NULL || device->mountpoint == NULL)
    return 0;
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL)
      continue;
    int readable = fgets(name, sizeof(name), file) != NULL;
    fclose(file);
    if (!readable)
      continue;
    name[strcspn(name, "\r\n")] = '\0';
    if (!strcmp(name, "overlap"))
      return 1;
    if (!strcmp(name, "separate"))
      return 0;
  }
  return 0;
}

int saveClassicArtOverlap(Target *target, int overlap) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, classicLayoutPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, classicLayoutTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777)) {
    DPRINTF("ERROR: Failed to create Classic layout directory: %d\n", errno);
    return -EIO;
  }
  file = fopen(tempPath, "w");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open Classic layout temp file: %d\n", errno);
    return -EIO;
  }
  int writeResult = fprintf(file, "%s\n", overlap ? "overlap" : "separate");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    DPRINTF("ERROR: Failed to write Classic layout temp file: %d\n", errno);
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path)) {
    DPRINTF("ERROR: Failed to commit Classic layout: %d\n", errno);
    return -EIO;
  }
  DPRINTF("Saved Classic artwork layout %s to %s\n",
          overlap ? "overlap" : "separate", path);
  return 0;
}

int loadAmbientOrbsBackground(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {backgroundTempPath, backgroundPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return 0;
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
    if (!strcmp(value, "orbs"))
      return 1;
    if (!strcmp(value, "stars"))
      return 0;
  }
  return 0;
}

int saveAmbientOrbsBackground(Target *target, int enabled) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, backgroundPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, backgroundTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", enabled ? "orbs" : "stars");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

AmbientOrbsTheme loadAmbientOrbsTheme(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {orbsThemeTempPath, orbsThemePath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return ORBS_THEME_LUNA;
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
    if (!strcmp(value, "ps2-original"))
      return ORBS_THEME_PS2_ORIGINAL;
    if (!strcmp(value, "luna"))
      return ORBS_THEME_LUNA;
  }
  return ORBS_THEME_LUNA;
}

int saveAmbientOrbsTheme(Target *target, AmbientOrbsTheme theme) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;

  if (device == NULL || device->mountpoint == NULL ||
      (theme != ORBS_THEME_LUNA && theme != ORBS_THEME_PS2_ORIGINAL))
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, orbsThemePath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, orbsThemeTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", theme == ORBS_THEME_PS2_ORIGINAL ?
                            "ps2-original" : "luna");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

AmbientOrbsAppearance loadAmbientOrbsAppearance(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {orbsAppearanceTempPath, orbsAppearancePath};
  char path[PATH_MAX];
  char value[24];
  if (device == NULL || device->mountpoint == NULL)
    return ORBS_APPEARANCE_LUNA;
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
    if (!strcmp(value, "ps2-original"))
      return ORBS_APPEARANCE_PS2_ORIGINAL;
    if (!strcmp(value, "luna"))
      return ORBS_APPEARANCE_LUNA;
  }
  return ORBS_APPEARANCE_LUNA;
}

int saveAmbientOrbsAppearance(Target *target, AmbientOrbsAppearance appearance) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  if (device == NULL || device->mountpoint == NULL ||
      (appearance != ORBS_APPEARANCE_LUNA &&
       appearance != ORBS_APPEARANCE_PS2_ORIGINAL))
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, orbsAppearancePath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, orbsAppearanceTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", appearance == ORBS_APPEARANCE_PS2_ORIGINAL ?
                            "ps2-original" : "luna");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

uint32_t loadEnabledLibraryViews(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {enabledViewsTempPath, enabledViewsPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return UI_VIEW_ALL_MASK;
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
    char *end;
    unsigned long mask = strtoul(value, &end, 10);
    if ((*end == '\0' || *end == '\r' || *end == '\n') &&
        mask != 0 && (mask & ~UI_VIEW_ALL_MASK) == 0)
      return (uint32_t)mask;
  }
  return UI_VIEW_ALL_MASK;
}

int saveEnabledLibraryViews(Target *target, uint32_t enabledViews) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;

  if (device == NULL || device->mountpoint == NULL || enabledViews == 0 ||
      (enabledViews & ~UI_VIEW_ALL_MASK) != 0)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, enabledViewsPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, enabledViewsTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%lu\n", (unsigned long)enabledViews);
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

GlassColorPreset loadGlassColorPreset(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {glassColorTempPath, glassColorPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return GLASS_COLOR_ORIGINAL;
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
    for (int preset = 0; preset < GLASS_COLOR_COUNT; preset++)
      if (!strcmp(value, glassColorNames[preset]))
        return (GlassColorPreset)preset;
  }
  return GLASS_COLOR_ORIGINAL;
}

int saveGlassColorPreset(Target *target, GlassColorPreset preset) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL ||
      preset < GLASS_COLOR_ORIGINAL || preset >= GLASS_COLOR_COUNT)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, glassColorPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, glassColorTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", glassColorNames[preset]);
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

UIFont loadUIFont(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {uiFontTempPath, uiFontPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return UI_FONT_DEJAVU;
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
    for (int selection = 0; selection < UI_FONT_COUNT; selection++)
      if (!strcmp(value, uiFontNames[selection]))
        return (UIFont)selection;
  }
  return UI_FONT_DEJAVU;
}

int saveUIFont(Target *target, UIFont selection) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL ||
      selection < UI_FONT_DEJAVU || selection >= UI_FONT_COUNT)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, uiFontPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, uiFontTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", uiFontNames[selection]);
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}

int loadAmbientSoundEnabled(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {ambientSoundTempPath, ambientSoundPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return 1;
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

int saveAmbientSoundEnabled(Target *target, int enabled) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, ambientSoundPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, ambientSoundTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  file = fopen(tempPath, "w");
  if (file == NULL)
    return -EIO;
  int writeResult = fprintf(file, "%s\n", enabled ? "on" : "off");
  int closeResult = fclose(file);
  if (writeResult < 0 || closeResult) {
    remove(tempPath);
    return -EIO;
  }
  remove(path);
  if (rename(tempPath, path))
    return -EIO;
  return 0;
}
