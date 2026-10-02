// Shared System Configuration scene math from osdbits/menu.c.
#ifndef LUNA_UI_PS2_MENU_SCENE_H
#define LUNA_UI_PS2_MENU_SCENE_H

#include <math.h>
#include <stdint.h>

static inline float ps2MenuSin(uint32_t angle) {
  return sinf((int16_t)angle * (6.28318530718f / 65536.0f));
}

static inline float ps2MenuCos(uint32_t angle) {
  return cosf((int16_t)angle * (6.28318530718f / 65536.0f));
}

// MenuCameraMatrix rotates both the camera basis AND its position. Its
// inverse is therefore inverse-rotation(world) minus the unrotated position.
// Normals use the same basis with no translation.
static inline void ps2MenuCamera(float *x, float *y, float *z, int normal) {
  const float cy = cosf(0.145f), sy = sinf(0.145f);
  const float cp = cosf(0.031f), sp = sinf(0.031f);
  const float a = *x * cy - *z * sy;
  const float b = *x * sy + *z * cy;
  const float c = *y * cp + b * sp;
  const float d = b * cp - *y * sp;
  *x = a - (normal ? 0.0f : 10.436f);
  *y = c;
  *z = d + (normal ? 0.0f : 103.0f);
}

static inline void ps2MenuProject(float x, float y, float z,
                                  int width, int height, float *sx, float *sy) {
  *sx = width * 0.5f + x * 512.0f / z;
  *sy = height * 0.5f - y * 512.0f * height / (480.0f * z);
}

// 0x2261B8: Rz(tilt) Ry(spin) Rz(pi) Ry(minutes*1100)
// Rx((index+21)*seconds) Translate(0,radius,0). The final Ry(8192)
// rotates the sprite basis and does not move the translated point.
static inline void ps2MenuOrbWorld(int index, uint64_t clockMs, float radius,
                                   uint32_t tilt, uint32_t spin,
                                   float *x, float *y, float *z) {
  const uint32_t seconds = (uint32_t)(((clockMs % 60000U) << 16) / 60000U);
  const uint32_t minutes = (uint32_t)((clockMs % 3600000U) *
                                      1100U * 65536U / 3600000U);
  const uint32_t phase = seconds * (uint32_t)(index + 21);
  const uint32_t tumble = spin - minutes;
  const float side = ps2MenuSin(phase) * ps2MenuSin(tumble) * radius;
  const float forward = ps2MenuSin(phase) * ps2MenuCos(tumble) * radius;
  const float up = -ps2MenuCos(phase) * radius;
  *x = side * ps2MenuCos(tilt) - up * ps2MenuSin(tilt);
  *y = side * ps2MenuSin(tilt) + up * ps2MenuCos(tilt);
  *z = forward;
}

#endif
