#include "ui/view_state.h"
#include "devices/devices.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char startupViewPath[] = "/startupView.txt";
static const char startupViewTempPath[] = "/startupView.txt.tmp";
static const char lastGameModePath[] = "/lastGameMode.txt";
static const char lastGameModeTempPath[] = "/lastGameMode.txt.tmp";
static const char *const gameModeNames[] = {"all", "ps2", "ps1"};
static const char enabledViewsPath[] = "/enabledViews.txt";
static const char enabledViewsTempPath[] = "/enabledViews.txt.tmp";
static const char classicLayoutPath[] = "/classicLayout.txt";
static const char classicLayoutTempPath[] = "/classicLayout.txt.tmp";
static const char collectionGameListPath[] = "/collectionGameList.txt";
static const char collectionGameListTempPath[] = "/collectionGameList.txt.tmp";
static const char backgroundPath[] = "/background.txt";
static const char backgroundTempPath[] = "/background.txt.tmp";
static const char orbsThemePath[] = "/orbsTheme.txt";
static const char orbsThemeTempPath[] = "/orbsTheme.txt.tmp";
static const char orbShapesPath[] = "/orbShapes.txt";
static const char orbShapesTempPath[] = "/orbShapes.txt.tmp";
static const char orbsAppearancePath[] = "/orbsAppearance.txt";
static const char orbsAppearanceTempPath[] = "/orbsAppearance.txt.tmp";
static const char orbsColorPath[] = "/orbsColor.txt";
static const char orbsColorTempPath[] = "/orbsColor.txt.tmp";
static const char tailsColorPath[] = "/orbsTailColor.txt";
static const char tailsColorTempPath[] = "/orbsTailColor.txt.tmp";
static const char glassColorPath[] = "/glassColor.txt";
static const char glassColorTempPath[] = "/glassColor.txt.tmp";
static const char uiFontPath[] = "/uiFont.txt";
static const char uiFontTempPath[] = "/uiFont.txt.tmp";
static const char ps1CaseStylePath[] = "/ps1CaseStyle.txt";
static const char ps1CaseStyleTempPath[] = "/ps1CaseStyle.txt.tmp";
static const char ambientSoundPath[] = "/ambientSound.txt";
static const char ambientSoundTempPath[] = "/ambientSound.txt.tmp";
static const char *const viewNames[UI_VIEW_ID_LIMIT] = {
    [UI_VIEW_CLASSIC] = "classic", [UI_VIEW_PSBBN] = "collection",
    [UI_VIEW_GRID] = "grid", [UI_VIEW_ORBIT] = "orbit",
    [UI_VIEW_ORBS] = "orbs", [UI_VIEW_3D] = "3d",
    [UI_VIEW_COLLECTION_V2] = "collection-v2"};
static const char *const glassColorNames[GLASS_COLOR_COUNT] = {
    "original", "white-gray", "black", "cyber"};
static const char *const uiFontNames[UI_FONT_COUNT] = {
    "dejavu", "psbbn"};
static const char *const orbsColorNames[ORBS_COLOR_COUNT] = {
    "original", "cyan", "violet", "rose", "green", "gold", "white"};

static struct DeviceMapEntry *viewDevice(Target *target) {
  if (target == NULL || target->device == NULL)
    return NULL;
  return target->device->metadev ? target->device->metadev : target->device;
}

static int readViewFile(const char *path, UILibraryView *view) {
  char name[24];
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return -ENOENT;
  size_t length = fread(name, 1, sizeof(name), file);
  int valid = length > 0 && length < sizeof(name) && !ferror(file);
  if (valid && fgetc(file) != EOF) valid = 0;
  if (fclose(file)) valid = 0;
  if (length && name[length - 1] == '\n') length--;
  if (length && name[length - 1] == '\r') length--;
  if (!valid) return -EINVAL;
  for (int i = UI_VIEW_CLASSIC; i < UI_VIEW_ID_LIMIT; i++) {
    if ((UI_VIEW_ALL_MASK & (1U << i)) && viewNames[i] &&
        length == strlen(viewNames[i]) && !memcmp(name, viewNames[i], length)) {
      *view = (UILibraryView)i;
      return 0;
    }
  }
  return -EINVAL;
}

UILibraryView loadStartupLibraryView(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  char path[PATH_MAX];
  UILibraryView view;

  if (device == NULL || device->mountpoint == NULL)
    return UI_VIEW_CLASSIC;
  // A complete staged file is the latest preference after an interrupted commit.
  if (!buildConfigFilePath(path, sizeof(path), device->mountpoint, startupViewTempPath) &&
      !readViewFile(path, &view)) {
    return view;
  }
  if (!buildConfigFilePath(path, sizeof(path), device->mountpoint, startupViewPath) &&
      !readViewFile(path, &view)) {
    return view;
  }
  return UI_VIEW_CLASSIC;
}

