// Host-side checks against the documented matrix chain; no BIOS data.
#include "ui/ps2_menu_scene.h"
#include <assert.h>
#include <stdio.h>

static void rotate(float p[3], int axis, float angle) {
  const float s = sinf(angle), c = cosf(angle);
  const int a = (axis + 1) % 3, b = (axis + 2) % 3;
  const float x = p[a], y = p[b];
  p[a] = x * c - y * s;
  p[b] = x * s + y * c;
}

static float angle(uint32_t phase) {
  return (int16_t)phase * (6.28318530718f / 65536.0f);
}

int main(void) {
  float x = 0, y = 0, z = 0, sx, sy;
  ps2MenuCamera(&x, &y, &z, 0);
  assert(fabsf(x + 10.436f) < 0.00001f);
  assert(y == 0 && z == 103);
  for (int height = 448; height <= 512; height += 64) {
    ps2MenuProject(x, y, z, 640, height, &sx, &sy);
    assert(fabsf(sx - (320 - 10.436f * 512 / 103)) < 0.0001f);
    assert(sy == height / 2.0f);
  }
  int cases = 0;
  for (int hour = 0; hour < 24; hour++) {
    for (int minute = 0; minute < 60; minute += 7) {
      for (int second = 0; second < 60; second += 3) {
        const uint64_t clockMs = ((uint64_t)hour * 3600 + minute * 60 + second) * 1000 + 137;
        const uint32_t tilt = (clockMs % 43200000U) * 65536U / 43200000U;
        const uint32_t spin = (clockMs % 60000U) * 65536U / 60000U;
        const uint32_t tumble = (clockMs % 3600000U) * 1100U * 65536U / 3600000U;
        const float radius = 10 + 7.25f * (clockMs % 3600000U) / 3600000.0f;
        assert(radius >= 10 && radius < 17.25f && radius < 20);
        for (int orb = 0; orb < 7; orb++) {
          float expected[3] = {0, radius, 0};
          // Execute the five reference rotations independently, in reverse
          // order of the MatrixDrive stack, rather than its reduced formula.
          rotate(expected, 0, angle(spin * (orb + 21)));
          rotate(expected, 1, angle(tumble));
          rotate(expected, 2, -3.14159265359f);
          rotate(expected, 1, angle(spin));
          rotate(expected, 2, angle(tilt));
          ps2MenuOrbWorld(orb, clockMs, radius, tilt, spin, &x, &y, &z);
          assert(fabsf(x - expected[0]) < 0.0001f);
          assert(fabsf(y - expected[1]) < 0.0001f);
          assert(fabsf(z - expected[2]) < 0.0001f);
          assert(fabsf(sqrtf(x*x + y*y + z*z) - radius) < 0.0001f);
          // Independent camera inversion: rotate its position first, subtract
          // it from the world point, then invert its basis. This catches the
          // previous incorrect pitch applied AFTER the +103 translation.
          float camera[3] = {10.436f, 0, -103};
          rotate(camera, 0, .031f);
          rotate(camera, 1, .145f);
          for (int i = 0; i < 3; i++) expected[i] -= camera[i];
          rotate(expected, 1, -.145f);
          rotate(expected, 0, -.031f);
          ps2MenuCamera(&x, &y, &z, 0);
          assert(fabsf(x - expected[0]) < 0.0001f);
          assert(fabsf(y - expected[1]) < 0.0001f);
          assert(fabsf(z - expected[2]) < 0.0001f);
          assert(z > 0);
          cases++;
        }
      }
    }
  }
  printf("PASS: %d orb positions match the source matrix chain and shared camera.\n", cases);
  puts("PASS: all orbits stay inside the 20-unit icicle ring; NTSC/PAL origins agree.");
  return 0;
}
