// LUNA owns card management; PSXCore owns shared identities and game-time IO.
#include "luna_psxcore_vmc.h"
#include "luna_psxcore_library.h"
#include "devices/devices.h"
#include "storage.h"
#include "psxcore_file.h"
#include "psxcore_hash.h"
#include "psxcore_boot.h"
#include "psxcore_shared_store.h"
#include "dprintf.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <timer.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static int absent(const char *path, int result) {
  // BDM FatFs returns negated FRESULTs, not POSIX errno: NO_FILE=4,
  // NO_PATH=5. Its -2 means an internal error and must never imply absence.
  if (!strncmp(path, "mass", 4) && path[4] >= '0' && path[4] <= '9' && path[5] == ':')
    return result == -4 || result == -5;
  return result == -ENOENT;
}
static int statPath(const char *path, iox_stat_t *st) {
  iox_stat_t local;
  return fileXioGetStat(path, st ? st : &local);
}
static int missing(const char *path) { return absent(path, statPath(path, NULL)); }
static const char *directory(const char *path) {
  iox_stat_t st;
  int e = statPath(path, &st);
  if (e >= 0) return FIO_S_ISDIR(st.mode) ? NULL : "A folder path is occupied by a file.";
  return absent(path, e) && fileXioMkdir(path, 0777) >= 0 ? NULL : "Cannot create the card folder.";
}
static int readAll(int fd, void *data, unsigned size) {
  unsigned done = 0;
  while (done < size) {
    int n = fileXioRead(fd, (unsigned char *)data + done, size - done);
    if (n <= 0 || (unsigned)n > size - done) return -1;
    done += n;
  }
  return 0;
}
static int writeAll(int fd, const void *data, unsigned size) {
  unsigned done = 0;
  while (done < size) {
    unsigned take = size - done;
    if (take > 4096) take = 4096;
    int n = fileXioWrite(fd, (const unsigned char *)data + done, take);
    if (n <= 0 || (unsigned)n > take) {
      DPRINTF("PS1 card write: fd=%d completed=%u total=%u result=%d\n", fd, done, size, n);
      return -1;
    }
    done += n;
  }
  return 0;
}
static const char *readExact(const char *path, void *data, unsigned size) {
  iox_stat_t st;
  if (statPath(path, &st) < 0 || !FIO_S_ISREG(st.mode) || st.hisize || st.size != size)
    return "The file is missing or has an unexpected size.";
  int fd = fileXioOpen(path, FIO_O_RDONLY, 0);
  if (fd < 0) return "Cannot open the file.";
  unsigned char extra;
  int bad = readAll(fd, data, size);
  if (!bad && fileXioRead(fd, &extra, 1) != 0) bad = 1;
  if (fileXioClose(fd) < 0) bad = 1;
  return bad ? "Cannot read the complete file." : NULL;
}
static const char *writeFresh(const char *path, const void *data, unsigned size) {
  if (!missing(path)) return "A file already exists or needs recovery.";
  int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT, 0666);
  if (fd < 0) { DPRINTF("PS1 card create: path=%s result=%d\n", path, fd); return "Cannot create the file."; }
  int bad = writeAll(fd, data, size);
  if (fileXioClose(fd) < 0) bad = 1;
  void *check = malloc(size);
  if (!check) return "Not enough memory to verify the file.";
  if (!bad && (readExact(path, check, size) || memcmp(check, data, size))) bad = 1;
  free(check);
  if (bad) { DPRINTF("PS1 card verification failed: path=%s bytes=%u\n", path, size); return "The write failed. Staged files were preserved for recovery."; }
  return NULL;
}
// Publish only a newly created, reopened and byte-verified file.
static const char *newFile(const char *path, const void *data, unsigned size) {
  char stage[256];
  if (snprintf(stage, sizeof(stage), "%s.new", path) >= sizeof(stage)) return "The file path is too long.";
  if (!missing(path)) return "A file already exists or needs recovery.";
  const char *error = writeFresh(stage, data, size);
  if (error) return error;
  return fileXioRename(stage, path) < 0 ? "Cannot publish the verified file." : NULL;
}
static void hexDigest(const unsigned char digest[32], char text[65]) {
  for (unsigned i = 0; i < 32; ++i) snprintf(text + i * 2, 3, "%02x", digest[i]);
}
int lunaPsxVmcDevice(const struct DeviceMapEntry *device, char root[8]) {
  if (!device || !device->mountpoint || !(STORAGE_SETTINGS.enabled & device->mode) ||
      (device->mode != MODE_ATA && device->mode != MODE_USB) || device->index > 1) return 0;
  const char *p = device->mountpoint;
  if (strlen(p) < 6 || strncmp(p, "mass", 4) || p[4] < '0' || p[4] > '9' || p[5] != ':' ||
      (p[6] && strcmp(p + 6, "/"))) return 0;
  memcpy(root, p, 6); root[6] = 0;
  return 1;
}
const char *lunaPsxVmcVolume(const char *root, unsigned char id[16], int create) {
  char path[96], stage[100]; NpFileVolumeMarker marker;
  memset(id, 0, 16);
  snprintf(path, sizeof(path), "%s%s", root, NP_FILE_VOLUME_MARKER);
  snprintf(stage, sizeof(stage), "%s.new", path);
  if (!missing(stage)) return "The drive identity needs recovery.";
  if (missing(path)) {
    if (!create) return "This drive has no PS1 card identity yet.";
    snprintf(stage, sizeof(stage), "%s/POPS", root);
    const char *error = directory(stage); if (error) return error;
    // Identity is a persistent uniqueness token, not a security secret.
    struct { uint64_t ticks; int64_t clock; char root[8]; } seed = {0};
    seed.ticks = GetTimerSystemTime(); seed.clock = (int64_t)time(NULL);
    snprintf(seed.root, sizeof(seed.root), "%s", root);
    unsigned char hash[32]; np_boot_sha256(&seed, sizeof(seed), hash);
    np_file_volume_marker_init(&marker, hash);
    error = newFile(path, &marker, sizeof(marker)); if (error) return error;
  }
  const char *error = readExact(path, &marker, sizeof(marker));
  if (error) return error;
  return np_file_volume_marker_check(&marker, sizeof(marker), id) ?
      "The drive identity is invalid. Preserve it before repair." : NULL;
}
const char *lunaPsxVmcFindVolume(const unsigned char id[16], char root[8]) {
  int matches = 0; root[0] = 0;
  if (!np_file_volume_id_present(id)) return "The card set has no drive identity.";
  for (int i = 0; i < MAX_DEVICES; ++i) {
    char candidate[8]; unsigned char found[16];
    if (!lunaPsxVmcDevice(&deviceModeMap[i], candidate)) continue;
    if (!lunaPsxVmcVolume(candidate, found, 0) && !memcmp(id, found, 16)) {
      if (matches && !strcmp(root, candidate)) continue;
      snprintf(root, 8, "%s", candidate); ++matches;
    }
  }
  if (matches != 1) { root[0] = 0; return matches ? "Two drives have the same PS1 identity. Disconnect the clone." : "The card set's drive is unavailable."; }
  return NULL;
}

