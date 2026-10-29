// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_COLLECTION_V2_H
#define LUNA_COLLECTION_V2_H

// Horizontal composition adjustment, in the original 640-wide scene units.
enum { COLLECTION_V2_LEFT_SHIFT = 48 };

typedef struct {
  float x, y, width, height, opacity;
} CollectionV2Pose;

// Position is in cover slots: zero is focus, negative is toward the viewer.
int collectionV2Pose(float position, int width, int height, CollectionV2Pose *pose);

#endif
