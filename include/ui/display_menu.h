#ifndef LUNA_DISPLAY_MENU_H
#define LUNA_DISPLAY_MENU_H
#include "ui/cube_menu.h"

#define LUNA_DISPLAY_UI_REFRESH 3
LunaCubeSubmenu uiDisplaySubmenu(void);
// Run after the library's workers, pad and renderer have closed.
// Returns -1 only when even the previous renderer cannot be restored.
int uiDisplayApplyRequest(void);
#endif
