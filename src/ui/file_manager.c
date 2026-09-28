// Storage browser, VMC manager, and the library's Start menu.
#include "ui/file_manager.h"
#include "vmc_create.h"
#include "devices/devices.h"
#include "ui/graphics.h"
#include "ui/pad.h"
#include "ui/view_internal.h"
#include <dirent.h>
#include <ctype.h>
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BROWSER_MAX_ROOTS (MAX_DEVICES + 3)
#define BROWSER_MAX_ENTRIES 512

typedef struct {
  char path[PATH_MAX + 1];
  char label[64];
} BrowserRoot;

typedef struct {
  char *name;
  uint64_t size;
  int isDirectory;
  int hasSize;
} BrowserEntry;

static void drawBrowserSheet(void) {
  int width = gsGlobal->Width;
  int height = gsGlobal->Height;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, 32, 80, width - 32,
                    height - footerHeight + 1, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(30, 78, width - 30, height - footerHeight + 3, 2);
}

static void presentBrowserFrame(void) {
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_vsync_wait();
  gsKit_display_buffer(gsGlobal);
  usleep(1000);
}

static void drawBrowserRow(int y, int selected, const char *name,
                           const char *detail) {
  int left = keepoutArea + 30;
  int right = gsGlobal->Width - left;
  int lineHeight = getFontLineHeight();
  int detailWidth = detail == NULL ? 0 : (int)getLineWidth(detail) + 14;
  if (selected)
    drawPSBBNFocusGlow(left, y, right, right - 12);
  drawTextWindow(left + 18, y, right - detailWidth - 10, y + lineHeight,
                 0, selected ? FontMainColor : HeaderTextColor,
                 ALIGN_LEFT, name);
  if (detail != NULL)
    drawTextWindow(right - detailWidth, y, right - 12, y + lineHeight,
                   0, selected ? ColorSelected : FontMainColor,
                   ALIGN_RIGHT, detail);
}

static void drawBrowserFrame(const char *heading, const char *path,
                             const char *status, const char *footer,
                             int count, int selected, int first,
                             int (*row)(int, char *, size_t, char *, size_t,
                                        void *), void *context) {
  int left = keepoutArea + 30;
  int right = gsGlobal->Width - left;
  int lineHeight = getFontLineHeight();
  int rowStep = lineHeight + lineHeight / 3;
  int listTop = 112;
  int listBottom = gsGlobal->Height - footerHeight - lineHeight;
  int visible = (listBottom - listTop) / rowStep;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet();
  drawTextWindow(left, headerHeight - lineHeight, right, headerHeight + 2,
                 0, HeaderTextColor, ALIGN_HCENTER, heading);
  drawTextWindow(left + 8, 82, right - 8, 82 + lineHeight,
                 0, FontMainColor, ALIGN_LEFT, path);
  for (int index = first; index < count && index < first + visible; index++) {
    char name[PATH_MAX + 3];
    char detail[32];
    if (!row(index, name, sizeof(name), detail, sizeof(detail), context))
      continue;
    drawBrowserRow(listTop + (index - first) * rowStep,
                   index == selected, name, detail[0] ? detail : NULL);
  }
  if (count == 0)
    drawTextWindow(left + 18, listTop, right, listTop + lineHeight,
                   0, HeaderTextColor, ALIGN_LEFT, "Nothing to show");
  drawTextWindow(left + 18, listBottom, right, gsGlobal->Height - footerHeight,
                 0, HeaderTextColor, ALIGN_LEFT, status);
  drawTextWindow(left + 18, gsGlobal->Height - footerHeight, right,
                 gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER, footer);
  presentBrowserFrame();
}

static int browserVisibleRows(void) {
  int rowStep = getFontLineHeight() + getFontLineHeight() / 3;
  int visible = (gsGlobal->Height - footerHeight - getFontLineHeight() - 112) /
                rowStep;
  return visible > 0 ? visible : 1;
}

static int browserFirstRow(int selected) {
  int visible = browserVisibleRows();
  return selected / visible * visible;
}

