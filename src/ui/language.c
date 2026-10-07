#include "ui/language.h"
#include "options.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *english, *spanish, *portuguese; } Translation;
// Sorted by English key; keep keys and printf placeholders identical to source.
static const Translation translations[] = {
#include "language_catalog.inc"
};
static LunaLanguage activeLanguage;
static char languagePath[PATH_MAX + 1];
static const char *const codes[] = {"en", "es", "pt-BR"};
static const char *const names[] = {"English", "Español", "Português"};

LunaLanguage lunaLanguage(void) { return activeLanguage; }
const char *lunaLanguageName(LunaLanguage language) {
  return names[language >= 0 && language < LUNA_LANGUAGE_COUNT ? language : 0];
}

const char *lunaText(const char *english) {
  if (!english || activeLanguage == LUNA_LANGUAGE_ENGLISH) return english;
  size_t left = 0, right = sizeof(translations) / sizeof(translations[0]);
  while (left < right) {
    size_t middle = left + (right - left) / 2;
    int order = strcmp(english, translations[middle].english);
    if (order < 0) right = middle;
    else if (order > 0) left = middle + 1;
    else {
      const char *value = activeLanguage == LUNA_LANGUAGE_SPANISH ?
          translations[middle].spanish : translations[middle].portuguese;
      return value && value[0] ? value : english;
    }
  }
  return english;
}

static int readLanguage(const char *path, LunaLanguage *language) {
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  char value[24];
  size_t length = fread(value, 1, sizeof(value), file);
  int valid = length > 0 && length < sizeof(value) && !ferror(file);
  if (valid && fgetc(file) != EOF) valid = 0;
  // Match the complete file, rejecting embedded NULs, extra lines and prefixes.
  if (length && value[length - 1] == '\n') length--;
  if (length && value[length - 1] == '\r') length--;
  if (fclose(file)) valid = 0;
  if (valid) for (int i = 0; i < LUNA_LANGUAGE_COUNT; i++) {
    if (length == strlen(codes[i]) && !memcmp(value, codes[i], length)) {
      *language = (LunaLanguage)i;
      return 1;
    }
  }
  return 0;
}

void lunaLanguageConfigure(const char *directory) {
  activeLanguage = LUNA_LANGUAGE_ENGLISH;
  languagePath[0] = '\0';
  if (!directory || !directory[0]) return;
  int length = snprintf(languagePath, sizeof(languagePath), "%s%slanguage.cfg",
                        directory, directory[strlen(directory) - 1] == '/' ? "" : "/");
  if (length < 0 || (size_t)length >= sizeof(languagePath)) { languagePath[0] = '\0'; return; }
  char backup[PATH_MAX + 8];
  snprintf(backup, sizeof(backup), "%s.bak", languagePath);
  if (!readLanguage(languagePath, &activeLanguage)) readLanguage(backup, &activeLanguage);
}

static int writeLanguage(const char *path, LunaLanguage language) {
  FILE *file = fopen(path, "wb");
  if (!file) return -EIO;
  int bad = fprintf(file, "%s\n", codes[language]) < 0;
  if (fflush(file)) bad = 1;
  if (fclose(file)) bad = 1;
  return bad ? -EIO : 0;
}

int lunaLanguageSave(LunaLanguage language) {
  if (language < 0 || language >= LUNA_LANGUAGE_COUNT) return -EINVAL;
  if (language == activeLanguage) return 0;
  if (!languagePath[0]) return -EROFS;
  char temp[PATH_MAX + 8], backup[PATH_MAX + 8];
  snprintf(temp, sizeof(temp), "%s.tmp", languagePath);
  snprintf(backup, sizeof(backup), "%s.bak", languagePath);
  if (writeLanguage(temp, language)) { remove(temp); return -EIO; }
  if (writeLanguage(backup, activeLanguage)) { remove(temp); return -EIO; }
  int result = commitConfigFile(temp, languagePath);
  if (result) return result;
  activeLanguage = language;
  remove(backup);
  return 0;
}
