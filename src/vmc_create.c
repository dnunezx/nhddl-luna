// Raw PS2 card layout follows OPL's genvmc formatter (AFL-3.0).
// https://github.com/ps2homebrew/Open-PS2-Loader/blob/master/modules/vmc/genvmc/genvmc.c
#include "vmc_create.h"
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define CLUSTER_SIZE 1024
#define CARD_CLUSTERS 8192
#define ERASE_CLUSTER_COUNT 16
#define ERASE_BLOCK_CLUSTERS 8
#define IFC_CLUSTER 8
#define FAT_CLUSTER 9
#define FAT_CLUSTER_COUNT 32
#define ALLOC_CLUSTER 41

static void put16(uint8_t *bytes, unsigned offset, uint16_t value) {
  bytes[offset] = (uint8_t)value;
  bytes[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, unsigned offset, uint32_t value) {
  put16(bytes, offset, (uint16_t)value);
  put16(bytes, offset + 2, (uint16_t)(value >> 16));
}

static int writeAll(int fd, const void *data, unsigned size) {
  const uint8_t *bytes = data;
  while (size != 0) {
    int written = write(fd, bytes, size);
    if (written <= 0)
      return -1;
    bytes += written;
    size -= written;
  }
  return 0;
}

static int writeCluster(int fd, unsigned cluster, const uint8_t *data) {
  if (lseek(fd, (off_t)cluster * CLUSTER_SIZE, SEEK_SET) < 0)
    return -1;
  return writeAll(fd, data, CLUSTER_SIZE);
}

int lunaCreateVMC8(const char *path, void (*progress)(int, void *), void *context) {
  uint8_t buffer[ERASE_CLUSTER_COUNT * CLUSTER_SIZE];
  int fd;
  if (path == NULL || path[0] == '\0')
    return -1;
  fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return -1;

  memset(buffer, 0xff, sizeof(buffer));
  for (unsigned cluster = 0; cluster < CARD_CLUSTERS;
       cluster += ERASE_CLUSTER_COUNT) {
    if (writeAll(fd, buffer, sizeof(buffer)) < 0)
      goto fail;
    if (progress != NULL && (cluster % 256) == 0)
      progress((int)(cluster * 95 / CARD_CLUSTERS), context);
  }

  // IFC maps each FAT cluster. FAT entries describe allocatable clusters.
  memset(buffer, 0, CLUSTER_SIZE);
  for (unsigned i = 0; i < FAT_CLUSTER_COUNT; i++)
    put32(buffer, i * 4, FAT_CLUSTER + i);
  if (writeCluster(fd, IFC_CLUSTER, buffer) < 0)
    goto fail;
  const unsigned allocEnd = (CARD_CLUSTERS / ERASE_BLOCK_CLUSTERS - 2) *
                            ERASE_BLOCK_CLUSTERS;
  for (unsigned fat = 0; fat < FAT_CLUSTER_COUNT; fat++) {
    memset(buffer, 0, CLUSTER_SIZE);
    for (unsigned entry = 0; entry < 256; entry++) {
      unsigned absolute = ALLOC_CLUSTER + fat * 256 + entry;
      if (absolute < allocEnd)
        put32(buffer, entry * 4,
              fat == 0 && entry == 0 ? 0xffffffffU : 0x7fffffffU);
    }
    if (writeCluster(fd, FAT_CLUSTER + fat, buffer) < 0)
      goto fail;
  }

  // Two empty directory entries: "." and "..".
  memset(buffer, 0xff, CLUSTER_SIZE);
  for (unsigned entry = 0; entry < 2; entry++) {
    uint8_t *item = buffer + entry * 512;
    memset(item, 0, 512);
    put16(item, 0, entry == 0 ? 0x8427 : 0xa426);
    put32(item, 4, 2);
    item[12] = 1; // creation day
    item[13] = 1; // creation month
    put16(item, 14, 2000);
    item[28] = 1; // modification day
    item[29] = 1; // modification month
    put16(item, 30, 2000);
    item[64] = '.';
    if (entry == 1)
      item[65] = '.';
  }
  if (writeCluster(fd, ALLOC_CLUSTER, buffer) < 0)
    goto fail;

  memset(buffer, 0xff, CLUSTER_SIZE);
  memset(buffer, 0, 40);
  memcpy(buffer, "Sony PS2 Memory Card Format ", 28);
  memcpy(buffer + 28, "1.2.0.0", 7);
  put16(buffer, 40, 512);  // page size
  put16(buffer, 42, 2);    // pages per cluster
  put16(buffer, 44, 16);   // pages per erase block
  put32(buffer, 48, CARD_CLUSTERS);
  put32(buffer, 52, ALLOC_CLUSTER);
  put32(buffer, 56, allocEnd - ALLOC_CLUSTER);
  put32(buffer, 60, 0);    // root directory cluster, relative to alloc offset
  put32(buffer, 64, CARD_CLUSTERS / ERASE_BLOCK_CLUSTERS - 1);
  put32(buffer, 68, CARD_CLUSTERS / ERASE_BLOCK_CLUSTERS - 2);
  memset(buffer + 80, 0xff, 32 * 4); // IFC list
  put32(buffer, 80, IFC_CLUSTER);
  memset(buffer + 208, 0xff, 32 * 4); // bad block list
  buffer[336] = 2;       // PS2 card
  buffer[337] = 0x2b;
  put32(buffer, 340, CLUSTER_SIZE);
  put32(buffer, 344, 256); // FAT entries per cluster
  put32(buffer, 348, ERASE_BLOCK_CLUSTERS);
  put32(buffer, 352, 1);   // formatted
  put32(buffer, 356, 0);   // rootdir_cluster2
  put32(buffer, 368, 8001); // OPL's max allocatable cluster calculation
  put32(buffer, 380, 0xffffffffU);
  if (writeCluster(fd, 0, buffer) < 0 || close(fd) < 0) {
    fd = -1;
    goto fail;
  }
  if (progress != NULL)
    progress(100, context);
  return 0;

fail:
  if (fd >= 0)
    close(fd);
  unlink(path);
  return -1;
}
