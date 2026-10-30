#ifndef LUNA_SPACE_FLIGHT_H
#define LUNA_SPACE_FLIGHT_H
#include <stdint.h>
#define LUNA_SPACE_POINTS 144
typedef struct { float x, y, tailX, tailY, size, light; } LunaSpacePoint;
int lunaSpaceSpeed(void);
int lunaSpaceDepth(void);
void lunaSpaceSetSpeed(int setting);
void lunaSpaceSetDepth(int setting);
void lunaSpaceStep(uint32_t nowMs);
int lunaSpaceProject(int index, int width, int height, LunaSpacePoint *point);
void drawSpaceFlight(uint32_t nowMs);
void resetSpaceFlightTexture(void);
#endif
