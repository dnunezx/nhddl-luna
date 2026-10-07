#include "cheats.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

static int fail(char *error, size_t size, int line, const char *message) {
  if (error && size) {
    if (line) snprintf(error, size, "Line %d: %s", line, message);
    else snprintf(error, size, "%s", message);
  }
  return -EINVAL;
}

static char *trim(char *s) {
  while (isspace((unsigned char)*s)) s++;
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
  return s;
}

static int hexword(const char *s, uint32_t *word) {
  uint32_t value = 0;
  for (int i = 0; i < 8; i++) {
    unsigned char c = (unsigned char)s[i];
    if (!c || !isxdigit(c)) return 0;
    value = (value << 4) | (c <= '9' ? c - '0' : (tolower(c) - 'a' + 10));
  }
  *word = value;
  return 1;
}

static int requiredName(const char *name) {
  char text[LUNA_CHEAT_NAME_MAX + 1];
  size_t i;
  for (i = 0; name[i]; i++) text[i] = (char)tolower((unsigned char)name[i]);
  text[i] = 0;
  return !strcmp(text, "mastercode") || !strcmp(text, "master code") ||
         !strncmp(text, "master code (", 13) || !strncmp(text, "mastercode (", 12) ||
         !strcmp(text, "enable code") || !strncmp(text, "enable code (", 13) || !strcmp(text, "(m)") ||
         !strcmp(text, "[master]") || !strcmp(text, "[enable]");
}

static uint64_t hashByte(uint64_t hash, unsigned char byte) {
  return (hash ^ byte) * UINT64_C(1099511628211);
}

static int readCheatLine(FILE *stream, char *buffer, size_t size, int *bytes,
                          const char **problem) {
  size_t used = 0;
  int c;
  while ((c = fgetc(stream)) != EOF) {
    if (++*bytes > 256 * 1024) { *problem = "Cheat file exceeds 256 KB"; return -1; }
    if (c == '\n') break;
    if (!c || (c < 32 && c != '\t' && c != '\r')) {
      *problem = "Cheat file contains non-text data"; return -1;
    }
    if (used + 1 == size) { *problem = "Line is too long"; return -1; }
    buffer[used++] = (char)c;
  }
  buffer[used] = 0;
  return c == EOF && !used ? 0 : 1;
}

void lunaCheatFileFree(LunaCheatFile *file) {
  free(file->entries);
  free(file->pairs);
  memset(file, 0, sizeof(*file));
}

static const char *validateEntry(const LunaCheatFile *file, const LunaCheatEntry *entry) {
  // Verify complete instructions inside each selectable block, after hooks are
  // removed by the engine. A continuation must never come from another cheat.
  for (int p = 0; p < entry->pairCount; p++) {
    LunaCheatPair pair = file->pairs[entry->firstPair + p];
    unsigned type = pair.address >> 28;
    if ((pair.address & 0xfe000000U) == 0x90000000U) continue;
    if (type == 8 || type == 9 || type == 10 || type == 11 || type == 15)
      return "Unsupported code type; use raw PS2RD codes";
    int continuation = type == 4 || type == 5 || type == 6 ||
        (type == 3 && (pair.address & 0x00600000U) == 0x00400000U);
    if (continuation) {
      if (++p == entry->pairCount) return "Incomplete multiline cheat entry";
      // SetupCheats protects 3/4 continuations from hook classification. The
      // 5/6 continuation formats have zero in their high bits instead.
      if ((type == 5 || type == 6) &&
          (file->pairs[entry->firstPair + p].address & 0xfe000000U) == 0x90000000U)
        return "Invalid multiline continuation";
    } else if (type == 13 || type == 14) {
      unsigned following = type == 13 ? pair.value >> 24 : (pair.address >> 16) & 0xff;
      if (!following) following = 1;
      int available = 0, nextHook = 1;
      for (int next = p + 1; next < entry->pairCount; next++) {
        uint32_t address = file->pairs[entry->firstPair + next].address;
        if ((address & 0xfe000000U) != 0x90000000U || !nextHook) available++;
        nextHook = (address & 0xf0000000U) != 0x40000000U &&
                   (address & 0xf0000000U) != 0x30000000U;
      }
      if (following > (unsigned)available) return "Conditional extends outside its cheat entry";
    }
  }
  return NULL;
}

