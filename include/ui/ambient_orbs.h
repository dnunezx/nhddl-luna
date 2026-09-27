#ifndef LUNA_UI_AMBIENT_ORBS_H
#define LUNA_UI_AMBIENT_ORBS_H

#include <stdint.h>

// Shared formations draw over a background supplied by the caller.
void resetAmbientOrbs(uint32_t startMs);
void observeAmbientOrbsSelection(int selectedTitleIdx, uint32_t now);
void drawAmbientOrbsBackground(int centerX, int centerY, int radiusX,
                               int radiusY, uint32_t elapsedMs, int trailZ);

// The alphabet formation and navigation reaction belong only to Scroll view.
void resetAmbientOrbsScroll(void);
void triggerAmbientOrbsScrollReaction(int direction, uint32_t now);
void drawAmbientOrbsScroll(int centerX, int centerY, int radiusX,
                           int radiusY, uint32_t elapsedMs, int fastScroll,
                           const char *title, int trailZ, int scrollFocusX);

#endif
