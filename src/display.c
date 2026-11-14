#include "display.h"
#include "options.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static LunaDisplayMode activeMode;
static char displayPath[PATH_MAX + 1];
static const char *const codes[] = {"auto", "ntsc", "pal", "480p"};
static const char *const labels[] = {
    "Auto (console default)", "NTSC - Interlaced", "PAL - Interlaced",
    "480p - Progressive"};
static const VModeType modes[] = {VMODE_NONE, VMODE_NTSC, VMODE_PAL, VMODE_480P};

LunaDisplayMode lunaDisplayMode(void) { return activeMode; }
VModeType lunaDisplayVMode(LunaDisplayMode mode) {
  return mode >= 0 && mode < LUNA_DISPLAY_COUNT ? modes[mode] : VMODE_NONE;
}
const char *lunaDisplayLabel(LunaDisplayMode mode) {
  return labels[mode >= 0 && mode < LUNA_DISPLAY_COUNT ? mode : LUNA_DISPLAY_AUTO];
}

static int readDisplay(const char *path, LunaDisplayMode *mode) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  char value[16];
  size_t length = fread(value, 1, sizeof(value), file);
  int valid = length > 0 && length < sizeof(value) && !ferror(file);
  if (valid && fgetc(file) != EOF) valid = 0;
  if (fclose(file)) valid = 0;
  if (length && value[length - 1] == '\n') length--;
  if (length && value[length - 1] == '\r') length--;
  if (valid) for (int i = 0; i < LUNA_DISPLAY_COUNT; i++) {
    if (length == strlen(codes[i]) && !memcmp(value, codes[i], length)) {
      *mode = (LunaDisplayMode)i;
      return 1;
    }
  }
  return 0;
}

void lunaDisplayConfigure(const char *directory) {
  activeMode = LUNA_DISPLAY_AUTO;
  for (int i = 0; i < LUNA_DISPLAY_COUNT; i++)
    if (modes[i] == LAUNCHER_OPTIONS.vmode) activeMode = (LunaDisplayMode)i;
  displayPath[0] = '\0';
  if (!directory || !directory[0]) return;
  int length = snprintf(displayPath, sizeof(displayPath), "%s%sdisplay.cfg",
      directory, directory[strlen(directory) - 1] == '/' ? "" : "/");
  if (length < 0 || (size_t)length >= sizeof(displayPath)) {
    displayPath[0] = '\0';
    return;
  }
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", displayPath);
  // A surviving backup means a previous save did not finish. Prefer its
  // confirmed mode even if a partial commit left a valid-looking new file.
  if (!readDisplay(backup, &activeMode)) readDisplay(displayPath, &activeMode);
  LAUNCHER_OPTIONS.vmode = lunaDisplayVMode(activeMode);
}

static int writeDisplay(const char *path, LunaDisplayMode mode) {
  FILE *file = fopen(path, "wb");
  if (!file) return -EIO;
  int bad = fprintf(file, "%s\n", codes[mode]) < 0;
  if (fflush(file)) bad = 1;
  if (fclose(file)) bad = 1;
  return bad ? -EIO : 0;
}

int lunaDisplaySave(LunaDisplayMode mode) {
  if (mode < 0 || mode >= LUNA_DISPLAY_COUNT) return -EINVAL;
  if (!displayPath[0]) return -EROFS;
  char temp[PATH_MAX + 8], backup[PATH_MAX + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", displayPath);
  snprintf(backup, sizeof(backup), "%s.bak", displayPath);
  if (writeDisplay(temp, mode)) { remove(temp); return -EIO; }
  if (writeDisplay(backup, activeMode)) { remove(temp); return -EIO; }
  int result = commitConfigFile(temp, displayPath);
  if (result) return result;
  if (remove(backup)) return -EIO;
  activeMode = mode;
  return 0;
}