// Fixed frontend settings record, keyed by full disc content, not its filename.
typedef struct {
  uint32_t magic, version;
  unsigned char disc[32];
  char key[33];
  unsigned char volume[16], reserved[7], sha256[32];
} VmcRecord;
_Static_assert(sizeof(VmcRecord) == 128, "LUNA PS1 settings layout");
static void settingsPath(const LunaPsxVmcSettings *s, char path[160]) {
  char hex[65]; hexDigest(s->disc, hex);
  snprintf(path, 160, "%s/POPS/SETTINGS/%s.vmc", s->gameRoot, hex);
}
const char *lunaPsxVmcLoad(Target *target, LunaPsxVmcSettings *s, LunaPsxVmcProgress progress) {
  memset(s, 0, sizeof(*s));
  if (!target || target->platform != TARGET_PS1 || !lunaPsxVmcDevice(target->device, s->gameRoot) ||
      strncmp(target->fullPath, s->gameRoot, 6) || target->fullPath[6] != '/')
    return "This PS1 game needs supported local storage.";
  iox_stat_t before, after;
  if (statPath(target->fullPath, &before) < 0 || !FIO_S_ISREG(before.mode)) return "The PS1 game is no longer readable.";
  uint64_t total = ((uint64_t)before.hisize << 32) | before.size, done = 0, nextProgress = 0;
  int fd = fileXioOpen(target->fullPath, FIO_O_RDONLY, 0);
  if (fd < 0) return "Cannot open the PS1 game.";
  unsigned char *buffer = malloc(65536); NpSha256 hash; np_sha256_init(&hash);
  const char *error = buffer ? NULL : "Not enough memory to check this disc.";
  while (!error && done < total) {
    unsigned take = total - done > 65536 ? 65536 : (unsigned)(total - done);
    if (readAll(fd, buffer, take)) { error = "Could not read the complete PS1 game."; break; }
    np_sha256_update(&hash, buffer, take); done += take;
    if (progress && (done >= nextProgress || done == total)) {
      if (progress(done, total)) error = "PS1 card preparation cancelled.";
      nextProgress = done + 1024 * 1024;
    }
  }
  if (fileXioClose(fd) < 0 && !error) error = "Could not close the PS1 game.";
  free(buffer);
  if (!error && (statPath(target->fullPath, &after) < 0 || before.size != after.size ||
      before.hisize != after.hisize || memcmp(before.mtime, after.mtime, sizeof(before.mtime))))
    error = "The PS1 game changed while checking its identity.";
  if (error) return error;
  np_sha256_final(&hash, s->disc);
  char path[160], stage[168]; settingsPath(s, path);
  snprintf(stage, sizeof(stage), "%s.new", path);
  int result = statPath(stage, NULL);
  if (result >= 0) return "The PS1 card assignment needs recovery.";
  if (!absent(stage, result)) return "Cannot check the PS1 card assignment. The drive reported a storage error.";
  snprintf(stage, sizeof(stage), "%s.bak", path);
  result = statPath(stage, NULL);
  if (result >= 0) return "The PS1 card assignment needs recovery.";
  if (!absent(stage, result)) return "Cannot check the PS1 card assignment. The drive reported a storage error.";
  result = statPath(path, NULL);
  if (absent(path, result)) return NULL;
  if (result < 0) return "Cannot read the PS1 card assignment. The drive reported a storage error.";
  VmcRecord record; unsigned char check[32];
  error = readExact(path, &record, sizeof(record)); if (error) return error;
  np_boot_sha256(&record, offsetof(VmcRecord, sha256), check);
  unsigned char zero[7] = {0};
  if (record.magic != 0x31434d56 || (record.version != 1 && record.version != 2) || memcmp(record.disc, s->disc, 32) ||
      memcmp(check, record.sha256, 32) ||
      (record.version == 1 ? memcmp(record.reserved, zero, 7) :
       record.reserved[0] > LUNA_PS1_CARDS_SHARED || memcmp(record.reserved + 1, zero, 6)) ||
      !memchr(record.key, 0, sizeof(record.key)) ||
      (record.key[0] ? !np_shared_key_valid(record.key, NP_SHARED_FILE) || !np_file_volume_id_present(record.volume) : np_file_volume_id_present(record.volume)))
    return "The saved PS1 card assignment is invalid. Preserve it before repair.";
  memcpy(s->key, record.key, sizeof(s->key)); memcpy(s->volume, record.volume, 16);
  s->mode = record.version == 1 ? (s->key[0] ? LUNA_PS1_CARDS_SHARED : LUNA_PS1_CARDS_PRIVATE) : record.reserved[0];
  if ((s->mode == LUNA_PS1_CARDS_AUTO && s->key[0]) ||
      (s->mode == LUNA_PS1_CARDS_PRIVATE && s->key[0]) ||
      (s->mode == LUNA_PS1_CARDS_SHARED && !s->key[0])) return "The saved PS1 card mode is invalid.";
  return NULL;
}
const char *lunaPsxVmcSave(const LunaPsxVmcSettings *s) {
  LunaPsxVmcSettings saved = *s;
  if (saved.mode > LUNA_PS1_CARDS_SHARED) return "The card mode is invalid.";
  // Automatic stores the policy, never the transient resolved group assignment.
  if (saved.mode == LUNA_PS1_CARDS_AUTO) { memset(saved.key, 0, sizeof(saved.key)); memset(saved.volume, 0, 16); }
  s = &saved;
  char path[160], stage[168], previous[168];
  snprintf(path, sizeof(path), "%s/POPS", s->gameRoot);
  const char *error = directory(path); if (error) return error;
  snprintf(path, sizeof(path), "%s/POPS/SETTINGS", s->gameRoot);
  error = directory(path); if (error) return error;
  settingsPath(s, path); snprintf(stage, sizeof(stage), "%s.new", path);
  snprintf(previous, sizeof(previous), "%s.bak", path);
  if (!missing(stage) || !missing(previous)) return "The PS1 card assignment needs recovery.";
  if ((s->mode == LUNA_PS1_CARDS_SHARED && !s->key[0]) ||
      (s->mode == LUNA_PS1_CARDS_PRIVATE && s->key[0]) ||
      (s->key[0] && (!np_shared_key_valid(s->key, NP_SHARED_FILE) || !np_file_volume_id_present(s->volume))) ||
      (!s->key[0] && np_file_volume_id_present(s->volume))) return "The card assignment is invalid.";
  VmcRecord record = {0}; record.magic = 0x31434d56; record.version = 2; record.reserved[0] = s->mode;
  memcpy(record.disc, s->disc, 32); memcpy(record.key, s->key, sizeof(record.key));
  memcpy(record.volume, s->volume, 16); np_boot_sha256(&record, offsetof(VmcRecord, sha256), record.sha256);
  error = writeFresh(stage, &record, sizeof(record)); if (error) return error;
  int existing = !missing(path);
  if ((existing && fileXioRename(path, previous) < 0) ||
      fileXioRename(stage, path) < 0) return "Could not save the assignment. Recovery files were preserved.";
  VmcRecord check;
  if (readExact(path, &check, sizeof(check)) || memcmp(&record, &check, sizeof(check)))
    return "The saved assignment could not be verified.";
  if (existing && fileXioRemove(previous) < 0) return "The card assignment needs recovery.";
  return NULL;
}
const char *lunaPsxVmcBase(const LunaPsxVmcSettings *s, char base[128]) {
  char root[8], key[65];
  if (s->key[0]) {
    const char *error = lunaPsxVmcFindVolume(s->volume, root); if (error) return error;
    if (!np_shared_key_valid(s->key, NP_SHARED_FILE)) return "The card set identity is invalid.";
    snprintf(key, sizeof(key), "%s", s->key);
  } else { snprintf(root, sizeof(root), "%s", s->gameRoot); hexDigest(s->disc, key); key[32] = 0; }
  snprintf(base, 128, "%s/POPS/SAVES/%s", root, key);
  return NULL;
}
const char *lunaPsxVmcCard(const char *path, unsigned char hash[32]) {
  iox_stat_t st;
  if (statPath(path, &st) < 0 || !FIO_S_ISREG(st.mode) || st.hisize || st.size != NP_FILE_CARD_BYTES)
    return "The card is missing or is not a raw 128 KB image.";
  int fd = fileXioOpen(path, FIO_O_RDONLY, 0); if (fd < 0) return "Cannot open the memory card.";
  unsigned char buffer[4096], extra; NpSha256 sha; np_sha256_init(&sha); int bad = 0;
  for (unsigned i = 0; i < NP_FILE_CARD_BYTES / sizeof(buffer) && !bad; ++i) {
    bad = readAll(fd, buffer, sizeof(buffer));
    if (!bad && !i && !np_file_card_header_valid(buffer, sizeof(buffer))) bad = 1;
    if (!bad) np_sha256_update(&sha, buffer, sizeof(buffer));
  }
  if (!bad && fileXioRead(fd, &extra, 1) != 0) bad = 1;
  if (fileXioClose(fd) < 0) bad = 1;
  if (!bad) np_sha256_final(&sha, hash);
  return bad ? "The PS1 card is invalid or unreadable." : NULL;
}
const char *lunaPsxVmcPrivate(const LunaPsxVmcSettings *s, int create) {
  LunaPsxVmcSettings private = *s; private.key[0] = 0;
  char base[128], path[160], owner[66], actual[65];
  lunaPsxVmcBase(&private, base); hexDigest(s->disc, owner); owner[64] = '\n'; owner[65] = 0;
  const char *error = NULL;
  if (create) {
    snprintf(path, sizeof(path), "%s/POPS", s->gameRoot); error = directory(path); if (error) return error;
    snprintf(path, sizeof(path), "%s/POPS/SAVES", s->gameRoot); error = directory(path); if (error) return error;
    error = directory(base); if (error) return error;
  }
  const char *conflicts[] = {"shared.set", "shared.set.new", "shared.set.prev", "shared.set.bak", "owner.sha256.new"};
  for (unsigned i = 0; i < sizeof(conflicts) / sizeof(conflicts[0]); ++i) {
    snprintf(path, sizeof(path), "%s/%s", base, conflicts[i]);
    if (!missing(path)) return "The private cards have conflicting metadata or need recovery.";
  }
  int missingSlots[2];
  for (unsigned i = 0; i < 2; ++i) {
    const char *suffix[] = {".new", ".bak"};
    for (unsigned j = 0; j < 2; ++j) {
      snprintf(path, sizeof(path), "%s/card%u%s", base, i, suffix[j]);
      if (!missing(path)) return "An unfinished PS1 save needs recovery.";
    }
    unsigned char hash[32];
    snprintf(path, sizeof(path), "%s/card%u", base, i); missingSlots[i] = missing(path);
    if (!missingSlots[i]) { error = lunaPsxVmcCard(path, hash); if (error) return error; }
    snprintf(path, sizeof(path), "%s/card%u.prev", base, i);
    if (!missing(path) && lunaPsxVmcCard(path, hash)) return "A previous PS1 save is invalid. Preserve it before repair.";
  }
  snprintf(path, sizeof(path), "%s/owner.sha256", base);
  if (missing(path)) {
    for (unsigned i = 0; i < 2; ++i) {
      if (!missingSlots[i]) return "Existing cards have no ownership record. Preserve them before repair.";
      snprintf(path, sizeof(path), "%s/card%u.prev", base, i);
      if (!missing(path)) return "Existing cards have no ownership record. Preserve them before repair.";
    }
    if (!create) return "Private cards have not been created yet.";
    snprintf(path, sizeof(path), "%s/owner.sha256", base);
    error = newFile(path, owner, 65); if (error) return error;
  }
  if (readExact(path, actual, sizeof(actual)) || memcmp(owner, actual, 65))
    return "Private card ownership is missing, invalid, or belongs to another disc.";
  for (unsigned i = 0; i < 2; ++i) {
    snprintf(path, sizeof(path), "%s/card%u", base, i);
    if (missingSlots[i] && create) {
      unsigned char *blank = malloc(NP_FILE_CARD_BYTES);
      if (!blank) return "Not enough memory to create a card.";
      for (unsigned frame = 0; frame < NP_FILE_CARD_BYTES / 128; ++frame) np_file_card_frame(frame, blank + frame * 128);
      error = newFile(path, blank, NP_FILE_CARD_BYTES); free(blank); if (error) return error;
    }
    if (missingSlots[i] && !create) return "Private cards have not been created yet.";
  }
  return NULL;
}
const char *lunaPsxVmcCreate(const char *root, const char *name, const LunaPsxVmcSettings *source, NpSharedSet *set) {
  unsigned char volume[16]; const char *error = lunaPsxVmcVolume(root, volume, 1); if (error) return error;
  char verified[8]; error = lunaPsxVmcFindVolume(volume, verified); if (error) return error;
  char base[128], sourceBase[128], key[33]; sourceBase[0] = 0;
  if (source) {
    error = lunaPsxVmcPrivate(source, 0); if (error) return error;
    LunaPsxVmcSettings private = *source; private.key[0] = 0;
    error = lunaPsxVmcBase(&private, sourceBase); if (error) return error;
  }
  snprintf(base, sizeof(base), "%s/POPS/SAVES", root); error = directory(base); if (error) return error;
  for (unsigned i = 1; i <= 9999; ++i) {
    snprintf(key, sizeof(key), "LUNA_PS1_%04u", i);
    snprintf(base, sizeof(base), "%s/POPS/SAVES/%s", root, key);
    if (missing(base)) {
      error = np_shared_init(set, NP_SHARED_FILE, key, name, volume); if (error) return "Enter a valid card set name.";
      error = directory(base); if (error) return error;
      return np_shared_store_create(base, set, source ? sourceBase : NULL) ?
          "Could not create the set. Preserve staged files if recovery is needed." : NULL;
    }
  }
  return "No unused card set names remain.";
}
const char *lunaPsxVmcEnroll(const char *root, const char *key, const unsigned char disc[32], int enroll) {
  char base[128]; unsigned char id[16]; NpSharedSet current, next;
  if (!np_shared_key_valid(key, NP_SHARED_FILE)) return "The card set identity is invalid.";
  const char *error = lunaPsxVmcVolume(root, id, 0); if (error) return error;
  char verified[8]; error = lunaPsxVmcFindVolume(id, verified); if (error) return error;
  snprintf(base, sizeof(base), "%s/POPS/SAVES/%s", root, key);
  error = np_shared_store_read(base, &current);
  if (error) return "The shared card set is invalid or needs recovery.";
  if (current.storage != NP_SHARED_FILE || strcmp(current.key, key) || memcmp(current.volume, id, 16))
    return "The card set belongs to another drive.";
  next = current;
  if (np_shared_member(&next, disc, enroll)) return "This set already has 32 enrolled discs.";
  return np_shared_store_update(base, &current, &next) ? "Could not update the set. Recovery may be needed." : NULL;
}
const char *lunaPsxVmcRename(const char *base, const char *name) {
  NpSharedSet current, next;
  if (np_shared_store_read(base, &current)) return "The shared set is invalid or needs recovery.";
  if (np_shared_init(&next, current.storage, current.key, name, current.volume)) return "Enter a valid card set name.";
  for (unsigned i = 0; i < current.count; ++i)
    if (np_shared_member(&next, current.members[i], 1)) return "Cannot preserve the set's enrolled discs.";
  return np_shared_store_update(base, &current, &next) ? "Could not rename the set. Recovery may be needed." : NULL;
}

