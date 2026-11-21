#include "quick_play.h"
#include "rocket_quick_play.h"
#include "common.h"
#include "devices/devices.h"
#include "options.h"
#include "ui/graphics.h"
#include <errno.h>
#include <fcntl.h>
#ifdef __PS2__
#include <libmc.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUICK_PLAY_FOLDER ":/APP_LUNA_EXPERIMENTAL/"
static char recordPath[PATH_MAX + 1];
static char tempPath[PATH_MAX + 1];
static char settingsPath[PATH_MAX + 1];
static const char enabledSetting[] = "On\n";
static int slot = -1;
static int enabled;
static int quickBoot;

static int readAll(int fd, void *buffer, size_t length) {
  uint8_t *bytes = buffer;
  while (length) {
    ssize_t count = read(fd, bytes, length);
    if (count <= 0) return -1;
    bytes += count;
    length -= count;
  }
  return 0;
}

static int writeFile(const char *path, const void *header, size_t headerBytes,
                     const void *payload, size_t payloadBytes) {
  FILE *file = fopen(path, "wb");
  if (!file) return -1;
  int failed = fwrite(header, 1, headerBytes, file) != headerBytes ||
      (payloadBytes && fwrite(payload, 1, payloadBytes, file) != payloadBytes);
  if (fclose(file)) failed = 1;
  return failed ? -1 : 0;
}

static int readEnabled(void) {
  int fd = open(settingsPath, O_RDONLY);
  if (fd < 0) return 0;
  char value[sizeof(enabledSetting)];
  int count = read(fd, value, sizeof(value));
  close(fd);
  return count == sizeof(enabledSetting) - 1 && !memcmp(value, enabledSetting, count);
}

void quickPlayConfigure(const char *directory, const char *bootPath) {
  settingsPath[0] = recordPath[0] = tempPath[0] = '\0';
  slot = -1;
  enabled = quickBoot = 0;
  if (!directory || !directory[0]) return;
  const char *separator = directory[strlen(directory) - 1] == '/' ? "" : "/";
  int length = snprintf(settingsPath, sizeof(settingsPath), "%s%sQUICKPLAY.CFG", directory, separator);
  if (length < 0 || (size_t)length >= sizeof(settingsPath)) {
    settingsPath[0] = '\0';
    return;
  }
  snprintf(recordPath, sizeof(recordPath), "%s%sQUICKPLAY.DAT", directory, separator);
  snprintf(tempPath, sizeof(tempPath), "%s%sQUICKPLAY.TMP", directory, separator);
  if (!strncmp(directory, "mc", 2) && (directory[2] == '0' || directory[2] == '1') &&
      directory[3] == ':' && directory[4] == '/') {
    slot = directory[2] - '0';
    // Auto-launch is reserved for the existing ROCKET shortcut on its test card.
    quickBoot = bootPath && !strncmp(bootPath, directory, 3) &&
                !strcmp(bootPath + 3, QUICK_PLAY_FOLDER "luna-quickstart.elf");
  }
  enabled = readEnabled();
}

int quickPlayEnabled(void) { return enabled; }
int quickPlayIsBoot(void) { return quickBoot && enabled; }

static int removeRecord(void) {
  if (!remove(recordPath) || errno == ENOENT || configFileMissing(recordPath)) return 0;
  // If deletion fails, opening an existing file with truncation invalidates it.
  int fd = open(recordPath, O_WRONLY | O_TRUNC);
  return fd < 0 ? -1 : close(fd);
}

int quickPlaySetEnabled(int value) {
  if (!settingsPath[0]) return -1;
  if (value) {
    if (writeFile(settingsPath, enabledSetting, sizeof(enabledSetting) - 1, NULL, 0) ||
        !readEnabled()) return -1;
    enabled = 1;
    return 0;
  }
  if (remove(settingsPath) && errno != ENOENT && !configFileMissing(settingsPath)) return -1;
  enabled = 0;
  remove(tempPath);
  return removeRecord() ? -2 : 0;
}

static int readRecord(const char *path, RocketQuickPlay *header) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  off_t size = lseek(fd, 0, SEEK_END);
  int result = -1;
  if (lseek(fd, 0, SEEK_SET) < 0 || readAll(fd, header, sizeof(*header)) ||
      !rocketQuickPlayValid(header) || (header->flags & ROCKET_QUICK_PLAY_SAMPLE_RECORD) ||
      size != (off_t)(sizeof(*header) + header->payloadBytes))
    goto done;

  uint8_t buffer[2048];
  uint32_t remaining = header->payloadBytes;
  uint32_t hash = 2166136261u;
  while (remaining) {
    uint32_t count = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (readAll(fd, buffer, count)) goto done;
    for (size_t i = 0; i < count; i++) hash = (hash ^ buffer[i]) * 16777619u;
    remaining -= count;
  }
  if (!header->payloadBytes || hash == header->coverHash) result = 0;
done:
  if (close(fd) < 0) result = -1;
  return result;
}

