// Storage browser, VMC manager, and the library's Start menu.
#include "ui/file_manager.h"
#include "vmc_create.h"
#include "devices/devices.h"
#include "devices/init.h"
#include "ui/graphics.h"
#include "ui/pad.h"
#include "ui/view_internal.h"
#include <dirent.h>
#include <ctype.h>
#include <fcntl.h>
#include <hdd-ioctl.h>
#include <libpad.h>
#include <libmc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

#define BROWSER_MAX_ROOTS (MAX_DEVICES + 3)
#define BROWSER_MAX_ENTRIES 512
#define COPY_MAX_QUEUE 128
#define COPY_BUFFER_SIZE 32768
#define COPY_MAX_DEPTH 16
#define FILE_MANAGER_GLASS_MARGIN 8
#define FILE_MANAGER_LIST_TOP 114
#define FILE_MANAGER_NAME_MAX 255

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

typedef struct {
  BrowserEntry *entries;
  int root;
  int rootSelected;
  int count;
  int selected;
  int truncated;
  char path[PATH_MAX + 1];
} BrowserPane;

typedef struct {
  char *path;
  int isDirectory;
  uint64_t measuredBytes;
} CopyItem;

typedef struct {
  CopyItem items[COPY_MAX_QUEUE];
  int count;
  int sourceSide;
} CopyQueue;

static void drawBrowserSheet(int fileManager) {
  int width = gsGlobal->Width;
  int height = gsGlobal->Height;
  int panelLeft = fileManager ? FILE_MANAGER_GLASS_MARGIN : 30;
  int panelTop = fileManager ? FILE_MANAGER_GLASS_MARGIN : 78;
  int panelRight = width - panelLeft;
  int panelBottom = fileManager ? height - FILE_MANAGER_GLASS_MARGIN
                                : height - footerHeight + 3;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  drawSharedLibraryBackground(uiNowMs());
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, panelLeft + 2, panelTop + 2,
                    panelRight - 2, panelBottom - 2, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(panelLeft, panelTop, panelRight, panelBottom, 2);
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
                           const char *detail, int right) {
  int left = keepoutArea + 30;
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
                             int previewColumn,
                             int (*row)(int, char *, size_t, char *, size_t,
                                        CardArtType *,
                                        void *), void *context) {
  int left = keepoutArea + 30;
  int right = gsGlobal->Width - left;
  int lineHeight = getFontLineHeight();
  int rowStep = lineHeight + lineHeight / 3;
  int listTop = 112;
  int listBottom = gsGlobal->Height - footerHeight - lineHeight;
  int visible = (listBottom - listTop) / rowStep;
  int rowRight = previewColumn ? right - previewColumn : right;
  CardArtType selectedArt = CARD_ART_NONE;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(0);
  drawTextWindow(left, headerHeight - lineHeight, right, headerHeight + 2,
                 0, HeaderTextColor, ALIGN_HCENTER, heading);
  drawTextWindow(left + 8, 82, right - 8, 82 + lineHeight,
                 0, FontMainColor, ALIGN_LEFT, path);
  for (int index = first; index < count && index < first + visible; index++) {
    char name[PATH_MAX + 3];
    char detail[32];
    CardArtType cardArt = CARD_ART_NONE;
    if (!row(index, name, sizeof(name), detail, sizeof(detail),
             &cardArt, context))
      continue;
    if (index == selected)
      selectedArt = cardArt;
    drawBrowserRow(listTop + (index - first) * rowStep,
                   index == selected, name, detail[0] ? detail : NULL,
                   rowRight);
  }
  if (previewColumn && selectedArt != CARD_ART_NONE)
    drawCardArt(selectedArt, right - previewColumn + 10, listTop + 8,
                previewColumn - 20);
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
                   char *detail, size_t detailSize, CardArtType *cardArt,
                   void *context) {
  BrowserRoot *roots = context;
  snprintf(name, nameSize, "%s", roots[index].label);
  snprintf(detail, detailSize, "%s", roots[index].path);
  if (!strcmp(roots[index].path, "mc0:/"))
    *cardArt = CARD_ART_SLOT_1;
  else if (!strcmp(roots[index].path, "mc1:/"))
    *cardArt = CARD_ART_SLOT_2;
  return 1;
}

