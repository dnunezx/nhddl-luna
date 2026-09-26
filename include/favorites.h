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

#endif