static int rootRow(int index, char *name, size_t nameSize,
                   char *detail, size_t detailSize, void *context) {
  BrowserRoot *roots = context;
  snprintf(name, nameSize, "%s", roots[index].label);
  snprintf(detail, detailSize, "%s", roots[index].path);
  return 1;
}

static int fileRow(int index, char *name, size_t nameSize,
                   char *detail, size_t detailSize, void *context) {
  BrowserEntry *entries = context;
  BrowserEntry *entry = &entries[index];
  snprintf(name, nameSize, "%s%s", entry->name,
           entry->isDirectory ? "/" : "");
  if (entry->isDirectory)
    snprintf(detail, detailSize, "Folder");
  else if (entry->hasSize)
    snprintf(detail, detailSize, "%llu KB",
             (unsigned long long)((entry->size + 1023) / 1024));
  else
    detail[0] = '\0';
  return 1;
}

static int addRoot(BrowserRoot *roots, int count, const char *path,
                   const char *label) {
  DIR *directory;
  if (count >= BROWSER_MAX_ROOTS || strlen(path) > PATH_MAX)
    return count;
  directory = opendir(path);
  if (directory == NULL)
    return count;
  closedir(directory);
  snprintf(roots[count].path, sizeof(roots[count].path), "%s", path);
  snprintf(roots[count].label, sizeof(roots[count].label), "%s", label);
  return count + 1;
}

static int collectRoots(BrowserRoot *roots) {
  int count = 0;
  for (int index = 0; index < MAX_DEVICES; index++) {
    struct DeviceMapEntry *device = &deviceModeMap[index];
    const char *kind;
    char label[64];
    if (device->mode == MODE_NONE || device->mountpoint == NULL)
      break;
    if (device->mode == MODE_HDL) {
      // hdd0: lists APA partitions, not ordinary files. The mounted PFS
      // metadata partition is the safe directory view for this backend.
      if (device->metadev != NULL && device->metadev->mountpoint != NULL)
        count = addRoot(roots, count, device->metadev->mountpoint,
                        "APA metadata");
      continue;
    }
    switch (device->mode) {
    case MODE_ATA: kind = "ATA HDD"; break;
    case MODE_USB: kind = "USB"; break;
    case MODE_MX4SIO: kind = "MX4SIO"; break;
    case MODE_ILINK: kind = "iLink"; break;
    case MODE_MMCE: kind = "MMCE"; break;
    case MODE_UDPFS: kind = "Network"; break;
    default: kind = "Storage"; break;
    }
    snprintf(label, sizeof(label), "%s", kind);
    count = addRoot(roots, count, device->mountpoint, label);
  }
  count = addRoot(roots, count, "mc0:/", "Memory card 1");
  count = addRoot(roots, count, "mc1:/", "Memory card 2");
#ifdef LUNA_EMULATOR_BUILD
  count = addRoot(roots, count, "host:/", "Emulator host");
#endif
  return count;
}

static void clearEntries(BrowserEntry *entries, int count) {
  for (int index = 0; index < count; index++) {
    free(entries[index].name);
    entries[index].name = NULL;
  }
}

static int entryCompare(const void *left, const void *right) {
  const BrowserEntry *a = left;
  const BrowserEntry *b = right;
  if (a->isDirectory != b->isDirectory)
    return b->isDirectory - a->isDirectory;
  return strcmp(a->name, b->name);
}

static int joinPath(char *destination, size_t capacity,
                    const char *directory, const char *name) {
  size_t length = strlen(directory);
  int written = snprintf(destination, capacity, "%s%s%s", directory,
                         length > 0 && directory[length - 1] == '/' ? "" : "/",
                         name);
  return written >= 0 && (size_t)written < capacity;
}

