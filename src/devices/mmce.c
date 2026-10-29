// Implements support for MMCE devices
#include "common.h"
#include "devices/devices.h"
#include "storage.h"
#include "ui/ui.h"
#include <errno.h>
#include <dirent.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Used to make devctl calls
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static char mmceMountpoint[] = "mmceX:";

// Initializes map entries for MMCE devices
int initMMCEDevices(int newDeviceIdx) {
  DIR *directory;

  int deviceCount = 0;
  for (int i = 0; i < 2 && newDeviceIdx < MAX_DEVICES; i++) {
    mmceMountpoint[4] = i + '0';

    directory = opendir(mmceMountpoint);
    if (directory != NULL) {
      closedir(directory);

      deviceModeMap[newDeviceIdx].mode = MODE_MMCE;
      deviceModeMap[newDeviceIdx].index = i;
      deviceModeMap[newDeviceIdx].mountpoint = strdup(mmceMountpoint);
      if (!deviceModeMap[newDeviceIdx].mountpoint) return -ENOMEM;
      deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
    }
  }
  return deviceCount;
}

// Uses MMCE devctl calls to switch memory card to given title ID
unsigned int mmceMountVMC(char *titleID) {
  // MMCE storage scanning is independent of a card used for physical saves.
  // Its SIO2 driver cannot coexist with the active MX4SIO driver.
  if (!STORAGE_SETTINGS.mmceGameID || (LAUNCHER_OPTIONS.mode & MODE_MX4SIO) ||
      !titleID || !titleID[0] || strlen(titleID) > 249)
    return 0;
  unsigned int switched = 0;
  // Send GameID to both MMCE devices
  for (int i = '0'; i < '2'; i++) {
    mmceMountpoint[4] = i;

    // Ping MMCE first to make sure the device is present
    if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
      continue;

    if ((fileXioDevctl(mmceMountpoint, 0x8, titleID, (strlen(titleID) + 1), NULL, 0)) < 0) {
      // Skip polling if devctl call has failed
      continue;
    }

    switched |= 1u << (i - '0');
    // Poll MMCE status until busy bit is clear
    for (int poll = 0; poll < 150; poll++) {
      usleep(100000);
      int status = fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0);
      if (status < 0 || (status & 1) == 0) {
        break;
      }
    }
  }
  return switched;
}