static int fileRow(int index, char *name, size_t nameSize,
                   char *detail, size_t detailSize, CardArtType *cardArt,
                   void *context) {
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
  *cardArt = CARD_ART_NONE;
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

static int queueFind(const CopyQueue *queue, const char *path) {
  for (int index = 0; index < queue->count; index++)
    if (!strcmp(queue->items[index].path, path))
      return index;
  return -1;
}

static void queueRemove(CopyQueue *queue, int index) {
  free(queue->items[index].path);
  for (int next = index + 1; next < queue->count; next++)
    queue->items[next - 1] = queue->items[next];
  queue->count--;
  if (queue->count == 0)
    queue->sourceSide = -1;
}

static void queueClear(CopyQueue *queue) {
  while (queue->count > 0)
    queueRemove(queue, queue->count - 1);
}

static int pathWithin(const char *path, const char *folder) {
  size_t length = strlen(folder);
  return !strncmp(path, folder, length) &&
         (path[length] == '\0' || path[length] == '/');
}

static void drawFileManagerSelection(int x1, int y, int x2,
                                     int lineHeight, int active);

static int validFileName(const char *name) {
  return name[0] != '\0' && strcmp(name, ".") && strcmp(name, "..") &&
         strpbrk(name, "/\\:") == NULL;
}

static int editFileName(const char *title, char *name, size_t capacity,
                        size_t maxLength) {
  static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._- ";
  int selected = 0;
  int lowerCase = 0;
  char message[96] = "";
  if (maxLength >= capacity)
    maxLength = capacity - 1;
  while (1) {
    int left = keepoutArea + 30;
    int right = gsGlobal->Width - left;
    int length = strlen(name);
    const char *visible = length > 42 ? name + length - 42 : name;
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawBrowserSheet(1);
    drawTextWindow(left, 22, right, 50, 0, HeaderTextColor,
                   ALIGN_HCENTER, title);
    gsKit_prim_sprite(gsGlobal, left, 73, right, 109, 3,
                      glassPresetColor(0x18, 0x4A, 0x68, 0x52));
    drawTextWindow(left + 10, 79, right - 10, 103, 0, FontMainColor,
                   ALIGN_LEFT, visible);
    for (int index = 0; index < 40; index++) {
      int x = left + 8 + (index % 10) * ((right - left - 16) / 10);
      int y = 127 + (index / 10) * 54;
      char label[3] = {keys[index], '\0', '\0'};
      if (label[0] == ' ')
        snprintf(label, sizeof(label), "SP");
      else if (lowerCase && label[0] >= 'A' && label[0] <= 'Z')
        label[0] += 'a' - 'A';
      if (index == selected)
        drawFileManagerSelection(x - 2, y, x + 42, 29, 1);
      drawTextWindow(x, y, x + 40, y + 29, 0, FontMainColor,
                     ALIGN_CENTER, label);
    }
    drawTextWindow(left + 8, 351, right - 8, 379, 0, ErrorTextColor,
                   ALIGN_LEFT, message);
    drawTextWindow(left + 8, 385, right - 8, 411, 0,
                   HeaderTextColor, ALIGN_CENTER,
                   "Arrows Pick  X Type  Sq Space  Tri Delete");
    drawTextWindow(left + 8, 411, right - 8, gsGlobal->Height - 8, 0,
                   HeaderTextColor, ALIGN_CENTER,
                   "R1 Case  Start Save  Circle Cancel");
    presentBrowserFrame();
    int input = waitForInput(-1);
    if (input & PAD_CIRCLE)
      return 0;
    if (input & PAD_START) {
      if (length <= maxLength && validFileName(name))
        return 1;
      snprintf(message, sizeof(message), "Enter a valid name (%u characters max).",
               (unsigned)maxLength);
    } else if (input & PAD_TRIANGLE) {
      if (length > 0)
        name[length - 1] = '\0';
      message[0] = '\0';
    } else if (input & PAD_R1) {
      lowerCase = !lowerCase;
    } else if (input & (PAD_CROSS | PAD_SQUARE)) {
      if (length >= maxLength) {
        snprintf(message, sizeof(message), "Name is at its length limit.");
      } else {
        char letter = input & PAD_SQUARE ? ' ' : keys[selected];
        if (lowerCase && letter >= 'A' && letter <= 'Z')
          letter += 'a' - 'A';
        name[length] = letter;
        name[length + 1] = '\0';
        message[0] = '\0';
      }
    } else if (input & PAD_LEFT) {
      selected = selected % 10 == 0 ? selected + 9 : selected - 1;
    } else if (input & PAD_RIGHT) {
      selected = selected % 10 == 9 ? selected - 9 : selected + 1;
    } else if (input & PAD_UP) {
      selected = (selected + 30) % 40;
    } else if (input & PAD_DOWN) {
      selected = (selected + 10) % 40;
    }
  }
}

static void formatBytes(uint64_t bytes, char *text, size_t size) {
  if (bytes >= 1048576)
    snprintf(text, size, "%llu.%02llu MiB (%llu bytes)",
             (unsigned long long)(bytes / 1048576),
             (unsigned long long)((bytes % 1048576) * 100 / 1048576),
             (unsigned long long)bytes);
  else
    snprintf(text, size, "%llu bytes", (unsigned long long)bytes);
}

static int getAvailableSpace(const char *root, uint64_t *bytes) {
  if (!strncmp(root, "pfs", 3)) {
    int zones = fileXioDevctl("pfs0:", PDIOC_ZONEFREE, NULL, 0, NULL, 0);
    int zoneSize = fileXioDevctl("pfs0:", PDIOC_ZONESZ, NULL, 0, NULL, 0);
    if (zones >= 0 && zoneSize > 0) {
      *bytes = (uint64_t)zones * zoneSize;
      return 1;
    }
  } else if (!strncmp(root, "mc0:", 4) || !strncmp(root, "mc1:", 4)) {
    int port = root[2] - '0';
    int type, freeClusters, formatted, result;
    mcInit(MC_TYPE_MC);
    if (mcGetInfo(port, 0, &type, &freeClusters, &formatted) >= 0 &&
        mcSync(0, NULL, &result) == 1 && result >= -1 &&
        freeClusters >= 0) {
      *bytes = (uint64_t)freeClusters * 1024;
      return 1;
    }
  }
  return 0;
}

static void showFileDetails(const BrowserRoot *roots, int rootCount,
                            const BrowserPane *pane) {
  char path[PATH_MAX + 1];
  const char *kind = "Drive";
  int root = pane->root < 0 ? pane->rootSelected : pane->root;
  if (root < 0 || root >= rootCount)
    return;
  snprintf(path, sizeof(path), "%s", roots[root].path);
  if (pane->root >= 0 && pane->count > 0) {
    if (!joinPath(path, sizeof(path), pane->path,
                  pane->entries[pane->selected].name))
      return;
    kind = pane->entries[pane->selected].isDirectory ? "Folder" : "File";
  } else if (pane->root >= 0) {
    snprintf(path, sizeof(path), "%s", pane->path);
    kind = "Folder";
  }
  struct stat info;
  int hasSize = stat(path, &info) == 0 && !S_ISDIR(info.st_mode);
  uint64_t freeBytes = 0;
  int hasFree = getAvailableSpace(roots[root].path, &freeBytes);
  char sizeText[88], freeText[88];
  if (hasSize)
    formatBytes((uint64_t)info.st_size, sizeText, sizeof(sizeText));
  else
    snprintf(sizeText, sizeof(sizeText), "Not calculated");
  if (hasFree)
    formatBytes(freeBytes, freeText, sizeof(freeText));
  else
    snprintf(freeText, sizeof(freeText), "Unavailable on this drive");
  int scroll = 0;
  int lines = ((int)strlen(path) + 41) / 42;
  while (1) {
    int left = keepoutArea + 30;
    int right = gsGlobal->Width - left;
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawBrowserSheet(1);
    drawTextWindow(left, 22, right, 50, 0, HeaderTextColor,
                   ALIGN_HCENTER, "File details");
    drawTextWindow(left + 8, 68, right - 8, 94, 0, FontMainColor,
                   ALIGN_LEFT, kind);
    drawTextWindow(left + 8, 102, right - 8, 128, 0, HeaderTextColor,
                   ALIGN_LEFT, "SIZE");
    drawTextWindow(left + 8, 129, right - 8, 155, 0, FontMainColor,
                   ALIGN_LEFT, sizeText);
    drawTextWindow(left + 8, 163, right - 8, 189, 0, HeaderTextColor,
                   ALIGN_LEFT, "AVAILABLE SPACE");
    drawTextWindow(left + 8, 190, right - 8, 216, 0, FontMainColor,
                   ALIGN_LEFT, freeText);
    drawTextWindow(left + 8, 224, right - 8, 250, 0, HeaderTextColor,
                   ALIGN_LEFT, "FULL PATH");
    for (int row = 0; row < 5 && scroll + row < lines; row++) {
      char segment[43];
      int offset = (scroll + row) * 42;
      snprintf(segment, sizeof(segment), "%s", path + offset);
      drawTextWindow(left + 8, 254 + row * 25, right - 8,
                     279 + row * 25, 0, FontMainColor, ALIGN_LEFT, segment);
    }
    drawTextWindow(left + 8, 391, right - 8, gsGlobal->Height - 8, 0,
                   HeaderTextColor, ALIGN_CENTER,
                   "Up/Down Scroll path     Circle Back");
    presentBrowserFrame();
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE))
      return;
    if ((input & PAD_DOWN) && scroll + 5 < lines)
      scroll++;
    if ((input & PAD_UP) && scroll > 0)
      scroll--;
  }
}

enum FileAction {
  FILE_ACTION_DETAILS,
  FILE_ACTION_RENAME,
  FILE_ACTION_NEW_FOLDER,
  FILE_ACTION_MOVE,
};

typedef struct {
  enum FileAction actions[4];
  int count;
} FileActionMenu;

static int fileActionRow(int index, char *name, size_t nameSize,
                         char *detail, size_t detailSize,
                         CardArtType *cardArt, void *context) {
  const FileActionMenu *menu = context;
  const char *label = menu->actions[index] == FILE_ACTION_DETAILS ? "Details" :
                      menu->actions[index] == FILE_ACTION_RENAME ? "Rename" :
                      menu->actions[index] == FILE_ACTION_NEW_FOLDER ? "New folder" :
                      "Move marked items";
  snprintf(name, nameSize, "%s", label);
  detail[0] = '\0';
  *cardArt = CARD_ART_NONE;
  (void)detailSize;
  return 1;
}