static int loadDirectory(const char *path, BrowserEntry *entries,
                         int *count, int *truncated) {
  DIR *directory = opendir(path);
  if (directory == NULL)
    return 0;
  clearEntries(entries, *count);
  *count = 0;
  *truncated = 0;
  struct dirent *item;
  while ((item = readdir(directory)) != NULL) {
    char child[PATH_MAX + 1];
    struct stat info;
    BrowserEntry *entry;
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
      continue;
    if (*count == BROWSER_MAX_ENTRIES) {
      *truncated = 1;
      break;
    }
    if (!joinPath(child, sizeof(child), path, item->d_name))
      continue;
    entry = &entries[*count];
    entry->name = strdup(item->d_name);
    if (entry->name == NULL) {
      *truncated = 1;
      break;
    }
    entry->isDirectory = item->d_type == DT_DIR;
    entry->hasSize = stat(child, &info) == 0;
    if (entry->hasSize) {
      entry->isDirectory = entry->isDirectory || S_ISDIR(info.st_mode);
      entry->size = (uint64_t)info.st_size;
    } else {
      entry->size = 0;
    }
    (*count)++;
  }
  closedir(directory);
  qsort(entries, *count, sizeof(entries[0]), entryCompare);
  return 1;
}

static void parentPath(char *path, const char *root) {
  size_t rootLength = strlen(root);
  char *slash = strrchr(path + rootLength, '/');
  if (slash == NULL || slash < path + rootLength)
    path[rootLength] = '\0';
  else
    *slash = '\0';
}

static void uiFileManagerLoop(void) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserEntry *entries = calloc(BROWSER_MAX_ENTRIES, sizeof(*entries));
  if (roots == NULL || entries == NULL) {
    free(roots);
    free(entries);
    return;
  }
  int rootCount = collectRoots(roots);
  int rootSelected = 0;
  int count = 0;
  int selected = 0;
  int truncated = 0;
  int activeRoot = -1;
  char path[PATH_MAX + 1] = "";
  char status[96] = "Folders and file sizes are read-only.";

  while (1) {
    if (activeRoot < 0) {
      drawBrowserFrame("File Manager", "Available storage", status,
                       "X Open                         Triangle Back",
                       rootCount, rootSelected, browserFirstRow(rootSelected),
                       rootRow, roots);
    } else {
      drawBrowserFrame("File Manager", path,
                       truncated ? "Showing first 512 entries" : status,
                       "X Open folder                 Triangle Back",
                       count, selected, browserFirstRow(selected),
                       fileRow, entries);
    }
    int input = waitForInput(-1);
    if (input & (PAD_TRIANGLE | PAD_CIRCLE)) {
      if (activeRoot < 0)
        break;
      if (!strcmp(path, roots[activeRoot].path)) {
        activeRoot = -1;
      } else {
        parentPath(path, roots[activeRoot].path);
        if (!loadDirectory(path, entries, &count, &truncated)) {
          activeRoot = -1;
        }
        selected = 0;
      }
      snprintf(status, sizeof(status), "Folders and file sizes are read-only.");
    } else if (input & PAD_UP) {
      int total = activeRoot < 0 ? rootCount : count;
      int *focus = activeRoot < 0 ? &rootSelected : &selected;
      if (total > 0)
        *focus = (*focus + total - 1) % total;
    } else if (input & PAD_DOWN) {
      int total = activeRoot < 0 ? rootCount : count;
      int *focus = activeRoot < 0 ? &rootSelected : &selected;
      if (total > 0)
        *focus = (*focus + 1) % total;
    } else if (input & PAD_CROSS) {
      if (activeRoot < 0 && rootCount > 0) {
        if (loadDirectory(roots[rootSelected].path, entries, &count,
                          &truncated)) {
          activeRoot = rootSelected;
          snprintf(path, sizeof(path), "%s", roots[activeRoot].path);
          selected = 0;
          status[0] = '\0';
        } else {
          snprintf(status, sizeof(status), "Could not open this device.");
        }
      } else if (activeRoot >= 0 && count > 0) {
        if (entries[selected].isDirectory) {
          char child[PATH_MAX + 1];
          if (!joinPath(child, sizeof(child), path, entries[selected].name)) {
            snprintf(status, sizeof(status), "Folder path is too long.");
          } else if (!loadDirectory(child, entries, &count, &truncated)) {
            snprintf(status, sizeof(status), "Could not open this folder.");
          } else {
            snprintf(path, sizeof(path), "%s", child);
            selected = 0;
            status[0] = '\0';
          }
        } else {
          snprintf(status, sizeof(status), "File details are shown on the right.");
        }
      }
    }
  }
  clearEntries(entries, count);
  free(entries);
  free(roots);
}

