#include "genres.h"
#include "devices/devices.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void normal(const char *input, const char *expected) {
  char result[LUNA_GENRE_SIZE];
  lunaNormalizeGenre(input, result);
  assert(!strcmp(result, expected));
}

int main(void) {
  normal(NULL, "Uncategorized"); normal("", "Uncategorized");
  normal(" Role-playing games ", "RPG"); normal("ACTION / Adventure", "Action");
  normal("survival_horror", "Horror"); normal("driving", "Racing");
  normal("Homebrew", "Homebrew"); normal("n/a", "Uncategorized");
  normal("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz", "Uncategorized");
  normal("\xc3\xa9", "Uncategorized");
  char result[LUNA_GENRE_SIZE];
  FILE *cfg = tmpfile(); assert(cfg);
  fputs("\xef\xbb\xbfTitle=Test\r\n$Compatibility=32\r\n #gEnRe = Role playing games\r\n", cfg);
  rewind(cfg); assert(lunaReadGenre(cfg, result)); assert(!strcmp(result, "RPG")); fclose(cfg);
  cfg = tmpfile(); assert(cfg);
  for (int i = 0; i < 1023; i++) fputc('x', cfg);
  fputs("Genre=Action\nGenre=Driving", cfg);
  rewind(cfg); assert(lunaReadGenre(cfg, result)); assert(!strcmp(result, "Racing")); fclose(cfg);

  char root[] = "/tmp/luna-genres-XXXXXX"; assert(mkdtemp(root));
  char directory[256], path[256];
  snprintf(directory, sizeof(directory), "%s/CFG", root); assert(!mkdir(directory, 0700));
  snprintf(path, sizeof(path), "%s/CFG/SLUS_203.12.cfg", root);
  cfg = fopen(path, "w"); assert(cfg); fputs("Genre=Role playing games\n", cfg); fclose(cfg);
  struct DeviceMapEntry metadata = {.mountpoint = root};
  struct DeviceMapEntry device = {.mountpoint = "/missing", .metadev = &metadata};
  Target a = {.idx = 8, .name = "Zulu", .id = "SLUS_203.12", .device = &device};
  Target b = {.idx = 2, .name = "Alpha", .id = "SLUS_203.12", .device = &device};
  Target c = {.idx = 4, .name = "Missing", .id = "../../escape", .device = &device};
  a.next = &b; b.next = &c;
  TargetList titles = {.total = 3, .first = &a, .last = &c};
  assert(lunaLoadLibraryGenres(&titles) == 2);
  LunaGenreIndex *index = titles.genres;
  assert(index->groupCount == 2 && index->total == 3);
  assert(!strcmp(index->groups[0].name, "RPG"));
  assert(!strcmp(index->groups[1].name, "Uncategorized"));
  assert(lunaGenreGroupTarget(index, 0, 0, NULL, 0) == &b);
  assert(lunaGenreGroupTarget(index, 0, 1, NULL, 0) == &a);
  assert(a.next == &b && a.idx == 8); // Launch ordering and identity stay intact.
  uint8_t favorites[9] = {0}; favorites[8] = 1;
  assert(lunaGenreGroupCount(index, 0, favorites, sizeof(favorites)) == 1);
  assert(lunaGenreGroupTarget(index, 0, 0, favorites, sizeof(favorites)) == &a);
  assert(!lunaGenreGroupTarget(index, 0, 1, favorites, sizeof(favorites)));
  assert(!lunaGenreGroupTarget(index, -1, 0, NULL, 0));
  assert(!lunaGenreGroupCount(index, 0, favorites, 3));
  cfg = fopen(path, "w"); assert(cfg); fputs("Genre=Racing\n", cfg); fclose(cfg);
  assert(lunaLoadLibraryGenres(&titles) == 2); // Refresh re-reads edited metadata.
  assert(!strcmp(titles.genres->groups[0].name, "Racing"));
  lunaFreeGenreIndex(titles.genres);
  TargetList empty = {0}; index = lunaBuildGenreIndex(&empty);
  assert(index && index->groupCount == 0); lunaFreeGenreIndex(index);
  assert(!unlink(path)); assert(!rmdir(directory)); assert(!rmdir(root));
  puts("genre metadata and grouping tests passed");
  return 0;
}
