// LUNA's per-game settings model. The launcher still owns the YAML argument list.
#include "ui/game_options.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char compatDigits[LUNA_GAME_COMPAT_COUNT] = {'0', '2', '3', '5', '7'};
static const char *const videoValues[] = {"", "fp1", "fp2", "1080ix1", "1080ix2", "1080ix3"};
static const char *const videoLabels[] = {"Default", "240p/288p", "480p/576p",
                                          "1080i x1", "1080i x2", "1080i x3"};
// Order matches the pinned OPL src/gsm.c preset table. Zero keeps GSM off.
static const char *const oplVideoLabels[LUNA_OPL_VIDEO_MODE_COUNT] = {
    "Default", "NTSC 480i", "NTSC non-interlaced", "PAL 576i",
    "PAL non-interlaced", "PAL 60Hz", "PAL 60Hz non-interlaced",
    "PS1 480p 60Hz", "PS1 576p 50Hz", "480p 60Hz", "576p 50Hz",
    "720p 60Hz", "1080i 60Hz", "1080i 60Hz (frame)",
    "VGA 640x480p 60Hz", "VGA 640x480p 72Hz", "VGA 640x480p 75Hz",
    "VGA 640x480p 85Hz", "VGA 640x960i 60Hz", "VGA 800x600p 56Hz",
    "VGA 800x600p 60Hz", "VGA 800x600p 72Hz", "VGA 800x600p 75Hz",
    "VGA 800x600p 85Hz", "VGA 1024x768p 60Hz", "VGA 1024x768p 70Hz",
    "VGA 1024x768p 75Hz", "VGA 1024x768p 85Hz",
    "VGA 1280x1024p 60Hz", "VGA 1280x1024p 75Hz"};
static const char *const flipLabels[] = {"Off", "Type 1", "Type 2", "Type 3"};

int lunaGameVideoModeCount(int opl) {
  return opl ? LUNA_OPL_VIDEO_MODE_COUNT : LUNA_NEUTRINO_VIDEO_MODE_COUNT;
}

const char *lunaGameVideoModeLabel(int opl, int mode) {
  if (mode < 0 || mode >= lunaGameVideoModeCount(opl))
    return "Custom";
  return opl ? oplVideoLabels[mode] : videoLabels[mode];
}

static int argumentEnabled(ArgumentList *arguments, const char *name) {
  Argument *argument = getArgument(arguments, name);
  return argument != NULL && !argument->isDisabled;
}

uint8_t lunaGetOplCompatMask(ArgumentList *arguments) {
  Argument *compat = getArgument(arguments, "luna_opl_compat");
  if (compat == NULL || compat->isDisabled || compat->value == NULL ||
      compat->value[0] == '\0')
    return 0;
  char *end;
  unsigned long mask = strtoul(compat->value, &end, 10);
  return *end == '\0' && mask < (1UL << LUNA_OPL_COMPAT_COUNT) ? mask : 0;
}

