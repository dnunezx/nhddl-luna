// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_FAVORITES_H
#define LUNA_FAVORITES_H

#include "target.h"
#include <stddef.h>
#include <stdint.h>

int loadFavoriteFlags(TargetList *titles, uint8_t *flags, size_t flagCount);
int saveFavoriteFlags(TargetList *titles, const uint8_t *flags, size_t flagCount,
                      Target *selectedTitle);
TargetList *buildFavoriteTargetList(TargetList *titles, const uint8_t *flags,
                                    size_t flagCount);
// Filter indexes differ from canonical Target.idx. Updates allocate nothing.
int favoriteTargetIndex(const TargetList *favorites, int originalIndex);
int setFavoriteTarget(TargetList *favorites, Target *source, int enabled);

#endif