static int vmcCardName(const char *name) {
  const char *extension = strrchr(name, '.');
  return extension != NULL && strlen(extension) == 4 &&
         tolower((unsigned char)extension[1]) == 'b' &&
         tolower((unsigned char)extension[2]) == 'i' &&
         tolower((unsigned char)extension[3]) == 'n';
}

static int vmcRow(int index, char *name, size_t nameSize,
                  char *detail, size_t detailSize, void *context) {
  if (index == 0) {
    snprintf(name, nameSize, "Create new card");
    snprintf(detail, detailSize, "8 MB");
    return 1;
  }
  return fileRow(index - 1, name, nameSize, detail, detailSize, context);
}

static int loadVMCCards(const char *path, BrowserEntry *entries, int *count) {
  DIR *directory = opendir(path);
  *count = 0;
  if (directory == NULL)
    return 1; // /VMC may not exist yet.
  struct dirent *item;
  while (*count < BROWSER_MAX_ENTRIES && (item = readdir(directory)) != NULL) {
    char child[PATH_MAX + 1];
    struct stat info;
    int hasSize;
    if (!vmcCardName(item->d_name) ||
        item->d_type == DT_DIR ||
        !joinPath(child, sizeof(child), path, item->d_name))
      continue;
    hasSize = stat(child, &info) == 0;
    if (hasSize && S_ISDIR(info.st_mode))
      continue;
    entries[*count].name = strdup(item->d_name);
    if (entries[*count].name == NULL)
      break;
    entries[*count].isDirectory = 0;
    entries[*count].hasSize = hasSize;
    entries[*count].size = hasSize ? (uint64_t)info.st_size : 0;
    (*count)++;
  }
  closedir(directory);
  qsort(entries, *count, sizeof(entries[0]), entryCompare);
  return 1;
}

typedef struct {
  const char *directory;
} VMCProgress;

static void drawVMCProgress(int percent, void *context) {
  const VMCProgress *state = context;
  char status[80];
  snprintf(status, sizeof(status), "Creating formatted card: %d%%", percent);
  drawBrowserFrame("Virtual Memory Cards", state->directory, status,
                   "Please wait until creation finishes", 0, 0, 0,
                   vmcRow, NULL);
}

static int createNextVMC(const char *directory, char *created,
                         size_t createdSize) {
  struct stat info;
  VMCProgress progress = {directory};
  if (stat(directory, &info) != 0 && mkdir(directory, 0777) != 0)
    return 0;
  for (int index = 1; index <= 999; index++) {
    char name[24];
    snprintf(name, sizeof(name), "LUNA_%03d.bin", index);
    if (!joinPath(created, createdSize, directory, name))
      return 0;
    if (stat(created, &info) == 0)
      continue;
    drawVMCProgress(0, &progress);
    return lunaCreateVMC8(created, drawVMCProgress, &progress) == 0;
  }
  return 0;
}