static int chooseFileAction(const BrowserPane *pane, const CopyQueue *queue) {
  FileActionMenu menu = {0};
  menu.actions[menu.count++] = FILE_ACTION_DETAILS;
  if (pane->root >= 0 && pane->count > 0)
    menu.actions[menu.count++] = FILE_ACTION_RENAME;
  if (pane->root >= 0)
    menu.actions[menu.count++] = FILE_ACTION_NEW_FOLDER;
  if (queue->count > 0)
    menu.actions[menu.count++] = FILE_ACTION_MOVE;
  int selected = 0;
  while (1) {
    drawBrowserFrame("File Manager", "Actions", "Choose an action.",
                     "X Select                  Circle Back",
                     menu.count, selected, 0, 0, fileActionRow, &menu);
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE))
      return -1;
    if (input & PAD_UP)
      selected = (selected + menu.count - 1) % menu.count;
    else if (input & PAD_DOWN)
      selected = (selected + 1) % menu.count;
    else if (input & PAD_CROSS)
      return menu.actions[selected];
  }
}

static void selectPaneEntry(BrowserPane *pane, const char *name) {
  if (!loadDirectory(pane->path, pane->entries, &pane->count,
                     &pane->truncated)) {
    clearEntries(pane->entries, pane->count);
    pane->root = -1;
    pane->count = 0;
    pane->selected = 0;
    return;
  }
  pane->selected = 0;
  if (name != NULL)
    for (int index = 0; index < pane->count; index++)
      if (!strcmp(pane->entries[index].name, name)) {
        pane->selected = index;
        break;
      }
}

static void renamePaneEntry(BrowserPane *pane, const BrowserRoot *roots,
                            CopyQueue *queue,
                            char *status, size_t statusSize) {
  if (pane->root < 0 || pane->count == 0)
    return;
  char source[PATH_MAX + 1], destination[PATH_MAX + 1];
  char name[PATH_MAX + 1];
  const char *oldName = pane->entries[pane->selected].name;
  if (!joinPath(source, sizeof(source), pane->path, oldName)) {
    snprintf(status, statusSize, "The current path is too long.");
    return;
  }
  snprintf(name, sizeof(name), "%s", oldName);
  size_t limit = !strncmp(roots[pane->root].path, "mc", 2) ? 31 :
                 FILE_MANAGER_NAME_MAX;
  if (!editFileName("Rename", name, sizeof(name), limit))
    return;
  if (!strcmp(name, oldName))
    return;
  if (!joinPath(destination, sizeof(destination), pane->path, name)) {
    snprintf(status, statusSize, "The new path is too long.");
    return;
  }
  struct stat info;
  if (stat(destination, &info) == 0) {
    snprintf(status, statusSize, "That name already exists.");
    return;
  }
  if (rename(source, destination) != 0) {
    snprintf(status, statusSize, "Rename failed on this drive.");
    return;
  }
  int cleared = 0;
  for (int index = 0; index < queue->count;) {
    if (pathWithin(queue->items[index].path, source) ||
        pathWithin(source, queue->items[index].path)) {
      queueRemove(queue, index);
      cleared++;
    } else {
      index++;
    }
  }
  selectPaneEntry(pane, name);
  if (cleared)
    snprintf(status, statusSize, "Renamed to %s; %d affected marks cleared.",
             name, cleared);
  else
    snprintf(status, statusSize, "Renamed to %s", name);
}

static void createPaneFolder(BrowserPane *pane, const BrowserRoot *roots,
                             char *status, size_t statusSize) {
  if (pane->root < 0)
    return;
  char name[PATH_MAX + 1] = "";
  char path[PATH_MAX + 1];
  size_t limit = !strncmp(roots[pane->root].path, "mc", 2) ? 31 :
                 FILE_MANAGER_NAME_MAX;
  if (!editFileName("New folder", name, sizeof(name), limit))
    return;
  if (!joinPath(path, sizeof(path), pane->path, name)) {
    snprintf(status, statusSize, "The folder path is too long.");
    return;
  }
  struct stat info;
  if (stat(path, &info) == 0) {
    snprintf(status, statusSize, "That name already exists.");
    return;
  }
  if (mkdir(path, 0777) != 0) {
    snprintf(status, statusSize, "Could not create folder on this drive.");
    return;
  }
  selectPaneEntry(pane, name);
  snprintf(status, statusSize, "Created folder %s", name);
}

static void drawFileManagerPane(int x1, int y1, int x2, int y2, int active) {
  uint64_t fill = glassPresetColor(0x0C, 0x25, 0x3A,
                                   active ? 0x22 : 0x11);
  uint64_t edge = glassPresetColor(0x62, 0xB8, 0xD8,
                                   active ? 0x58 : 0x22);
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, 3, fill);
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y1 + 2, 4, edge);
  gsKit_prim_sprite(gsGlobal, x1, y1, x1 + 1, y2, 4, edge);
  gsKit_prim_sprite(gsGlobal, x2 - 1, y1, x2, y2, 4, edge);
  gsKit_prim_sprite(gsGlobal, x1, y2 - 1, x2, y2, 4, edge);
}

static void drawFileManagerSelection(int x1, int y, int x2,
                                     int lineHeight, int active) {
  uint64_t fill = glassPresetColor(0x16, 0x56, 0x78,
                                   active ? 0x48 : 0x20);
  gsKit_prim_sprite(gsGlobal, x1, y - 2, x2, y + lineHeight + 2, 4, fill);
  if (active) {
    uint64_t edge = glassPresetColor(0x7C, 0xD8, 0xF0, 0x70);
    gsKit_prim_sprite(gsGlobal, x1, y - 2, x1 + 3,
                      y + lineHeight + 2, 5, edge);
    gsKit_prim_sprite(gsGlobal, x1, y - 2, x2, y - 1, 5, edge);
    gsKit_prim_sprite(gsGlobal, x1, y + lineHeight + 1,
                      x2, y + lineHeight + 2, 5, edge);
  }
}

static void drawFileManagerItemIcon(int x, int y, int root, int directory,
                                    int active) {
  uint64_t color = glassPresetColor(0x78, 0xBD, 0xD7,
                                    active ? 0x68 : 0x3C);
  if (root) {
    gsKit_prim_sprite(gsGlobal, x, y + 6, x + 16, y + 16, 5, color);
    gsKit_prim_sprite(gsGlobal, x + 2, y + 8, x + 14, y + 12, 6,
                      glassPresetColor(0x08, 0x20, 0x32, 0x70));
    gsKit_prim_sprite(gsGlobal, x + 3, y + 14, x + 5, y + 15, 6,
                      ColorSelected);
  } else if (directory) {
    gsKit_prim_sprite(gsGlobal, x, y + 5, x + 7, y + 8, 5, color);
    gsKit_prim_sprite(gsGlobal, x, y + 8, x + 16, y + 17, 5, color);
  } else {
    gsKit_prim_sprite(gsGlobal, x + 2, y + 4, x + 14, y + 18, 5, color);
    gsKit_prim_sprite(gsGlobal, x + 4, y + 7, x + 12, y + 8, 6,
                      glassPresetColor(0x08, 0x20, 0x32, 0x70));
  }
}

static void drawFileManagerWatermark(int x1, int x2, int y1, int y2) {
  int x = (x1 + x2) / 2;
  int y = (y1 + y2) / 2;
  uint64_t color = glassPresetColor(0x55, 0x9A, 0xBB, 0x14);
  gsKit_prim_sprite(gsGlobal, x - 34, y - 19, x + 34, y - 18, 3, color);
  gsKit_prim_sprite(gsGlobal, x - 34, y + 18, x + 34, y + 19, 3, color);
  gsKit_prim_sprite(gsGlobal, x - 34, y - 19, x - 33, y + 19, 3, color);
  gsKit_prim_sprite(gsGlobal, x + 33, y - 19, x + 34, y + 19, 3, color);
  gsKit_prim_sprite(gsGlobal, x - 26, y + 9, x + 26, y + 10, 3, color);
  gsKit_prim_sprite(gsGlobal, x - 25, y + 13, x - 20, y + 15, 3, color);
}