int saveStartupLibraryView(Target *target, UILibraryView view) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX];
  char path[PATH_MAX];
  char tempPath[PATH_MAX];
  struct stat st;
  FILE *file;

  if (device == NULL || device->mountpoint == NULL ||
      view < UI_VIEW_CLASSIC || view >= UI_VIEW_ID_LIMIT || !viewNames[view] ||
      !(UI_VIEW_ALL_MASK & (1U << view)))
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, startupViewPath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, startupViewTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777)) {
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
  if (commitConfigFile(tempPath, path)) {
    return -EIO;
  }
  return 0;
}

TargetFilter loadLastGameMode(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {lastGameModeTempPath, lastGameModePath};
  char path[PATH_MAX], name[24];
  if (device == NULL || device->mountpoint == NULL)
    return TARGET_ALL;
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL) continue;
    int readable = fgets(name, sizeof(name), file) != NULL;
    int bad = ferror(file);
    fclose(file);
    if (!readable || bad) continue;
    size_t length = strcspn(name, "\r\n");
    if (name[length] == '\0') continue;
    name[length] = '\0';
    for (int filter = TARGET_ALL; filter <= TARGET_PS1_ONLY; filter++) {
      if (!strcmp(name, gameModeNames[filter]))
        return (TargetFilter)filter;
    }
  }
  return TARGET_ALL;
}

int saveLastGameMode(Target *target, TargetFilter filter) {
  struct DeviceMapEntry *device = viewDevice(target);
  char directory[PATH_MAX], path[PATH_MAX], tempPath[PATH_MAX];
  struct stat st;
  if (device == NULL || device->mountpoint == NULL ||
      filter < TARGET_ALL || filter > TARGET_PS1_ONLY)
    return -EINVAL;
  if (buildConfigFilePath(directory, sizeof(directory), device->mountpoint, NULL) ||
      buildConfigFilePath(path, sizeof(path), device->mountpoint, lastGameModePath) ||
      buildConfigFilePath(tempPath, sizeof(tempPath), device->mountpoint, lastGameModeTempPath))
    return -ENAMETOOLONG;
  if (stat(directory, &st) == -1 && mkdir(directory, 0777))
    return -EIO;
  FILE *file = fopen(tempPath, "w");
  if (file == NULL) return -EIO;
  int written = fprintf(file, "%s\n", gameModeNames[filter]);
  int closed = fclose(file);
  if (written < 0 || closed) {
    remove(tempPath);
    return -EIO;
  }
  return commitConfigFile(tempPath, path) ? -EIO : 0;
}

static int legacyClassicArtOverlap(Target *target) {
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

static int legacyCollectionGameList(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {collectionGameListTempPath, collectionGameListPath};
  char path[PATH_MAX], value[24];
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
    if (!strcmp(value, "on"))
      return 1;
    if (!strcmp(value, "off"))
      return 0;
  }
  return 0;
}

static LibraryBackground legacyLibraryBackground(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {backgroundTempPath, backgroundPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return LIBRARY_BACKGROUND_STARS;
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
      return LIBRARY_BACKGROUND_ORBS;
    if (!strcmp(value, "red-clouds") || !strcmp(value, "tunnel"))
      return LIBRARY_BACKGROUND_RED_CLOUDS;
    if (!strcmp(value, "midnight-cubes"))
      return LIBRARY_BACKGROUND_MIDNIGHT_CUBES;
    if (!strcmp(value, "system-config"))
      return LIBRARY_BACKGROUND_SYSTEM_CONFIG;
    if (!strcmp(value, "neo"))
      return LIBRARY_BACKGROUND_NEO;
    if (!strcmp(value, "aurora"))
      return LIBRARY_BACKGROUND_AURORA;
    if (!strcmp(value, "ronin"))
      return LIBRARY_BACKGROUND_RONIN;
    if (!strcmp(value, "gridrunner"))
      return LIBRARY_BACKGROUND_STARS;
    if (!strcmp(value, "torch") || !strcmp(value, "ember"))
      return LIBRARY_BACKGROUND_TORCH;
    if (!strcmp(value, "tempest"))
      return LIBRARY_BACKGROUND_TEMPEST;
    if (!strcmp(value, "stars"))
      return LIBRARY_BACKGROUND_STARS;
  }
  return LIBRARY_BACKGROUND_STARS;
}

