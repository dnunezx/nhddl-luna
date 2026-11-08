#define _GNU_SOURCE
#include "ui/ui_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char directory[] = "/tmp/luna-ui-XXXXXX";
static char path[256];
static void writeConfig(const char *bytes, size_t count) {
  FILE *file = fopen(path, "wb"); assert(file);
  assert(fwrite(bytes, 1, count, file) == count);
  assert(!fclose(file));
  lunaUIConfigure(directory);
}
#define CONFIG(text) writeConfig(text, sizeof(text)-1)
int main(void) {
  assert(mkdtemp(directory));
  snprintf(path, sizeof(path), "%s/ui.cfg", directory);
  lunaUIConfigure(directory);
  assert(lunaUIConfig.start_label_delay_ms == 1500);
  CONFIG(" # comment\r\n start_label_delay_ms = 0 ; instant\r\n"
         "start_ring_fade_ms=5000\nmenu_move_ms=2000\n"
         "start_orb_brightness_percent=35\nstart_burst_brightness_percent=0");
  assert(lunaUIConfig.start_label_delay_ms == 0);
  assert(lunaUIConfig.start_ring_fade_ms == 5000);
  assert(lunaUIConfig.menu_move_ms == 2000);
  assert(lunaUIConfig.start_orb_brightness_percent == 35);
  assert(lunaUIConfig.start_burst_brightness_percent == 0);
  CONFIG("menu_open_ms=80\nmenu_open_ms=-1\nmenu_open_ms=5001\n"
         "menu_open_ms=99999999999999999999999\nmenu_open_ms=5junk\n"
         "menu_open_ms=nan\nmenu_open_ms=\nmenu_open_ms=1.5\n"
         "menu_open_ms=+20\nunknown=99\nstart_orb_brightness_percent=101\n");
  assert(lunaUIConfig.menu_open_ms == 80);
  assert(lunaUIConfig.start_orb_brightness_percent == 100);
  assert(lunaUIConfig.start_label_delay_ms == 1500);
  CONFIG("menu_open_ms=80\nmenu_open_ms=90\nmenu_close_ms=0\n");
  assert(lunaUIConfig.menu_open_ms == 90 && !lunaUIConfig.menu_close_ms);
  CONFIG("menu_open_ms=1\0garbage\nmenu_close_ms=42\n");
  assert(lunaUIConfig.menu_open_ms == 500 && lunaUIConfig.menu_close_ms == 42);
  char longLine[1024]; memset(longLine, ' ', sizeof(longLine));
  memcpy(longLine, "menu_open_ms=1", 13);
  memcpy(longLine+900, "\nmenu_close_ms=41\n", 18);
  writeConfig(longLine, sizeof(longLine));
  assert(lunaUIConfig.menu_open_ms == 500 && lunaUIConfig.menu_close_ms == 41);
  char huge[16385]; memset(huge, '\n', sizeof(huge));
  memcpy(huge, "menu_open_ms=1\n", 14);
  writeConfig(huge, sizeof(huge));
  assert(lunaUIConfig.menu_open_ms == 500);
  lunaUIConfigure(NULL); assert(lunaUIConfig.submenu_open_ms == 1440);
  lunaUIConfigure(""); assert(lunaUIConfig.start_close_ms == 2000);
  assert(lunaUIProgress(-1, 0) == 0);
  assert(lunaUIProgress(0, 0) == 1);
  assert(lunaUIProgress(250, 500) == .5f);
  assert(lunaUIProgress(600, 500) == 1);
  assert(lunaUITimeStep(16, 0, 1440) == 1440);
  assert(lunaUITimeStep(16, 500, 500) == 16);
  assert(lunaUITimeStep(16, 250, 500) == 32);
  unlink(path); rmdir(directory);
  puts("PASS: UI defaults, config validation, reload, bounds and zero-duration timing");
}