static void drawFileManagerControl(int x, int right, int top,
                                   IconType icon, const char *label) {
  int bottom = gsGlobal->Height - FILE_MANAGER_GLASS_MARGIN;
  drawIconWindow(x, top + 6, 0, bottom, 0, FontMainColor,
                 ALIGN_VCENTER, icon);
  drawTextWindow(x + getIconWidth(icon) + 4, top + 6, right, bottom,
                 0, HeaderTextColor, ALIGN_VCENTER, label);
}

static void drawFileManagerFooter(int left, int right, const char *footer) {
  int top = gsGlobal->Height - footerHeight;
  int bottom = gsGlobal->Height - FILE_MANAGER_GLASS_MARGIN;
  gsKit_prim_sprite(gsGlobal, FILE_MANAGER_GLASS_MARGIN + 2, top,
                    gsGlobal->Width - FILE_MANAGER_GLASS_MARGIN - 2,
                    bottom, 3, glassPresetColor(0x02, 0x10, 0x22, 0x48));
  gsKit_prim_sprite(gsGlobal, left + 8, top, right - 8, top + 1, 4,
                    glassPresetColor(0x55, 0x9A, 0xB8, 0x48));
  if (footer != NULL) {
    drawTextWindow(left + 8, top + 6, right - 8, bottom, 0,
                   HeaderTextColor, ALIGN_CENTER, footer);
    return;
  }
  int slot = (right - left - 16) / 6;
  int x = left + 8;
  drawTextWindow(x, top + 6, x + slot, bottom, 0,
                 HeaderTextColor, ALIGN_VCENTER, "L/R Wide");
  drawFileManagerControl(x + slot, x + 2 * slot, top, ICON_CROSS, "Open");
  drawFileManagerControl(x + 2 * slot, x + 3 * slot, top, ICON_TRIANGLE, "Up");
  drawFileManagerControl(x + 3 * slot, x + 4 * slot, top, ICON_SQUARE, "Mark");
  drawFileManagerControl(x + 4 * slot, x + 5 * slot, top, ICON_START, "Copy");
  drawTextWindow(x + 5 * slot, top + 6, right - 8, bottom, 0,
                 HeaderTextColor, ALIGN_VCENTER, "R2 More");
}

static void drawFileManagerFrame(const BrowserRoot *roots, int rootCount,
                                 const BrowserPane *panes, const CopyQueue *queue,
                                 int active, int expandedPane,
                                 const char *status, const char *footer,
                                 int progressPercent) {
  int left = keepoutArea + 10;
  int right = gsGlobal->Width - left;
  int paneSpace = right - left - 12;
  int leftPaneWidth = expandedPane == 0 ? paneSpace * 3 / 4 :
                      expandedPane == 1 ? paneSpace / 4 : paneSpace / 2;
  int lineHeight = getFontLineHeight();
  int rowStep = lineHeight + lineHeight / 3;
  int listTop = FILE_MANAGER_LIST_TOP;
  int listBottom = gsGlobal->Height - footerHeight - lineHeight;
  int visible = (listBottom - listTop) / rowStep;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(1);
  drawTextWindow(left, headerHeight - lineHeight, right, headerHeight + 2,
                 0, HeaderTextColor, ALIGN_HCENTER, "File Manager");
  for (int side = 0; side < 2; side++) {
    const BrowserPane *pane = &panes[side];
    int paneLeft = side == 0 ? left : left + leftPaneWidth + 12;
    int paneRight = side == 0 ? left + leftPaneWidth : right;
    int compact = paneRight - paneLeft < 180;
    int x1 = paneLeft + (compact ? 8 : 10);
    int x2 = paneRight - (compact ? 8 : 10);
    int total = pane->root < 0 ? rootCount : pane->count;
    int selected = pane->root < 0 ? pane->rootSelected : pane->selected;
    int first = visible > 0 ? selected / visible * visible : 0;
    drawFileManagerPane(paneLeft, 51, paneRight, listBottom + 2,
                        side == active);
    char heading[48];
    const char *sideName = side == 0 ? "LEFT" : "RIGHT";
    if (queue->count > 0 && queue->sourceSide == side) {
      if (compact)
        snprintf(heading, sizeof(heading), "%s SRC", sideName);
      else
        snprintf(heading, sizeof(heading), "%s SOURCE (%d)",
                 sideName, queue->count);
    } else if (queue->count > 0)
      snprintf(heading, sizeof(heading), compact ? "%s DST" : "%s DESTINATION",
               sideName);
    else
      snprintf(heading, sizeof(heading), "%s", sideName);
    drawTextWindow(x1, 54, x2, 54 + lineHeight, 0,
                   side == active ? ColorSelected : HeaderTextColor,
                   ALIGN_LEFT, heading);
    drawTextWindow(x1, 79, x2, 79 + lineHeight, 0,
                   glassPresetColor(0x86, 0xA9, 0xBC, 0x60), ALIGN_LEFT,
                   pane->root < 0 ? "DRIVES" : compact ? "FOLDER" : pane->path);
    for (int index = first; index < total && index < first + visible; index++) {
      char name[PATH_MAX + 16];
      char detail[32] = "";
      int y = listTop + (index - first) * rowStep;
      int isRoot = pane->root < 0;
      int isDirectory = 0;
      if (isRoot) {
        const char *label = roots[index].label;
        if (compact && !strcmp(label, "Emulator host"))
          label = "Host";
        else if (compact && !strcmp(label, "Memory card 1"))
          label = "MC 1";
        else if (compact && !strcmp(label, "Memory card 2"))
          label = "MC 2";
        snprintf(name, sizeof(name), "%s", label);
        snprintf(detail, sizeof(detail), "%s", roots[index].path);
      } else {
        const BrowserEntry *entry = &pane->entries[index];
        isDirectory = entry->isDirectory;
        char path[PATH_MAX + 1];
        int marked = joinPath(path, sizeof(path), pane->path, entry->name) &&
                     queueFind(queue, path) >= 0;
        snprintf(name, sizeof(name), "%s%s%s", marked ? "[x] " : "", entry->name,
                 entry->isDirectory ? "/" : "");
        if (!entry->isDirectory && entry->hasSize)
          snprintf(detail, sizeof(detail), "%llu K",
                   (unsigned long long)((entry->size + 1023) / 1024));
      }
      if (index == selected)
        drawFileManagerSelection(x1 - 4, y, x2 + 4, lineHeight,
                                 side == active);
      if (!compact)
        drawFileManagerItemIcon(x1 + 4, y, isRoot, isDirectory,
                                side == active);
      // Keep the narrower pane useful for choosing a destination folder.
      if (paneRight - paneLeft < 220)
        detail[0] = '\0';
      int detailWidth = detail[0] ? (int)getLineWidth(detail) + 10 : 0;
      drawTextWindow(x1 + (compact ? 6 : 27), y,
                     x2 - detailWidth - 4, y + lineHeight,
                     0, index == selected && side == active ? FontMainColor
                                                            : HeaderTextColor,
                     ALIGN_LEFT, name);
      if (detail[0])
        drawTextWindow(x2 - detailWidth, y, x2, y + lineHeight, 0,
                       glassPresetColor(0x94, 0xB7, 0xC8, 0x68),
                       ALIGN_RIGHT, detail);
    }
    if (!compact && pane->root < 0 && total <= 2 &&
        listTop + (total + 3) * rowStep < listBottom)
      drawFileManagerWatermark(x1, x2, listTop + total * rowStep + 18,
                               listBottom);
    if (total == 0)
      drawTextWindow(x1 + 8, listTop, x2, listTop + lineHeight, 0,
                     HeaderTextColor, ALIGN_LEFT,
                     compact ? "Empty" : "Nothing to show");
  }
  if (progressPercent >= 0) {
    int filled = (right - left - 16) * progressPercent / 100;
    gsKit_prim_sprite(gsGlobal, left + 8, listBottom - 8,
                      right - 8, listBottom - 4, 3,
                      glassPresetColor(0x28, 0x38, 0x50, 0x70));
    if (filled > 0)
      gsKit_prim_sprite(gsGlobal, left + 8, listBottom - 8,
                        left + 8 + filled, listBottom - 4, 4,
                        glassPresetColor(0x50, 0x90, 0xB0, 0x80));
  }
  drawTextWindow(left + 8, listBottom, right - 8,
                 gsGlobal->Height - footerHeight, 0, HeaderTextColor,
                 ALIGN_LEFT, status);
  drawFileManagerFooter(left, right, footer);
  presentBrowserFrame();
}