int lunaCheatParse(FILE *stream, LunaCheatFile *file, char *error, size_t size) {
  memset(file, 0, sizeof(*file));
  if (error && size) error[0] = 0;
  file->entries = calloc(LUNA_CHEAT_MAX_ENTRIES, sizeof(*file->entries));
  file->pairs = calloc(LUNA_CHEAT_MAX_PAIRS, sizeof(*file->pairs));
  if (!file->entries || !file->pairs) {
    lunaCheatFileFree(file);
    return fail(error, size, 0, "Not enough memory for cheats");
  }
  char buffer[512];
  int line = 0, bytes = 0, current = -1;
  const char *problem = NULL;
  while (1) {
    int read = readCheatLine(stream, buffer, sizeof(buffer), &bytes, &problem);
    if (!read) break;
    line++;
    if (read < 0) break;
    size_t length = strlen(buffer);
    char *s = buffer;
    if (line == 1 && length >= 3 && !memcmp(s, "\xef\xbb\xbf", 3)) s += 3;
    char *comment = strchr(s, '#'), *slash = strstr(s, "//");
    if (slash && (!comment || slash < comment)) comment = slash;
    if (comment) *comment = 0;
    s = trim(s);
    if (!*s) continue;
    LunaCheatPair pair;
    int isPair = 0;
    if (strlen(s) >= 16 && hexword(s, &pair.address)) {
      const char *value = s + 8;
      while (isspace((unsigned char)*value)) value++;
      if (strlen(value) == 8 && hexword(value, &pair.value)) isPair = 1;
    }
    if (!isPair) {
      // Numeric-looking lines are errors, rather than accidental new labels.
      int digits = 0;
      while (isxdigit((unsigned char)s[digits])) digits++;
      int onlyCodeChars = 1;
      for (const char *p = s; *p; p++)
        if (!isxdigit((unsigned char)*p) && !isspace((unsigned char)*p)) onlyCodeChars = 0;
      if (digits >= 8 || (isdigit((unsigned char)*s) && onlyCodeChars)) {
        problem = "Expected two 8-digit raw hexadecimal words"; break;
      }
      if (current >= 0 && !file->entries[current].pairCount) {
        // PS2RD files may begin with a game-title heading before the first label.
        if (current == 0 && file->pairCount == 0) {
          if (strlen(s) > LUNA_CHEAT_NAME_MAX) { problem = "Cheat name too long"; break; }
          strcpy(file->entries[0].name, s);
          file->entries[0].required = requiredName(s);
          file->entries[0].sourceLine = line;
          continue;
        }
        problem = "Cheat entry has no codes"; break;
      }
      if (strlen(s) > LUNA_CHEAT_NAME_MAX || file->entryCount == LUNA_CHEAT_MAX_ENTRIES) {
        problem = "Too many entries or cheat name too long"; break;
      }
      current = file->entryCount++;
      LunaCheatEntry *entry = &file->entries[current];
      strcpy(entry->name, s);
      entry->firstPair = file->pairCount;
      entry->required = requiredName(s);
      entry->sourceLine = line;
      continue;
    }
    if (!pair.address && !pair.value) { problem = "Zero pair terminates the cheat engine"; break; }
    if (file->pairCount == LUNA_CHEAT_MAX_PAIRS) { problem = "Too many code lines"; break; }
    if (current < 0) {
      current = file->entryCount++;
      strcpy(file->entries[current].name, "Cheat codes");
      file->entries[current].sourceLine = line;
    }
    file->pairs[file->pairCount++] = pair;
    file->entries[current].pairCount++;
  }
  if (!problem && ferror(stream)) problem = "Could not read cheat file";
  if (!problem && (!file->entryCount || !file->entries[file->entryCount - 1].pairCount))
    problem = "Cheat file has no codes or ends with an empty entry";
  if (!problem) {
    for (int e = 0; e < file->entryCount; e++) {
      LunaCheatEntry *entry = &file->entries[e];
      problem = validateEntry(file, entry);
      if (problem) { line = entry->sourceLine; break; }
      int allHooks = 1;
      for (int p = 0; p < entry->pairCount; p++)
        if ((file->pairs[entry->firstPair + p].address & 0xfe000000U) != 0x90000000U) allHooks = 0;
      if (allHooks) entry->required = 1;
      uint64_t hash = UINT64_C(14695981039346656037);
      for (const char *s = entry->name; *s; s++) hash = hashByte(hash, (unsigned char)*s);
      hash = hashByte(hash, 0);
      for (int p = 0; p < entry->pairCount; p++) {
        LunaCheatPair pair = file->pairs[entry->firstPair + p];
        for (int shift = 24; shift >= 0; shift -= 8) hash = hashByte(hash, pair.address >> shift);
        for (int shift = 24; shift >= 0; shift -= 8) hash = hashByte(hash, pair.value >> shift);
      }
      entry->id = hash;
      for (int prior = 0; prior < e; prior++) {
        if (file->entries[prior].id == hash) { problem = "Duplicate cheat entry"; break; }
      }
      if (problem) { line = entry->sourceLine; break; }
    }
  }
  if (problem) {
    lunaCheatFileFree(file);
    return fail(error, size, line, problem);
  }
  return 0;
}

