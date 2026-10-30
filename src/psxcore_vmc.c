// LUNA provisions shared PS1 cards; unchanged PSXCore owns game-time IO.
#include "luna_psxcore_vmc.h"
#include "luna_psxcore_library.h"
#include "luna_psxcore_discset.h"
#include "devices/devices.h"
#include "storage.h"
#include "vmc_storage.h"
#include "psxcore_file.h"
#include "psxcore_hash.h"
#include "psxcore_boot.h"
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
  if (fd < 0) { return "Cannot create the file."; }
  int bad = writeAll(fd, data, size);
  if (fileXioClose(fd) < 0) bad = 1;
  void *check = malloc(size);
  if (!check) return "Not enough memory to verify the file.";
  if (!bad && (readExact(path, check, size) || memcmp(check, data, size))) bad = 1;
  free(check);
  if (bad) { return "The write failed. Staged files were preserved for recovery."; }
  return NULL;
}
// Publish only a newly created, reopened and byte-verified file.
static const char *newFile(const char *path, const void *data, unsigned size) {
  char stage[256];
  int length = snprintf(stage, sizeof(stage), "%s.new", path);
  if (length < 0 || (size_t)length >= sizeof(stage)) return "The file path is too long.";
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
      (device->mode != MODE_ATA && device->mode != MODE_USB) || device->index > 1 ||
      (device->mode == MODE_ATA && device->index != 0)) return 0;
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


int lunaPsxVmcKeyValid(const char *key) {
  if (!key || !key[0] || strlen(key) > 48 || key[0] == ' ' || key[strlen(key)-1] == ' ') return 0;
  for (const unsigned char *p = (const unsigned char *)key; *p; ++p)
    if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == ' ')) return 0;
  return 1;
}
static int rootValid(const char *root) {
  return root && strlen(root) == 6 && !strncmp(root,"mass",4) &&
      root[4] >= '0' && root[4] <= '9' && root[5] == ':';
}
static int baseValid(const char *base) {
  if (!base || strlen(base) < 7 || strlen(base) >= 128) return 0;
  char root[8]; snprintf(root,sizeof(root),"%.6s",base);
  return rootValid(root) && (!strcmp(base+6,"/POPS/ps1emu") ||
      (!strncmp(base+6,"/POPS/CARDS/",12) && lunaPsxVmcKeyValid(base+18)));
}
const char *lunaPsxVmcPaths(const char *base, char cards[2][NP_FILE_PATH_BYTES]) {
  if (!baseValid(base)) return "The card folder is unsupported.";
  memset(cards,0,2*NP_FILE_PATH_BYTES);
  int named = strcmp(base+6,"/POPS/ps1emu") != 0;
  for (unsigned i=0;i<2;++i) snprintf(cards[i],NP_FILE_PATH_BYTES,"%s/card%u%s",base+6,i,named?".vmc":"");
  return np_file_card_pair_valid(cards) ? NULL : "The selected card paths are invalid.";
}
const char *lunaPsxVmcBase(const LunaPsxVmcSettings *s, char base[128]) {
  char root[8];
  if (s->mode == LUNA_PS1_CARDS_DEFAULT) {
    if (s->key[0] || np_file_volume_id_present(s->volume) || !rootValid(s->gameRoot)) return "The default card selection is invalid.";
    snprintf(base,128,"%s/POPS/ps1emu",s->gameRoot);
  } else if (s->mode == LUNA_PS1_CARDS_CUSTOM && lunaPsxVmcKeyValid(s->key)) {
    const char *error = lunaPsxVmcFindVolume(s->volume,root); if (error) return error;
    snprintf(base,128,"%s/POPS/CARDS/%s",root,s->key);
  } else return "The card selection is invalid.";
  return NULL;
}

const char *lunaPsxVmcResolve(const LunaPsxVmcSettings *s,char root[8],char cards[2][NP_FILE_PATH_BYTES]) {
  if(s->mode==LUNA_PS1_CARDS_SLOTS) {
    if(s->key[0] || !np_file_card_pair_valid(s->cards))return "The independent card selection is invalid.";
    const char *error=lunaPsxVmcFindVolume(s->volume,root);if(error)return error;
    memcpy(cards,s->cards,sizeof(s->cards));return NULL;
  }
  char base[128];const char *error=lunaPsxVmcBase(s,base);if(error)return error;
  snprintf(root,8,"%.6s",base);return lunaPsxVmcPaths(base,cards);
}

