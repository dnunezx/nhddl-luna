// Implements support for BDM devices
// LUNA 2026
#include "common.h"
#include "devices/devices.h"
#include "ui/ui.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usbhdfsd-common.h>

// Used to get BDM driver name and make devctl calls
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Maps driver name to ModeType
ModeType mapBDMDriverName(char *driverName) {
  if (!strncmp(driverName, "ata", 3))
    return MODE_ATA;
  else if (!strncmp(driverName, "sdc", 3))
    return MODE_MX4SIO;
  else if (!strncmp(driverName, "usb", 3))
    return MODE_USB;
  else if (!strncmp(driverName, "sd", 2))
    return MODE_ILINK;
  else if (!strncmp(driverName, "hdlfs", 3))
    return MODE_HDL;
  return MODE_NONE;
}

//
// The following functions are based on code by AKuHAK
//

// Gets BDM driver name via fileXio
int getBDMDeviceDriver(char *mountpoint, struct DeviceMapEntry *entry) {
  int fd = fileXioDopen(mountpoint);
  if (fd < 0) {
    return -ENODEV;
  }

  char driverName[10] = {0};
  int deviceNumber = -1;
  int result = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0,
                            driverName, sizeof(driverName) - 1);
  if (result >= 0)
    result = fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0,
                           &deviceNumber, sizeof(deviceNumber));
  int closeResult = fileXioDclose(fd);
  if (result < 0 || closeResult < 0 || !driverName[0] ||
      deviceNumber < 0 || deviceNumber > UINT8_MAX) {
    return -EIO;
  }
  entry->mode = mapBDMDriverName(driverName);
  entry->index = deviceNumber;

  return 0;
}

// Initializes map entries for BDM devices
int initBDMDevices(int deviceIdx) {
  if (deviceIdx < 0 || deviceIdx >= MAX_DEVICES) return -ENOSPC;
  DIR *directory = NULL;
  char mountpoint[] = "massX:";
  int deviceCount = 0;
  int delayAttempts = 2;

  if (LAUNCHER_OPTIONS.image) {
    // Only try the image mountpoint
    char *sep = strchr(LAUNCHER_OPTIONS.image, ':');
    if (!sep)
      return -EINVAL;

    sep++;
    char temp = *sep;
    *sep = '\0';

    directory = opendir(LAUNCHER_OPTIONS.image);
    if (directory != NULL)
      closedir(directory);

    if (getBDMDeviceDriver(LAUNCHER_OPTIONS.image, &deviceModeMap[deviceIdx]) < 0) {
      *sep = temp;
      return -EIO;
    }

    // Set device mountpoint
    deviceModeMap[deviceIdx].mountpoint = strdup(LAUNCHER_OPTIONS.image);
    *sep = temp;
    if (!deviceModeMap[deviceIdx].mountpoint) return -ENOMEM;
    // Set scan function
    deviceModeMap[deviceIdx].scan = &findISO;
    return 1;
  }

  for (int i = 0; i < 10 && deviceIdx < MAX_DEVICES; i++) {
    deviceModeMap[deviceIdx].mode = MODE_NONE;
    mountpoint[4] = i + '0';

    // Probe immediately, retaining the full readiness window for slow devices.
    for (int attempts = 0; attempts <= delayAttempts; attempts++) {
      if (attempts > 0)
        sleep(1);
      directory = opendir(mountpoint);
      if (directory != NULL) {
        closedir(directory);
        break;
      }
    }
    if (directory == NULL) {
      // Since BDM devices are always mounted sequentially,
      // non-exiting mountpoint means that there will be no more devices
      break;
    }

    if (getBDMDeviceDriver(mountpoint, &deviceModeMap[deviceIdx]) < 0) {
      return -EIO;
    }

    // Set device mountpoint
    deviceModeMap[deviceIdx].mountpoint = strdup(mountpoint);
    if (!deviceModeMap[deviceIdx].mountpoint) return -ENOMEM;
    // Set scan function
    deviceModeMap[deviceIdx].scan = &findISO;

    deviceIdx++;
    deviceCount++;

    // A PS2 has one internal ATA drive. In ATA-only mode there cannot be a
    // second HDD library device, so do not spend two seconds probing mass1:.
    if ((LAUNCHER_OPTIONS.mode & MODE_BDM) == MODE_ATA && deviceModeMap[deviceIdx - 1].mode == MODE_ATA)
      break;
  }

  return deviceCount;
}
