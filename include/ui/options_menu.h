#ifndef LUNA_UI_OPTIONS_MENU_H
#define LUNA_UI_OPTIONS_MENU_H

#include "target.h"
#include <stdint.h>

int uiTitleOptionsLoop(Target *target, int *classicArtOverlap,
                       int *ambientOrbsBackgroundSetting, int *glassColorPreset,
                       int *fontSetting, int *ambientEnabled, int *orbsThemeSetting,
                       int *orbsAppearanceSetting,
                       uint32_t *enabledViews);

#endif
