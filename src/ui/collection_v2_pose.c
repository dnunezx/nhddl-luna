// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/collection_v2.h"
#include <math.h>

// Reimplemented from the verified game controller's nine NPOS declarations.
// Camera virtual screen: 640x480, projection distance 500; album plane: 256.
// Keep this spatial evaluator independent of LUNA's existing animation clocks.
static void node(int index, float *x, float *z) {
  float u = index - 1;
  *x = -20.0f * u * u;
  *z = u < 0 ? -380.0f * u : -400.0f * u;
}

static void tangent(int index, float *x, float *z) {
  float ax, az, bx, bz;
  node((index + 8) % 9, &ax, &az);
  node((index + 1) % 9, &bx, &bz);
  // Native PredictorNodeVec uses half the adjacent-node displacement.
  // Its frame interval cancels ResetHermite's reciprocal interval; open-path
  // endpoint vectors are multiplied by 0.0001 in the native predictor.
  float factor = (index == 0 || index == 8) ? 0.00005f : 0.5f;
  *x = (bx - ax) * factor;
  *z = (bz - az) * factor;
}

static float opacity(int index) {
  if (index == 1) return 1.0f;
  if (index == 0 || index == 8) return 0.15f;
  // general_menu's one visible item and margins (1,7): nonfocused items
  // use its 0.15 floor and 0.75 idle multiplier, along the rear margin.
  float t = index / 8.0f;
  return (0.15f + 0.85f * (1.0f - t) / 0.875f) * 0.75f;
}

int collectionV2Pose(float position, int width, int height, CollectionV2Pose *pose) {
  if (!pose || width <= 0 || height <= 0 || !isfinite(position) ||
      position < -1.0f || position >= 7.0f)
    return 0;
  float parameter = position + 1.0f;
  int segment = (int)parameter;
  float t = parameter - segment;
  float ax, az, bx, bz, vx, vz, wx, wz;
  node(segment, &ax, &az);
  node(segment + 1, &bx, &bz);
  tangent(segment, &vx, &vz);
  tangent(segment + 1, &wx, &wz);
  float t2 = t * t, t3 = t2 * t;
  float a = 2 * t3 - 3 * t2 + 1, b = -2 * t3 + 3 * t2;
  float c = t3 - 2 * t2 + t, d = t3 - t2;
  float x = a * ax + b * bx + c * vx + d * wx;
  float z = a * az + b * bz + c * vz + d * wz;
  float projection = 500.0f / (500.0f - z);
  float sx = width / 640.0f, sy = height / 480.0f;
  // Enlarge the focal jacket by 15%, around its existing center. Blend only
  // across its adjacent slots using the current glide position, with no new
  // animation clock or change to the recovered path or distant jacket sizes.
  float focus = fmaxf(0.0f, 1.0f - fabsf(position));
  float focalScale = 1.0f + 0.15f * focus * focus * (3.0f - 2.0f * focus);
  pose->width = 256.0f * projection * sx * focalScale;
  pose->height = 256.0f * projection * sy * focalScale;
  // Preserve the recovered path and projection, then shift the whole stream
  // left by 7.5% of screen width for LUNA's more centered composition.
  pose->x = (320.0f - COLLECTION_V2_LEFT_SHIFT + (168.0f + x) * projection) * sx - pose->width * 0.5f;
  pose->y = height * 0.5f - pose->height * 0.5f;
  pose->opacity = opacity(segment) * (1 - t) + opacity(segment + 1) * t;
  return pose->x < width && pose->x + pose->width > 0;
}