typedef struct {
  const BrowserRoot *roots;
  int rootCount;
  BrowserPane *panes;
  const CopyQueue *queue;
  int active;
  int expandedPane;
  uint64_t totalBytes;
  uint64_t bytes;
  uint64_t scannedBytes;
  int totalFiles;
  int files;
  int lastFiles;
  int itemIndex;
  int itemCount;
  uint32_t startMs;
  uint32_t lastDrawMs;
  int cancelled;
} CopyProgress;

static int copyCancelled(CopyProgress *progress) {
  if (readInput() & PAD_CIRCLE)
    progress->cancelled = 1;
  return progress->cancelled;
}

static void showCopyProgress(CopyProgress *progress, const char *name,
                             int force) {
  char status[160], footer[160];
  const char *basename = strrchr(name, '/');
  if (basename != NULL)
    name = basename + 1;
  uint32_t now = uiNowMs();
  if (!force && now - progress->lastDrawMs < 250 &&
      progress->files == progress->lastFiles)
    return;
  progress->lastDrawMs = now;
  progress->lastFiles = progress->files;
  uint32_t elapsed = now - progress->startMs;
  int percent = progress->totalBytes ?
      (int)((progress->bytes > progress->totalBytes ? progress->totalBytes :
             progress->bytes) * 100 / progress->totalBytes) : 100;
  uint64_t speedTenths = elapsed ?
      progress->bytes * 10000 / ((uint64_t)elapsed * 1048576) : 0;
  uint64_t secondsLeft = progress->bytes && progress->totalBytes > progress->bytes ?
      (progress->totalBytes - progress->bytes) * elapsed / progress->bytes / 1000 : 0;
  snprintf(status, sizeof(status), "%d/%d items  %d%%  %llu/%llu MiB  %llu.%llu MiB/s",
           progress->itemIndex, progress->itemCount, percent,
           (unsigned long long)(progress->bytes / 1048576),
           (unsigned long long)((progress->totalBytes + 1048575) / 1048576),
           (unsigned long long)(speedTenths / 10),
           (unsigned long long)(speedTenths % 10));
  snprintf(footer, sizeof(footer), "ETA %llu:%02llu  %s  |  Circle Cancel",
           (unsigned long long)(secondsLeft / 60),
           (unsigned long long)(secondsLeft % 60), name);
  drawFileManagerFrame(progress->roots, progress->rootCount, progress->panes,
                       progress->queue, progress->active,
                       progress->expandedPane, status, footer,
                       percent);
}

static int measureTree(const char *source, int depth, CopyProgress *progress) {
  struct stat info;
  if (depth > COPY_MAX_DEPTH || stat(source, &info) != 0 ||
      copyCancelled(progress))
    return -1;
  if (S_ISDIR(info.st_mode)) {
    DIR *directory = opendir(source);
    if (directory == NULL)
      return -1;
    struct dirent *item;
    while ((item = readdir(directory)) != NULL) {
      char *child;
      int result;
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        continue;
      child = malloc(PATH_MAX + 1);
      if (child == NULL) {
        closedir(directory);
        return -1;
      }
      if (!joinPath(child, PATH_MAX + 1, source, item->d_name)) {
        free(child);
        closedir(directory);
        return -1;
      }
      result = measureTree(child, depth + 1, progress);
      free(child);
      if (result != 0) {
        closedir(directory);
        return -1;
      }
    }
    closedir(directory);
    return 0;
  }
  if (!S_ISREG(info.st_mode))
    return -1;
  progress->totalFiles++;
  if (info.st_size > 0)
    progress->scannedBytes += (uint64_t)info.st_size;
  uint32_t now = uiNowMs();
  if (now - progress->lastDrawMs >= 300) {
    char status[120];
    snprintf(status, sizeof(status), "Measuring queue: %d files, %llu MiB",
             progress->totalFiles,
             (unsigned long long)(progress->scannedBytes / 1048576));
    drawFileManagerFrame(progress->roots, progress->rootCount, progress->panes,
                         progress->queue, progress->active,
                         progress->expandedPane, status,
                         "Circle Cancel measurement", -1);
    progress->lastDrawMs = now;
  }
  return 0;
}

static int copyTree(const char *source, const char *destination, int depth,
                    CopyProgress *progress) {
  struct stat info;
  if (depth > COPY_MAX_DEPTH || stat(source, &info) != 0 ||
      copyCancelled(progress))
    return -1;
  if (S_ISDIR(info.st_mode)) {
    DIR *directory;
    struct dirent *item;
    if (mkdir(destination, 0777) != 0)
      return -1;
    directory = opendir(source);
    if (directory == NULL)
      return -1;
    while ((item = readdir(directory)) != NULL) {
      char *paths;
      int result;
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        continue;
      paths = malloc(2 * (PATH_MAX + 1));
      if (paths == NULL) {
        closedir(directory);
        return -1;
      }
      if (!joinPath(paths, PATH_MAX + 1, source, item->d_name) ||
          !joinPath(paths + PATH_MAX + 1, PATH_MAX + 1, destination,
                    item->d_name)) {
        free(paths);
        closedir(directory);
        return -1;
      }
      result = copyTree(paths, paths + PATH_MAX + 1, depth + 1, progress);
      free(paths);
      if (result != 0) {
        closedir(directory);
        return -1;
      }
    }
    closedir(directory);
    return 0;
  }
  if (!S_ISREG(info.st_mode))
    return -1;
  int input = open(source, O_RDONLY);
  if (input < 0)
    return -1;
  int output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (output < 0) {
    close(input);
    return -1;
  }
  char *buffer = malloc(COPY_BUFFER_SIZE);
  int result = buffer == NULL ? -1 : 0;
  progress->files++;
  showCopyProgress(progress, source, 0);
  while (result == 0 && !copyCancelled(progress)) {
    int bytes = read(input, buffer, COPY_BUFFER_SIZE);
    if (bytes < 0) {
      result = -1;
      break;
    }
    if (bytes == 0)
      break;
    for (int offset = 0; offset < bytes;) {
      int written = write(output, buffer + offset, bytes - offset);
      if (written <= 0) {
        result = -1;
        break;
      }
      offset += written;
    }
    if (result == 0) {
      progress->bytes += bytes;
      showCopyProgress(progress, source, 0);
    }
  }
  if (progress->cancelled)
    result = -1;
  free(buffer);
  if (close(output) != 0)
    result = -1;
  close(input);
  if (result != 0)
    unlink(destination);
  return result;
}