static const char *copyVerified(const char *source, const char *destination) {
  iox_stat_t before, after;
  if (!missing(destination)) return "The destination exists or cannot be checked.";
  if (statPath(source, &before) < 0 || !FIO_S_ISREG(before.mode) || before.hisize || before.size > 2 * 1024 * 1024)
    return "The source file is missing or has an unexpected size.";
  int in = fileXioOpen(source, FIO_O_RDONLY, 0); if (in < 0) return "Cannot open the source file.";
  int out = fileXioOpen(destination, FIO_O_WRONLY | FIO_O_CREAT, 0666);
  if (out < 0) { fileXioClose(in); return "Cannot create the destination file."; }
  unsigned char buffer[4096], original[32], copied[32]; NpSha256 sha; np_sha256_init(&sha);
  int bad = 0; unsigned done = 0;
  while (done < before.size && !bad) {
    unsigned take = before.size - done; if (take > sizeof(buffer)) take = sizeof(buffer);
    bad = readAll(in, buffer, take);
    if (!bad) { np_sha256_update(&sha, buffer, take); bad = writeAll(out, buffer, take); }
    done += take;
  }
  if (fileXioClose(in) < 0) bad = 1;
  if (fileXioClose(out) < 0) bad = 1;
  if (statPath(source, &after) < 0 || before.size != after.size || memcmp(before.mtime, after.mtime, sizeof(before.mtime))) bad = 1;
  if (bad) return "Copy failed. The partial copy was preserved.";
  np_sha256_final(&sha, original);
  if (statPath(destination, &after) < 0 || after.hisize || after.size != before.size) return "The copied file has the wrong size.";
  in = fileXioOpen(destination, FIO_O_RDONLY, 0); if (in < 0) return "Cannot reopen the copied file.";
  np_sha256_init(&sha); done = 0;
  while (done < before.size && !bad) {
    unsigned take = before.size - done; if (take > sizeof(buffer)) take = sizeof(buffer);
    bad = readAll(in, buffer, take); if (!bad) np_sha256_update(&sha, buffer, take); done += take;
  }
  if (fileXioClose(in) < 0) bad = 1;
  np_sha256_final(&sha, copied);
  return bad || memcmp(original, copied, 32) ? "The copied file failed verification." : NULL;
}
static const char *checkRecovery(const char *base) {
  const char *names[] = {"shared.set.new", "shared.set.prev", "shared.set.bak", "owner.sha256.new",
                         "card0.new", "card0.bak", "card1.new", "card1.bak"};
  char path[256];
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    snprintf(path, sizeof(path), "%s/%s", base, names[i]);
    if (!missing(path)) return "This set needs recovery. Preserve its staged files before repair.";
  }
  return NULL;
}
const char *lunaPsxVmcBackup(const char *base, char out[256]) {
  out[0] = 0;
  const char *error = checkRecovery(base); if (error) return error;
  if (strlen(base) < 19 || strlen(base) >= 128 || strncmp(base, "mass", 4) || strncmp(base + 6, "/POPS/SAVES/", 12))
    return "The card directory is unsupported.";
  const char *key = base + 18;
  if (!np_shared_key_valid(key, NP_SHARED_FILE)) return "The card directory is invalid.";
  char parent[160], path[256], destination[256];
  snprintf(parent, sizeof(parent), "%.6s/POPS/BACKUPS", base); error = directory(parent); if (error) return error;
  snprintf(parent, sizeof(parent), "%.6s/POPS/BACKUPS/%s", base, key); error = directory(parent); if (error) return error;
  char date[32] = "Backup"; time_t now = time(NULL); struct tm *clock = localtime(&now);
  if (clock && clock->tm_year >= 100) strftime(date, sizeof(date), "%Y%m%d_%H%M%S", clock);
  for (unsigned i = 1; i <= 9999; ++i) {
    snprintf(out, 256, "%s/%s_%04u", parent, date, i);
    if (missing(out)) break;
    if (i == 9999) { out[0] = 0; return "No unused backup names remain."; }
  }
  error = directory(out); if (error) return error;
  const char *names[] = {"card0", "card1", "shared.set", "owner.sha256", "card0.prev", "card1.prev"};
  int copied = 0;
  for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
    snprintf(path, sizeof(path), "%s/%s", base, names[i]); if (missing(path)) continue;
    snprintf(destination, sizeof(destination), "%s/%s", out, names[i]);
    error = copyVerified(path, destination); if (error) return error; ++copied;
  }
  if (!copied) return "There are no card files to back up.";
  snprintf(path, sizeof(path), "%s/backup.ok", out);
  static const char completed[] = "LUNA PS1 card backup 1\n";
  return newFile(path, completed, sizeof(completed) - 1);
}
const char *lunaPsxVmcExport(const char *source, const char *destination) {
  unsigned char hash[32]; const char *error = lunaPsxVmcCard(source, hash); if (error) return error;
  if (strlen(destination) > 240) return "The export path is too long.";
  char stage[256]; snprintf(stage, sizeof(stage), "%s.new", destination);
  if (!missing(destination)) return "Choose a filename that does not already exist.";
  error = copyVerified(source, stage); if (error) return error;
  unsigned char copied[32]; error = lunaPsxVmcCard(stage, copied);
  if (error || memcmp(hash, copied, 32)) return "The exported card failed verification.";
  return fileXioRename(stage, destination) < 0 ? "Cannot publish the exported card." : NULL;
}
const char *lunaPsxVmcReplace(const char *base, const char *source, int slot) {
  if (slot < -1 || slot > 1) return "Choose a valid card slot.";
  const char *error = checkRecovery(base); if (error) return error;
  char inputs[2][256], staged[2][256], cards[2][256], old[2][256];
  unsigned char hashes[2][32]; int existed[2] = {0};
  for (int i = 0; i < 2; ++i) {
    if (slot >= 0 && slot != i) continue;
    if (slot < 0) {
      if (snprintf(inputs[i], sizeof(inputs[i]), "%s/card%d", source, i) >= sizeof(inputs[i])) return "The backup path is too long.";
    } else {
      if (strlen(source) >= sizeof(inputs[i])) return "The import path is too long.";
      snprintf(inputs[i], sizeof(inputs[i]), "%s", source);
    }
    error = lunaPsxVmcCard(inputs[i], hashes[i]); if (error) return error;
    snprintf(cards[i], sizeof(cards[i]), "%s/card%d", base, i);
    snprintf(staged[i], sizeof(staged[i]), "%s/card%d.new", base, i);
    snprintf(old[i], sizeof(old[i]), "%s/card%d.bak", base, i); existed[i] = !missing(cards[i]);
  }
  char backup[256]; error = lunaPsxVmcBackup(base, backup); if (error) return error;
  // Stage the entire replacement before touching either active card.
  for (int i = 0; i < 2; ++i) {
    if (slot >= 0 && slot != i) continue;
    error = copyVerified(inputs[i], staged[i]); if (error) return error;
    unsigned char copied[32]; error = lunaPsxVmcCard(staged[i], copied);
    if (error || memcmp(hashes[i], copied, 32)) return "The replacement card failed verification. Recovery files were preserved.";
  }
  for (int i = 0; i < 2; ++i) {
    if (slot >= 0 && slot != i) continue;
    if ((existed[i] && fileXioRename(cards[i], old[i]) < 0) || fileXioRename(staged[i], cards[i]) < 0)
      return "Replacement was interrupted. The original cards and backup were preserved.";
  }
  for (int i = 0; i < 2; ++i) {
    if (slot >= 0 && slot != i) continue;
    unsigned char copied[32]; error = lunaPsxVmcCard(cards[i], copied);
    if (error || memcmp(hashes[i], copied, 32)) return "Replacement needs recovery. The original cards were preserved.";
  }
  for (int i = 0; i < 2; ++i) {
    if (slot >= 0 && slot != i) continue;
    if (existed[i] && fileXioRemove(old[i]) < 0) return "The replacement completed but its staging files need recovery.";
  }
  return NULL;
}

