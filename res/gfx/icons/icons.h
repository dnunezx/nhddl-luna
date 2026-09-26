#ifndef _UI_ICONS_H_
#define _UI_ICONS_H_

// Face buttons use assets/playstation-buttons; other icons retain the OPL artwork.

#include <stdint.h>

typedef struct Icon {
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
} Icon;

// Icon types, must match ICONS array index
typedef enum {
  ICON_CIRCLE,
  ICON_CROSS,
  ICON_SQUARE,
  ICON_TRIANGLE,
  ICON_L1,
  ICON_R1,
  ICON_SELECT,
  ICON_START,
  ICON_ENABLED
} IconType;

const Icon ICONS[] = {
    {0, 0, 26, 26},   // Circle
    {27, 0, 26, 26},  // Cross
    {54, 0, 26, 26},  // Square
    {81, 0, 26, 26},  // Triangle
    {0, 28, 25, 17},  // L1
    {25, 28, 25, 17}, // R1
    {0, 46, 22, 12},  // Select
    {22, 46, 22, 12}, // Start
    {0, 60, 10, 10}   // Enabled
};

#endif