static int verifyTree(const char *source, const char *destination, int depth,
                      CopyProgress *progress) {
  struct stat sourceInfo, destinationInfo;
  if (depth > COPY_MAX_DEPTH || stat(source, &sourceInfo) != 0 ||
      stat(destination, &destinationInfo) != 0 || copyCancelled(progress))
    return -1;
  if (S_ISDIR(sourceInfo.st_mode)) {
    if (!S_ISDIR(destinationInfo.st_mode))
      return -1;
    DIR *directory = opendir(source);
    if (directory == NULL)
      return -1;
    struct dirent *item;
    int result = 0;
    while ((item = readdir(directory)) != NULL) {
      char *paths;
      if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
        continue;
      paths = malloc(2 * (PATH_MAX + 1));
      if (paths == NULL ||
          !joinPath(paths, PATH_MAX + 1, source, item->d_name) ||
          !joinPath(paths + PATH_MAX + 1, PATH_MAX + 1, destination,
                    item->d_name) ||
          verifyTree(paths, paths + PATH_MAX + 1, depth + 1, progress) != 0) {
        result = -1;
        free(paths);
        break;
      }
      free(paths);
    }
    closedir(directory);
    return result;
  }
  if (!S_ISREG(sourceInfo.st_mode) || !S_ISREG(destinationInfo.st_mode) ||
      sourceInfo.st_size != destinationInfo.st_size)
    return -1;
  int sourceFd = open(source, O_RDONLY);
  int destinationFd = open(destination, O_RDONLY);
  if (sourceFd < 0 || destinationFd < 0) {
    if (sourceFd >= 0)
      close(sourceFd);
    if (destinationFd >= 0)
      close(destinationFd);
    return -1;
  }
  char *buffers = malloc(2 * COPY_BUFFER_SIZE);
  int result = buffers == NULL ? -1 : 0;
  while (result == 0 && !copyCancelled(progress)) {
    int count = read(sourceFd, buffers, COPY_BUFFER_SIZE);
    if (count < 0) {
      result = -1;
      break;
    }
    if (count == 0)
      break;
    int received = 0;
    while (received < count) {
      int chunk = read(destinationFd, buffers + COPY_BUFFER_SIZE + received,
                       count - received);
      if (chunk <= 0) {
        result = -1;
        break;
      }
      received += chunk;
    }
    if (result == 0 && memcmp(buffers, buffers + COPY_BUFFER_SIZE, count))
      result = -1;
  }
  if (progress->cancelled)
    result = -1;
  free(buffers);
  close(sourceFd);
  close(destinationFd);
  return result;
}

// Removes temporary trees, replaced-item backups, or a verified move source.
static int removeCopyTree(const char *path, int depth) {
  struct stat info;
  if (depth > COPY_MAX_DEPTH || stat(path, &info) != 0)
    return -1;
  if (!S_ISDIR(info.st_mode))
    return unlink(path);
  DIR *directory = opendir(path);
  if (directory == NULL)
    return -1;
  struct dirent *item;
  int result = 0;
  while ((item = readdir(directory)) != NULL) {
    char *child;
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
      continue;
    child = malloc(PATH_MAX + 1);
    if (child == NULL || !joinPath(child, PATH_MAX + 1, path, item->d_name) ||
        removeCopyTree(child, depth + 1) != 0)
      result = -1;
    free(child);
  }
  closedir(directory);
  if (result == 0 && rmdir(path) != 0)
    result = -1;
  return result;
}

static int findCopyStaging(const char *folder, const char *destination,
                           char *staging, size_t capacity) {
  struct stat info;
  for (int index = 0; index < 100; index++) {
    char name[16];
    snprintf(name, sizeof(name), "LUNA%02d.TMP", index);
    if (!joinPath(staging, capacity, folder, name))
      return 0;
    if (strcmp(staging, destination) && stat(staging, &info) != 0)
      return 1;
  }
  return 0;
}

static int findCopyBackup(const char *folder, const char *destination,
                          char *backup, size_t capacity) {
  struct stat info;
  for (int index = 0; index < 100; index++) {
    char name[16];
    snprintf(name, sizeof(name), "LUNA%02d.BAK", index);
    if (!joinPath(backup, capacity, folder, name))
      return 0;
    if (strcmp(backup, destination) && stat(backup, &info) != 0)
      return 1;
  }
  return 0;
}

static int findKeepBothPath(const char *folder, const char *name,
                            int isDirectory, char *path, size_t capacity) {
  const char *extension = isDirectory ? NULL : strrchr(name, '.');
  if (extension == name)
    extension = NULL;
  int stemLength = extension ? (int)(extension - name) : (int)strlen(name);
  struct stat info;
  for (int index = 2; index <= 99; index++) {
    char candidate[PATH_MAX + 1];
    int written = snprintf(candidate, sizeof(candidate), "%.*s (%d)%s",
                           stemLength, name, index,
                           extension == NULL ? "" : extension);
    if (written < 0 || (size_t)written >= sizeof(candidate) ||
        !joinPath(path, capacity, folder, candidate))
      return 0;
    if (stat(path, &info) != 0)
      return 1;
  }
  return 0;
}

static int conflictRow(int index, char *name, size_t nameSize,
                        char *detail, size_t detailSize,
                        CardArtType *cardArt, void *context) {
  (void)context;
  snprintf(name, nameSize, "%s", index == 0 ? "Skip" :
           index == 1 ? "Keep Both" : "Replace");
  detail[0] = '\0';
  *cardArt = CARD_ART_NONE;
  (void)detailSize;
  return 1;
}

static int replaceConfirmRow(int index, char *name, size_t nameSize,
                              char *detail, size_t detailSize,
                              CardArtType *cardArt, void *context) {
  (void)index;
  (void)context;
  (void)detailSize;
  snprintf(name, nameSize, "Replace existing item");
  detail[0] = '\0';
  *cardArt = CARD_ART_NONE;
  return 1;
}

static int chooseConflict(const char *name, int samePath,
                           int existingDirectory) {
  int selected = 0;
  int count = samePath ? 2 : 3;
  while (1) {
    drawBrowserFrame("Name conflict", name,
                     "The destination already contains this name.",
                     "X Choose                     Circle Cancel",
                     count, selected, 0, 0, conflictRow, NULL);
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE))
      return -1;
    if (input & PAD_UP)
      selected = (selected + count - 1) % count;
    else if (input & PAD_DOWN)
      selected = (selected + 1) % count;
    else if (input & PAD_CROSS) {
      if (selected != 2)
        return selected;
      drawBrowserFrame("Confirm Replace", name,
                       existingDirectory ?
                       "The existing folder and its contents will be removed." :
                       "The existing file will be removed.",
                       "X Replace                    Circle Cancel",
                       1, 0, 0, 0, replaceConfirmRow, NULL);
      return (waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS) ?
             2 : -1;
    }
  }
}

// Returns 1 if an old destination backup remains, -2 if rollback failed.
static int commitStagedCopy(const char *folder, const char *staging,
                            const char *destination, int replace) {
  struct stat info;
  if (replace && stat(destination, &info) == 0) {
    char backup[PATH_MAX + 1];
    if (!findCopyBackup(folder, destination, backup, sizeof(backup)) ||
        rename(destination, backup) != 0)
      return -1;
    if (rename(staging, destination) != 0)
      return rename(backup, destination) == 0 ? -1 : -2;
    return removeCopyTree(backup, 0) == 0 ? 0 : 1;
  }
  if (stat(destination, &info) == 0 || rename(staging, destination) != 0)
    return -1;
  return 0;
}

