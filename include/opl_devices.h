// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_OPL_DEVICES_H
#define LUNA_OPL_DEVICES_H

#include "common.h"
#include <stddef.h>

typedef struct {
  const char *cdvdman;
  const char *gameMode;
  const char *driver;
  const char *modules[3];
  unsigned char moduleIds[3];
  int fileHandles;
} LunaOplDevice;

// IDs 21..23 extend the pinned OPL module table without changing existing IDs.
static inline const LunaOplDevice *lunaOplDevice(ModeType mode) {
  static const LunaOplDevice ata = {
      .cdvdman = "bdm_ata_cdvdman.irx", .gameMode = "BDM_ATA_MODE", .driver = "ata"};
  static const LunaOplDevice usb = {
      .cdvdman = "bdm_cdvdman.irx", .gameMode = "BDM_USB_MODE", .driver = "usb",
      .modules = {"usbd_mini.irx", "usbmass_bd_mini.irx"}, .moduleIds = {5, 6}};
  static const LunaOplDevice mx4sio = {
      .cdvdman = "bdm_cdvdman.irx", .gameMode = "BDM_M4S_MODE", .driver = "sdc",
      .modules = {"mx4sio_bd_mini.irx"}, .moduleIds = {9}};
  static const LunaOplDevice ilink = {
      .cdvdman = "bdm_cdvdman.irx", .gameMode = "BDM_ILK_MODE", .driver = "sd",
      .modules = {"iLinkman.irx", "IEEE1394_bd_mini.irx"}, .moduleIds = {7, 8}};
  static const LunaOplDevice mmce = {
      .cdvdman = "fhi_cdvdman.irx", .gameMode = "LUNA_MMCE_MODE",
      .modules = {"mmcefhi.irx"}, .moduleIds = {21}, .fileHandles = 1};
  static const LunaOplDevice udpfs = {
      .cdvdman = "fhi_cdvdman.irx", .gameMode = "LUNA_UDPFS_MODE",
      .modules = {"smap.irx", "ministack.irx", "udpfs_fhi.irx"},
      .moduleIds = {11, 23, 22}, .fileHandles = 1};
  switch (mode) {
    case MODE_ATA: case MODE_HDL: return &ata;
    case MODE_USB: return &usb;
    case MODE_MX4SIO: return &mx4sio;
    case MODE_ILINK: return &ilink;
    case MODE_MMCE: return &mmce;
    case MODE_UDPFS: return &udpfs;
    default: return NULL;
  }
}

#endif
