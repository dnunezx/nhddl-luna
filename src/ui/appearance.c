// LUNA appearance preferences: one validated record per metadata location.
#include "ui/appearance.h"
#include "ui/ring_menu.h"
#include "ui/view_state.h"
#include "devices/devices.h"
#include "options.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#define APPEARANCE_VERSION 4U
#define RECORD_LIMIT 2048
#define RECORD_WRITE_SIZE 512
static const char *const recordNames[] = {
  "/appearance.cfg", "/appearance.cfg.tmp", "/appearance.cfg.bak"
};
static const char *const backgrounds[] = {
  "stars", "orbs", "red-clouds", "midnight-cubes", "system-config", "neo",
  "aurora", "ronin", "torch", "tempest", "space-flight"
};
static const char *const glasses[] = {"original", "white-gray", "black", "cyber"};
static const char *const fonts[] = {"dejavu", "psbbn"};
static const char *const switches[] = {"off", "on"};
static const char *const layouts[] = {"separate", "overlap"};
static const char *const cases[] = {"tall", "square"};
static const char *const themes[] = {"luna", "ps2-original"};
static const char *const colors[] = {"original", "cyan", "violet", "rose", "green", "gold", "white"};
static const char *const ringColors[] = {"original","cyan","blue","violet","rose","green","white"};
static const char *const spaceSpeeds[] = {"0","1","2","3","4","5","6","7","8"};
static const char *const spaceDepths[] = {"0","1","2","3","4"};
_Static_assert(sizeof(ringColors)/sizeof(*ringColors)==LUNA_RING_COLOR_COUNT,"Update ring color storage names");
static const char *const views[UI_VIEW_ID_LIMIT] = {
  [UI_VIEW_CLASSIC] = "classic", [UI_VIEW_PSBBN] = "collection",
  [UI_VIEW_ORBIT] = "orbit", [UI_VIEW_ORBS] = "scroll", [UI_VIEW_3D] = "3d",
  [UI_VIEW_COLLECTION_V2] = "collection-v2"
};
static const char *const shapes[] = {
  "diamond", "cube", "octahedron", "sphere", "luna", "skull", "atom", "iron-cross"
};

_Static_assert(sizeof(backgrounds) / sizeof(*backgrounds) == LIBRARY_BACKGROUND_COUNT, "Update background storage names");
_Static_assert(sizeof(glasses) / sizeof(*glasses) == GLASS_COLOR_COUNT, "Update glass storage names");
_Static_assert(sizeof(fonts) / sizeof(*fonts) == UI_FONT_COUNT, "Update font storage names");
_Static_assert(sizeof(colors) / sizeof(*colors) == ORBS_COLOR_COUNT, "Update color storage names");
_Static_assert(sizeof(shapes) / sizeof(*shapes) == ORB_SHAPE_PLAYTIME, "Update shape storage names");

typedef struct {
  const char *key;
  size_t offset;
  const char *const *names;
  unsigned count;
  int mask;
} AppearanceField;
#define FIELD(member, names, mask) {#member, offsetof(LunaAppearance, member), names, sizeof(names)/sizeof(*names), mask}
static const AppearanceField fields[] = {
  FIELD(background, backgrounds, 0), FIELD(glassColor, glasses, 0),
  FIELD(font, fonts, 0), FIELD(ambientSound, switches, 0),
  FIELD(enabledViews, views, 1), FIELD(classicOverlap, layouts, 0),
  FIELD(collectionGameList, switches, 0), FIELD(ps1CaseStyle, cases, 0),
  FIELD(orbsTheme, themes, 0), FIELD(orbsAppearance, themes, 0),
  FIELD(orbsColor, colors, 0), FIELD(tailsColor, colors, 0),
  FIELD(orbShapes, shapes, 1), FIELD(ringColor, ringColors, 0),
  FIELD(spaceSpeed, spaceSpeeds, 0), FIELD(spaceDepth, spaceDepths, 0)
};
#define FIELD_COUNT (sizeof(fields)/sizeof(*fields))
typedef struct {
  LunaAppearance settings;
  uint32_t generation;
} AppearanceRecord;
static struct {
  char root[PATH_MAX];
  AppearanceRecord record;
  int valid, error;
} cache;

void resetAppearanceCache(void) { memset(&cache, 0, sizeof(cache)); }

static const char *appearanceRoot(Target *target) {
  if (!target || !target->device) return NULL;
  struct DeviceMapEntry *device = target->device->metadev ? target->device->metadev : target->device;
  return device->mountpoint;
}

