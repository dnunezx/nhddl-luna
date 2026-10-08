// Disc-logo validation follows the pinned loader's src/util.c:CheckPS2Logo.
#include "opl_logo.h"
#include "dprintf.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static int readExact(int fd, void *buffer, size_t size) {
  unsigned char *out = buffer;
  while (size) {
    ssize_t count = read(fd, out, size);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      return 0;
    out += count;
    size -= count;
  }
  return 1;
}

int oplPrepareLogo(int discFd) {
  char romver[14];
  int fd = open("rom0:ROMVER", O_RDONLY);
  if (fd < 0)
    return 0;
  int known = readExact(fd, romver, sizeof(romver));
  close(fd);
  if (!known || (romver[4] != 'E' && romver[4] != 'J' &&
                 romver[4] != 'A' && romver[4] != 'C' && romver[4] != 'H'))
    return 0;
  fd = open("rom0:PS2LOGO", O_RDONLY);
  if (fd < 0)
    return 0;
  close(fd);

  const size_t size = 12 * 2048;
  unsigned char *logo = malloc(size);
  if (logo == NULL)
    return 0;
  int valid = lseek(discFd, 0, SEEK_SET) == 0 &&
              readExact(discFd, logo, size) && logo[0] != 0;
  uint32_t checksum = 0;
  if (valid) {
    unsigned char key = logo[0];
    for (size_t i = 0; i < size; i++) {
      unsigned char decoded = logo[i] ^ key;
      checksum += (unsigned char)((decoded << 3) | (decoded >> 5));
    }
    valid = checksum == (romver[4] == 'E' ? 0x1555abu : 0x120519u);
  }
  free(logo);
  DPRINTF("OPL: startup logo %s (checksum %06x, BIOS region %c)\n",
          valid ? "enabled" : "skipped", (unsigned int)checksum, romver[4]);
  return valid;
}
