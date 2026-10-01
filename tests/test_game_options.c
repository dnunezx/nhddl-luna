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
  assert(!options.neutrinoIgrDisabled);
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_NEUTRINO_DISABLE_IGR, 1));
  Argument *neutrinoIgr = getArgument(&arguments, "luna_neutrino_disable_igr");
  assert(neutrinoIgr && !neutrinoIgr->isDisabled && options.neutrinoIgrDisabled);
  assert(!strcmp(lunaGameOptionsValue(&options,
                                      LUNA_GAME_NEUTRINO_DISABLE_IGR), "On"));
  lunaGameOptionsRead(&options, &arguments);
  assert(options.neutrinoIgrDisabled);
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_NEUTRINO_DISABLE_IGR, 1));
  assert(neutrinoIgr->isDisabled && !options.neutrinoIgrDisabled);
  assert(options.oplCompat == 0);
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_OPL_ACCURATE_READS, 1));
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_OPL_SKIP_VIDEOS, 1));
  Argument *oplCompat = getArgument(&arguments, "luna_opl_compat");
  assert(oplCompat && !strcmp(oplCompat->value, "9"));
  assert(options.oplCompat == 9);
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_OPL_ACCURATE_READS, 1));
  assert(lunaGameOptionsChange(&options, &arguments,
                               LUNA_GAME_OPL_SKIP_VIDEOS, 1));
  assert(oplCompat->isDisabled && options.oplCompat == 0);
  assert(!options.oplCore && options.coreInherited);
  assert(!strcmp(lunaGameOptionsValue(&options, LUNA_GAME_CORE), "Inherit (Neutrino)"));
  assert(lunaGameOptionsCycleCore(&options, &arguments, 0, 1));
  Argument *core = getArgument(&arguments, "luna_core");
  assert(core != NULL && !core->isGlobal && !strcmp(core->value, "opl"));
  assert(!strcmp(lunaGameOptionsValue(&options, LUNA_GAME_CORE), "OPL"));
  assert(lunaGameOptionsCycleCore(&options, &arguments, 0, 1));
  assert(!core->isGlobal && !strcmp(core->value, "neutrino"));
  assert(lunaGameOptionsCycleCore(&options, &arguments, 0, 1));
  assert(core->isGlobal && !options.oplCore && options.coreInherited);
  assert(lunaApplyGlobalGameCore(&arguments, 1));
  lunaGameOptionsRead(&options, &arguments);
  assert(options.oplCore && options.coreInherited);
  assert(lunaGameOptionsCycleCore(&options, &arguments, 1, 1));
  assert(!core->isGlobal && !strcmp(core->value, "neutrino"));
  assert(!options.oplCore && !options.coreInherited);

  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_FAST_READS, 1));
  assert(!strcmp(compat->value, "27") && !compat->isGlobal);
  assert(lunaGameOptionsChange(&options, &arguments, LUNA_GAME_FIELD_FLIP, 1));
  assert(!strcmp(video->value, "fp2:3"));
  assert(lunaGameOptionsCyclePS2Logo(&options, &arguments, 1, 1));
  assert(logo->isDisabled && !logo->isGlobal);
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
  assert(lunaGameOptionsCyclePS2Logo(&options, &arguments, 1, 1));
  assert(!logo->isDisabled && !logo->isGlobal);
  assert(lunaGameOptionsCyclePS2Logo(&options, &arguments, 1, 1));
  assert(!logo->isDisabled && logo->isGlobal);
  assert(lunaGameOptionsCyclePS2Logo(&options, &arguments, 1, -1));
  assert(!logo->isDisabled && !logo->isGlobal);
  assert(!strcmp(video->value, "vendor-mode"));

  assert(lunaGameOptionsSetVMC(&options, &arguments, 0,
                               "mass0:/VMC/SLUS_123.45_0.bin"));
  Argument *card = getArgument(&arguments, "mc0");
  assert(card != NULL && !card->isDisabled);
  assert(!strcmp(options.vmcSlotLabel[0], "SLUS_123.45_0.bin"));
  assert(lunaGameOptionsSetVMC(&options, &arguments, 1,
                               "mass0:/VMC/shared.bin"));
  assert(!strcmp(options.vmcSlotLabel[1], "shared.bin"));
  assert(lunaGameOptionsSetVMC(&options, &arguments, 0, ""));
  assert(card->isDisabled);
  assert(!strcmp(options.vmcSlotLabel[0], "Physical card"));

  logo->isGlobal = 1;
  assert(lunaApplyGlobalPS2Logo(&arguments, 0));
  assert(logo->isDisabled);
  assert(lunaApplyGlobalPS2Logo(&arguments, 1));
  assert(!logo->isDisabled);
  logo->isGlobal = 0;
  assert(lunaApplyGlobalPS2Logo(&arguments, 0));
  assert(!logo->isDisabled); // Per-game On overrides global Off.
  logo->isDisabled = 1;
  assert(lunaApplyGlobalPS2Logo(&arguments, 1));
  assert(logo->isDisabled); // Per-game Off overrides global On.
  ArgumentList noLogo = {0};
  assert(lunaApplyGlobalPS2Logo(&noLogo, 0));
  assert(getArgument(&noLogo, "logo") == NULL);
  assert(lunaApplyGlobalPS2Logo(&noLogo, 1));
  assert(!getArgument(&noLogo, "logo")->isDisabled);
  assert(getArgument(&noLogo, "logo")->isGlobal);
  ArgumentList noLogoDefaultOff = {0};
  lunaGameOptionsRead(&options, &noLogoDefaultOff);
  assert(lunaGameOptionsCyclePS2Logo(&options, &noLogoDefaultOff, 0, 1));
  Argument *newLogo = getArgument(&noLogoDefaultOff, "logo");
  assert(newLogo != NULL && !newLogo->isDisabled && !newLogo->isGlobal);
  assert(lunaGameOptionsCyclePS2Logo(&options, &noLogoDefaultOff, 0, 1));
  assert(newLogo->isDisabled && !newLogo->isGlobal);
  assert(lunaGameOptionsCyclePS2Logo(&options, &noLogoDefaultOff, 0, 1));
  assert(newLogo->isDisabled && newLogo->isGlobal);
  freeArguments(&noLogoDefaultOff);
  freeArguments(&noLogo);

  freeArguments(&arguments);

  ArgumentList outputs = {0};
  lunaGameOptionsRead(&options, &outputs);
  assert(options.videoMode == 0 && options.oplVideoMode == 0);
  assert(!options.fieldFlip && !options.oplFieldFlip);
  assert(!strcmp(lunaGameOptionsValue(&options, LUNA_GAME_VIDEO_MODE), "Default"));
  assert(!strcmp(lunaGameOptionsValue(&options, LUNA_GAME_OPL_VIDEO_MODE), "Default"));
  assert(!lunaGameOptionsSetVideoMode(&options, &outputs, 0, -1));
  assert(!lunaGameOptionsSetVideoMode(&options, &outputs, 1, LUNA_OPL_VIDEO_MODE_COUNT));
  for (int opl = 0; opl <= 1; opl++) {
    for (int mode = 1; mode < lunaGameVideoModeCount(opl); mode++) {
      assert(lunaGameOptionsSetVideoMode(&options, &outputs, opl, mode));
      lunaGameOptionsRead(&options, &outputs);
      assert((opl ? options.oplVideoMode : options.videoMode) == mode);
      assert(strcmp(lunaGameVideoModeLabel(opl, mode), "Custom"));
    }
  }
  assert(options.videoMode == 5 && options.oplVideoMode == 29);
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 0, 2));
  assert(lunaGameOptionsChange(&options, &outputs, LUNA_GAME_FIELD_FLIP, 1));
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 0, 3));
  assert(!strcmp(getArgument(&outputs, "gsm")->value, "1080ix1:1"));
  assert(lunaGameOptionsChange(&options, &outputs, LUNA_GAME_OPL_FIELD_FLIP, 1));
  assert(options.oplFieldFlip && options.fieldFlip == 1);
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 1, 11));
  assert(options.videoMode == 3 && options.oplVideoMode == 11);
  getArgument(&outputs, "gsm")->isGlobal = 1;
  getArgument(&outputs, "luna_opl_gsm")->isGlobal = 1;
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 0, 0));
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 1, 0));
  assert(getArgument(&outputs, "gsm")->isDisabled);
  assert(!getArgument(&outputs, "gsm")->isGlobal);
  assert(getArgument(&outputs, "luna_opl_gsm")->isDisabled);
  assert(!getArgument(&outputs, "luna_opl_gsm")->isGlobal);
  assert(options.videoMode == 0 && options.oplVideoMode == 0 && !options.fieldFlip);
  // A malformed advanced value stays visible as Custom; unrelated edits preserve it.
  Argument *oplVideo = getArgument(&outputs, "luna_opl_gsm");
  free(oplVideo->value);
  oplVideo->value = strdup("999");
  oplVideo->isDisabled = 0;
  lunaGameOptionsRead(&options, &outputs);
  assert(options.oplVideoMode == -1);
  assert(lunaGameOptionsSetVideoMode(&options, &outputs, 0, 2));
  assert(!strcmp(oplVideo->value, "999"));
  freeArguments(&outputs);
  puts("game options: ok");
  return 0;
}
