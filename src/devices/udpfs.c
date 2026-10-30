// Implements support for MMCE devices
#include "common.h"
#include "devices/devices.h"
#include "ui/ui.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char udpfsMountpoint[] = "udpfs0:";

// Initializes map entries for MMCE devices
int initUDPFSDevices(int newDeviceIdx) {
  if (newDeviceIdx < 0 || newDeviceIdx >= MAX_DEVICES) return -ENOSPC;
  DIR *directory;

  int deviceCount = 0;

  // Probe immediately, retaining the five-second window for network startup.
  for (int attempts = 0; attempts <= 5; attempts++) {
    if (attempts > 0)
      sleep(1);
    directory = opendir(udpfsMountpoint);
    if (directory != NULL) {
      closedir(directory);

      deviceModeMap[newDeviceIdx].mode = MODE_UDPFS;
      deviceModeMap[newDeviceIdx].index = 0;
      deviceModeMap[newDeviceIdx].mountpoint = strdup(udpfsMountpoint);
      if (!deviceModeMap[newDeviceIdx].mountpoint) return -ENOMEM;
      deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
      return deviceCount;
    }
  }
  return 0;
}