static void runCopyQueue(const BrowserRoot *roots, int rootCount,
                         BrowserPane *panes, CopyQueue *queue,
                         int active, int expandedPane, int move,
                         char *status, size_t statusSize) {
  if (queue->count == 0) {
    snprintf(status, statusSize, "Mark items with Square first.");
    return;
  }
  BrowserPane *destinationPane = &panes[1 - queue->sourceSide];
  int sourceSide = queue->sourceSide;
  if (destinationPane->root < 0) {
    snprintf(status, statusSize, "Open a destination folder in the other pane.");
    return;
  }
  for (int index = 0; index < queue->count; index++) {
    const CopyItem *item = &queue->items[index];
    if (item->isDirectory && pathWithin(destinationPane->path, item->path)) {
      snprintf(status, statusSize, "Destination is inside a marked folder.");
      return;
    }
  }
  snprintf(status, statusSize, "%s %d marked items to %s?",
           move ? "Move" : "Copy", queue->count, destinationPane->path);
  drawFileManagerFrame(roots, rootCount, panes, queue, active, expandedPane,
                       status,
                       move ? "X Confirm move   Circle Cancel" :
                              "X Confirm batch copy   Circle Cancel", -1);
  if (!(waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS)) {
    snprintf(status, statusSize, "Queue kept. %s cancelled.",
             move ? "Move" : "Copy");
    return;
  }

  CopyProgress progress = {0};
  progress.roots = roots;
  progress.rootCount = rootCount;
  progress.panes = panes;
  progress.queue = queue;
  progress.active = active;
  progress.expandedPane = expandedPane;
  progress.itemCount = queue->count;
  progress.lastDrawMs = uiNowMs();
  drawFileManagerFrame(roots, rootCount, panes, queue, active, expandedPane,
                       "Measuring selected files...", "Circle Cancel measurement",
                       -1);
  for (int index = 0; index < queue->count; index++) {
    uint64_t before = progress.scannedBytes;
    if (measureTree(queue->items[index].path, 0, &progress) != 0) {
      snprintf(status, statusSize, progress.cancelled ?
               "Measurement cancelled. Queue kept." :
               "Cannot read a queued item. Queue kept.");
      return;
    }
    queue->items[index].measuredBytes = progress.scannedBytes - before;
  }
  progress.totalBytes = progress.scannedBytes;
  progress.startMs = uiNowMs();
  progress.lastDrawMs = 0;

  int completed = 0, skipped = 0, warnings = 0, index = 0, stopped = 0;
  while (index < queue->count) {
    const CopyItem *item = &queue->items[index];
    const char *basename = strrchr(item->path, '/');
    const char *name = basename == NULL ? item->path : basename + 1;
    char destination[PATH_MAX + 1], staging[PATH_MAX + 1];
    struct stat info;
    if (!joinPath(destination, sizeof(destination), destinationPane->path,
                  name)) {
      snprintf(status, statusSize, "%d completed; destination path too long.",
               completed);
      stopped = 1;
      break;
    }
    if (item->isDirectory &&
        pathWithin(destinationPane->path, item->path)) {
      snprintf(status, statusSize, "%d completed; choose another destination.",
               completed);
      stopped = 1;
      break;
    }
    int samePath = !strcmp(item->path, destination);
    int replace = 0;
    if (stat(destination, &info) == 0) {
      int choice = chooseConflict(name, samePath, S_ISDIR(info.st_mode));
      if (choice < 0) {
        snprintf(status, statusSize, "%d completed; queue kept.", completed);
        stopped = 1;
        break;
      }
      if (choice == 0) {
        progress.totalBytes -= item->measuredBytes;
        skipped++;
        index++;
        continue;
      }
      if (choice == 1 &&
          !findKeepBothPath(destinationPane->path, name, item->isDirectory,
                            destination, sizeof(destination))) {
        snprintf(status, statusSize, "No available name for Keep Both.");
        stopped = 1;
        break;
      }
      replace = choice == 2;
    } else if (samePath) {
      snprintf(status, statusSize, "Cannot use the source as its destination.");
      stopped = 1;
      break;
    }
    if (!findCopyStaging(destinationPane->path, destination, staging,
                         sizeof(staging))) {
      snprintf(status, statusSize, "No temporary name on destination drive.");
      stopped = 1;
      break;
    }
    progress.itemIndex = completed + skipped + 1;
    showCopyProgress(&progress, name, 1);
    int result = copyTree(item->path, staging, 0, &progress);
    int verificationFailed = 0;
    if (result == 0) {
      drawFileManagerFrame(roots, rootCount, panes, queue, active,
                           expandedPane, "Verifying copied data...",
                           "Circle Cancel verification", -1);
      result = verifyTree(item->path, staging, 0, &progress);
      verificationFailed = result != 0;
    }
    if (result == 0)
      result = commitStagedCopy(destinationPane->path, staging,
                                destination, replace);
    if (result < 0) {
      int cleanup = stat(staging, &info) == 0 ? removeCopyTree(staging, 0) : 0;
      if (result == -2)
        snprintf(status, statusSize,
                 "Replace rollback failed; check LUNAxx.BAK and LUNAxx.TMP.");
      else if (cleanup != 0)
        snprintf(status, statusSize, "Check LUNAxx.TMP; queue kept.");
      else
        snprintf(status, statusSize, "%d completed; %s. Queue kept.",
                 completed, progress.cancelled ? "cancelled" :
                 verificationFailed ? "verification failed" : "copy failed");
      stopped = 1;
      break;
    }
    if (result == 1)
      warnings++;
    if (move && removeCopyTree(item->path, 0) != 0)
      warnings++;
    completed++;
    queueRemove(queue, index);
  }
  if (!stopped && warnings > 0)
    snprintf(status, statusSize,
             "%d completed; inspect source or LUNAxx.BAK for %d warnings.",
             completed, warnings);
  else if (!stopped && skipped > 0)
    snprintf(status, statusSize, "%s %d; skipped %d (still marked).",
             move ? "Moved" : "Copied", completed, skipped);
  else if (!stopped)
    snprintf(status, statusSize, "%s %d items (%llu MiB, %d files).",
             move ? "Moved" : "Copied", completed,
             (unsigned long long)(progress.bytes / 1048576), progress.files);
  if (completed > 0) {
    loadDirectory(destinationPane->path, destinationPane->entries,
                  &destinationPane->count, &destinationPane->truncated);
    if (move)
      selectPaneEntry(&panes[sourceSide], NULL);
  }
}

