// Original LUNA code: Danny Nunez (dnunezx) 2026
// Card validation follows neutrino/ee/loader/src/vmc_image.c.
#include "opl_vmc.h"
#include "opl_pfs_vmc.h"
#include "devices/devices.h"
#include "storage.h"
#include "dprintf.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
_off64_t lseek64(int fd, _off64_t offset, int whence);
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

static uint16_t read16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const unsigned char *p) {
  return read16(p) | ((uint32_t)read16(p + 2) << 16);
}

static int inspectCard(int fd, OplVmcCard *card, uint64_t *size) {
  unsigned char header[52];
  if (read(fd, header, sizeof(header)) != sizeof(header) ||
      memcmp(header, "Sony PS2 Memory Card Format ", 28))
    return -EINVAL;
  uint16_t pageSize = read16(header + 40), clusterPages = read16(header + 42);
  uint16_t blockSize = read16(header + 44);
  uint64_t expected = (uint64_t)pageSize * clusterPages * read32(header + 48);
  _off64_t actual = lseek64(fd, 0, SEEK_END);
  if (pageSize != 512 || clusterPages != 2 || blockSize < 16 || blockSize > 64 ||
      (blockSize & (blockSize - 1)) || expected < 8 * 1024 * 1024 ||
      expected > 64 * 1024 * 1024 || (expected & (expected - 1)) ||
      actual < 0 || expected != (uint64_t)actual || expected / 512 % blockSize)
    return -EINVAL;
  memset(card, 0, sizeof(*card));
  card->active = 1;
  card->flags = 0x12b;
  card->cspec.PageSize = pageSize;
  card->cspec.BlockSize = blockSize;
  card->cspec.CardSize = expected / 512;
  *size = expected;
  return 0;
}

static OplVmcCard *findCard(unsigned char *module, size_t size, int slot) {
  for (size_t offset = 0; offset + sizeof(OplVmcCard) <= size; offset += 4) {
    uint32_t marker;
    memcpy(&marker, module + offset, sizeof(marker));
    if (marker == 0xc0defac0u + slot)
      return (OplVmcCard *)(module + offset);
  }
  return NULL;
}

static int validateMap(const OplVmcCard *card) {
  uint64_t sectors = 0;
  if (!card->frag_count || card->frag_count > 64)
    return -EFBIG;
  for (unsigned int i = 0; i < card->frag_count; i++) {
    const bd_fragment_t *f = &card->frags[i];
    if (!f->count || f->sector > UINT64_MAX - f->count)
      return -EINVAL;
    sectors += f->count;
    for (unsigned int j = 0; j < i; j++)
      if (f->sector < card->frags[j].sector + card->frags[j].count &&
          card->frags[j].sector < f->sector + f->count)
        return -EINVAL;
  }
  return sectors >= card->cspec.CardSize ? 0 : -EINVAL;
}

