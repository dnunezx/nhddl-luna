#ifndef LUNA_GENRES_H
#define LUNA_GENRES_H

#include "target.h"
#include <stddef.h>
#include <stdio.h>

#define LUNA_GENRE_SIZE 48
#define LUNA_GENRE_UNCATEGORIZED "Uncategorized"

typedef struct {
  char name[LUNA_GENRE_SIZE];
  int first, count;
  int selectedTitleIdx; // Canonical Target.idx; available for the future view.
} LunaGenreGroup;

typedef struct LunaGenreIndex {
  int total, groupCount;
  LunaGenreGroup *groups;
  Target **targets; // Borrowed pointers; never reorders the launch library.
} LunaGenreIndex;

void lunaNormalizeGenre(const char *value, char output[LUNA_GENRE_SIZE]);
int lunaReadGenre(FILE *file, char output[LUNA_GENRE_SIZE]);
// Rebuilds metadata and groups. Missing CFGs are ordinary Uncategorized games.
int lunaLoadLibraryGenres(TargetList *titles);
LunaGenreIndex *lunaBuildGenreIndex(TargetList *titles);
void lunaFreeGenreIndex(LunaGenreIndex *index);
// NULL flags includes all titles. Non-NULL flags filters using canonical idx.
int lunaGenreGroupCount(const LunaGenreIndex *index, int group,
                        const uint8_t *flags, size_t flagCount);
Target *lunaGenreGroupTarget(const LunaGenreIndex *index, int group, int rank,
                            const uint8_t *flags, size_t flagCount);

#endif
