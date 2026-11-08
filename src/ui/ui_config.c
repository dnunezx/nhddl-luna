#include "ui/ui_config.h"
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_DEFAULTS {1000,1500,1000,2000,100,100,500,500,1440,1440,150}
LunaUIConfig lunaUIConfig = UI_DEFAULTS;
static const LunaUIConfig defaults = UI_DEFAULTS;
static const struct { const char *name; size_t offset; unsigned maximum; } fields[] = {
  {"start_ring_fade_ms", offsetof(LunaUIConfig, start_ring_fade_ms), 5000},
  {"start_label_delay_ms", offsetof(LunaUIConfig, start_label_delay_ms), 5000},
  {"start_label_fade_ms", offsetof(LunaUIConfig, start_label_fade_ms), 5000},
  {"start_close_ms", offsetof(LunaUIConfig, start_close_ms), 5000},
  {"start_orb_brightness_percent", offsetof(LunaUIConfig, start_orb_brightness_percent), 100},
  {"start_burst_brightness_percent", offsetof(LunaUIConfig, start_burst_brightness_percent), 100},
  {"menu_open_ms", offsetof(LunaUIConfig, menu_open_ms), 5000},
  {"menu_close_ms", offsetof(LunaUIConfig, menu_close_ms), 5000},
  {"submenu_open_ms", offsetof(LunaUIConfig, submenu_open_ms), 5000},
  {"submenu_close_ms", offsetof(LunaUIConfig, submenu_close_ms), 5000},
  {"menu_move_ms", offsetof(LunaUIConfig, menu_move_ms), 2000},
};

static char *trim(char *text) {
  while (isspace((unsigned char)*text)) text++;
  char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) *--end = 0;
  return text;
}

static void parseLine(LunaUIConfig *config, char *line) {
  char *comment = strpbrk(line, "#;");
  if (comment) *comment = 0;
  char *equals = strchr(line, '=');
  if (!equals) return;
  *equals++ = 0;
  char *key = trim(line), *value = trim(equals), *end;
  if (!isdigit((unsigned char)*value)) return;
  errno = 0;
  unsigned long number = strtoul(value, &end, 10);
  if (errno || *end) return;
  for (size_t i = 0; i < sizeof(fields)/sizeof(fields[0]); i++) {
    if (!strcmp(key, fields[i].name)) {
      if (number <= fields[i].maximum)
        *(unsigned *)((char *)config + fields[i].offset) = (unsigned)number;
      return;
    }
  }
}

void lunaUIConfigure(const char *directory) {
  lunaUIConfig = defaults;
  if (!directory || !*directory) return;
  char path[1024];
  size_t length = strlen(directory);
  int count = snprintf(path, sizeof(path), "%s%sui.cfg", directory,
      directory[length-1] == '/' || directory[length-1] == ':' ? "" : "/");
  if (count < 0 || (size_t)count >= sizeof(path)) return;
  FILE *file = fopen(path, "rb");
  if (!file) return;
  LunaUIConfig config = defaults;
  char line[256];
  size_t used = 0, total = 0;
  int byte, badLine = 0;
  /* Byte-wise assembly rejects embedded NULs and whole overlong lines.
   * A bounded file keeps startup work predictable on slow storage. */
  while ((byte = fgetc(file)) != EOF && ++total <= 16384) {
    if (byte == '\n') {
      line[used] = 0;
      if (!badLine) parseLine(&config, line);
      used = 0; badLine = 0;
    } else if (!byte || used == sizeof(line)-1) badLine = 1;
    else if (!badLine) line[used++] = (char)byte;
  }
  line[used] = 0;
  if (!badLine) parseLine(&config, line);
  int valid = !ferror(file) && total <= 16384;
  if (fclose(file)) valid = 0;
  if (valid) lunaUIConfig = config;
}

float lunaUITimeStep(float elapsed, unsigned duration, float authoredDuration) {
  return duration ? elapsed * authoredDuration / duration : authoredDuration;
}
float lunaUIProgress(float elapsed, unsigned duration) {
  if (elapsed < 0) return 0;
  if (!duration || elapsed >= duration) return 1;
  return elapsed / duration;
}
