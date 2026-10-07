#ifndef LUNA_UI_LANGUAGE_H
#define LUNA_UI_LANGUAGE_H

typedef enum {
  LUNA_LANGUAGE_ENGLISH,
  LUNA_LANGUAGE_SPANISH,
  LUNA_LANGUAGE_PORTUGUESE,
  LUNA_LANGUAGE_COUNT
} LunaLanguage;

LunaLanguage lunaLanguage(void);
const char *lunaLanguageName(LunaLanguage language);
// Load launcher-wide language.cfg after the launch device is initialized.
void lunaLanguageConfigure(const char *directory);
// Changes the active language only after a successful save.
int lunaLanguageSave(LunaLanguage language);
// English source strings are catalog keys. Unknown keys fall back unchanged.
// Call only for UI-owned text, never game titles, filenames or arguments.
const char *lunaText(const char *english);

#endif