static AmbientOrbsTheme legacyAmbientOrbsTheme(Target *target) {
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

static uint32_t legacyEnabledOrbShapes(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {orbShapesTempPath, orbShapesPath};
  char path[PATH_MAX], value[32];
  if (device == NULL || device->mountpoint == NULL)
    return ORBS_SHAPES_ALL_MASK;
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL)
      continue;
    int readable = fgets(value, sizeof(value), file) != NULL;
    int complete = readable && (strchr(value, '\n') || feof(file));
    fclose(file);
    if (!complete)
      continue;
    char *end;
    errno = 0;
    unsigned long mask = strtoul(value, &end, 10);
    if (!errno && end != value && mask <= ORBS_SHAPES_ALL_MASK &&
        strspn(end, " \t\r\n") == strlen(end))
      return (uint32_t)mask;
  }
  return ORBS_SHAPES_ALL_MASK;
}

static AmbientOrbsAppearance legacyAmbientOrbsAppearance(Target *target) {
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

static AmbientOrbsColor legacyAmbientOrbsColor(Target *target, AmbientOrbsColorPart part) {
  struct DeviceMapEntry *device = viewDevice(target);
  if (device == NULL || device->mountpoint == NULL ||
      (part != ORBS_COLOR_PART_ORBS && part != ORBS_COLOR_PART_TAILS))
    return ORBS_COLOR_ORIGINAL;
  const char *paths[] = {
      part == ORBS_COLOR_PART_ORBS ? orbsColorTempPath : tailsColorTempPath,
      part == ORBS_COLOR_PART_ORBS ? orbsColorPath : tailsColorPath};
  char path[PATH_MAX];
  char value[24];
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
    for (int color = 0; color < ORBS_COLOR_COUNT; color++) {
      if (!strcmp(value, orbsColorNames[color]))
        return (AmbientOrbsColor)color;
    }
  }
  return ORBS_COLOR_ORIGINAL;
}

static uint32_t legacyEnabledLibraryViews(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {enabledViewsTempPath, enabledViewsPath};
  char path[PATH_MAX];
  char value[24];

  if (device == NULL || device->mountpoint == NULL)
    return UI_VIEW_DEFAULT_MASK;
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
        mask != 0 && (mask & ~((1UL << UI_VIEW_ID_LIMIT) - 1UL)) == 0) {
      mask &= UI_VIEW_ALL_MASK;
      return mask != 0 ? (uint32_t)mask : UI_VIEW_DEFAULT_MASK;
    }
  }
  return UI_VIEW_DEFAULT_MASK;
}

static GlassColorPreset legacyGlassColorPreset(Target *target) {
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

static UIFont legacyUIFont(Target *target) {
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
      if (!strcmp(value, uiFontNames[selection])) {
        return (UIFont)selection;
      }
  }
  return UI_FONT_DEJAVU;
}

static Ps1CaseStyle legacyPs1CaseStyle(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {ps1CaseStyleTempPath, ps1CaseStylePath};
  char path[PATH_MAX], value[24];
  if (device == NULL || device->mountpoint == NULL)
    return PS1_CASE_TALL;
  for (int i = 0; i < 2; i++) {
    if (buildConfigFilePath(path, sizeof(path), device->mountpoint, paths[i]))
      continue;
    FILE *file = fopen(path, "r");
    if (file == NULL) continue;
    int readable = fgets(value, sizeof(value), file) != NULL;
    fclose(file);
    if (!readable) continue;
    value[strcspn(value, "\r\n")] = '\0';
    if (!strcmp(value, "tall") || !strcmp(value, "square")) {
      return !strcmp(value, "square") ? PS1_CASE_SQUARE : PS1_CASE_TALL;
    }
  }
  return PS1_CASE_TALL;
}

static int legacyAmbientSoundEnabled(Target *target) {
  struct DeviceMapEntry *device = viewDevice(target);
  const char *paths[] = {ambientSoundTempPath, ambientSoundPath};
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
    if (!strcmp(value, "off"))
      return 0;
    if (!strcmp(value, "on"))
      return 1;
  }
  return 0;
}


void loadLegacyAppearance(Target *target, LunaAppearance *settings) {
  settings->spaceSpeed = 4;
  settings->spaceDepth = 2;
  settings->ringColor = 0;
  settings->ringEffectsColor = 0;
  settings->background = legacyLibraryBackground(target);
  settings->glassColor = legacyGlassColorPreset(target);
  settings->font = legacyUIFont(target);
  settings->ambientSound = legacyAmbientSoundEnabled(target);
  settings->enabledViews = legacyEnabledLibraryViews(target);
  settings->classicOverlap = legacyClassicArtOverlap(target);
  settings->collectionGameList = legacyCollectionGameList(target);
  settings->ps1CaseStyle = legacyPs1CaseStyle(target);
  settings->orbsTheme = legacyAmbientOrbsTheme(target);
  settings->orbsAppearance = legacyAmbientOrbsAppearance(target);
  settings->orbsColor = legacyAmbientOrbsColor(target, ORBS_COLOR_PART_ORBS);
  settings->orbShapes = legacyEnabledOrbShapes(target);
  settings->tailsColor = legacyAmbientOrbsColor(target, ORBS_COLOR_PART_TAILS);
}