static uint32_t checksum(const char *bytes, size_t length) {
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < length; i++) {
    crc ^= (unsigned char)bytes[i];
    for (int bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

static uint32_t *fieldValue(LunaAppearance *settings, const AppearanceField *field) {
  return (uint32_t *)((char *)settings + field->offset);
}

static int parseUnsigned(const char *text, uint32_t *value) {
  if (!text[0] || strspn(text, "0123456789") != strlen(text)) return -EINVAL;
  char *end;
  errno = 0;
  unsigned long parsed = strtoul(text, &end, 10);
  if (errno || *end || parsed > UINT32_MAX) return -EINVAL;
  *value = (uint32_t)parsed;
  return 0;
}

static int parseField(const AppearanceField *field, char *value, uint32_t *out) {
  *out = 0;
  // Retired backgrounds fall back without discarding the other preferences.
  if (field->offset == offsetof(LunaAppearance, background) && !strcmp(value, "gridrunner")) {
    *out = LIBRARY_BACKGROUND_STARS;
    return 0;
  }
  if (field->mask && !strcmp(value, "none"))
    return field->offset == offsetof(LunaAppearance, orbShapes) ? 0 : -EINVAL;
  do {
    char *next = strchr(value, ',');
    if (next) *next++ = '\0';
    unsigned i;
    for (i = 0; i < field->count; i++)
      if (field->names[i] && !strcmp(value, field->names[i])) break;
    if (i == field->count || (!field->mask && next)) return -EINVAL;
    if (!field->mask) { *out = i; return 0; }
    if (*out & (1U << i)) return -EINVAL;
    *out |= 1U << i;
    if (!next) return 0;
    value = next;
  } while (*value);
  return -EINVAL;
}

static int validSettings(const LunaAppearance *settings) {
  for (unsigned i = 0; i < FIELD_COUNT; i++) {
    const AppearanceField *f = &fields[i];
    uint32_t value = *(const uint32_t *)((const char *)settings + f->offset);
    if (!f->mask) {
      if (value >= f->count || !f->names[value]) return 0;
    } else {
      if (!value && f->offset != offsetof(LunaAppearance, orbShapes)) return 0;
      for (unsigned bit = 0; bit < 32; bit++)
        if ((value & (1U << bit)) && (bit >= f->count || !f->names[bit])) return 0;
    }
  }
  return 1;
}

static int recordMissing(const char *path) {
  // BDM FatFs exposes negated FRESULTs through the PS2 file API. NO_FILE
  // and NO_PATH are 4/5; its 2 is an internal error, not POSIX ENOENT.
  if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':') {
    iox_stat_t info;
    int result = fileXioGetStat(path, &info);
    return result == -4 || result == -5;
  }
  struct stat info;
  return stat(path, &info) < 0 && errno == ENOENT;
}

static int readRecord(const char *path, AppearanceRecord *record) {
  FILE *file = fopen(path, "rb");
  if (!file) return recordMissing(path) ? -ENOENT : -EIO;
  char bytes[RECORD_LIMIT + 1];
  size_t length = fread(bytes, 1, RECORD_LIMIT, file);
  int bad = ferror(file), extra = fgetc(file);
  if (ferror(file)) bad = 1;
  if (fclose(file)) bad = 1;
  if (bad) return -EIO;
  if (extra != EOF || !length || memchr(bytes, 0, length)) return -EINVAL;
  bytes[length] = '\0';
  char *lineEnd = strchr(bytes, '\n');
  if (!lineEnd || strncmp(bytes, "format=", 7)) return -EINVAL;
  *lineEnd = '\0';
  uint32_t version;
  int result = parseUnsigned(bytes + 7, &version);
  *lineEnd = '\n';
  if (result) return result;
  // Protect future formats even when an older supported backup exists.
  if (version > APPEARANCE_VERSION) return -ENOTSUP;
  if (version < 1U) return -EINVAL;
  char *footer = strstr(bytes, "\nchecksum=");
  if (!footer || strlen(footer + 1) != 18 || footer[18] != '\n') return -EINVAL;
  char *hex = footer + 10;
  if (strspn(hex, "0123456789abcdefABCDEF") != 8) return -EINVAL;
  uint32_t expected = (uint32_t)strtoul(hex, NULL, 16);
  if (checksum(bytes, (size_t)(footer + 1 - bytes)) != expected) return -EINVAL;
  *footer = '\0';
  memset(record, 0, sizeof(*record));
  // Older records retain Space Flight's original speed and depth defaults.
  record->settings.spaceSpeed=4;
  record->settings.spaceDepth=2;
  char *line = lineEnd + 1;
  lineEnd = strchr(line, '\n');
  if (!lineEnd) return -EINVAL;
  *lineEnd = '\0';
  if (strncmp(line, "generation=", 11) || parseUnsigned(line + 11, &record->generation) ||
      !record->generation) return -EINVAL;
  line = lineEnd + 1;
  uint32_t seen = 0;
  while (*line) {
    lineEnd = strchr(line, '\n');
    if (lineEnd) *lineEnd = '\0';
    char *equals = strchr(line, '=');
    if (!equals) return -EINVAL;
    *equals++ = '\0';
    unsigned i;
    for (i = 0; i < FIELD_COUNT; i++) if (!strcmp(line, fields[i].key)) break;
    if (i == FIELD_COUNT || (seen & (1U << i)) ||
        parseField(&fields[i], equals, fieldValue(&record->settings, &fields[i]))) return -EINVAL;
    seen |= 1U << i;
    if (!lineEnd) break;
    line = lineEnd + 1;
  }
  // Fields are appended: v1 has no ring color; v2 has no Space Flight settings.
  unsigned count=FIELD_COUNT-(version==1U?3:version==2U?2:0);
  uint32_t required=(1U << count)-1U;
  // Introduce the new view once; subsequent records preserve its On/Off choice.
  if (version < 4U && (record->settings.enabledViews & (1U << UI_VIEW_PSBBN)))
    record->settings.enabledViews |= 1U << UI_VIEW_COLLECTION_V2;
  return seen == required && validSettings(&record->settings) ? 0 : -EINVAL;
}

static int serializeRecord(const AppearanceRecord *record, char bytes[RECORD_LIMIT]) {
  int length = snprintf(bytes, RECORD_LIMIT, "format=%u\ngeneration=%u\n", APPEARANCE_VERSION,
                        (unsigned)record->generation);
  for (unsigned i = 0; i < FIELD_COUNT; i++) {
    const AppearanceField *f = &fields[i];
    uint32_t value = *(const uint32_t *)((const char *)&record->settings + f->offset);
    char valueText[256] = {0};
    if (!f->mask) snprintf(valueText, sizeof(valueText), "%s", f->names[value]);
    else {
      for (unsigned bit = 0; bit < f->count; bit++) if (value & (1U << bit)) {
        size_t used = strlen(valueText);
        int added = snprintf(valueText + used, sizeof(valueText) - used, "%s%s",
                             used ? "," : "", f->names[bit]);
        if (added < 0 || (size_t)added >= sizeof(valueText) - used) return -EOVERFLOW;
      }
      if (!value) strcpy(valueText, "none");
    }
    int added = snprintf(bytes + length, RECORD_LIMIT - length, "%s=%s\n", f->key, valueText);
    if (added < 0 || added >= RECORD_LIMIT - length) return -EOVERFLOW;
    length += added;
  }
  // Keep replacements the same size using version-1's existing numeric format.
  // Leading zeroes in the generation preserve compatibility with older readers.
  const int padding = RECORD_WRITE_SIZE - 18 - length;
  if (padding < 0) return -EOVERFLOW;
  char *generation = strchr(bytes, '\n') + 1 + strlen("generation=");
  size_t offset = (size_t)(generation - bytes);
  memmove(generation + padding, generation, (size_t)length - offset);
  memset(generation, '0', (size_t)padding);
  length += padding;
  uint32_t crc = checksum(bytes, (size_t)length);
  int added = snprintf(bytes + length, RECORD_LIMIT - length, "checksum=%08x\n", (unsigned)crc);
  return added < 0 || added >= RECORD_LIMIT - length ? -EOVERFLOW : length + added;
}

static int readLatest(Target *target, AppearanceRecord *record) {
  const char *root = appearanceRoot(target);
  memset(record, 0, sizeof(*record));
  if (!root) { loadLegacyAppearance(target, &record->settings); return -EINVAL; }
  int error = 0, found = 0;
  for (unsigned i = 0; i < 3; i++) {
    char path[PATH_MAX];
    AppearanceRecord candidate;
    int result = buildConfigFilePath(path, sizeof(path), root, recordNames[i]);
    if (!result) result = readRecord(path, &candidate);
    if (!result && (!found || candidate.generation > record->generation)) {
      *record = candidate;
      found = 1;
    }
    if (result == -ENOTSUP || (result && result != -ENOENT && result != -EINVAL && !error))
      error = result;
  }
  if (!found) loadLegacyAppearance(target, &record->settings);
  return error;
}

int loadAppearance(Target *target, LunaAppearance *settings) {
  if (!settings) return -EINVAL;
  const char *root = appearanceRoot(target);
  if (!root) { loadLegacyAppearance(target, settings); return -EINVAL; }
  if (!cache.valid || strcmp(cache.root, root)) {
    cache.valid = 0;
    if (strlen(root) >= sizeof(cache.root)) {
      loadLegacyAppearance(target, settings);
      return -ENAMETOOLONG;
    }
    cache.error = readLatest(target, &cache.record);
    strcpy(cache.root, root);
    // A transient read failure must not disable settings until a storage rescan.
    cache.valid = cache.error == 0;
  }
  *settings = cache.record.settings;
  return cache.error;
}

static int writeRecord(const char *path, const AppearanceRecord *record,
                       struct DeviceMapEntry *device) {
  char bytes[RECORD_LIMIT];
  int length = serializeRecord(record, bytes);
  if (length < 0) return length;
  // Reuse file entries: repeated unlink/create can leave newly written BDM
  // files unavailable. Fixed lengths also prevent stale bytes from old tails.
  // An oversized legacy copy can be recreated with recovery elsewhere intact.
  struct stat info;
  if (!stat(path, &info) && info.st_size > length && remove(path)) return -EIO;
  FILE *file = fopen(path, "r+b");
  if (!file && recordMissing(path)) file = fopen(path, "wb");
  if (!file) return -EIO;
  int result = fwrite(bytes, 1, (size_t)length, file) == (size_t)length ? 0 : -EIO;
  if (fclose(file)) result = -EIO;
  if (device->sync) device->sync();
  if (result) return result;
  AppearanceRecord verified;
  result = readRecord(path, &verified);
  if (result || verified.generation != record->generation ||
      memcmp(&verified.settings, &record->settings, sizeof(record->settings))) return -EIO;
  return 0;
}

int saveAppearance(Target *target, const LunaAppearance *settings) {
  const char *root = appearanceRoot(target);
  if (!root || !settings || !validSettings(settings)) return -EINVAL;
  char paths[3][PATH_MAX], directory[PATH_MAX];
  for (unsigned i = 0; i < 3; i++)
    if (buildConfigFilePath(paths[i], sizeof(paths[i]), root, recordNames[i])) return -ENAMETOOLONG;
  if (buildConfigFilePath(directory, sizeof(directory), root, NULL)) return -ENAMETOOLONG;
  AppearanceRecord previous;
  int result = readLatest(target, &previous);
  if (result) return result;
  int unchanged = previous.generation && !memcmp(settings, &previous.settings, sizeof(*settings));
  if (!unchanged && previous.generation == UINT32_MAX) return -EOVERFLOW;
  struct stat info;
  if (stat(directory, &info) && mkdir(directory, 0777)) return -EIO;
  struct DeviceMapEntry *device = target->device->metadev ? target->device->metadev : target->device;
  // Establish a verified primary before reusing either recovery file. In
  // particular, the only surviving record may currently be the backup or tmp.
  AppearanceRecord primary;
  if (previous.generation && (readRecord(paths[0], &primary) ||
      primary.generation != previous.generation ||
      memcmp(&primary.settings, &previous.settings, sizeof(primary.settings)))) {
    result = writeRecord(paths[0], &previous, device);
    if (result) return result;
  }
  if (unchanged) {
    resetAppearanceCache();
    return 0;
  }
  // First migration also gets a recoverable snapshot of the imported settings.
  AppearanceRecord backup = previous;
  if (!backup.generation) backup.generation = 1;
  result = writeRecord(paths[2], &backup, device);
  if (result) return result;
  AppearanceRecord next = {.settings = *settings, .generation = backup.generation + 1};
  result = writeRecord(paths[1], &next, device);
  if (result) return result;
  // Copy rather than rename: some PS2 filesystems cannot replace by rename.
  // The validated tmp survives until the committed primary is read back.
  result = writeRecord(paths[0], &next, device);
  if (result) return result;
  if (device->sync) device->sync();
  resetAppearanceCache();
  return 0;
}

#define GETTER(type, name, field) \
  type name(Target *target) { LunaAppearance settings; loadAppearance(target, &settings); return (type)settings.field; }
GETTER(LibraryBackground, loadLibraryBackground, background)
GETTER(GlassColorPreset, loadGlassColorPreset, glassColor)
GETTER(UIFont, loadUIFont, font)
GETTER(int, loadAmbientSoundEnabled, ambientSound)
GETTER(uint32_t, loadEnabledLibraryViews, enabledViews)
GETTER(int, loadClassicArtOverlap, classicOverlap)
GETTER(int, loadCollectionGameList, collectionGameList)
GETTER(Ps1CaseStyle, loadPs1CaseStyle, ps1CaseStyle)
GETTER(AmbientOrbsTheme, loadAmbientOrbsTheme, orbsTheme)
GETTER(AmbientOrbsAppearance, loadAmbientOrbsAppearance, orbsAppearance)
GETTER(uint32_t, loadEnabledOrbShapes, orbShapes)
AmbientOrbsColor loadAmbientOrbsColor(Target *target, AmbientOrbsColorPart part) {
  if (part != ORBS_COLOR_PART_ORBS && part != ORBS_COLOR_PART_TAILS) return ORBS_COLOR_ORIGINAL;
  LunaAppearance settings;
  loadAppearance(target, &settings);
  return (AmbientOrbsColor)(part == ORBS_COLOR_PART_ORBS ? settings.orbsColor : settings.tailsColor);
}
