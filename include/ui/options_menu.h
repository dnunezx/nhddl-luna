#ifndef LUNA_UI_OPTIONS_MENU_H
#define LUNA_UI_OPTIONS_MENU_H

#include "target.h"
#include <stdint.h>

int uiTitleOptionsLoop(Target *target, int *classicArtOverlap,
                       int *ambientOrbsBackgroundSetting, int *glassColorPreset,
                       int *fontSetting, int *ambientEnabled, int *orbsThemeSetting,
                       int *orbsAppearanceSetting, int *orbsColorSetting,
                       int *tailsColorSetting, uint32_t *enabledOrbShapes,
                       uint32_t *enabledViews);

#endif