int lunaCheatSelected(const LunaCheatSettings *settings, uint64_t id) {
  for (int i = 0; i < settings->count; i++) if (settings->ids[i] == id) return 1;
  return 0;
}

int lunaCheatToggle(LunaCheatSettings *settings, uint64_t id) {
  for (int i = 0; i < settings->count; i++) {
    if (settings->ids[i] == id) {
      memmove(&settings->ids[i], &settings->ids[i + 1],
              (settings->count - i - 1) * sizeof(settings->ids[0]));
      settings->count--;
      return 1;
    }
  }
  if (settings->count == LUNA_CHEAT_MAX_ENTRIES) return 0;
  settings->ids[settings->count++] = id;
  return 1;
}

int lunaCheatPayload(const LunaCheatFile *file, const LunaCheatSettings *settings,
                     char **payload, char *error, size_t size) {
  *payload = NULL;
  if (!settings->enabled || !settings->count) return 0;
  for (int i = 0; i < settings->count; i++) {
    int found = 0;
    for (int e = 0; e < file->entryCount; e++)
      if (file->entries[e].id == settings->ids[i] && !file->entries[e].required) found = 1;
    if (!found) return fail(error, size, 0, "Cheat file changed. Open Cheats and review selections.");
  }
  char *out = malloc(LUNA_CHEAT_PAYLOAD_MAX);
  if (!out) return fail(error, size, 0, "Not enough memory for cheats");
  strcpy(out, "1:");
  int hooks = 0, codes = 0, nextHook = 1, used = 2;
  for (int e = 0; e < file->entryCount; e++) {
    const LunaCheatEntry *entry = &file->entries[e];
    if (!entry->required && !lunaCheatSelected(settings, entry->id)) continue;
    for (int p = 0; p < entry->pairCount; p++) {
      LunaCheatPair pair = file->pairs[entry->firstPair + p];
      if ((pair.address & 0xfe000000U) == 0x90000000U && nextHook) hooks++;
      else codes++;
      nextHook = (pair.address & 0xf0000000U) != 0x40000000U &&
                 (pair.address & 0xf0000000U) != 0x30000000U;
      if (hooks > LUNA_CHEAT_MAX_HOOKS || codes > LUNA_CHEAT_MAX_CODES) {
        free(out);
        return fail(error, size, 0, "Selection exceeds 5 hooks or 250 code lines");
      }
      snprintf(out + used, 17, "%08" PRIX32 "%08" PRIX32, pair.address, pair.value);
      used += 16;
    }
  }
  if (!hooks) {
    free(out);
    return fail(error, size, 0, "Selected cheats need a raw PS2RD master/enable hook in the .cht file");
  }
  *payload = out;
  return 0;
}

int lunaCheatSettingsRead(FILE *stream, LunaCheatSettings *settings) {
  LunaCheatSettings read = {0};
  char line[80];
  if (!fgets(line, sizeof(line), stream) || strcmp(trim(line), "LUNA_CHEATS_1")) return -EINVAL;
  if (!fgets(line, sizeof(line), stream)) return -EINVAL;
  char *s = trim(line);
  if (!strcmp(s, "enabled=1")) read.enabled = 1;
  else if (strcmp(s, "enabled=0")) return -EINVAL;
  while (fgets(line, sizeof(line), stream)) {
    s = trim(line);
    if (!*s) continue;
    uint32_t hi, lo;
    if (strncmp(s, "selected=", 9) || strlen(s + 9) != 16 ||
        !hexword(s + 9, &hi) || !hexword(s + 17, &lo)) return -EINVAL;
    uint64_t id = ((uint64_t)hi << 32) | lo;
    if (read.count == LUNA_CHEAT_MAX_ENTRIES || lunaCheatSelected(&read, id)) return -EINVAL;
    read.ids[read.count++] = id;
  }
  if (ferror(stream)) return -EIO;
  *settings = read;
  return 0;
}

int lunaCheatSettingsWrite(FILE *stream, const LunaCheatSettings *settings) {
  if (fprintf(stream, "LUNA_CHEATS_1\nenabled=%d\n", !!settings->enabled) < 0) return -EIO;
  for (int i = 0; i < settings->count; i++)
    if (fprintf(stream, "selected=%016" PRIX64 "\n", settings->ids[i]) < 0) return -EIO;
  return fflush(stream) ? -EIO : 0;
}
