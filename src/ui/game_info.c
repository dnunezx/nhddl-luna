// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/game_info.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *trim(char *text) {
  while (isspace((unsigned char)*text)) text++;
  size_t length = strlen(text);
  while (length && isspace((unsigned char)text[length - 1])) text[--length] = '\0';
  return text;
}

static void copyValue(char *destination, size_t capacity, const char *value) {
  size_t length = strlen(value);
  if (length >= capacity) {
    length = capacity - 1;
    // Do not cut a UTF-8 character in half at the buffer boundary.
    while (length && ((unsigned char)value[length] & 0xc0) == 0x80) length--;
  }
  memcpy(destination, value, length);
  destination[length] = '\0';
  for (size_t i = 0; i < length; i++)
    if ((unsigned char)destination[i] < 0x20) destination[i] = ' ';
}

int lunaReadGameInfo(const char *path, LunaGameInfo *info) {
  memset(info, 0, sizeof(*info));
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  char line[2048];
  char fallbackPlayers[sizeof(info->players)] = {0};
  size_t bytes = 0;
  int firstLine = 1;
  while (fgets(line, sizeof(line), file)) {
    bytes += strlen(line);
    // Bound both memory and I/O even for a malformed local CFG.
    if (bytes > 65536) break;
    if (!strchr(line, '\n') && !feof(file)) {
      int c;
      while ((c = fgetc(file)) != EOF && c != '\n' && ++bytes <= 65536) {}
      if (bytes > 65536) break;
      continue; // Discard an oversized line, including its continuation.
    }
    char *key = line;
    if (firstLine && !strncmp(key, "\xef\xbb\xbf", 3)) key += 3;
    firstLine = 0;
    key = trim(key);
    if (*key == '#' || *key == ';') continue;
    char *value = strchr(key, '=');
    if (!value) continue;
    *value++ = '\0';
    key = trim(key);
    value = trim(value);
    if (!strcmp(key, "Release")) copyValue(info->release, sizeof(info->release), value);
    else if (!strcmp(key, "Developer")) copyValue(info->developer, sizeof(info->developer), value);
    else if (!strcmp(key, "Genre")) copyValue(info->genre, sizeof(info->genre), value);
    else if (!strcmp(key, "Description")) copyValue(info->description, sizeof(info->description), value);
    else if (!strcmp(key, "PlayersText")) copyValue(info->players, sizeof(info->players), value);
    else if (!strcmp(key, "Players")) {
      if (!strncmp(value, "players/", 8)) value += 8;
      copyValue(fallbackPlayers, sizeof(fallbackPlayers), value);
    }
  }
  int failed = ferror(file);
  if (fclose(file)) failed = 1;
  if (failed) { memset(info, 0, sizeof(*info)); return 0; }
  if (!info->players[0]) copyValue(info->players, sizeof(info->players), fallbackPlayers);
  return info->release[0] || info->developer[0] || info->genre[0] ||
         info->players[0] || info->description[0];
}
