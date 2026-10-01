// Real card files and per-game YAML across device toggles and root renumbering.
#include "storage.h"
#include "devices/devices.h"
#include "options.h"
#include "ui/game_options.h"
#include "vmc_create.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

LauncherOptions LAUNCHER_OPTIONS;
char NEUTRINO_ELF_PATH[PATH_MAX + 1] = "host:/neutrino.elf";
static int probes, switches;
int fileXioDevctl(const char *path, int command, void *input, int inputSize,
                 void *output, int outputSize) {
  (void)path; (void)output; (void)outputSize;
  if (command == 1) { probes++; return 0; }
  assert(command == 8);
  assert(inputSize == 12 && !strcmp(input, "SLUS_123.45"));
  switches++;
  return -1; // Do not wait for a hardware status poll in this host test.
}
int getRelativePathIdx(char *path) {
  char *colon = strchr(path, ':');
  return colon && (colon[1] == '/' || colon[1] == '\\') ? (int)(colon + 1 - path) : -1;
}

static void clearArguments(ArgumentList *list) {
  // Production frees the list container too; these test containers are local.
  ArgumentList *owned = malloc(sizeof(*owned));
  assert(owned);
  *owned = *list;
  freeArgumentList(owned);
  memset(list, 0, sizeof(*list));
}

static void roundTrip(Target *target, const char *root, int physicalSlot) {
  ArgumentList arguments = {0}, loaded = {0};
  LunaGameOptions options;
  char card[PATH_MAX + 1], config[PATH_MAX + 1], configDirectory[PATH_MAX + 1];
  snprintf(card, sizeof(card), "%s/VMC/card.bin", root);
  assert(lunaGameOptionsSetVMC(&options, &arguments, 0, card));
  assert(lunaGameOptionsSetVMC(&options, &arguments, 1, physicalSlot ? "" : card));
  for (int pass = 0; pass < 2; pass++) {
    assert(updateTitleLaunchArguments(target, &arguments) == 0);
    assert(getTitleLaunchArguments(&loaded, target) == 0);
    assert(!strcmp(getArgument(&loaded, "mc0")->value, card));
    Argument *second = getArgument(&loaded, "mc1");
    if (physicalSlot) assert(!second || second->isDisabled);
    else assert(second && !strcmp(second->value, card));
    clearArguments(&arguments);
    arguments = loaded;
    memset(&loaded, 0, sizeof(loaded));
  }
  clearArguments(&arguments);
  snprintf(configDirectory, sizeof(configDirectory), "%s/LUNA", root);
  snprintf(config, sizeof(config), "%s/LUNA/Game.yaml", root);
  assert(remove(config) == 0 && rmdir(configDirectory) == 0);
}