void lunaGameOptionsRead(LunaGameOptions *options, ArgumentList *arguments) {
  memset(options, 0, sizeof(*options));
  Argument *compat = getArgument(arguments, "gc");
  if (compat != NULL && !compat->isDisabled && compat->value != NULL) {
    for (int i = 0; i < LUNA_GAME_COMPAT_COUNT; i++)
      if (strchr(compat->value, compatDigits[i]) != NULL)
        options->compat |= 1U << i;
  }
  options->oplCompat = lunaGetOplCompatMask(arguments);
  Argument *oplVideo = getArgument(arguments, "luna_opl_gsm");
  if (oplVideo != NULL && !oplVideo->isDisabled && oplVideo->value != NULL) {
    char *end;
    long mode = strtol(oplVideo->value, &end, 10);
    options->oplVideoMode = end != oplVideo->value && *end == '\0' &&
        mode >= 0 && mode < LUNA_OPL_VIDEO_MODE_COUNT ? (int)mode : -1;
  }
  options->oplFieldFlip = argumentEnabled(arguments, "luna_opl_field_flip");

  Argument *video = getArgument(arguments, "gsm");
  if (video != NULL && !video->isDisabled && video->value != NULL) {
    options->videoMode = -1;
    for (int i = 1; i < 6; i++) {
      size_t length = strlen(videoValues[i]);
      if (!strncmp(video->value, videoValues[i], length)) {
        const char *suffix = video->value + length;
        if (*suffix == '\0' ||
            (*suffix == ':' && suffix[1] >= '1' && suffix[1] <= '3' && suffix[2] == '\0')) {
          options->videoMode = i;
          if (*suffix == ':')
            options->fieldFlip = suffix[1] - '0';
          break;
        }
      }
    }
  }
  options->ps2Logo = argumentEnabled(arguments, "logo");
  options->debugColors = argumentEnabled(arguments, "dbc");
  options->neutrinoIgrDisabled =
      argumentEnabled(arguments, "luna_neutrino_disable_igr");
  Argument *core = getArgument(arguments, "luna_core");
  options->oplCore = core != NULL && !core->isDisabled &&
                     core->value != NULL && !strcmp(core->value, "opl");
  options->coreInherited = core == NULL || core->isGlobal;
  for (int slot = 0; slot < 2; slot++) {
    const char *name = slot == 0 ? "mc0" : "mc1";
    Argument *card = getArgument(arguments, name);
    const char *label = "Physical card";
    if (card != NULL && !card->isDisabled && card->value != NULL && card->value[0]) {
      const char *slash = strrchr(card->value, '/');
      const char *backslash = strrchr(card->value, '\\');
      if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
      label = slash != NULL ? slash + 1 : card->value;
    }
    snprintf(options->vmcSlotLabel[slot], sizeof(options->vmcSlotLabel[slot]),
             "%.24s", label);
  }
}

static int setArgument(ArgumentList *arguments, const char *name, const char *value,
                       int enabled) {
  Argument *argument = getArgument(arguments, name);
  if (argument == NULL) {
    if (!enabled)
      return 1;
    argument = insertArgument(arguments, name, (char *)value);
    return argument != NULL;
  }

  if (argument->value == NULL || strcmp(argument->value, value)) {
    char *replacement = strdup(value);
    if (replacement == NULL)
      return 0;
    free(argument->value);
    argument->value = replacement;
    argument->isGlobal = 0;
  }
  if (enabled && argument->isDisabled)
    argument->isGlobal = 0;
  argument->isDisabled = !enabled;
  return 1;
}

int lunaGameOptionsSetVideoMode(LunaGameOptions *options,
                               ArgumentList *arguments, int opl, int mode) {
  if (mode < 0 || mode >= lunaGameVideoModeCount(opl))
    return 0;
  char value[12];
  const char *name = opl ? "luna_opl_gsm" : "gsm";
  if (opl)
    snprintf(value, sizeof(value), "%d", mode);
  else if (mode && options->fieldFlip)
    snprintf(value, sizeof(value), "%s:%d", videoValues[mode], options->fieldFlip);
  else
    snprintf(value, sizeof(value), "%s", videoValues[mode]);
  if (!setArgument(arguments, name, value, mode != 0))
    return 0;
  Argument *video = getArgument(arguments, name);
  if (video != NULL)
    video->isGlobal = 0; // Off must also override an inherited forced mode.
  lunaGameOptionsRead(options, arguments);
  return 1;
}

int lunaGameOptionsSetVMC(LunaGameOptions *options, ArgumentList *arguments,
                          int slot, const char *path) {
  if (slot < 0 || slot > 1 || path == NULL)
    return 0;
  if (!setArgument(arguments, slot == 0 ? "mc0" : "mc1", path, path[0] != '\0'))
    return 0;
  lunaGameOptionsRead(options, arguments);
  return 1;
}