int oplPrepareVmcs(Target *target, ArgumentList *arguments, const LunaOplDevice *device,
                   unsigned char *module, size_t moduleSize,
                   OplCdvdSettingsBdm *disc, OplFhiSettings *fhi, int fds[2]) {
  OplVmcCard *cards[2];
  char paths[2][PATH_MAX + 1] = {{0}};
  int count = 0;
  for (int slot = 0; slot < 2; slot++) {
    cards[slot] = findCard(module, moduleSize, slot);
    if (!cards[slot])
      return -EINVAL;
    memset(cards[slot], 0, sizeof(*cards[slot]));
    Argument *arg = getArgument(arguments, slot ? "mc1" : "mc0");
    if (!arg || arg->isDisabled || !arg->value || !arg->value[0] ||
        !strcmp(arg->value, "no"))
      continue;
    if (!strcmp(arg->value, "yes"))
      return -EINVAL; // OPL needs a concrete image, rather than an empty emulated card.
    const char *root = storageVMCRoot(target->device);
    if (device->fileHandles)
      root = target->device->mountpoint;
    if (!root)
      return -ENODEV;
    int length;
    if (strchr(arg->value, ':'))
      length = snprintf(paths[slot], sizeof(paths[slot]), "%s", arg->value);
    else {
      size_t n = strlen(root);
      while (n && root[n - 1] == '/') n--;
      const char *relative = arg->value;
      while (*relative == '/') relative++;
      length = snprintf(paths[slot], sizeof(paths[slot]), "%.*s/%s", (int)n, root, relative);
    }
    if (length < 0 || (size_t)length >= sizeof(paths[slot]))
      return -ENAMETOOLONG;
    if (slot && paths[0][0] && !strcmp(paths[0], paths[1]))
      return -EINVAL;
    int fd = open(paths[slot], device->fileHandles ? O_RDWR : O_RDONLY);
    if (fd < 0)
      return -ENOENT;
    fds[slot] = fd;
    uint64_t size;
    int result = inspectCard(fd, cards[slot], &size);
    if (result)
      return result;
    int iopFd = ps2sdk_get_iop_fd(fd);
    if (device->fileHandles) {
      // File IDs belong to the same server/card as the ISO's transport.
      size_t mountLength = strcspn(root, ":");
      if (!fhi || strncmp(paths[slot], root, mountLength) || paths[slot][mountLength] != ':')
        return -EXDEV;
      int id = fileXioIoctl2(iopFd, 0x80, NULL, 0, NULL, 0);
      if (id < 0 || id == fhi->file[0].id ||
          (slot && cards[0]->active && id == fhi->file[4].id))
        return -EINVAL;
      fhi->file[4 + slot].id = id;
      fhi->file[4 + slot].size = size;
      cards[slot]->transport = 1;
    } else {
      if (!strncmp(paths[slot], "pfs0:", 5)) {
        const char *partition = getMountedPFSPartition();
        uint64_t mappedSize = 0;
        if (target->device->mode != MODE_HDL || !partition || disc->bdDeviceId != 0 ||
            pfs_vmc_map_file(paths[slot], partition, cards[slot]->frags,
                             &cards[slot]->frag_count, &mappedSize) || mappedSize != size)
          return -EINVAL;
      } else {
        char driver[8] = {0};
        int number = -1;
        int driverId = fileXioIoctl2(iopFd, USBMASS_IOCTL_GET_DRIVERNAME,
                                    NULL, 0, driver, sizeof(driver) - 1);
        if (!driver[0] && driverId >= 0) memcpy(driver, &driverId, sizeof(driverId));
        if (driverId < 0 || strncmp(driver, device->driver, strlen(device->driver)) ||
            fileXioIoctl2(iopFd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0,
                         &number, sizeof(number)) < 0 || number != (int)disc->bdDeviceId)
          return -EXDEV;
        int frags = fileXioIoctl2(iopFd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0,
                                  cards[slot]->frags, sizeof(cards[slot]->frags));
        if (frags <= 0 || frags > 64)
          return -EFBIG;
        cards[slot]->frag_count = frags;
      }
      if (validateMap(cards[slot]))
        return -EINVAL;
      // Refuse overlapping card images or extents that point inside the game ISO.
      for (unsigned int i = 0; i < cards[slot]->frag_count; i++) {
        bd_fragment_t *a = &cards[slot]->frags[i];
        for (unsigned int j = 0; j < disc->fragfile[0].frag_count; j++) {
          bd_fragment_t *b = &disc->frags[j];
          if (a->sector < b->sector + b->count && b->sector < a->sector + a->count)
            return -EINVAL;
        }
        if (slot && cards[0]->active)
          for (unsigned int j = 0; j < cards[0]->frag_count; j++) {
            bd_fragment_t *b = &cards[0]->frags[j];
            if (a->sector < b->sector + b->count && b->sector < a->sector + a->count)
              return -EINVAL;
          }
      }
      close(fd);
      fds[slot] = -1;
    }
    DPRINTF("OPL: VMC slot %d prepared (%u pages)\n", slot + 1, cards[slot]->cspec.CardSize);
    count++;
  }
  return count;
}