static void uiFileManagerLoop(void) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserPane *panes = calloc(2, sizeof(*panes));
  if (roots == NULL || panes == NULL) {
    free(roots);
    free(panes);
    return;
  }
  for (int side = 0; side < 2; side++) {
    panes[side].entries = calloc(BROWSER_MAX_ENTRIES, sizeof(BrowserEntry));
    panes[side].root = -1;
  }
  if (panes[0].entries == NULL || panes[1].entries == NULL)
    goto done;
  int rootCount = collectRoots(roots);
  CopyQueue queue = {0};
  queue.sourceSide = -1;
  int active = 0;
  int expandedPane = -1;
  char status[160] = "Open a source and destination drive to copy files.";
  while (1) {
    BrowserPane *pane = &panes[active];
    drawFileManagerFrame(roots, rootCount, panes, &queue, active,
                         expandedPane, status,
                         NULL, -1);
    int input = readInput();
    if (input & PAD_CIRCLE)
      break;
    if (input & (PAD_L1 | PAD_R1 | PAD_LEFT | PAD_RIGHT)) {
      int side = (input & (PAD_L1 | PAD_LEFT)) ? 0 : 1;
      expandedPane = expandedPane == side ? -1 : side;
      active = side;
      continue;
    }
    if (input & PAD_R2) {
      int action = chooseFileAction(pane, &queue);
      if (action == FILE_ACTION_DETAILS)
        showFileDetails(roots, rootCount, pane);
      else if (action == FILE_ACTION_RENAME)
        renamePaneEntry(pane, roots, &queue, status, sizeof(status));
      else if (action == FILE_ACTION_NEW_FOLDER)
        createPaneFolder(pane, roots, status, sizeof(status));
      else if (action == FILE_ACTION_MOVE)
        runCopyQueue(roots, rootCount, panes, &queue, active,
                     expandedPane, 1, status, sizeof(status));
      continue;
    }
    if (input & PAD_TRIANGLE) {
      if (pane->root >= 0) {
        if (!strcmp(pane->path, roots[pane->root].path)) {
          pane->root = -1;
        } else {
          parentPath(pane->path, roots[pane->root].path);
          if (!loadDirectory(pane->path, pane->entries, &pane->count,
                             &pane->truncated))
            pane->root = -1;
        }
        pane->selected = 0;
      }
    } else if (input & (PAD_UP | PAD_DOWN)) {
      int total = pane->root < 0 ? rootCount : pane->count;
      int *focus = pane->root < 0 ? &pane->rootSelected : &pane->selected;
      if (total > 0)
        *focus = (*focus + (input & PAD_UP ? total - 1 : 1)) % total;
    } else if (input & PAD_CROSS) {
      if (pane->root < 0 && rootCount > 0) {
        int root = pane->rootSelected;
        if (loadDirectory(roots[root].path, pane->entries, &pane->count,
                          &pane->truncated)) {
          pane->root = root;
          snprintf(pane->path, sizeof(pane->path), "%s", roots[root].path);
          pane->selected = 0;
          status[0] = '\0';
        } else {
          snprintf(status, sizeof(status), "Could not open this device.");
        }
      } else if (pane->root >= 0 && pane->count > 0 &&
                 pane->entries[pane->selected].isDirectory) {
        char child[PATH_MAX + 1];
        if (!joinPath(child, sizeof(child), pane->path,
                      pane->entries[pane->selected].name)) {
          snprintf(status, sizeof(status), "Folder path is too long.");
        } else if (!loadDirectory(child, pane->entries, &pane->count,
                                  &pane->truncated)) {
          snprintf(status, sizeof(status), "Could not open this folder.");
        } else {
          snprintf(pane->path, sizeof(pane->path), "%s", child);
          pane->selected = 0;
          status[0] = '\0';
        }
      }
    } else if (input & PAD_SQUARE) {
      char path[PATH_MAX + 1];
      if (pane->root < 0 || pane->count == 0) {
        snprintf(status, sizeof(status), "Open a folder and select an item to mark.");
        continue;
      }
      if (queue.count > 0 && queue.sourceSide != active) {
        snprintf(status, sizeof(status), "Mark on the source side or clear the queue with Select.");
        continue;
      }
      if (!joinPath(path, sizeof(path), pane->path,
                    pane->entries[pane->selected].name)) {
        snprintf(status, sizeof(status), "Item path is too long.");
        continue;
      }
      int marked = queueFind(&queue, path);
      if (marked >= 0) {
        queueRemove(&queue, marked);
        snprintf(status, sizeof(status), "%d items queued.", queue.count);
        continue;
      }
      if (queue.count >= COPY_MAX_QUEUE) {
        snprintf(status, sizeof(status), "Queue is full (128 items).");
        continue;
      }
      int overlaps = 0;
      for (int index = 0; index < queue.count; index++)
        if (pathWithin(path, queue.items[index].path) ||
            pathWithin(queue.items[index].path, path))
          overlaps = 1;
      if (overlaps) {
        snprintf(status, sizeof(status), "A parent or child is already marked.");
        continue;
      }
      queue.items[queue.count].path = strdup(path);
      if (queue.items[queue.count].path == NULL) {
        snprintf(status, sizeof(status), "Not enough memory to mark this item.");
        continue;
      }
      queue.items[queue.count].isDirectory =
          pane->entries[pane->selected].isDirectory;
      queue.items[queue.count].measuredBytes = 0;
      queue.count++;
      queue.sourceSide = active;
      snprintf(status, sizeof(status), "%d items queued. Open the destination, then Start.",
               queue.count);
    } else if (input & PAD_SELECT) {
      if (queue.count == 0)
        continue;
      snprintf(status, sizeof(status), "Clear all %d marked items?", queue.count);
      drawFileManagerFrame(roots, rootCount, panes, &queue, active,
                           expandedPane, status,
                           "X Clear queue   Circle Keep queue", -1);
      if (waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS) {
        queueClear(&queue);
        snprintf(status, sizeof(status), "Queue cleared.");
      } else {
        snprintf(status, sizeof(status), "Queue kept.");
      }
    } else if (input & PAD_START) {
      runCopyQueue(roots, rootCount, panes, &queue, active, expandedPane,
                   0, status, sizeof(status));
    }
  }
  queueClear(&queue);
done:
  for (int side = 0; side < 2; side++) {
    if (panes[side].entries != NULL) {
      clearEntries(panes[side].entries, panes[side].count);
      free(panes[side].entries);
    }
  }
  free(panes);
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
                  char *detail, size_t detailSize, CardArtType *cardArt,
                  void *context) {
  if (index == 0) {
    snprintf(name, nameSize, "Create new card");
    snprintf(detail, detailSize, "8 MB");
    *cardArt = CARD_ART_VIRTUAL;
    return 1;
  }
  return fileRow(index - 1, name, nameSize, detail, detailSize, cardArt,
                 context);
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
                   "Please wait until creation finishes", 0, 0, 0, 0,
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
                       rootCount, rootSelected, browserFirstRow(rootSelected), 0,
                       rootRow, roots);
    else
      drawBrowserFrame("Virtual Memory Cards", directory, status,
                       "X Create/View                  Triangle Back",
                       count + 1, selected, browserFirstRow(selected), 128,
                       vmcRow, entries);
    int input = readInput();
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
                       char *detail, size_t detailSize, CardArtType *cardArt,
                       void *context) {
  int hasLibrary = *(int *)context;
  const char *label = index == 0 ? "File Manager" :
                      index == 1 ? "Virtual Memory Cards" :
                      (hasLibrary && index == 2 ? "Return to Library" :
                       index == (hasLibrary ? 4 : 3) ? "Shutdown" : "Exit LUNA");
  snprintf(name, nameSize, "%s", label);
  detail[0] = '\0';
  *cardArt = index == 1 ? CARD_ART_MEMORY_CARD_MENU : CARD_ART_NONE;
  (void)detailSize;
  return 1;
}

int uiMainMenuLoop(int hasLibrary) {
  int selected = 0;
  int count = hasLibrary ? 5 : 4;
  while (1) {
    drawBrowserFrame("LUNA", hasLibrary ? "Main menu" : "No games found",
                     hasLibrary ? "Browse storage or return to your games."
                                : "Browse storage even without a game library.",
                     "X Select                       Triangle Back",
                     count, selected, 0, selected == 1 ? 220 : 0,
                     mainMenuRow, &hasLibrary);
    int input = readInput();
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
      else if (selected == count - 1) {
        drawBrowserFrame("LUNA", "Confirm shutdown",
                         "Power off the console?",
                         "X Shutdown                     Circle Cancel",
                         count, selected, 0, 0, mainMenuRow, &hasLibrary);
        if (waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS) {
          powerOffConsole();
          while (1)
            sleep(1);
        }
      } else
        return 1;
    }
  }
}