int lunaGameOptionsChange(LunaGameOptions *options, ArgumentList *arguments,
                          LunaGameRow row, int direction) {
  if (row == LUNA_GAME_OPL_VIDEO_MODE) {
    int mode = options->oplVideoMode < 0 ? 0 :
        (options->oplVideoMode + (direction < 0 ? LUNA_OPL_VIDEO_MODE_COUNT - 1 : 1)) %
        LUNA_OPL_VIDEO_MODE_COUNT;
    return lunaGameOptionsSetVideoMode(options, arguments, 1, mode);
  }
  if (row == LUNA_GAME_OPL_FIELD_FLIP) {
    if (!setArgument(arguments, "luna_opl_field_flip", "", !options->oplFieldFlip))
      return 0;
    Argument *flip = getArgument(arguments, "luna_opl_field_flip");
    if (flip != NULL)
      flip->isGlobal = 0;
    lunaGameOptionsRead(options, arguments);
    return 1;
  }
  if (row == LUNA_GAME_NEUTRINO_DISABLE_IGR) {
    int disabled = !options->neutrinoIgrDisabled;
    if (!setArgument(arguments, "luna_neutrino_disable_igr", "", disabled))
      return 0;
    options->neutrinoIgrDisabled = disabled;
    return 1;
  }
  if (row >= LUNA_GAME_OPL_ACCURATE_READS && row <= LUNA_GAME_OPL_DISABLE_IGR) {
    uint8_t next = options->oplCompat ^
                   (1U << (row - LUNA_GAME_OPL_ACCURATE_READS));
    char value[4];
    snprintf(value, sizeof(value), "%u", next);
    if (!setArgument(arguments, "luna_opl_compat", value, next != 0))
      return 0;
    Argument *compat = getArgument(arguments, "luna_opl_compat");
    if (compat != NULL)
      compat->isGlobal = 0;
    options->oplCompat = next;
    return 1;
  }
  if (row >= LUNA_GAME_FAST_READS && row <= LUNA_GAME_BUFFER_OVERRUN) {
    int bit = 1 << (row - LUNA_GAME_FAST_READS);
    uint8_t next = options->compat ^ bit;
    char value[LUNA_GAME_COMPAT_COUNT + 1];
    int length = 0;
    for (int i = 0; i < LUNA_GAME_COMPAT_COUNT; i++)
      if (next & (1 << i))
        value[length++] = compatDigits[i];
    value[length] = '\0';
    if (!setArgument(arguments, "gc", value, length != 0))
      return 0;
    options->compat = next;
    return 1;
  }

  if (row == LUNA_GAME_VIDEO_MODE || row == LUNA_GAME_FIELD_FLIP) {
    int nextMode = options->videoMode;
    int nextFlip = options->fieldFlip;
    if (row == LUNA_GAME_VIDEO_MODE) {
      if (nextMode < 0)
        nextMode = 0;
      else
        nextMode = (nextMode + (direction < 0 ? 5 : 1)) % 6;
      if (nextMode == 0)
        nextFlip = 0;
    } else {
      nextFlip = (nextFlip + (direction < 0 ? 3 : 1)) % 4;
      if (nextFlip != 0 && nextMode <= 0)
        nextMode = 2; // The legacy menu selected 480p/576p for field flipping.
      if (nextMode < 0)
        nextMode = 0;
    }
    char value[12];
    if (nextFlip)
      snprintf(value, sizeof(value), "%s:%d", videoValues[nextMode], nextFlip);
    else
      snprintf(value, sizeof(value), "%s", videoValues[nextMode]);
    if (!setArgument(arguments, "gsm", value, nextMode != 0))
      return 0;
    Argument *video = getArgument(arguments, "gsm");
    if (video != NULL)
      video->isGlobal = 0;
    options->videoMode = nextMode;
    options->fieldFlip = nextFlip;
    return 1;
  }

  if (row == LUNA_GAME_DEBUG_COLORS) {
    int enabled = !options->debugColors;
    if (!setArgument(arguments, "dbc", "", enabled))
      return 0;
    options->debugColors = enabled;
    return 1;
  }
  return 0;
}

int lunaGameOptionsCycleCore(LunaGameOptions *options,
                             ArgumentList *arguments,
                             int globalOpl, int direction) {
  Argument *core = getArgument(arguments, "luna_core");
  int current = core == NULL || core->isGlobal ? 0 :
      (options->oplCore != !!globalOpl ? 1 : 2);
  int next = (current + (direction < 0 ? 2 : 1)) % 3;
  if (next == 0) {
    if (core != NULL)
      core->isGlobal = 1;
    if (!lunaApplyGlobalGameCore(arguments, globalOpl)) {
      if (core != NULL)
        core->isGlobal = 0;
      return 0;
    }
  } else {
    int opl = next == 1 ? !globalOpl : !!globalOpl;
    if (!setArgument(arguments, "luna_core", opl ? "opl" : "neutrino", 1))
      return 0;
    getArgument(arguments, "luna_core")->isGlobal = 0;
  }
  lunaGameOptionsRead(options, arguments);
  return 1;
}

