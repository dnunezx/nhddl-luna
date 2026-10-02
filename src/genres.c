#include "genres.h"
#include "devices/devices.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const struct { const char *alias, *name; } aliases[] = {
  {"rpg", "RPG"}, {"role playing", "RPG"}, {"role playing game", "RPG"},
  {"role playing games", "RPG"},
  {"action rpg", "RPG"}, {"jrpg", "RPG"}, {"racing", "Racing"},
  {"driving", "Racing"}, {"race", "Racing"}, {"sport", "Sports"},
  {"sports", "Sports"}, {"shooting", "Shooter"}, {"shooter", "Shooter"},
  {"fps", "Shooter"}, {"first person shooter", "Shooter"},
  {"third person shooter", "Shooter"}, {"platform", "Platformer"},
  {"platformer", "Platformer"}, {"platforming", "Platformer"},
  {"puzzle", "Puzzle"}, {"fighting", "Fighting"}, {"fight", "Fighting"},
  {"action", "Action"}, {"adventure", "Adventure"}, {"strategy", "Strategy"},
  {"simulation", "Simulation"}, {"simulator", "Simulation"},
  {"music", "Rhythm"}, {"rhythm", "Rhythm"}, {"music and rhythm", "Rhythm"},
  {"horror", "Horror"}, {"survival horror", "Horror"}, {"party", "Party"},
  {"other", "Other"}, {"unknown", LUNA_GENRE_UNCATEGORIZED},
  {"unclassified", LUNA_GENRE_UNCATEGORIZED},
  {"uncategorized", LUNA_GENRE_UNCATEGORIZED}, {"n a", LUNA_GENRE_UNCATEGORIZED}
};

