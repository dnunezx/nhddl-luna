// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_GENRES_H
#define LUNA_GENRES_H
#include "target.h"
#include <stdint.h>
#define LUNA_GENRE_DEFAULTS 12
#define LUNA_GENRE_LIMIT LUNA_GENRE_DEFAULTS
#define LUNA_GENRE_NAME_BYTES 49
#define LUNA_GENRE_UNASSIGNED (-3)
// Genre membership belongs to the metadata drive, using stable playlist keys.
int lunaGenresLoad(TargetList *library);
void lunaGenresClose(void);
int lunaGenreCount(void);
const char *lunaGenreName(int genre);
uint32_t lunaGenreMask(const Target *target);
int lunaGenreSet(Target *target, uint32_t mask);
int lunaGenreGameCount(int genre);
int lunaGenreFilter(TargetList *view, const TargetList *source,
                    TargetFilter platform, const uint8_t *favorites, int genre);
#endif
