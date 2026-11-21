#include "homebrew_launch.h"
#include "homebrew_elf.h"
#include "devices/devices.h"
#include "ui/ambient.h"
#include "ui/art_cache.h"
#include "ui/graphics.h"
#include "ui/handoff.h"
#include "ui/pad.h"
#include <dmaKit.h>
#include <kernel.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  LunaHomebrewElf elf;
  uint32_t source, argv[3],argc;
  char path[PATH_MAX + 1];
  char argument[129];
} HomebrewControl;
// The existing resident argument workspace survives replacement of application RAM.
static HomebrewControl *control;
extern void luna_homebrew_transfer(HomebrewControl *) __attribute__((noreturn));
_Static_assert(offsetof(HomebrewControl, source) == 524, "homebrew source offset");
_Static_assert(offsetof(HomebrewControl, argv) == 528, "homebrew argv offset");
_Static_assert(offsetof(HomebrewControl, argc) == 540, "homebrew argc offset");
static void *stagedFile;

void lunaHomebrewDiscard(void) {
  free(stagedFile);
  stagedFile = NULL;
}

const char *lunaHomebrewPrepare(const char *path, LunaHomebrewProgress progress, void *context) {
  return lunaHomebrewPrepareWithArgument(path,NULL,progress,context);
}
const char *lunaHomebrewPrepareWithArgument(const char *path,const char *argument,
                                          LunaHomebrewProgress progress,void *context) {
  lunaHomebrewDiscard();
  control = lunaElfLaunchWorkspace(sizeof(*control));
  if (!control) return "Not enough memory to prepare this application.";
  if (!path || !*path || strlen(path) > PATH_MAX) return "The current path is too long.";
  if(argument && strlen(argument)>=sizeof(control->argument))return "Launch arguments are too large. Reduce selected cheats or custom arguments.";
  FILE *file = fopen(path, "rb");
  if (!file) return "Could not open the selected ELF.";
  const char *error = NULL;
  long length = -1;
  if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET))
    error = "Could not read the selected ELF.";
  else if (length < 52 || (unsigned long)length > LUNA_HOMEBREW_FILE_MAX)
    error = "The ELF is empty, truncated, or larger than 16 MiB.";
  else if (!(stagedFile = malloc((size_t)length)))
    error = "Not enough memory to prepare this application.";
  size_t completed = 0;
  while (!error && completed < (size_t)length) {
    if (progress && progress(completed, (size_t)length, context)) {
      error = "Application launch cancelled.";
      break;
    }
    size_t chunk = (size_t)length - completed;
    if (chunk > 32768) chunk = 32768;
    if (fread((unsigned char *)stagedFile + completed, 1, chunk, file) != chunk)
      error = "Could not read the selected ELF.";
    else completed += chunk;
  }
  if (!error && (fgetc(file) != EOF || ferror(file))) error = "The ELF changed while being read.";
  if (fclose(file) && !error) error = "Could not read the selected ELF.";
  if (!error) error = lunaHomebrewCheck(stagedFile, completed, (uint32_t)(uintptr_t)stagedFile,
                                       (uint32_t)GetMemorySize(), &control->elf);
  if (error) { lunaHomebrewDiscard(); return error; }
  control->source = (uint32_t)(uintptr_t)stagedFile;
  snprintf(control->path, sizeof(control->path), "%s", path);
  control->argv[0] = (uint32_t)(uintptr_t)control->path;
  snprintf(control->argument,sizeof(control->argument),"%s",argument?argument:"");
  control->argc=control->argument[0]?2:1;
  control->argv[1]=control->argument[0]?(uint32_t)(uintptr_t)control->argument:0;
  control->argv[2]=0;
  return NULL;
}

void lunaHomebrewExecute(void) {
  if (!stagedFile) __builtin_trap();
  uiPlayLaunchTransition();
  ambientStop();
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  dmaKit_wait_fast();
  artCacheShutdown();
  closePad();
  syncDeviceMap();
  FlushCache(0);
  FlushCache(2);
  TerminateLibrary();
  DIntr();
  luna_homebrew_transfer(control);
}
