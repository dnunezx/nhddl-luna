#ifndef LUNA_UI_NEO_H
#define LUNA_UI_NEO_H

#include <stdint.h>

typedef enum { NEO_SPEED_SLOW, NEO_SPEED_NORMAL, NEO_SPEED_FAST, NEO_SPEED_COUNT } NeoSpeed;
typedef enum { NEO_DENSITY_SPARSE, NEO_DENSITY_NORMAL, NEO_DENSITY_DENSE, NEO_DENSITY_COUNT } NeoDensity;
typedef enum {
  NEO_COLOR_GREEN, NEO_COLOR_CYAN, NEO_COLOR_BLUE, NEO_COLOR_VIOLET,
  NEO_COLOR_RED, NEO_COLOR_AMBER, NEO_COLOR_WHITE, NEO_COLOR_COUNT
} NeoColor;

// Updates the live rain without resetting its animation or texture.
void setNeoAppearance(unsigned speed, unsigned density, unsigned color);
void resetNeoBackground(void);
void drawNeoBackground(uint32_t nowMs);

#endif
