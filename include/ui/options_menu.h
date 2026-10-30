#ifndef LUNA_UI_OPTIONS_MENU_H
#define LUNA_UI_OPTIONS_MENU_H

#include "target.h"
#include <stdint.h>

typedef struct {
  // Borrowed scene; ownership stays with the library.
  // Retreat progress: 0 is the original library, 1 is fully aside.
  void (*drawLibrary)(void *context, float progress);
  void *context;
} LunaOptionsScene;

int uiTitleOptionsLoop(Target *target, const LunaOptionsScene *scene);

#endif