static int compareText(const char *a, const char *b) {
  while (*a && *b) {
    int difference = tolower((unsigned char)*a) - tolower((unsigned char)*b);
    if (difference) return difference;
    a++; b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

void lunaNormalizeGenre(const char *value, char output[LUNA_GENRE_SIZE]) {
  char text[LUNA_GENRE_SIZE];
  size_t length = 0;
  int space = 0;
  strcpy(output, LUNA_GENRE_UNCATEGORIZED);
  if (!value) return;
  // A single primary genre: use the first category in compound CFG values.
  // All labels are bounded ASCII so both desktop and PS2 group identically.
  for (; *value && !strchr("/,;|", *value); value++) {
    unsigned char c = (unsigned char)*value;
    if (c == '-' || c == '_' || isspace(c)) { space = length != 0; continue; }
    if (c < 32 || c > 126 || length + (space ? 1 : 0) + 1 >= sizeof(text)) return;
    if (space) text[length++] = ' ';
    space = 0;
    text[length++] = (char)tolower(c);
  }
  text[length] = '\0';
  if (!length || !strcmp(text, "unknown") || !strcmp(text, "n")) return;
  for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
    if (!strcmp(text, aliases[i].alias)) { strcpy(output, aliases[i].name); return; }
  }
  int wordStart = 1;
  for (size_t i = 0; i < length; i++) {
    output[i] = wordStart ? (char)toupper((unsigned char)text[i]) : text[i];
    wordStart = text[i] == ' ';
  }
  output[length] = '\0';
}

int lunaReadGenre(FILE *file, char output[LUNA_GENRE_SIZE]) {
  char line[1024];
  strcpy(output, LUNA_GENRE_UNCATEGORIZED);
  if (!file) return 0;
  int found = 0;
  while (fgets(line, sizeof(line), file)) {
    if (!strchr(line, '\n') && !feof(file)) {
      int c; while ((c = fgetc(file)) != EOF && c != '\n') {}
      continue; // Never interpret fragments of an oversized line as metadata.
    }
    char *key = line;
    if (strlen(key) >= 3 && (unsigned char)key[0] == 0xef && (unsigned char)key[1] == 0xbb &&
        (unsigned char)key[2] == 0xbf) key += 3;
    while (isspace((unsigned char)*key)) key++;
    char *eq = strchr(key, '=');
    if (!eq) continue;
    char *end = eq;
    while (end > key && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    if (*key == '#') key++;
    if (compareText(key, "Genre")) continue;
    lunaNormalizeGenre(eq + 1, output);
    found = 1;
  }
  return found;
}

static int safeID(const char *id) {
  if (!id || strlen(id) != 11 || id[4] != '_' || id[8] != '.') return 0;
  for (int i = 0; i < 11; i++) {
    if (i == 4 || i == 8) continue;
    if (i < 4 ? !isalpha((unsigned char)id[i]) : !isdigit((unsigned char)id[i])) return 0;
  }
  return 1;
}

static int compareTargets(const void *left, const void *right) {
  Target *a = *(Target *const *)left, *b = *(Target *const *)right;
  int aUnknown = !strcmp(a->genre, LUNA_GENRE_UNCATEGORIZED);
  int bUnknown = !strcmp(b->genre, LUNA_GENRE_UNCATEGORIZED);
  if (aUnknown != bUnknown) return aUnknown - bUnknown;
  int difference = compareText(a->genre, b->genre);
  if (!difference) difference = compareText(a->name ? a->name : "", b->name ? b->name : "");
  return difference ? difference : (int)a->idx - (int)b->idx;
}

void lunaFreeGenreIndex(LunaGenreIndex *index) {
  if (!index) return;
  free(index->groups); free(index->targets); free(index);
}

LunaGenreIndex *lunaBuildGenreIndex(TargetList *titles) {
  if (!titles || titles->total < 0) return NULL;
  LunaGenreIndex *index = calloc(1, sizeof(*index));
  if (!index) return NULL;
  if (!titles->total) return index;
  index->targets = calloc((size_t)titles->total, sizeof(*index->targets));
  index->groups = calloc((size_t)titles->total, sizeof(*index->groups));
  if (!index->targets || !index->groups) { lunaFreeGenreIndex(index); return NULL; }
  for (Target *target = titles->first; target && index->total < titles->total; target = target->next) {
    char normalized[LUNA_GENRE_SIZE];
    lunaNormalizeGenre(target->genre, normalized);
    memcpy(target->genre, normalized, strlen(normalized) + 1);
    index->targets[index->total++] = target;
  }
  qsort(index->targets, (size_t)index->total, sizeof(*index->targets), compareTargets);
  for (int i = 0; i < index->total; i++) {
    Target *target = index->targets[i];
    if (!index->groupCount || strcmp(index->groups[index->groupCount - 1].name, target->genre)) {
      LunaGenreGroup *group = &index->groups[index->groupCount++];
      strcpy(group->name, target->genre);
      group->first = i;
      group->selectedTitleIdx = target->idx;
    }
    index->groups[index->groupCount - 1].count++;
  }
  return index;
}

int lunaLoadLibraryGenres(TargetList *titles) {
  if (!titles) return -EINVAL;
  lunaFreeGenreIndex(titles->genres);
  titles->genres = NULL;
  int loaded = 0;
  for (Target *target = titles->first; target; target = target->next) {
    strcpy(target->genre, LUNA_GENRE_UNCATEGORIZED);
    if (!target->device || !safeID(target->id)) continue;
    struct DeviceMapEntry *device = target->device->metadev ? target->device->metadev : target->device;
    if (!device->mountpoint) continue;
    char path[PATH_MAX + 1], id[12];
    for (int i = 0; i < 12; i++) id[i] = (char)toupper((unsigned char)target->id[i]);
    size_t length = strlen(device->mountpoint);
    int written = snprintf(path, sizeof(path), "%s%sCFG/%s.cfg", device->mountpoint,
                           length && device->mountpoint[length - 1] == '/' ? "" : "/", id);
    if (written < 0 || (size_t)written >= sizeof(path)) continue;
    FILE *file = fopen(path, "r");
    if (file) { lunaReadGenre(file, target->genre); fclose(file); }
    if (strcmp(target->genre, LUNA_GENRE_UNCATEGORIZED)) loaded++;
  }
  LunaGenreIndex *index = lunaBuildGenreIndex(titles);
  if (!index) return -ENOMEM;
  titles->genres = index;
  return loaded;
}

static int included(Target *target, const uint8_t *flags, size_t count) {
  return !flags || (target->idx < count && flags[target->idx]);
}

int lunaGenreGroupCount(const LunaGenreIndex *index, int group,
                        const uint8_t *flags, size_t flagCount) {
  if (!index || group < 0 || group >= index->groupCount) return 0;
  const LunaGenreGroup *g = &index->groups[group];
  int count = 0;
  for (int i = 0; i < g->count; i++) count += included(index->targets[g->first + i], flags, flagCount) != 0;
  return count;
}

Target *lunaGenreGroupTarget(const LunaGenreIndex *index, int group, int rank,
                            const uint8_t *flags, size_t flagCount) {
  if (!index || group < 0 || group >= index->groupCount || rank < 0) return NULL;
  const LunaGenreGroup *g = &index->groups[group];
  for (int i = 0; i < g->count; i++) {
    Target *target = index->targets[g->first + i];
    if (included(target, flags, flagCount) && rank-- == 0) return target;
  }
  return NULL;
}