static int publishRecord(const RocketQuickPlay *expected) {
  RocketQuickPlay checked;
  if (readRecord(tempPath, &checked) || memcmp(expected, &checked, sizeof(checked)))
    return -1;
  if (slot < 0) {
    if (commitConfigFile(tempPath, recordPath)) return -1;
  } else if (rename(tempPath, recordPath)) {
    // Some filesystems need the old name removed before renaming a replacement.
    remove(recordPath);
    if (!rename(tempPath, recordPath)) goto verify;
#ifdef __PS2__
    // Some MC drivers only expose the same-directory rename through libmc.
    int status;
    if (mcInit(MC_TYPE_MC) < 0 || mcRename(slot, 0, tempPath + 4, "QUICKPLAY.DAT") < 0 ||
        mcSync(MC_WAIT, NULL, &status) != 1 || status != 0)
#endif
    {
      // The card may lack rename support. The verified stage stays available
      // until the copy closes, and readers reject an incomplete destination.
      if (commitConfigFile(tempPath, recordPath)) return -1;
    }
  }
verify:
  return readRecord(recordPath, &checked) || memcmp(expected, &checked, sizeof(checked)) ? -1 : 0;
}

// Match the older bridge's bilinear filter and conversion from inverted GS alpha.
static void resampleCover(uint8_t *output, const GSTEXTURE *source) {
  const uint8_t *pixels = (const uint8_t *)source->Mem;
  int width = source->Width;
  int height = source->Height;
  for (int y = 0; y < ROCKET_QUICK_PLAY_HEIGHT; y++) {
    int sy = ((2 * y + 1) * height * 256 / (2 * ROCKET_QUICK_PLAY_HEIGHT)) - 128;
    if (sy < 0) sy = 0;
    int y0 = sy >> 8;
    int y1 = y0 + 1 < height ? y0 + 1 : y0;
    int fy = sy & 255;
    for (int x = 0; x < ROCKET_QUICK_PLAY_WIDTH; x++) {
      int sx = ((2 * x + 1) * width * 256 / (2 * ROCKET_QUICK_PLAY_WIDTH)) - 128;
      if (sx < 0) sx = 0;
      int x0 = sx >> 8;
      int x1 = x0 + 1 < width ? x0 + 1 : x0;
      int fx = sx & 255;
      uint8_t *dst = output + (y * ROCKET_QUICK_PLAY_WIDTH + x) * 4;
      const uint8_t *a = pixels + (y0 * width + x0) * 4;
      const uint8_t *b = pixels + (y0 * width + x1) * 4;
      const uint8_t *c = pixels + (y1 * width + x0) * 4;
      const uint8_t *d = pixels + (y1 * width + x1) * 4;
      for (int channel = 0; channel < 4; channel++) {
        int top = a[channel] * (256 - fx) + b[channel] * fx;
        int bottom = c[channel] * (256 - fx) + d[channel] * fx;
        int value = (top * (256 - fy) + bottom * fy + 32768) >> 16;
        dst[channel] = channel == 3 ? 255 - value * 255 / 128 : value;
      }
    }
  }
}

static uint8_t *readCover(const Target *target) {
  const struct DeviceMapEntry *device = target->device->metadev ?
      target->device->metadev : target->device;
  if (!device->mountpoint) return NULL;
  char path[PATH_MAX + 1];
  int length = snprintf(path, sizeof(path), "%s/ART/%s_COV.png", device->mountpoint, target->id);
  if (length < 0 || (size_t)length >= sizeof(path)) return NULL;
  GSTEXTURE texture = {.Delayed = 1};
  if (decodePNGTextureForQuickPlay(&texture, path)) return NULL;
  uint8_t *payload = malloc(ROCKET_QUICK_PLAY_PIXELS);
  if (payload) resampleCover(payload, &texture);
  free(texture.Mem);
  return payload;
}

int quickPlayExport(Target *target) {
  if (!enabled || !target || target->platform != TARGET_PS2) return 0;
  RocketQuickPlay header = {
    .magic = ROCKET_QUICK_PLAY_MAGIC, .version = 1, .headerBytes = sizeof(header),
    .flags = ROCKET_QUICK_PLAY_GAME, .width = ROCKET_QUICK_PLAY_WIDTH,
    .height = ROCKET_QUICK_PLAY_HEIGHT
  };
  int relative = target->fullPath ? getRelativePathIdx(target->fullPath) : -1;
  int result = -1;
  if (target->device && target->id && target->id[0] && relative >= 0 &&
      strlen(target->id) < sizeof(header.titleId) &&
      strlen(target->fullPath + relative) < sizeof(header.relativePath)) {
    strcpy(header.titleId, target->id);
    strcpy(header.relativePath, target->fullPath + relative);
    snprintf(header.title, sizeof(header.title), "%s",
             target->name && target->name[0] ? target->name : target->id);
    uint8_t *payload = readCover(target);
    if (payload) {
      header.flags |= ROCKET_QUICK_PLAY_COVER;
      header.payloadBytes = ROCKET_QUICK_PLAY_PIXELS;
      header.coverHash = rocketQuickPlayHash(payload, header.payloadBytes);
    }
    header.headerHash = rocketQuickPlayHeaderHash(&header);
    if (!writeFile(tempPath, &header, sizeof(header), payload, header.payloadBytes))
      result = publishRecord(&header);
    free(payload);
  }
  remove(tempPath);
  if (result) removeRecord();
  return result;
}

Target *quickPlayFindTarget(TargetList *titles) {
  RocketQuickPlay header;
  if (!enabled || !titles || readRecord(recordPath, &header)) return NULL;
  Target *match = NULL;
  for (Target *target = titles->first; target; target = target->next) {
    if (target->platform != TARGET_PS2 || !target->id || !target->fullPath ||
        strcmp(header.titleId, target->id)) continue;
    int relative = getRelativePathIdx(target->fullPath);
    if (relative < 0 || strcmp(header.relativePath, target->fullPath + relative)) continue;
    if (match) return NULL;
    match = target;
  }
  return match;
}