static void uiVMCManagerLoop(void) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserEntry *entries = calloc(BROWSER_MAX_ENTRIES, sizeof(*entries));
  if (roots == NULL || entries == NULL) {
    free(roots);
    free(entries);
    return;
  }
  int rootCount = 0, rootSelected = 0, activeRoot = -1;
  int count = 0, selected = 0;
  char directory[PATH_MAX + 1] = "";
  char status[96] = "Choose the drive that holds your games.";
  for (int index = 0; index < MAX_DEVICES; index++) {
    struct DeviceMapEntry *device = &deviceModeMap[index];
    if (device->mode == MODE_NONE || device->mountpoint == NULL)
      break;
    if (device->mode == MODE_ATA || device->mode == MODE_USB ||
        device->mode == MODE_MX4SIO || device->mode == MODE_ILINK)
      rootCount = addRoot(roots, rootCount, device->mountpoint,
                          device->mode == MODE_ATA ? "ATA HDD" : "Local storage");
  }
  while (1) {
    if (activeRoot < 0)
      drawBrowserFrame("Virtual Memory Cards", "Choose a drive",
                       rootCount ? status : "No supported local drives found.",
                       "X Open                         Triangle Back",
                       rootCount, rootSelected, browserFirstRow(rootSelected),
                       rootRow, roots);
    else
      drawBrowserFrame("Virtual Memory Cards", directory, status,
                       "X Create/View                  Triangle Back",
                       count + 1, selected, browserFirstRow(selected),
                       vmcRow, entries);
    int input = waitForInput(-1);
    if (input & (PAD_TRIANGLE | PAD_CIRCLE)) {
      if (activeRoot < 0)
        break;
      clearEntries(entries, count);
      count = 0;
      activeRoot = -1;
      snprintf(status, sizeof(status), "Choose the drive that holds your games.");
    } else if (input & PAD_UP) {
      int total = activeRoot < 0 ? rootCount : count + 1;
      int *focus = activeRoot < 0 ? &rootSelected : &selected;
      if (total > 0)
        *focus = (*focus + total - 1) % total;
    } else if (input & PAD_DOWN) {
      int total = activeRoot < 0 ? rootCount : count + 1;
      int *focus = activeRoot < 0 ? &rootSelected : &selected;
      if (total > 0)
        *focus = (*focus + 1) % total;
    } else if (input & PAD_CROSS) {
      if (activeRoot < 0 && rootCount > 0) {
        if (!joinPath(directory, sizeof(directory), roots[rootSelected].path,
                      "VMC")) {
          snprintf(status, sizeof(status), "Drive path is too long.");
          continue;
        }
        activeRoot = rootSelected;
        selected = 0;
        loadVMCCards(directory, entries, &count);
        snprintf(status, sizeof(status), "Create a card, then assign it in pregame settings.");
      } else if (activeRoot >= 0 && selected == 0) {
        char created[PATH_MAX + 1];
        if (createNextVMC(directory, created, sizeof(created))) {
          clearEntries(entries, count);
          loadVMCCards(directory, entries, &count);
          selected = 0;
          snprintf(status, sizeof(status), "Created %s", strrchr(created, '/') + 1);
        } else {
          snprintf(status, sizeof(status), "Could not create card. Check free space and drive.");
        }
      } else if (activeRoot >= 0) {
        snprintf(status, sizeof(status), "Assign this card in a game's VMC slot options.");
      }
    }
  }
  clearEntries(entries, count);
  free(entries);
  free(roots);
}

static int mainMenuRow(int index, char *name, size_t nameSize,
                       char *detail, size_t detailSize, void *context) {
  int hasLibrary = *(int *)context;
  const char *label = index == 0 ? "File Manager" :
                      index == 1 ? "Virtual Memory Cards" :
                      (hasLibrary && index == 2 ? "Return to Library" : "Exit LUNA");
  snprintf(name, nameSize, "%s", label);
  detail[0] = '\0';
  (void)detailSize;
  return 1;
}

int uiMainMenuLoop(int hasLibrary) {
  int selected = 0;
  int count = hasLibrary ? 4 : 3;
  while (1) {
    drawBrowserFrame("LUNA", hasLibrary ? "Main menu" : "No games found",
                     hasLibrary ? "Browse storage or return to your games."
                                : "Browse storage even without a game library.",
                     "X Select                       Triangle Back",
                     count, selected, 0, mainMenuRow, &hasLibrary);
    int input = waitForInput(-1);
    if ((input & (PAD_TRIANGLE | PAD_CIRCLE)) && hasLibrary)
      return 0;
    if (input & PAD_UP)
      selected = (selected + count - 1) % count;
    else if (input & PAD_DOWN)
      selected = (selected + 1) % count;
    else if (input & PAD_CROSS) {
      if (selected == 0)
        uiFileManagerLoop();
      else if (selected == 1)
        uiVMCManagerLoop();
      else if (hasLibrary && selected == 2)
        return 0;
      else
        return 1;
    }
  }
}
