#include "ui/game_options.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Argument *getArgument(ArgumentList *list, const char *name) {
  for (Argument *argument = list->first; argument != NULL; argument = argument->next)
    if (!strcmp(argument->arg, name))
      return argument;
  return NULL;
}

Argument *insertArgument(ArgumentList *list, const char *name, char *value) {
  Argument *argument = calloc(1, sizeof(*argument));
  assert(argument != NULL);
  argument->arg = strdup(name);
  argument->value = strdup(value);
  argument->prev = list->last;
  if (list->last != NULL)
    list->last->next = argument;
  else
    list->first = argument;
  list->last = argument;
  list->total++;
  return argument;
}

static void freeArguments(ArgumentList *list) {
  Argument *argument = list->first;
  while (argument != NULL) {
    Argument *next = argument->next;
    free(argument->arg);
    free(argument->value);
    free(argument);
    argument = next;
  }
}

int main(void) {
  ArgumentList arguments = {0};
  Argument *compat = insertArgument(&arguments, "gc", "027");
  compat->isGlobal = 1;
  Argument *video = insertArgument(&arguments, "gsm", "fp2:2");
  Argument *logo = insertArgument(&arguments, "logo", "");
  logo->isGlobal = 1;
  Argument *debug = insertArgument(&arguments, "dbc", "");
  debug->isDisabled = 1;
  debug->isGlobal = 1;
  Argument *custom = insertArgument(&arguments, "custom_arg", "keep-me");

  LunaGameOptions options;
  lunaGameOptionsRead(&options, &arguments);
  assert(options.compat == ((1 << 0) | (1 << 1) | (1 << 4)));
  assert(options.videoMode == 2 && options.fieldFlip == 2);
  assert(options.ps2Logo && !options.debugColors);

  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_FAST_READS, 1));
  assert(!strcmp(compat->value, "27") && !compat->isGlobal);
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_FIELD_FLIP, 1));
  assert(!strcmp(video->value, "fp2:3"));
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_PS2_LOGO, 1));
  assert(logo->isDisabled && logo->isGlobal);
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_DEBUG_COLORS, 1));
  assert(!debug->isDisabled && !debug->isGlobal);
  assert(!strcmp(custom->value, "keep-me") && !custom->isDisabled);

  lunaGameOptionsRead(&options, &arguments);
  assert(options.videoMode == 2 && options.fieldFlip == 3);
  assert(!options.ps2Logo && options.debugColors);
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_VIDEO_MODE, -1));
  assert(!strcmp(video->value, "fp1:3"));

  free(video->value);
  video->value = strdup("vendor-mode");
  lunaGameOptionsRead(&options, &arguments);
  assert(options.videoMode == -1);
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_PS2_LOGO, 1));
  assert(!strcmp(video->value, "vendor-mode"));

  freeArguments(&arguments);
  puts("game options: ok");
  return 0;
}
