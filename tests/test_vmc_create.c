#include "vmc_create.h"
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t get32(const unsigned char *data) {
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void readAt(int fd, off_t offset, unsigned char *data, size_t size) {
  assert(lseek(fd, offset, SEEK_SET) == offset);
  assert(read(fd, data, size) == (ssize_t)size);
}

int main(void) {
  char directory[] = "/tmp/luna-vmc-XXXXXX";
  char path[80];
  unsigned char data[1024];
  struct stat info;
  assert(mkdtemp(directory) != NULL);
  snprintf(path, sizeof(path), "%s/card.bin", directory);
  assert(lunaCreateVMC8(path, NULL, NULL) == 0);
  assert(stat(path, &info) == 0 && info.st_size == 8 * 1024 * 1024);
  int fd = open(path, O_RDONLY);
  assert(fd >= 0);
  readAt(fd, 0, data, sizeof(data));
  assert(memcmp(data, "Sony PS2 Memory Card Format ", 28) == 0);
  assert(get32(data + 48) == 8192);
  assert(get32(data + 52) == 41);
  assert(get32(data + 80) == 8);
  assert(get32(data + 352) == 1);
  readAt(fd, 8 * 1024, data, sizeof(data));
  assert(get32(data) == 9 && get32(data + 31 * 4) == 40);
  readAt(fd, 9 * 1024, data, sizeof(data));
  assert(get32(data) == 0xffffffffU && get32(data + 4) == 0x7fffffffU);
  readAt(fd, 41 * 1024, data, sizeof(data));
  assert(data[64] == '.' && data[65] == 0);
  assert(data[512 + 64] == '.' && data[512 + 65] == '.');
  close(fd);
  assert(lunaCreateVMC8(path, NULL, NULL) == -1);
  assert(stat(path, &info) == 0 && info.st_size == 8 * 1024 * 1024);
  unlink(path);
  rmdir(directory);
  puts("VMC creation: ok");
  return 0;
}