int main(void) {
  char temporary[] = "/tmp/luna-vmc-storage-XXXXXX";
  assert(mkdtemp(temporary) && chdir(temporary) == 0);
  struct DeviceMapEntry metadata = {.mountpoint = "pfs0:/OPL"};
  struct DeviceMapEntry device = {.mode = MODE_HDL, .mountpoint = "hdd0:", .metadev = &metadata};
  Target target = {.name = "Game", .id = "SLUS_123.45", .device = &device};
  STORAGE_SETTINGS.enabled = MODE_HDL;
  assert(mkdir("pfs0:", 0777) == 0 && mkdir("pfs0:/OPL", 0777) == 0);
  assert(mkdir("pfs0:/OPL/VMC", 0777) == 0);
  assert(lunaCreateVMC8("pfs0:/OPL/VMC/card.bin", NULL, NULL) == 0);
  roundTrip(&target, "pfs0:/OPL", 0);
  roundTrip(&target, "pfs0:/OPL", 1);
  assert(!loadGameCoreOpl(&target));
  assert(saveGameCoreOpl(&target, 1) == 0);
  assert(loadGameCoreOpl(&target));
  ArgumentList *coreArgs = loadLaunchArgumentLists(&target);
  Argument *core = getArgument(coreArgs, "luna_core");
  assert(core && core->isGlobal && !strcmp(core->value, "opl"));
  LunaGameOptions coreOptions;
  lunaGameOptionsRead(&coreOptions, coreArgs);
  assert(lunaGameOptionsCycleCore(&coreOptions, coreArgs, 1, 1));
  assert(!core->isGlobal && !strcmp(core->value, "neutrino"));
  assert(updateTitleLaunchArguments(&target, coreArgs) == 0);
  freeArgumentList(coreArgs);
  coreArgs = loadLaunchArgumentLists(&target);
  core = getArgument(coreArgs, "luna_core");
  assert(core && !core->isGlobal && !strcmp(core->value, "neutrino"));
  freeArgumentList(coreArgs);
  assert(saveGameCoreOpl(&target, 0) == 0);
  assert(!loadGameCoreOpl(&target));
  assert(remove("pfs0:/OPL/LUNA/Game.yaml") == 0);
  assert(remove("pfs0:/OPL/LUNA/gameCore.txt") == 0);
  assert(rmdir("pfs0:/OPL/LUNA") == 0);
  // Prefix siblings and other drives must not be silently rebased.
  assert(!strcmp(storageVMCRelativePath(&device, "pfs0:/OPL2/VMC/card.bin"),
                 "pfs0:/OPL2/VMC/card.bin"));
  assert(remove("pfs0:/OPL/VMC/card.bin") == 0);
  assert(rmdir("pfs0:/OPL/VMC") == 0 && rmdir("pfs0:/OPL") == 0 && rmdir("pfs0:") == 0);

  device.mode = MODE_USB; device.metadev = NULL; device.mountpoint = "mass3:";
  STORAGE_SETTINGS.enabled = MODE_USB;
  assert(mkdir("mass3:", 0777) == 0 && mkdir("mass3:/VMC", 0777) == 0);
  assert(lunaCreateVMC8("mass3:/VMC/card.bin", NULL, NULL) == 0);
  ArgumentList saved = {0}, loaded = {0};
  insertArgument(&saved, "mc0", "mass3:/VMC/card.bin");
  insertArgument(&saved, "mc1", "mass3:/VMC/card.bin");
  getArgument(&saved, "mc1")->isDisabled = 1;
  assert(updateTitleLaunchArguments(&target, &saved) == 0);
  STORAGE_SETTINGS.enabled = MODE_NONE;
  assert(storageVMCRoot(&device) == NULL);
  assert(rename("mass3:", "mass0:") == 0);
  device.mountpoint = "mass0:";
  STORAGE_SETTINGS.enabled = MODE_USB;
  assert(getTitleLaunchArguments(&loaded, &target) == 0);
  assert(!strcmp(getArgument(&loaded, "mc0")->value, "mass0:/VMC/card.bin"));
  assert(getArgument(&loaded, "mc1")->isDisabled);
  struct stat info;
  assert(stat(getArgument(&loaded, "mc0")->value, &info) == 0 && info.st_size == 8 * 1024 * 1024);
  clearArguments(&saved); clearArguments(&loaded);
  assert(remove("mass0:/LUNA/Game.yaml") == 0 && rmdir("mass0:/LUNA") == 0);
  assert(remove("mass0:/VMC/card.bin") == 0 && rmdir("mass0:/VMC") == 0 && rmdir("mass0:") == 0);

  LAUNCHER_OPTIONS.mode = MODE_MMCE | MODE_BASIC; // Driver remains loaded.
  mmceMountVMC("SLUS_123.45");
  assert(probes == 0 && switches == 0);
  STORAGE_SETTINGS.enabled = MODE_MMCE;
  mmceMountVMC(NULL); mmceMountVMC("");
  assert(probes == 0);
  mmceMountVMC("SLUS_123.45");
  assert(probes == 2 && switches == 2);
  strcpy(NEUTRINO_ELF_PATH, "mc0:/APPS/neutrino.elf");
  mmceMountVMC("SLUS_123.45");
  assert(probes == 3 && switches == 3); // Runtime card stays mounted.
  assert(chdir("/") == 0 && rmdir(temporary) == 0);
  puts("VMC storage, YAML round-trip, and MMCE enable/disable tests passed");
  return 0;
}