int lunaGameOptionsCyclePS2Logo(LunaGameOptions *options,
                                ArgumentList *arguments,
                                int globalEnabled, int direction) {
  globalEnabled = !!globalEnabled;
  Argument *logo = getArgument(arguments, "logo");
  int current = logo == NULL || logo->isGlobal ? 0 :
      ((!logo->isDisabled) != globalEnabled ? 1 : 2);
  int next = (current + (direction < 0 ? 2 : 1)) % 3;
  if (next == 0) {
    if (logo != NULL)
      logo->isGlobal = 1;
    if (!lunaApplyGlobalPS2Logo(arguments, globalEnabled))
      return 0;
    options->ps2Logo = globalEnabled;
    return 1;
  }
  int enabled = next == 1 ? !globalEnabled : globalEnabled;
  if (logo == NULL) {
    logo = insertArgument(arguments, "logo", "");
    if (logo == NULL)
      return 0;
  }
  if (!setArgument(arguments, "logo", "", enabled))
    return 0;
  logo->isGlobal = 0;
  options->ps2Logo = enabled;
  return 1;
}

const char *lunaGameOptionsValue(const LunaGameOptions *options, LunaGameRow row) {
  if (row == LUNA_GAME_NEUTRINO_DISABLE_IGR)
    return options->neutrinoIgrDisabled ? "On" : "Off";
  if (row >= LUNA_GAME_OPL_ACCURATE_READS && row <= LUNA_GAME_OPL_DISABLE_IGR)
    return options->oplCompat & (1U << (row - LUNA_GAME_OPL_ACCURATE_READS)) ?
        "On" : "Off";
  if (row >= LUNA_GAME_FAST_READS && row <= LUNA_GAME_BUFFER_OVERRUN)
    return (options->compat & (1 << (row - LUNA_GAME_FAST_READS))) ? "On" : "Off";
  if (row == LUNA_GAME_VIDEO_MODE)
    return lunaGameVideoModeLabel(0, options->videoMode);
  if (row == LUNA_GAME_OPL_VIDEO_MODE)
    return lunaGameVideoModeLabel(1, options->oplVideoMode);
  if (row == LUNA_GAME_OPL_FIELD_FLIP)
    return options->oplFieldFlip ? "On" : "Off";
  if (row == LUNA_GAME_FIELD_FLIP)
    return flipLabels[options->fieldFlip];
  if (row == LUNA_GAME_VMC_SLOT1 || row == LUNA_GAME_VMC_SLOT2)
    return options->vmcSlotLabel[row - LUNA_GAME_VMC_SLOT1];
  if (row == LUNA_GAME_PS2_LOGO)
    return options->ps2Logo ? "On" : "Off";
  if (row == LUNA_GAME_DEBUG_COLORS)
    return options->debugColors ? "On" : "Off";
  if (row == LUNA_GAME_CORE)
    return options->coreInherited ?
        (options->oplCore ? "Inherit (OPL)" : "Inherit (Neutrino)") :
        (options->oplCore ? "OPL" : "Neutrino");
  return ">";
}

int lunaApplyGlobalPS2Logo(ArgumentList *arguments, int enabled) {
  Argument *logo = getArgument(arguments, "logo");
  if (logo != NULL && !logo->isGlobal)
    return 1;
  if (logo == NULL) {
    if (!enabled)
      return 1;
    logo = insertArgument(arguments, "logo", "");
    if (logo == NULL)
      return 0;
  }
  logo->isGlobal = 1;
  logo->isDisabled = !enabled;
  return 1;
}

int lunaApplyGlobalGameCore(ArgumentList *arguments, int opl) {
  Argument *core = getArgument(arguments, "luna_core");
  if (core != NULL && !core->isGlobal)
    return 1;
  const char *value = opl ? "opl" : "neutrino";
  if (core == NULL) {
    core = insertArgument(arguments, "luna_core", (char *)value);
    if (core == NULL)
      return 0;
  } else if (core->value == NULL || strcmp(core->value, value)) {
    char *replacement = strdup(value);
    if (replacement == NULL)
      return 0;
    free(core->value);
    core->value = replacement;
  }
  core->isGlobal = 1;
  core->isDisabled = 0;
  return 1;
}
