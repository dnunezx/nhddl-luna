#ifndef LUNA_RING_MENU_H
#define LUNA_RING_MENU_H

enum { LUNA_RING_LIBRARY, LUNA_RING_BROWSER, LUNA_RING_SETTINGS };
enum { LUNA_MENU_APPEARANCE = 3 };
typedef enum {
  LUNA_RING_ORIGINAL,LUNA_RING_CYAN,LUNA_RING_BLUE,LUNA_RING_VIOLET,
  LUNA_RING_ROSE,LUNA_RING_GREEN,LUNA_RING_WHITE,LUNA_RING_COLOR_COUNT
} LunaRingColor;
void uiRingSetColor(LunaRingColor color);
LunaRingColor uiRingGetColor(void);
void uiRingSetEffectsColor(LunaRingColor color);
int uiRingMenuLoop(int hasLibrary, const char *status);

#endif