// Separate namespace: old hash-keyed ownership and settings files are ignored.
typedef struct {
  uint32_t magic, version, mode;
  char gamePath[NP_FILE_PATH_BYTES], key[49];
  unsigned char volume[16], reserved[3], sha256[32];
} VmcSelectionV1;
_Static_assert(sizeof(VmcSelectionV1) == 368, "LUNA legacy card selection layout");
typedef struct {
  uint32_t magic, version, mode;
  char gamePath[NP_FILE_PATH_BYTES], key[49];
  unsigned char volume[16], reserved[3];
  char cards[2][NP_FILE_PATH_BYTES];
  unsigned char sha256[32];
} VmcSelection;
static void settingsPath(const LunaPsxVmcSettings *s, char path[160]) {
  unsigned char hash[32]; char hex[65];
  np_boot_sha256(s->gamePath,strlen(s->gamePath)+1,hash); hexDigest(hash,hex);
  snprintf(path,160,"%s/POPS/CARD_SELECTIONS/%s.vmc",s->gameRoot,hex);
}
static const char *selectionCheck(const VmcSelection *r, const LunaPsxVmcSettings *s) {
  unsigned char hash[32]; np_boot_sha256(r,offsetof(VmcSelection,sha256),hash);
  if (r->magic != 0x33434d4c || r->version != 2 || r->mode > LUNA_PS1_CARDS_SLOTS ||
      r->reserved[0] || r->reserved[1] || r->reserved[2] || memcmp(hash,r->sha256,32) ||
      !memchr(r->gamePath,0,sizeof(r->gamePath)) || strcmp(r->gamePath,s->gamePath) ||
      !memchr(r->key,0,sizeof(r->key))) return "The memory card selection is invalid.";
  if (r->mode == LUNA_PS1_CARDS_DEFAULT ? r->key[0] || np_file_volume_id_present(r->volume) :
      !np_file_volume_id_present(r->volume) || (r->mode==LUNA_PS1_CARDS_CUSTOM ?
      !lunaPsxVmcKeyValid(r->key) : r->key[0] || !np_file_card_pair_valid(r->cards))) return "The memory card selection is invalid.";
  if(r->mode!=LUNA_PS1_CARDS_SLOTS)for(unsigned i=0;i<sizeof(r->cards);++i)
    if(((const unsigned char *)r->cards)[i])return "The memory card selection is invalid.";
  return NULL;
}
static const char *readSelection(const char *path,VmcSelection *record,const LunaPsxVmcSettings *s) {
  iox_stat_t st;if(statPath(path,&st)<0)return "Cannot read the card selection.";
  if(!st.hisize && st.size==sizeof(VmcSelectionV1)) {
    VmcSelectionV1 old;unsigned char hash[32];const char *error=readExact(path,&old,sizeof(old));if(error)return error;
    np_boot_sha256(&old,offsetof(VmcSelectionV1,sha256),hash);
    if(old.version!=1 || old.mode>LUNA_PS1_CARDS_CUSTOM || memcmp(hash,old.sha256,32))return "The legacy card selection is invalid.";
    memset(record,0,sizeof(*record));memcpy(record,&old,offsetof(VmcSelectionV1,sha256));record->version=2;
    np_boot_sha256(record,offsetof(VmcSelection,sha256),record->sha256);
  } else {const char *error=readExact(path,record,sizeof(*record));if(error)return error;}
  return selectionCheck(record,s);
}
const char *lunaPsxVmcLoad(Target *target, LunaPsxVmcSettings *s, LunaPsxVmcProgress progress) {
  (void)progress; memset(s,0,sizeof(*s));
  if (!target || target->platform != TARGET_PS1 || !target->fullPath ||
      !lunaPsxVmcDevice(target->device,s->gameRoot) || strncmp(target->fullPath,s->gameRoot,6))
    return "This PS1 game needs supported local storage.";
  const char *identityError=lunaPsxGameIdentity(target,s->gamePath);if(identityError)return identityError;
  char path[160], stage[168]; settingsPath(s,path); snprintf(stage,sizeof(stage),"%s.new",path);
  if (!missing(stage)) return "An interrupted card selection needs recovery. Preserve its .new file.";
  snprintf(stage,sizeof(stage),"%s.prev",path);
  if (missing(path)) {
    if(!missing(stage))return "The previous card selection needs recovery.";
    if(target->discs) {
      LunaPsxVmcSettings first={0};
      for(unsigned i=0;i<target->discs->count;++i) {
        char member[264];snprintf(member,sizeof(member),"%s%s",s->gameRoot,target->discs->paths[i]);
        Target disc=*target;disc.fullPath=member;disc.discs=NULL;
        LunaPsxVmcSettings choice;const char *error=lunaPsxVmcLoad(&disc,&choice,progress);if(error)return error;
        if(!i)first=choice;
        else if(choice.mode!=first.mode || strcmp(choice.key,first.key) || memcmp(choice.volume,first.volume,16) || memcmp(choice.cards,first.cards,sizeof(choice.cards)))
          return "Discs use different cards. Reset this set's selection, then choose its card pair.";
      }
      if(target->discs->count) {s->mode=first.mode;memcpy(s->key,first.key,sizeof(s->key));memcpy(s->volume,first.volume,16);memcpy(s->cards,first.cards,sizeof(s->cards));}
    }
    return NULL;
  }
  VmcSelection record; const char *error = readSelection(path,&record,s);
  if (error) return error;
  s->mode=record.mode; memcpy(s->key,record.key,sizeof(s->key)); memcpy(s->volume,record.volume,16);memcpy(s->cards,record.cards,sizeof(s->cards));
  return NULL;
}
const char *lunaPsxVmcSave(const LunaPsxVmcSettings *s) {
  if (!rootValid(s->gameRoot) || !np_file_path_valid(s->gamePath,sizeof(s->gamePath),1)) return "The game path is invalid.";
  VmcSelection record={0}; record.magic=0x33434d4c; record.version=2; record.mode=s->mode;
  memcpy(record.key,s->key,sizeof(record.key)); memcpy(record.volume,s->volume,16);
  memcpy(record.gamePath,s->gamePath,sizeof(record.gamePath));
  if(s->mode==LUNA_PS1_CARDS_SLOTS)memcpy(record.cards,s->cards,sizeof(record.cards));
  np_boot_sha256(&record,offsetof(VmcSelection,sha256),record.sha256);
  const char *error=selectionCheck(&record,s); if (error) return error;
  if (s->mode!=LUNA_PS1_CARDS_DEFAULT) { error=lunaPsxVmcPrepare(s,0); if(error)return error; }
  char path[160], stage[168], previous[168];
  snprintf(path,sizeof(path),"%s/POPS",s->gameRoot); error=directory(path); if(error)return error;
  snprintf(path,sizeof(path),"%s/POPS/CARD_SELECTIONS",s->gameRoot); error=directory(path); if(error)return error;
  settingsPath(s,path); snprintf(stage,sizeof(stage),"%s.new",path); snprintf(previous,sizeof(previous),"%s.prev",path);
  if (!missing(stage)) return "The card selection needs recovery.";
  if (!missing(previous)) {
    VmcSelection old;
    if (readSelection(previous,&old,s) || missing(path) || fileXioRemove(previous)<0)
      return "The previous card selection needs recovery.";
  }
  error=writeFresh(stage,&record,sizeof(record)); if(error)return error;
  if ((!missing(path) && fileXioRename(path,previous)<0) || fileXioRename(stage,path)<0)
    return "Could not publish the selection. Recovery files were preserved.";
  VmcSelection check;
  return readExact(path,&check,sizeof(check)) || memcmp(&check,&record,sizeof(check)) ? "The selection could not be verified." : NULL;
}
const char *lunaPsxVmcReset(Target *target) {
  LunaPsxVmcSettings s; (void)lunaPsxVmcLoad(target,&s,NULL);
  if(!rootValid(s.gameRoot) || !np_file_path_valid(s.gamePath,sizeof(s.gamePath),1))return "This game's card selection cannot be reset.";
  char path[160],source[168],archived[224];settingsPath(&s,path);
  const char *suffix[]={"",".new",".prev"};uint64_t stamp=GetTimerSystemTime();
  for(unsigned i=0;i<3;++i) {
    snprintf(source,sizeof(source),"%s%s",path,suffix[i]);if(missing(source))continue;
    snprintf(archived,sizeof(archived),"%s.saved-%llu%s",path,(unsigned long long)stamp,suffix[i]);
    if(!missing(archived) || fileXioRename(source,archived)<0)return "Could not archive the selection. Retry before choosing cards.";
  }
  if(target->discs) {
    // Persist an explicit default after resolving a migration conflict; never
    // rewrite any member's old selection or its memory-card images.
    s.mode=LUNA_PS1_CARDS_DEFAULT;memset(s.key,0,sizeof(s.key));memset(s.volume,0,16);memset(s.cards,0,sizeof(s.cards));
    return lunaPsxVmcSave(&s);
  }
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
  char paths[2][NP_FILE_PATH_BYTES], path[288];
  const char *error=lunaPsxVmcPaths(base,paths); if(error)return error;
  snprintf(path,sizeof(path),"%s/cards.pending",base);
  if (!missing(path)) return "A card operation was interrupted. Open Manage cards > Recover cards.";
  snprintf(path,sizeof(path),"%s/cards.pending.new",base);
  if (!missing(path)) return "A card operation was interrupted. Open Manage cards > Recover cards.";
  const char *suffix[]={".new",".bak",".recover",".recovery-old"};
  for(unsigned i=0;i<2;++i) {
    for(unsigned j=0;j<4;++j) {
      snprintf(path,sizeof(path),"%.6s%s%s",base,paths[i],suffix[j]);
      if(!missing(path))return "An unfinished save needs recovery. Open Manage cards > Recover cards.";
    }
    snprintf(path,sizeof(path),"%.6s%s.prev",base,paths[i]);
    unsigned char hash[32];
    if(!missing(path) && lunaPsxVmcCard(path,hash))return "A previous save is invalid. Open Manage cards > Recover cards.";
  }
  return NULL;
}
// Formatting belongs to LUNA, not the core's runtime.
static void cardFrame(unsigned frame,unsigned char p[128]) {
  memset(p,0,128);
  if(frame==0 || frame==63){p[0]='M';p[1]='C';}
  else if(frame>=1 && frame<=15){p[0]=0xa0;p[8]=p[9]=0xff;}
  else if(frame>=16 && frame<=35){p[0]=p[1]=p[2]=p[3]=p[8]=p[9]=0xff;}
  for(unsigned i=0;i<127;++i)p[127]^=p[i];
}
const char *lunaPsxVmcPrepare(const LunaPsxVmcSettings *s,int create) {
  if(s->mode==LUNA_PS1_CARDS_SLOTS) {
    char root[8],cards[2][NP_FILE_PATH_BYTES],path[LUNA_VMC_PATH];
    const char *error=lunaPsxVmcResolve(s,root,cards);if(error)return error;
    for(unsigned i=0;i<2;++i) {
      snprintf(path,sizeof(path),"%s%s",root,cards[i]);
      error=lunaVmcReady(path,LUNA_VMC_PS1);if(error)return error;
    }
    return NULL;
  }
  char base[128], paths[2][NP_FILE_PATH_BYTES], path[288];
  const char *error=lunaPsxVmcBase(s,base); if(error)return error;
  error=checkRecovery(base); if(error)return error;
  error=lunaPsxVmcPaths(base,paths); if(error)return error;
  int absentSlots[2]; unsigned char hash[32];
  for(unsigned i=0;i<2;++i) {
    snprintf(path,sizeof(path),"%.6s%s",base,paths[i]); absentSlots[i]=missing(path);
    if(!absentSlots[i]){error=lunaVmcReady(path,LUNA_VMC_PS1);if(!error)error=lunaPsxVmcCard(path,hash);if(error)return error;}
    else {
      snprintf(path,sizeof(path),"%.6s%s.prev",base,paths[i]);
      if(!missing(path))return "A card is missing but a previous save exists. Recover it before launch.";
    }
  }
  if(!absentSlots[0] && !absentSlots[1])return NULL;
  if(!create)return "Memory cards have not been created yet.";
  snprintf(path,sizeof(path),"%s/POPS",s->gameRoot);
  // Named pair creation makes its parent separately; default cards use gameRoot.
  if(s->mode==LUNA_PS1_CARDS_DEFAULT){error=directory(path);if(error)return error;}
  error=directory(base);if(error)return error;
  // Both missing images are fully staged before either is published. A leftover
  // .new blocks launch, including a failure after publishing the first slot.
  unsigned char *blank=malloc(NP_FILE_CARD_BYTES); if(!blank)return "Not enough memory to create cards.";
  for(unsigned f=0;f<NP_FILE_CARD_BYTES/128;++f)cardFrame(f,blank+f*128);
  for(unsigned i=0;i<2 && !error;++i)if(absentSlots[i]) {
    snprintf(path,sizeof(path),"%.6s%s.new",base,paths[i]); error=writeFresh(path,blank,NP_FILE_CARD_BYTES);
  }
  free(blank); if(error)return error;
  for(unsigned i=0;i<2;++i)if(absentSlots[i]) {
    char stage[288];snprintf(path,sizeof(path),"%.6s%s",base,paths[i]);snprintf(stage,sizeof(stage),"%s.new",path);
    if(!missing(path) || fileXioRename(stage,path)<0)return "Card creation was interrupted. Recover the pending operation.";
    error=lunaPsxVmcCard(path,hash);if(error)return error;
  }
  return NULL;
}
const char *lunaPsxVmcCreate(const char *root,const char *name,char key[49]) {
  key[0]=0; if(!rootValid(root) || !lunaPsxVmcKeyValid(name))return "Use 1-48 letters, numbers, spaces, underscores or hyphens.";
  unsigned char id[16];const char *error=lunaPsxVmcVolume(root,id,1);if(error)return error;
  char verified[8];error=lunaPsxVmcFindVolume(id,verified);if(error)return error;
  char base[128];snprintf(base,sizeof(base),"%s/POPS/CARDS",root);error=directory(base);if(error)return error;
  snprintf(base,sizeof(base),"%s/POPS/CARDS/%s",root,name);
  if(!missing(base))return "Choose a card name that does not already exist.";
  LunaPsxVmcSettings s={0};s.mode=LUNA_PS1_CARDS_CUSTOM;memcpy(s.volume,id,16);snprintf(s.key,sizeof(s.key),"%s",name);
  error=lunaPsxVmcPrepare(&s,1);if(!error)snprintf(key,49,"%s",name);return error;
}
static const char *snapshot(const char *base,char out[256],int recovery) {
  char cards[2][NP_FILE_PATH_BYTES],path[288],destination[288],parent[128];
  const char *error=lunaPsxVmcPaths(base,cards);if(error)return error;
  if(!recovery) { error=checkRecovery(base);if(error)return error; }
  snprintf(parent,sizeof(parent),"%.6s/POPS/BACKUPS",base);error=directory(parent);if(error)return error;
  for(unsigned i=1;i<=9999;++i) {
    snprintf(out,256,"%s/%llu_%04u",parent,(unsigned long long)GetTimerSystemTime(),i);
    if(missing(out))break;
    if(i==9999)return "No unused backup names remain.";
  }
  error=directory(out);if(error)return error;
  const char *suffix[]={"",".prev",".new",".bak",".recover",".recovery-old"};
  int copied=0;
  for(unsigned i=0;i<2;++i)for(unsigned j=0;j<6;++j) {
    snprintf(path,sizeof(path),"%.6s%s%s",base,cards[i],suffix[j]);
    if(missing(path))continue;
    if(!recovery && j==0){unsigned char hash[32];error=lunaPsxVmcCard(path,hash);if(error)return error;}
    snprintf(destination,sizeof(destination),"%s/card%u%s",out,i,suffix[j]);
    error=copyVerified(path,destination);if(error)return error;++copied;
  }
  if(!copied && !recovery)return "There are no card images to back up.";
  const char *journals[]={"cards.pending","cards.pending.new"};
  for(unsigned i=0;i<2;++i) {
    snprintf(path,sizeof(path),"%s/%s",base,journals[i]);
    if(missing(path))continue;
    snprintf(destination,sizeof(destination),"%s/%s",out,journals[i]);
    error=copyVerified(path,destination);if(error)return error;
  }
  snprintf(path,sizeof(path),"%s/backup.ok",out);
  return newFile(path,"LUNA PS1 shared backup 2\n",25);
}
const char *lunaPsxVmcBackup(const char *base,char out[256]) {return snapshot(base,out,0);}
const char *lunaPsxVmcReplace(const char *base,const char *source,int slot) {
  if(slot < -1 || slot > 1)return "Choose a valid slot.";
  const char *error=checkRecovery(base);if(error)return error;
  char relative[2][NP_FILE_PATH_BYTES],cards[2][288],inputs[2][288],stage[2][288],old[2][288],pending[160],backup[256];
  unsigned char hashes[2][32],check[32];
  error=lunaPsxVmcPaths(base,relative);if(error)return error;
  if(slot<0) {
    char marker[288],text[25];snprintf(marker,sizeof(marker),"%s/backup.ok",source);
    if(readExact(marker,text,sizeof(text)) || memcmp(text,"LUNA PS1 shared backup 2\n",25))return "Choose a completed shared-card backup.";
  }
  for(int i=0;i<2;++i)if(slot<0 || slot==i) {
    if(slot<0)snprintf(inputs[i],sizeof(inputs[i]),"%s/card%d",source,i);
    else if(strlen(source)>=sizeof(inputs[i]))return "The import path is too long.";
    else snprintf(inputs[i],sizeof(inputs[i]),"%s",source);
    error=lunaPsxVmcCard(inputs[i],hashes[i]);if(error)return error;
    snprintf(cards[i],sizeof(cards[i]),"%.6s%s",base,relative[i]);
    error=lunaPsxVmcCard(cards[i],check);if(error)return error;
    snprintf(stage[i],sizeof(stage[i]),"%s.new",cards[i]);snprintf(old[i],sizeof(old[i]),"%s.bak",cards[i]);
  }
  error=lunaPsxVmcBackup(base,backup);if(error)return error;
  snprintf(pending,sizeof(pending),"%s/cards.pending",base);error=newFile(pending,"replace\n",8);if(error)return error;
  for(int i=0;i<2;++i)if(slot<0 || slot==i) {
    error=copyVerified(inputs[i],stage[i]);if(error)return error;
    error=lunaPsxVmcCard(stage[i],check);if(error || memcmp(check,hashes[i],32))return "The staged replacement failed verification.";
  }
  for(int i=0;i<2;++i)if(slot<0 || slot==i) {
    if(fileXioRename(cards[i],old[i])<0 || fileXioRename(stage[i],cards[i])<0)return "Replacement was interrupted. Recover cards before launch.";
    error=lunaPsxVmcCard(cards[i],check);if(error || memcmp(check,hashes[i],32))return "The replacement needs recovery.";
  }
  for(int i=0;i<2;++i)if(slot<0 || slot==i)if(fileXioRemove(old[i])<0)return "The completed replacement needs recovery.";
  return fileXioRemove(pending)<0?"The completed replacement needs recovery.":NULL;
}
const char *lunaPsxVmcRecover(const char *base,int slot,const char *suffix) {
  if(slot<0 || slot>1 || !suffix || (strcmp(suffix,".blank") && strcmp(suffix,"") && strcmp(suffix,".prev") && strcmp(suffix,".bak") &&
      strcmp(suffix,".new") && strcmp(suffix,".recover") && strcmp(suffix,".recovery-old")))return "Choose a recovery image.";
  char relative[2][NP_FILE_PATH_BYTES],card[288],source[288],stage[288],old[288],backup[256],pending[160];
  unsigned char hash[32],check[32];const char *error=lunaPsxVmcPaths(base,relative);if(error)return error;
  snprintf(card,sizeof(card),"%.6s%s",base,relative[slot]);snprintf(source,sizeof(source),"%s%s",card,suffix);
  int blank=!strcmp(suffix,".blank");
  if(blank) {
    if(!missing(card))return "An existing card cannot be replaced with a blank recovery card.";
    const char *candidates[]={".prev",".bak",".new",".recover",".recovery-old"};
    for(unsigned i=0;i<5;++i) {
      snprintf(source,sizeof(source),"%s%s",card,candidates[i]);
      if(!missing(source) && !lunaPsxVmcCard(source,check))return "A valid saved card exists. Choose that recovery image.";
    }
  } else { error=lunaPsxVmcCard(source,hash);if(error)return error; }
  error=snapshot(base,backup,1);if(error)return error;
  snprintf(pending,sizeof(pending),"%s/cards.pending",base);
  if(missing(pending)) {
    char journalStage[168];snprintf(journalStage,sizeof(journalStage),"%s.new",pending);
    if(!missing(journalStage) && fileXioRemove(journalStage)<0)return "Cannot clear the archived operation journal.";
    error=newFile(pending,"recover\n",8);if(error)return error;
  }
  // Every removed candidate has been copied and verified in the snapshot.
  snprintf(stage,sizeof(stage),"%s.recover",card);snprintf(old,sizeof(old),"%s.recovery-old",card);
  if(blank) {
    unsigned char *data=malloc(NP_FILE_CARD_BYTES);if(!data)return "Not enough memory to recover a blank card.";
    for(unsigned i=0;i<NP_FILE_CARD_BYTES/128;++i)cardFrame(i,data+i*128);
    np_boot_sha256(data,NP_FILE_CARD_BYTES,hash);
    if(!missing(stage) && fileXioRemove(stage)<0) {free(data);return "Cannot clear the backed-up recovery stage.";}
    error=writeFresh(stage,data,NP_FILE_CARD_BYTES);free(data);if(error)return error;
  } else if(strcmp(source,stage)) {
    if(!missing(stage) && fileXioRemove(stage)<0)return "Cannot clear the backed-up recovery stage.";
    error=copyVerified(source,stage);if(error)return error;
  }
  error=lunaPsxVmcCard(stage,check);if(error || memcmp(hash,check,32))return "The recovery copy failed verification.";
  if(!missing(old) && fileXioRemove(old)<0)return "Cannot clear the backed-up previous candidate.";
  if((!missing(card) && fileXioRename(card,old)<0) || fileXioRename(stage,card)<0)return "Recovery was interrupted. All candidates remain backed up.";
  error=lunaPsxVmcCard(card,check);if(error || memcmp(hash,check,32))return "The recovered card failed verification.";
  const char *remnants[]={".new",".bak",".prev",".recovery-old"};
  for(unsigned i=0;i<4;++i){snprintf(source,sizeof(source),"%s%s",card,remnants[i]);if(!missing(source) && fileXioRemove(source)<0)return "Recovery cleanup was interrupted.";}
  // Only clear the pair journal after both slots and every remnant are safe.
  for(unsigned i=0;i<2;++i) {
    snprintf(source,sizeof(source),"%.6s%s",base,relative[i]);if(lunaPsxVmcCard(source,check))return NULL;
    const char *pendingSuffix[]={".new",".bak",".recover",".recovery-old"};
    for(unsigned j=0;j<4;++j){snprintf(source,sizeof(source),"%.6s%s%s",base,relative[i],pendingSuffix[j]);if(!missing(source))return NULL;}
    snprintf(source,sizeof(source),"%.6s%s.prev",base,relative[i]);if(!missing(source) && lunaPsxVmcCard(source,check))return NULL;
  }
  snprintf(source,sizeof(source),"%s/cards.pending.new",base);
  if(!missing(source) && fileXioRemove(source)<0)return "The recovered pair still needs cleanup.";
  return fileXioRemove(pending)<0?"The recovered pair still needs cleanup.":NULL;
}