void lunaPsxVmcDiscName(const char *root, const unsigned char disc[32], char *name, size_t size) {
  char hex[65], path[128]; hexDigest(disc, hex);
  snprintf(name, size, "Disc %.12s", hex);
  snprintf(path, sizeof(path), "%s/POPS/TITLES/%s.txt", root, hex);
  iox_stat_t st;
  if (statPath(path, &st) < 0 || !FIO_S_ISREG(st.mode) || st.hisize || !st.size || st.size >= size) return;
  char label[128]; if (st.size >= sizeof(label) || readExact(path, label, st.size)) return;
  label[st.size] = 0; snprintf(name, size, "%s", label);
}
void lunaPsxVmcRememberDisc(const char *root, const unsigned char disc[32], const char *name) {
  char hex[65], path[128]; hexDigest(disc, hex);
  snprintf(path, sizeof(path), "%s/POPS/TITLES", root); if (directory(path)) return;
  snprintf(path, sizeof(path), "%s/POPS/TITLES/%s.txt", root, hex);
  char label[128]; snprintf(label, sizeof(label), "%.127s", name);
  if (missing(path)) newFile(path, label, strlen(label));
}

typedef struct { const char *key, *name, *ids[8]; } Ps1DiscGroup;
#include "ps1_disc_groups.inc"
static const TargetList *ps1Library;
void lunaPsxVmcLibrary(const TargetList *library) { ps1Library = library; }
static const Ps1DiscGroup *discGroup(const char *id) {
  if (!id) return NULL;
  for (unsigned i = 0; i < sizeof(ps1DiscGroups) / sizeof(ps1DiscGroups[0]); ++i)
    for (unsigned j = 0; j < 8 && ps1DiscGroups[i].ids[j]; ++j)
      if (!strcmp(id, ps1DiscGroups[i].ids[j])) return &ps1DiscGroups[i];
  return NULL;
}
const char *lunaPsxVmcGroup(const char *id, char key[33]) {
  const Ps1DiscGroup *group = discGroup(id); key[0] = 0;
  if (group) snprintf(key, 33, "%s", group->key);
  return group ? group->name : NULL;
}
// 1 = complete private pair, 0 = no cards, -1 = unsafe or incomplete pair.
static int automaticPrivate(const LunaPsxVmcSettings *s, unsigned char hashes[2][32], const char **error) {
  LunaPsxVmcSettings private = *s; memset(private.key, 0, sizeof(private.key));
  char base[128], path[160]; *error = lunaPsxVmcBase(&private, base);
  if (*error) return -1;
  int present = 0;
  for (int slot = 0; slot < 2; ++slot) {
    snprintf(path, sizeof(path), "%s/card%d", base, slot);
    if (!missing(path)) ++present;
  }
  *error = lunaPsxVmcPrivate(&private, 0);
  if (*error) {
    if (!present && !strcmp(*error, "Private cards have not been created yet.")) { *error = NULL; return 0; }
    return -1;
  }
  for (int slot = 0; slot < 2; ++slot) {
    snprintf(path, sizeof(path), "%s/card%d", base, slot);
    *error = lunaPsxVmcCard(path, hashes[slot]); if (*error) return -1;
  }
  // Exactly formatted empty cards contain no progress to choose or migrate.
  static unsigned char emptyHash[32]; static int emptyHashReady;
  if (!emptyHashReady) {
    NpSha256 hash; unsigned char frame[128]; np_sha256_init(&hash);
    for (unsigned i = 0; i < NP_FILE_CARD_BYTES / sizeof(frame); ++i) {
      np_file_card_frame(i, frame); np_sha256_update(&hash, frame, sizeof(frame));
    }
    np_sha256_final(&hash, emptyHash); emptyHashReady = 1;
  }
  if (!memcmp(hashes[0], emptyHash, 32) && !memcmp(hashes[1], emptyHash, 32)) return 0;
  return 1;
}
const char *lunaPsxVmcAutomatic(Target *target, LunaPsxVmcSettings *s,
    int create, LunaPsxVmcProgress progress, LunaPsxVmcChoose choose) {
  if (s->mode != LUNA_PS1_CARDS_AUTO) return NULL;
  // An automatic group's identity is derived again from the actual VCD serial.
  char id[12]; const char *error = lunaPsxDiscInfo(target->fullPath, id);
  if (error) return "Cannot identify the disc for automatic memory cards.";
  memset(s->key, 0, sizeof(s->key)); memset(s->volume, 0, sizeof(s->volume));
  const Ps1DiscGroup *group = discGroup(id);
  if (!group) return NULL; // Unknown and single-disc titles remain private.
  char base[128], path[160];
  snprintf(base, sizeof(base), "%s/POPS/SAVES/%s", s->gameRoot, group->key);
  int exists = !missing(base); NpSharedSet current;
  if (exists) {
    if (np_shared_store_read(base, &current)) return "The automatic card pair needs recovery. Its files were preserved.";
    unsigned char volume[16]; error = lunaPsxVmcVolume(s->gameRoot, volume, 0); if (error) return error;
    char root[8]; error = lunaPsxVmcFindVolume(volume, root); if (error) return error;
    if (current.storage != NP_SHARED_FILE || strcmp(current.key, group->key) || memcmp(current.volume, volume, 16))
      return "The automatic card pair belongs to another drive.";
    if (!create || !np_shared_match(&current, NP_SHARED_FILE, group->key, volume, s->disc)) {
      snprintf(s->key, sizeof(s->key), "%s", group->key); memcpy(s->volume, volume, 16); return NULL;
    }
    unsigned char hashes[2][32], shared[2][32];
    int private = automaticPrivate(s, hashes, &error); if (private < 0) return error;
    if (private) {
      for (int slot = 0; slot < 2; ++slot) {
        snprintf(path, sizeof(path), "%s/card%d", base, slot);
        error = lunaPsxVmcCard(path, shared[slot]); if (error) return error;
      }
      if (memcmp(hashes, shared, sizeof(hashes))) {
        const char *labels[] = {"Use this game's multi-disc cards", "Keep this disc's private cards"};
        if (!choose) return "Existing saves need a choice. Open this game in LUNA to select which cards to use.";
        int selected = choose(labels, 2);
        if (selected < 0) return "Automatic memory card setup cancelled.";
        if (selected > 1) return "Invalid memory card choice.";
        if (selected == 1) { s->mode = LUNA_PS1_CARDS_PRIVATE; return lunaPsxVmcSave(s); }
        // Preserve a verified copy of the private pair before choosing the group.
        LunaPsxVmcSettings privateSettings = *s; char privateBase[128], backup[256];
        error = lunaPsxVmcBase(&privateSettings, privateBase); if (error) return error;
        error = lunaPsxVmcBackup(privateBase, backup); if (error) return error;
      }
    }
    error = lunaPsxVmcEnroll(s->gameRoot, group->key, s->disc, 1); if (error) return error;
    snprintf(s->key, sizeof(s->key), "%s", group->key); memcpy(s->volume, current.volume, 16);
    lunaPsxVmcRememberDisc(s->gameRoot, s->disc, target->name); return NULL;
  }
  if (!create) return NULL; // Inspection doesn't create or enroll cards.
  // First use checks every installed automatic disc in this regional group.
  typedef struct { LunaPsxVmcSettings settings; const char *name; unsigned char hashes[2][32]; int hasCards; } Candidate;
  Candidate *candidates = calloc(NP_SHARED_MEMBERS, sizeof(*candidates));
  if (!candidates) return "Not enough memory to group these discs.";
  candidates[0].settings = *s; candidates[0].name = target->name; unsigned count = 1;
  for (Target *other = ps1Library ? ps1Library->first : NULL; other; other = other->next) {
    if (other == target || other->platform != TARGET_PS1 || discGroup(other->id) != group ||
        !other->device || !other->fullPath || !strcmp(other->fullPath, target->fullPath)) continue;
    char root[8];
    if (!lunaPsxVmcDevice(other->device, root) || strcmp(root, s->gameRoot)) continue;
    char actualId[12]; error = lunaPsxDiscInfo(other->fullPath, actualId);
    if (error || discGroup(actualId) != group) { error = "A multi-disc image changed. Refresh the game library first."; goto done; }
    if (count == NP_SHARED_MEMBERS) { error = "Too many copies of this multi-disc game. Use a manual card set."; goto done; }
    LunaPsxVmcSettings settings; error = lunaPsxVmcLoad(other, &settings, progress); if (error) goto done;
    if (settings.mode != LUNA_PS1_CARDS_AUTO) continue;
    int duplicate = 0;
    for (unsigned i = 0; i < count; ++i) if (!memcmp(settings.disc, candidates[i].settings.disc, 32)) duplicate = 1;
    if (duplicate) continue;
    candidates[count].settings = settings; candidates[count++].name = other->name;
  }
  unsigned sources[NP_SHARED_MEMBERS], sourceCount = 0;
  for (unsigned i = 0; i < count; ++i) {
    candidates[i].hasCards = automaticPrivate(&candidates[i].settings, candidates[i].hashes, &error);
    if (candidates[i].hasCards < 0) goto done;
    if (!candidates[i].hasCards) continue;
    int same = 0;
    for (unsigned j = 0; j < sourceCount; ++j)
      if (!memcmp(candidates[i].hashes, candidates[sources[j]].hashes, sizeof(candidates[i].hashes))) same = 1;
    if (!same) sources[sourceCount++] = i;
  }
  int selected = 0;
  if (sourceCount > 1) {
    const char *labels[NP_SHARED_MEMBERS];
    for (unsigned i = 0; i < sourceCount; ++i) labels[i] = candidates[sources[i]].name;
    if (!choose) { error = "These discs have different saves. Open this game in LUNA to choose which pair to use."; goto done; }
    selected = choose(labels, sourceCount);
    if (selected < 0) { error = "Automatic memory card setup cancelled."; goto done; }
    if ((unsigned)selected >= sourceCount) { error = "Invalid memory card choice."; goto done; }
  }
  // All source cards survive unchanged, with an additional checked backup.
  for (unsigned i = 0; i < count; ++i) if (candidates[i].hasCards) {
    char privateBase[128], backup[256]; error = lunaPsxVmcBase(&candidates[i].settings, privateBase);
    if (!error) error = lunaPsxVmcBackup(privateBase, backup);
    if (error) goto done;
  }
  unsigned char volume[16]; error = lunaPsxVmcVolume(s->gameRoot, volume, 1); if (error) goto done;
  char verifiedRoot[8]; error = lunaPsxVmcFindVolume(volume, verifiedRoot); if (error) goto done;
  snprintf(path, sizeof(path), "%s/POPS/SAVES", s->gameRoot); error = directory(path); if (error) goto done;
  NpSharedSet next; error = np_shared_init(&next, NP_SHARED_FILE, group->key, group->name, volume); if (error) goto done;
  for (unsigned i = 0; i < count; ++i) { error = np_shared_member(&next, candidates[i].settings.disc, 1); if (error) goto done; }
  char sourceBase[128];
  if (sourceCount) { error = lunaPsxVmcBase(&candidates[sources[selected]].settings, sourceBase); if (error) goto done; }
  error = directory(base); if (error) goto done;
  if (np_shared_store_create(base, &next, sourceCount ? sourceBase : NULL)) {
    error = "Cannot create the automatic pair. Original and staged files were preserved."; goto done;
  }
  for (unsigned i = 0; i < count; ++i) lunaPsxVmcRememberDisc(s->gameRoot, candidates[i].settings.disc, candidates[i].name);
  snprintf(s->key, sizeof(s->key), "%s", group->key); memcpy(s->volume, volume, 16);
done:
  free(candidates); return error;
}
