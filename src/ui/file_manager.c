// Storage browser, VMC manager, and the library's Start menu.
#include "ui/file_manager.h"
#include "ui/ring_menu.h"
#include "ui/cube_menu.h"
#include "ui/appearance_menu.h"
#include "ui/appearance.h"
#include "ui/file_browser.h"
#include "ui/file_operations.h"
#include "ui/text_file.h"
#include "homebrew_launch.h"
#include "vmc_create.h"
#include "vmc_storage.h"
#include "ui/game_options.h"
#include "ui/space_flight.h"
#include <strings.h>
#include "devices/devices.h"
#include "devices/init.h"
#include "storage.h"
#include "ui/graphics.h"
#include "ui/language.h"
#include "ui/pad.h"
#include "ui/view_internal.h"
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <hdd-ioctl.h>
#include <libpad.h>
#include <libmc.h>
#include <stdint.h>
#include <math.h>
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
#define FILE_MANAGER_NAME_MAX 255
#define BROWSER_SELECTOR_GLIDE_MS 110

#define PROMPT_TEXT(text) ((PromptBar){text, NULL, 0})
#define PROMPT_ONE(icon, label) \
  ((PromptBar){NULL, (ButtonPrompt[]){{icon, label}}, 1})
#define PROMPT_TWO(icon1, label1, icon2, label2) \
  ((PromptBar){NULL, (ButtonPrompt[]){{icon1, label1}, {icon2, label2}}, 2})
#define PROMPT_NOTE(text, icon, label) \
  ((PromptBar){text, (ButtonPrompt[]){{icon, label}}, 1})

typedef struct {
  char path[PATH_MAX + 1];
  char label[64];
} BrowserRoot;

typedef struct {
  char *path;
  int isDirectory;
  uint64_t measuredBytes;
} CopyItem;

typedef struct {
  CopyItem items[COPY_MAX_QUEUE];
  int count;
} CopyQueue;

typedef struct {
  int paneTop, listTop, listBottom, paneBottom, summaryTop;
  int rowStep, visible;
} FileManagerLayout;

static FileManagerLayout fileManagerLayout(void) {
  int line = getFontLineHeight();
  FileManagerLayout layout = {0};
  layout.paneTop = 48;
  layout.listTop = layout.paneTop + 2 * line + 12;
  layout.summaryTop = gsGlobal->Height - footerHeight - line - 10;
  layout.paneBottom = layout.summaryTop - 6;
  layout.listBottom = layout.paneBottom - 4;
  layout.rowStep = line + 6;
  layout.visible = 1 + (layout.listBottom - layout.listTop - line) / layout.rowStep;
  if (layout.visible < 1) layout.visible = 1;
  return layout;
}

// Modal surfaces reuse the live browser without flipping the frame underneath.
// The scene exists only while the File Explorer owns these objects.
static struct {
  const BrowserRoot *roots;
  int rootCount;
  const BrowserPane *browser;
  const CopyQueue *queue;
  const int *trayCollapsed;
  const int *move;
} fileManagerScene;
static MenuPromptTransition fileFooterTransitions[6];

static void drawFileManagerContents(const BrowserRoot *roots, int rootCount,
    const BrowserPane *browser, const CopyQueue *queue, int trayCollapsed,
    const char *status, PromptBar footer, int progressPercent);
static void fileManagerFitText(const char *text, int width,
    char *out, size_t capacity, int tail);

typedef struct {
  int selected;
  int wordOnly;
  int first;
  int fromY;
  int toY;
  uint32_t startMs;
} BrowserSelector;

static int browserSelectorProgress(const BrowserSelector *menu, uint32_t now) {
  uint32_t elapsed = now - menu->startMs;
  return elapsed >= BROWSER_SELECTOR_GLIDE_MS ? 1000 :
      lunaNavEase((int)(elapsed * 1000U / BROWSER_SELECTOR_GLIDE_MS));
}

static int browserSelectorY(BrowserSelector *menu, int selected, int first, int y) {
  uint32_t now = uiNowMs();
  int progress = browserSelectorProgress(menu, now);
  if (menu->selected < 0 || menu->first != first) {
    menu->fromY = menu->toY = y * 256;
    menu->selected = selected;
    menu->first = first;
  } else if (menu->selected != selected) {
    menu->fromY += (menu->toY - menu->fromY) * progress / 1000;
    menu->toY = y * 256;
    menu->startMs = now;
    menu->selected = selected;
    progress = 0;
  }
  return (menu->fromY + (menu->toY - menu->fromY) * progress / 1000) / 256;
}

static void (*browserBackdrop)(void *);
static void *browserBackdropContext;
void uiFileManagerSetBackdrop(void (*draw)(void *), void *context) {
  browserBackdrop=draw; browserBackdropContext=context;
}

static void drawBrowserSheetPanel(int panelLeft, int panelTop,
                                   int panelRight, int panelBottom) {
  int width = gsGlobal->Width;
  int height = gsGlobal->Height;
  if (browserBackdrop) {
    browserBackdrop(browserBackdropContext);
    gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
    gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
    gsKit_prim_sprite(gsGlobal,0,0,width,height,0,GS_SETREG_RGBA(0,0,0,64));
  } else {
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                      GS_SETREG_RGBA(0x04, 0x0A, 0x18, 0x80));
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    drawSharedLibraryBackground(uiNowMs());
  }
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    glassPresetColor(0x02, 0x07, 0x16, 0x38));
  gsKit_prim_sprite(gsGlobal, panelLeft + 2, panelTop + 2,
                    panelRight - 2, panelBottom - 2, 1,
                    glassPresetColor(0x02, 0x08, 0x18, 0x48));
  drawGlassPanel(panelLeft, panelTop, panelRight, panelBottom, 2);
}

static void drawBrowserSheet(int fileManager) {
  int margin = fileManager ? FILE_MANAGER_GLASS_MARGIN : 30;
  drawBrowserSheetPanel(margin, fileManager ? FILE_MANAGER_GLASS_MARGIN : 78,
                       gsGlobal->Width - margin,
                       fileManager ? gsGlobal->Height - FILE_MANAGER_GLASS_MARGIN
                                   : gsGlobal->Height - footerHeight + 3);
}

static void presentBrowserFrame(void) {
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  gsKit_sync_flip(gsGlobal);
  usleep(1000);
}

static void drawFileManagerModal(int left, int top, int right, int bottom,
                                 const char *title) {
  if (fileManagerScene.browser)
    drawFileManagerContents(fileManagerScene.roots, fileManagerScene.rootCount,
        fileManagerScene.browser, fileManagerScene.queue,
        *fileManagerScene.trayCollapsed, "", (PromptBar){0}, -1);
  else {
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawBrowserSheet(1);
  }
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 10,
                    GS_SETREG_RGBA(0x00, 0x03, 0x0A, 0x48));
  gsKit_prim_sprite(gsGlobal, left + 5, top + 6, right + 5, bottom + 6, 11,
                    GS_SETREG_RGBA(0, 0, 0, 0x50));
  gsKit_prim_sprite(gsGlobal, left, top, right, bottom, 12,
                    glassPresetColor(0x10, 0x20, 0x34, 0x7C));
  uint64_t edge = glassPresetColor(0x89, 0xAE, 0xC6, 0x40);
  gsKit_prim_sprite(gsGlobal, left, top, right, top + 1, 13, edge);
  gsKit_prim_sprite(gsGlobal, left, top, left + 1, bottom, 13, edge);
  gsKit_prim_sprite(gsGlobal, right - 1, top, right, bottom, 13, edge);
  gsKit_prim_sprite(gsGlobal, left, bottom - 1, right, bottom, 13, edge);
  int line = getFontLineHeight();
  char fitted[PATH_MAX + 64];
  fileManagerFitText(title, right - left - 28, fitted, sizeof(fitted), 0);
  drawTextWindow(left + 14, top + 10, right - 14, top + 10 + line, 14,
                 HeaderTextColor, ALIGN_LEFT, fitted);
  gsKit_prim_sprite(gsGlobal, left + 12, top + line + 19,
                    right - 12, top + line + 20, 13, edge);
}

static void drawFileManagerModalHelp(int left, int right, int bottom,
                                     PromptBar help) {
  int line = getFontLineHeight();
  drawMenuPromptBar(&fileFooterTransitions[2],left + 14, bottom - line - 12, right - 14, bottom - 8, 14,
                    HeaderTextColor, help,1);
}

static void drawFileManagerWrapped(const char *text, int left, int top,
                                   int right, int rows, uint64_t color) {
  int line = getFontLineHeight();
  for (int row = 0; row < rows && *text; row++) {
    char fitted[PATH_MAX + 160];
    if (row == rows - 1) {
      fileManagerFitText(text, right - left, fitted, sizeof(fitted), 0);
    } else {
      size_t length = strlen(text), end = 0, word = 0;
      while (end < length && end + 4 < sizeof(fitted)) {
        size_t next = end + 1;
        while (next < length && ((unsigned char)text[next] & 0xC0) == 0x80) next++;
        memcpy(fitted, text, next); fitted[next] = '\0';
        if ((int)getLineWidth(fitted) > right - left) break;
        if (text[end] == ' ') word = end;
        end = next;
      }
      if (end < length && word) end = word;
      if (!end) return;
      memcpy(fitted, text, end); fitted[end] = '\0';
      text += end;
      while (*text == ' ') text++;
    }
    drawTextWindow(left, top + row * line, right, top + (row + 1) * line,
                   14, color, ALIGN_LEFT, fitted);
  }
}

static void drawBrowserRow(int y, int selected, const char *name,
                           const char *detail, int right, int selectorY,
                           int selectorRight,int detailWidth) {
  int left = keepoutArea + 30;
  if (selected) {
    if (selectorY >= 0) {
      if (selectorRight <= 0) {
        selectorRight = left + 36 + (int)getLineWidth(name);
        if (selectorRight > right - detailWidth)
          selectorRight = right - detailWidth;
      }
      drawMenuRowSelector(left, selectorY, selectorRight);
    } else {
      drawPSBBNFocusGlow(left, y, right, right - 12);
    }
    if (detail != NULL)
      drawPSBBNFocusGlow(right - detailWidth, selectorY >= 0 ? selectorY : y,
                          right, right - 12);
  }
  uiCubeMenuTextDraw(left + 18, y, right - detailWidth - 10,
                 0, selected ? (selectorY >= 0 ? GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80)
                                                : FontMainColor) : HeaderTextColor,
                 name,0);
  if (detail != NULL)
    uiCubeMenuTextDraw(right - detailWidth, y, right - 12,
                   0, selected ? ColorSelectedText : FontMainColor,
                   detail,1);
}

static void drawBrowserFrameSelected(const char *heading, const char *path,
                             const char *status, PromptBar footer,
                             int count, int selected, int first,
                             int (*row)(int, char *, size_t, char *, size_t,
                                        void *), void *context,
                             BrowserSelector *selector) {
  int left = keepoutArea + 30;
  int right = gsGlobal->Width - left;
  int lineHeight = getFontLineHeight();
  int rowStep = lineHeight + lineHeight / 3;
  int listTop = 112;
  int listBottom = gsGlobal->Height - footerHeight - lineHeight;
  int visible = (listBottom - listTop) / rowStep;
  if (visible < 1) visible = 1;
  int rowRight = right - (count > visible ? 14 : 0);
  int detailWidth=0;
  for(int index=first;index<count&&index<first+visible;index++) {
    char name[PATH_MAX+3],detail[32];
    if(row(index,name,sizeof(name),detail,sizeof(detail),context)&&detail[0]) {
      int width=(int)ceilf(getLineDrawWidth(detail))+14;
      if(width>detailWidth)detailWidth=width;
    }
  }
  if(detailWidth>(rowRight-left-36)/2)detailWidth=(rowRight-left-36)/2;
  int selectorY = selector != NULL
      ? browserSelectorY(selector, selected, first,
                           listTop + (selected - first) * rowStep) : -1;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(0);
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
                   index == selected, name, detail[0] ? detail : NULL,
                   rowRight, selectorY,
                   selector != NULL && !selector->wordOnly ? rowRight : 0,detailWidth);
  }
  drawListScrollbar(right - 6, listTop, listTop + visible * rowStep,
                     count, first, visible, 6);
  if (count == 0)
    drawTextWindow(left + 18, listTop, right, listTop + lineHeight,
                   0, HeaderTextColor, ALIGN_LEFT, lunaText("Nothing to show"));
  drawTextWindow(left + 18, listBottom, right, gsGlobal->Height - footerHeight,
                 0, HeaderTextColor, ALIGN_LEFT, status);
  drawMenuPromptBar(&fileFooterTransitions[0],left + 18, gsGlobal->Height - footerHeight, right,
                    gsGlobal->Height, 0, HeaderTextColor, footer,1);
  presentBrowserFrame();
}

static void drawBrowserFrame(const char *heading, const char *path,
                             const char *status, PromptBar footer,
                             int count, int selected, int first,
                             int (*row)(int, char *, size_t, char *, size_t,
                                        void *), void *context) {
  drawBrowserFrameSelected(heading, path, status, footer, count, selected, first,
                           row, context, NULL);
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
    snprintf(detail, detailSize, lunaText("Folder"));
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
    if (!(STORAGE_SETTINGS.enabled & device->mode))
      continue;
    if (device->mode == MODE_HDL) {
      // hdd0: lists APA partitions, not ordinary files. The mounted PFS
      // metadata partition is the safe directory view for this backend.
      if (device->metadev != NULL && device->metadev->mountpoint != NULL)
        count = addRoot(roots, count, device->metadev->mountpoint,
                        lunaText("APA metadata"));
      continue;
    }
    switch (device->mode) {
    case MODE_ATA: kind = "ATA HDD"; break;
    case MODE_USB: kind = "USB"; break;
    case MODE_MX4SIO: kind = "MX4SIO"; break;
    case MODE_ILINK: kind = "iLink"; break;
    case MODE_MMCE: kind = "MMCE"; break;
    case MODE_UDPFS: kind = lunaText("Network"); break;
    default: kind = lunaText("Storage"); break;
    }
    snprintf(label, sizeof(label), "%s", kind);
    count = addRoot(roots, count, device->mountpoint, label);
  }
  count = addRoot(roots, count, "mc0:/", lunaText("Memory card 1"));
  count = addRoot(roots, count, "mc1:/", lunaText("Memory card 2"));
#ifdef LUNA_EMULATOR_BUILD
  count = addRoot(roots, count, "host:/", lunaText("Emulator host"));
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

// The PS1 VMC picker uses a bounded, synchronous listing.
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
}

static void queueClear(CopyQueue *queue) {
  while (queue->count > 0)
    queueRemove(queue, queue->count - 1);
}

static int pathWithin(const char *path, const char *folder) {
  size_t length = strlen(folder);
  while (length > 0 && folder[length - 1] == '/') length--;
  return !strncmp(path, folder, length) &&
         (path[length] == '\0' || path[length] == '/');
}

static void drawFileManagerSelection(int x1, int y, int x2,
                                     int lineHeight, int active);

static void drawFileManagerList(const char *title, const char *subtitle,
    const char *notice, int count, int selected,
    int (*row)(int, char *, size_t, char *, size_t, void *),
    void *context, PromptBar help) {
  int left = keepoutArea + 32, right = gsGlobal->Width - left;
  int top = 62, bottom = gsGlobal->Height - 28;
  int line = getFontLineHeight(), step = line + 8;
  drawFileManagerModal(left, top, right, bottom, title);
  char fitted[PATH_MAX + 160];
  fileManagerFitText(subtitle, right - left - 32, fitted, sizeof(fitted), 1);
  int y = top + line + 30;
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 FontMainColor, ALIGN_LEFT, fitted);
  y += line + 8;
  drawFileManagerWrapped(notice, left + 16, y, right - 16, 2, HeaderTextColor);
  y += 2 * line + 12;
  int visible = (bottom - line - 28 - y) / step;
  if (visible < 1) visible = 1;
  int first = selected / visible * visible;
  int rowRight = right - 10 - (count > visible ? 14 : 0);
  for (int i = first; i < count && i < first + visible; i++) {
    char name[PATH_MAX + 16], detail[PATH_MAX + 16];
    if (!row(i, name, sizeof(name), detail, sizeof(detail), context)) continue;
    int rowY = y + (i - first) * step;
    if (i == selected) drawFileManagerSelection(left + 10, rowY, rowRight, line, 1);
    fileManagerFitText(name, rowRight - left - 26, fitted, sizeof(fitted), 0);
    drawTextWindow(left + 18, rowY, rowRight - 8, rowY + line, 14,
                   i == selected ? ColorSelectedText : FontMainColor,
                   ALIGN_LEFT, fitted);
  }
  drawListScrollbar(right - 16, y, y + visible * step, count, first, visible, 14);
  drawFileManagerModalHelp(left, right, bottom, help);
  presentBrowserFrame();
}

static int validFileName(const char *name) {
  return name[0] != '\0' && strcmp(name, ".") && strcmp(name, "..") &&
         strpbrk(name, "/\\:") == NULL;
}

static int editBrowserText(const char *title, char *name, size_t capacity,
                           size_t maxLength, int allowEmpty) {
  static const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._- ";
  int selected = 0;
  int lowerCase = 0;
  char message[96] = "";
  if (maxLength >= capacity)
    maxLength = capacity - 1;
  while (1) {
    int left = keepoutArea + 24, right = gsGlobal->Width - left;
    int line = getFontLineHeight();
    int top = 54, bottom = gsGlobal->Height - 24;
    int length = strlen(name);
    char visible[FILE_MANAGER_NAME_MAX + 8];
    fileManagerFitText(name, right - left - 40, visible, sizeof(visible), 1);
    drawFileManagerModal(left, top, right, bottom, title);
    int fieldTop = top + line + 32;
    gsKit_prim_sprite(gsGlobal, left + 14, fieldTop, right - 14,
                      fieldTop + line + 12, 14,
                      glassPresetColor(0x06, 0x12, 0x22, 0x70));
    drawTextWindow(left + 22, fieldTop + 6, right - 22,
                   fieldTop + 6 + line, 15, FontMainColor, ALIGN_LEFT, visible);
    int gridTop = fieldTop + line + 26;
    int step = (bottom - gridTop - 3 * line - 24) / 4;
    int cell = (right - left - 28) / 10;
    for (int index = 0; index < 40; index++) {
      int x = left + 14 + (index % 10) * cell;
      int y = gridTop + (index / 10) * step;
      char label[3] = {keys[index], '\0', '\0'};
      if (label[0] == ' ') snprintf(label, sizeof(label), "SP");
      else if (lowerCase && label[0] >= 'A' && label[0] <= 'Z')
        label[0] += 'a' - 'A';
      if (index == selected)
        drawFileManagerSelection(x + 1, y, x + cell - 3, line, 1);
      drawTextWindow(x, y, x + cell, y + line, 15,
                     index == selected ? ColorSelectedText : FontMainColor,
                     ALIGN_CENTER, label);
    }
    char fitted[128];
    fileManagerFitText(message, right - left - 28, fitted, sizeof(fitted), 0);
    drawTextWindow(left + 14, bottom - 3 * line - 20, right - 14,
                   bottom - 2 * line - 20, 15, ErrorTextColor, ALIGN_LEFT, fitted);
    const ButtonPrompt edit[] = {
        {ICON_CROSS, lunaText("Type")}, {ICON_SQUARE, lunaText("Space")},
        {ICON_TRIANGLE, lunaText("Delete")}, {ICON_R1, lunaText("Case")}};
    drawMenuPromptBar(&fileFooterTransitions[3],left + 14, bottom - 2 * line - 16, right - 14,
                  bottom - line - 16, 15, HeaderTextColor,
                  (PromptBar){NULL, edit, 4},1);
    drawFileManagerModalHelp(left, right, bottom, PROMPT_TWO(
        ICON_START, lunaText("Save"), ICON_CIRCLE, lunaText("Cancel")));
    presentBrowserFrame();
    int input = waitForInput(-1);
    if (input & PAD_CIRCLE)
      return 0;
    if (input & PAD_START) {
      if (length <= maxLength && (validFileName(name) ||
          (!name[0] && allowEmpty)))
        return 1;
      snprintf(message, sizeof(message), lunaText("Enter a valid name (%u characters max)."),
               (unsigned)maxLength);
    } else if (input & PAD_TRIANGLE) {
      if (length > 0) {
        length--;
        while (length > 0 && ((unsigned char)name[length] & 0xC0) == 0x80) length--;
        name[length] = '\0';
      }
      message[0] = '\0';
    } else if (input & PAD_R1) {
      lowerCase = !lowerCase;
    } else if (input & (PAD_CROSS | PAD_SQUARE)) {
      if (length >= maxLength) {
        snprintf(message, sizeof(message), lunaText("Name is at its length limit."));
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

static int editFileName(const char *title, char *name, size_t capacity,
                        size_t maxLength) {
  return editBrowserText(title, name, capacity, maxLength,
      !strcmp(title, lunaText("UDPFS console IP")));
}

static void formatBytes(uint64_t bytes, char *text, size_t size) {
  if (bytes >= 1048576)
    snprintf(text, size, "%llu.%02llu MiB (%llu bytes)",
             (unsigned long long)(bytes / 1048576),
             (unsigned long long)((bytes % 1048576) * 100 / 1048576),
             (unsigned long long)bytes);
  else
    snprintf(text, size, lunaText("%llu bytes"), (unsigned long long)bytes);
}

static void fileManagerItemSummary(int count, int transfer, int move,
                                    char *text, size_t size) {
  const char *single = !transfer ? "1 item" :
      move ? "1 item ready to move" : "1 item ready to copy";
  const char *multiple = !transfer ? "%d items" :
      move ? "%d items ready to move" : "%d items ready to copy";
  if (count == 1) snprintf(text, size, "%s", lunaText(single));
  else snprintf(text, size, lunaText(multiple), count);
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
                            const BrowserPane *pane, int folder) {
  char path[PATH_MAX + 1];
  const char *kind = lunaText("Drive");
  int root = pane->root < 0 ? pane->rootSelected : pane->root;
  if (root < 0 || root >= rootCount)
    return;
  snprintf(path, sizeof(path), "%s", roots[root].path);
  if (!folder && pane->root >= 0 && pane->visibleCount > 0) {
    if (!joinPath(path, sizeof(path), pane->path,
                  browserPaneEntry(pane, pane->selected)->name))
      return;
    kind = browserPaneEntry(pane, pane->selected)->isDirectory ? lunaText("Folder") : lunaText("File");
  } else if (pane->root >= 0) {
    snprintf(path, sizeof(path), "%s", pane->path);
    kind = lunaText("Folder");
  }
  struct stat info;
  int hasSize = stat(path, &info) == 0 && !S_ISDIR(info.st_mode);
  uint64_t freeBytes = 0;
  int hasFree = getAvailableSpace(roots[root].path, &freeBytes);
  char sizeText[88], freeText[88];
  if (hasSize)
    formatBytes((uint64_t)info.st_size, sizeText, sizeof(sizeText));
  else
    snprintf(sizeText, sizeof(sizeText), lunaText("Not calculated"));
  if (hasFree)
    formatBytes(freeBytes, freeText, sizeof(freeText));
  else
    snprintf(freeText, sizeof(freeText), lunaText("Unavailable on this drive"));
  int scroll = 0;
  int line = getFontLineHeight();
  int left = keepoutArea + 42, right = gsGlobal->Width - left;
  int top = 56, bottom = gsGlobal->Height - 28;
  // Wrap on measured UTF-8 boundaries; paths remain readable at every font size.
  size_t offsets[PATH_MAX + 2];
  int lines = 0;
  size_t offset = 0, length = strlen(path);
  while (offset < length) {
    offsets[lines++] = offset;
    size_t end = offset;
    char segment[PATH_MAX + 1];
    do {
      size_t next = end + 1;
      while (next < length && ((unsigned char)path[next] & 0xC0) == 0x80) next++;
      memcpy(segment, path + offset, next - offset);
      segment[next - offset] = '\0';
      if (end > offset && (int)getLineWidth(segment) > right - left - 32) break;
      end = next;
    } while (end < length);
    offset = end;
  }
  offsets[lines] = length;
  int pathTop = top + 7 * line + 44;
  int visible = (bottom - line - 24 - pathTop) / line;
  if (visible < 1) visible = 1;
  while (1) {
    drawFileManagerModal(left, top, right, bottom, lunaText("File details"));
    int y = top + line + 28;
    char fitted[128];
    drawTextWindow(left + 16, y, right - 16, y + line, 14,
                   ColorSelectedText, ALIGN_LEFT, kind);
    y += line + 8;
    drawTextWindow(left + 16, y, right - 16, y + line, 14,
                   HeaderTextColor, ALIGN_LEFT, lunaText("SIZE"));
    fileManagerFitText(sizeText, right - left - 32, fitted, sizeof(fitted), 0);
    drawTextWindow(left + 16, y + line, right - 16, y + 2 * line, 14,
                   FontMainColor, ALIGN_LEFT, fitted);
    y += 2 * line + 4;
    drawTextWindow(left + 16, y, right - 16, y + line, 14,
                   HeaderTextColor, ALIGN_LEFT, lunaText("AVAILABLE SPACE"));
    fileManagerFitText(freeText, right - left - 32, fitted, sizeof(fitted), 0);
    drawTextWindow(left + 16, y + line, right - 16, y + 2 * line, 14,
                   FontMainColor, ALIGN_LEFT, fitted);
    y += 2 * line + 4;
    drawTextWindow(left + 16, y, right - 16, y + line, 14,
                   HeaderTextColor, ALIGN_LEFT, lunaText("FULL PATH"));
    for (int row = 0; row < visible && scroll + row < lines; row++) {
      char segment[PATH_MAX + 1];
      size_t start = offsets[scroll + row];
      size_t count = offsets[scroll + row + 1] - start;
      memcpy(segment, path + start, count); segment[count] = '\0';
      drawTextWindow(left + 16, pathTop + row * line, right - 16,
                     pathTop + (row + 1) * line, 14,
                     FontMainColor, ALIGN_LEFT, segment);
    }
    drawFileManagerModalHelp(left, right, bottom, PROMPT_TWO(
        ICON_DPAD, lunaText("Scroll path"), ICON_CIRCLE, lunaText("Back")));
    presentBrowserFrame();
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) return;
    if ((input & PAD_DOWN) && scroll + visible < lines) scroll++;
    if ((input & PAD_UP) && scroll > 0) scroll--;
  }
}

enum FileAction {
  FILE_ACTION_DETAILS,
  FILE_ACTION_RENAME,
  FILE_ACTION_NEW_FOLDER,
  FILE_ACTION_MOVE,
  FILE_ACTION_COPY,
  FILE_ACTION_DELETE,
  FILE_ACTION_DELETE_MARKED,
  FILE_ACTION_REFRESH,
  FILE_ACTION_FILTER,
  FILE_ACTION_CLEAR_FILTER,
  FILE_ACTION_SORT_NAME,
  FILE_ACTION_SORT_SIZE,
  FILE_ACTION_SORT_DATE,
  FILE_ACTION_MARK_ALL,
  FILE_ACTION_INVERT,
  FILE_ACTION_QUEUE,
  FILE_ACTION_VIEW_TEXT,
  FILE_ACTION_LAUNCH_ELF,
};

enum {
  FILE_ACTION_OPEN = FILE_ACTION_LAUNCH_ELF + 1,
  FILE_ACTION_COPY_ITEM, FILE_ACTION_MOVE_ITEM,
  FILE_ACTION_FOLDER_MENU, FILE_ACTION_SORT_MENU, FILE_ACTION_SELECTION_MENU,
  FILE_ACTION_CLEAR_QUEUE, FILE_ACTION_COPY_MARKED, FILE_ACTION_MOVE_MARKED,
  FILE_ACTION_FOLDER_DETAILS
};

typedef struct {
  int actions[12];
  int count;
} FileActionMenu;

static const char *fileActionLabel(int action) {
  static const char *const labels[] = {
      "Details", "Rename", "New folder", "Move here", "Paste here",
      "Delete", "Delete marked items", "Refresh folder", "Filter names",
      "Clear filter", "Name", "Size", "Date",
      "Mark all listed items", "Invert listed marks", "Review tray",
      "View as text", "Launch ELF", "Open", "Copy", "Move",
      "Folder options", "Sort by", "Selection", "Clear tray", "Copy", "Move", "Folder details"};
  return lunaText(labels[action]);
}

// Menus remain anchored to the selected row, including on the last visible row.
static int chooseFileAction(const BrowserPane *pane, const CopyQueue *queue,
                            int folderMenu) {
  int page = folderMenu ? FILE_ACTION_FOLDER_MENU : 0;
  int selected = 0;
  int saved[2] = {0, 0};
  const BrowserEntry *entry = pane->root >= 0 ?
      browserPaneEntry(pane, pane->selected) : NULL;
  while (1) {
    FileActionMenu menu = {0};
    const char *title = entry ? entry->name : pane->root < 0 &&
        pane->rootSelected < fileManagerScene.rootCount ?
        fileManagerScene.roots[pane->rootSelected].label : lunaText("Folder");
    if (!page) {
      if (pane->root < 0 && fileManagerScene.rootCount > 0)
        menu.actions[menu.count++] = FILE_ACTION_OPEN;
      if (entry && !pane->scanning) {
        menu.actions[menu.count++] = FILE_ACTION_OPEN;
        menu.actions[menu.count++] = FILE_ACTION_COPY_ITEM;
        menu.actions[menu.count++] = FILE_ACTION_MOVE_ITEM;
        menu.actions[menu.count++] = FILE_ACTION_RENAME;
        if (!entry->isDirectory && lunaFileKind(entry->name) != LUNA_FILE_TEXT)
          menu.actions[menu.count++] = FILE_ACTION_VIEW_TEXT;
      }
      menu.actions[menu.count++] = FILE_ACTION_DETAILS;
      if (pane->root >= 0) menu.actions[menu.count++] = FILE_ACTION_FOLDER_MENU;
      if (queue->count) menu.actions[menu.count++] = FILE_ACTION_SELECTION_MENU;
      if (entry && !pane->scanning) menu.actions[menu.count++] = FILE_ACTION_DELETE;
    } else if (page == FILE_ACTION_FOLDER_MENU) {
      title = lunaText("Folder options");
      if (queue->count && pane->root >= 0)
        menu.actions[menu.count++] = fileManagerScene.move && *fileManagerScene.move ?
            FILE_ACTION_MOVE : FILE_ACTION_COPY;
      if (pane->root >= 0 && !pane->scanning) {
        menu.actions[menu.count++] = FILE_ACTION_NEW_FOLDER;
        menu.actions[menu.count++] = FILE_ACTION_SORT_MENU;
        menu.actions[menu.count++] = FILE_ACTION_FILTER;
        if (pane->filter[0]) menu.actions[menu.count++] = FILE_ACTION_CLEAR_FILTER;
        menu.actions[menu.count++] = FILE_ACTION_REFRESH;
        if (pane->visibleCount || queue->count)
          menu.actions[menu.count++] = FILE_ACTION_SELECTION_MENU;
      }
      menu.actions[menu.count++] = FILE_ACTION_FOLDER_DETAILS;
    } else if (page == FILE_ACTION_SORT_MENU) {
      title = lunaText("Sort by");
      menu.actions[menu.count++] = FILE_ACTION_SORT_NAME;
      menu.actions[menu.count++] = FILE_ACTION_SORT_SIZE;
      menu.actions[menu.count++] = FILE_ACTION_SORT_DATE;
    } else {
      title = lunaText("Selection");
      if (pane->root >= 0 && !pane->scanning && pane->visibleCount) {
        menu.actions[menu.count++] = FILE_ACTION_MARK_ALL;
        menu.actions[menu.count++] = FILE_ACTION_INVERT;
      }
      if (queue->count) {
        menu.actions[menu.count++] = FILE_ACTION_COPY_MARKED;
        menu.actions[menu.count++] = FILE_ACTION_MOVE_MARKED;
        menu.actions[menu.count++] = FILE_ACTION_QUEUE;
        menu.actions[menu.count++] = FILE_ACTION_CLEAR_QUEUE;
        menu.actions[menu.count++] = FILE_ACTION_DELETE_MARKED;
      }
    }
    if (!menu.count) return -1;
    if (selected >= menu.count) selected = menu.count - 1;
    int left = keepoutArea + 10, right = gsGlobal->Width - left;
    int width = (right - left) * 3 / 5;
    int line = getFontLineHeight(), step = line + 8;
    int maxRows = (gsGlobal->Height - 48 - 2 * line - 48) / step;
    if (maxRows < 1) maxRows = 1;
    int rows = menu.count < maxRows ? menu.count : maxRows;
    int height = 2 * line + 48 + rows * step;
    FileManagerLayout layout = fileManagerLayout();
    int focus = pane->root < 0 ? pane->rootSelected : pane->selected;
    int top = layout.listTop + (focus % layout.visible) * layout.rowStep - 8;
    if (top + height > gsGlobal->Height - 24) top = gsGlobal->Height - 24 - height;
    if (top < 24) top = 24;
    int x = right - width - 12, bottom = top + height;
    drawFileManagerModal(x, top, x + width, bottom, title);
    int first = selected >= rows ? selected - rows + 1 : 0;
    int rowRight = x + width - 8 - (menu.count > rows ? 14 : 0);
    for (int i = first; i < menu.count && i < first + rows; i++) {
      int y = top + line + 30 + (i - first) * step;
      int action = menu.actions[i];
      if (i == selected) drawFileManagerSelection(x + 8, y, rowRight, line, 1);
      int submenu = action == FILE_ACTION_FOLDER_MENU ||
          action == FILE_ACTION_SORT_MENU || action == FILE_ACTION_SELECTION_MENU;
      char label[160];
      fileManagerFitText(fileActionLabel(action), rowRight - x - 36, label, sizeof(label), 0);
      uint64_t color = action == FILE_ACTION_DELETE || action == FILE_ACTION_DELETE_MARKED ?
          ErrorTextColor : i == selected ? ColorSelectedText : FontMainColor;
      drawTextWindow(x + 16, y, rowRight - 20, y + line, 14, color, ALIGN_LEFT, label);
      const char *detail = submenu ? ">" :
          page == FILE_ACTION_SORT_MENU && action - FILE_ACTION_SORT_NAME == (int)pane->sort ? "*" : "";
      drawTextWindow(rowRight - 20, y, rowRight - 6, y + line, 14,
                     HeaderTextColor, ALIGN_RIGHT, detail);
    }
    drawListScrollbar(x + width - 14, top + line + 30,
                       top + line + 30 + rows * step, menu.count, first, rows, 14);
    drawFileManagerModalHelp(x, x + width, bottom, PROMPT_TWO(
        ICON_CROSS, lunaText("Select"), ICON_CIRCLE, lunaText("Back")));
    presentBrowserFrame();
    int input = waitForInput(-1);
    if (input & PAD_TRIANGLE) return -1;
    if (input & (PAD_CIRCLE | PAD_LEFT)) {
      if (!page || (folderMenu && page == FILE_ACTION_FOLDER_MENU)) return -1;
      if (page == FILE_ACTION_SORT_MENU ||
          (page == FILE_ACTION_SELECTION_MENU && saved[1] >= 0)) {
        page = FILE_ACTION_FOLDER_MENU; selected = saved[1];
      } else { page = 0; selected = saved[0]; }
    } else if (input & PAD_UP) selected = (selected + menu.count - 1) % menu.count;
    else if (input & PAD_DOWN) selected = (selected + 1) % menu.count;
    else if (input & (PAD_CROSS | PAD_RIGHT)) {
      int action = menu.actions[selected];
      if (action == FILE_ACTION_FOLDER_MENU || action == FILE_ACTION_SORT_MENU ||
          action == FILE_ACTION_SELECTION_MENU) {
        if (!page) { saved[0] = selected; saved[1] = -1; }
        else if (page == FILE_ACTION_FOLDER_MENU) saved[1] = selected;
        page = action; selected = 0;
      } else if (input & PAD_CROSS) {
        return action;
      }
    }
  }
}

static void selectPaneEntry(BrowserPane *pane, const char *name) {
  if (!browserPaneRefresh(pane, name)) {
    browserPaneDestroy(pane);
    pane->root = -1;
    pane->selected = 0;
  }
}

static void renamePaneEntry(BrowserPane *pane, const BrowserRoot *roots,
                            CopyQueue *queue,
                            char *status, size_t statusSize) {
  if (pane->root < 0 || pane->visibleCount == 0)
    return;
  char source[PATH_MAX + 1], destination[PATH_MAX + 1];
  char name[PATH_MAX + 1];
  const char *oldName = browserPaneEntry(pane, pane->selected)->name;
  if (!joinPath(source, sizeof(source), pane->path, oldName)) {
    snprintf(status, statusSize, lunaText("The current path is too long."));
    return;
  }
  snprintf(name, sizeof(name), "%s", oldName);
  size_t limit = !strncmp(roots[pane->root].path, "mc", 2) ? 31 :
                 FILE_MANAGER_NAME_MAX;
  if (!editFileName(lunaText("Rename"), name, sizeof(name), limit))
    return;
  if (!strcmp(name, oldName))
    return;
  if (!joinPath(destination, sizeof(destination), pane->path, name)) {
    snprintf(status, statusSize, lunaText("The new path is too long."));
    return;
  }
  struct stat info;
  if (stat(destination, &info) == 0) {
    snprintf(status, statusSize, lunaText("That name already exists."));
    return;
  }
  if (rename(source, destination) != 0) {
    snprintf(status, statusSize, lunaText("Rename failed on this drive."));
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
    snprintf(status, statusSize, lunaText("Renamed to %s; %d affected marks cleared."),
             name, cleared);
  else
    snprintf(status, statusSize, lunaText("Renamed to %s"), name);
}

static void createPaneFolder(BrowserPane *pane, const BrowserRoot *roots,
                             char *status, size_t statusSize) {
  if (pane->root < 0)
    return;
  char name[PATH_MAX + 1] = "";
  char path[PATH_MAX + 1];
  size_t limit = !strncmp(roots[pane->root].path, "mc", 2) ? 31 :
                 FILE_MANAGER_NAME_MAX;
  if (!editFileName(lunaText("New folder"), name, sizeof(name), limit))
    return;
  if (!joinPath(path, sizeof(path), pane->path, name)) {
    snprintf(status, statusSize, lunaText("The folder path is too long."));
    return;
  }
  struct stat info;
  if (stat(path, &info) == 0) {
    snprintf(status, statusSize, lunaText("That name already exists."));
    return;
  }
  if (mkdir(path, 0777) != 0) {
    snprintf(status, statusSize, lunaText("Could not create folder on this drive."));
    return;
  }
  selectPaneEntry(pane, name);
  snprintf(status, statusSize, lunaText("Created folder %s"), name);
}

static void drawFileManagerPane(int x1, int y1, int x2, int y2, int active) {
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, 3,
                    glassPresetColor(0x08, 0x19, 0x2C, active ? 0x38 : 0x24));
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y1 + 1, 4,
                    glassPresetColor(0x78, 0x9A, 0xB0, 0x30));
}

static void drawFileManagerSelection(int x1, int y, int x2,
                                     int lineHeight, int active) {
  gsKit_prim_sprite(gsGlobal, x1, y - 3, x2, y + lineHeight + 3, 4,
                    glassPresetColor(0x26, 0x61, 0x88, active ? 0x60 : 0x24));
  if (active)
    gsKit_prim_sprite(gsGlobal, x1, y - 3, x1 + 2, y + lineHeight + 3, 5,
                      glassPresetColor(0xA0, 0xDA, 0xF0, 0x78));
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

static void drawFileManagerFooter(int left, int right, PromptBar footer) {
  int top = gsGlobal->Height - footerHeight;
  int bottom = gsGlobal->Height - FILE_MANAGER_GLASS_MARGIN;
  gsKit_prim_sprite(gsGlobal, FILE_MANAGER_GLASS_MARGIN + 2, top,
                    gsGlobal->Width - FILE_MANAGER_GLASS_MARGIN - 2,
                    bottom, 3, glassPresetColor(0x02, 0x10, 0x22, 0x48));
  gsKit_prim_sprite(gsGlobal, left + 8, top, right - 8, top + 1, 4,
                    glassPresetColor(0x55, 0x9A, 0xB8, 0x48));
  if (footer.note != NULL || footer.count > 0) {
    drawMenuPromptBar(&fileFooterTransitions[1],left + 8, top + 6, right - 8, bottom, 0,
                      HeaderTextColor, footer,1);
    return;
  }
  const ButtonPrompt controls[] = {
      {ICON_CROSS, lunaText("Open")}, {ICON_SQUARE, lunaText("Select")},
      {ICON_TRIANGLE, lunaText("Options")}, {ICON_CIRCLE, lunaText("Back")}};
  drawMenuPromptBar(&fileFooterTransitions[1],left + 8, top + 6, right - 8, bottom, 0,
                    HeaderTextColor, (PromptBar){NULL, controls, 4},1);
}


// Fit a complete UTF-8 string onto one line, retaining the useful end of paths.

static void fileManagerFitText(const char *text, int width,
                               char *out, size_t capacity, int tail) {
  if (!capacity) return;
  out[0] = '\0';
  if (width <= 0) return;
  size_t length = strlen(text);
  if (length < capacity && (int)getLineWidth(text) <= width) {
    memcpy(out, text, length + 1);
    return;
  }
  if (capacity < 4 || (int)getLineWidth("...") > width) return;
  // Search UTF-8 boundaries in logarithmic passes rather than repeatedly
  // measuring a long path one character at a time on every rendered frame.
  int low = 0, high = (int)length;
  size_t best = tail ? length : 0;
  while (low <= high) {
    int middle = low + (high - low) / 2;
    size_t cut = (size_t)middle;
    if (tail) {
      while (cut < length && ((unsigned char)text[cut] & 0xc0) == 0x80) cut++;
      if (length - cut + 4 > capacity) { low = middle + 1; continue; }
      snprintf(out, capacity, "...%s", text + cut);
    } else {
      while (cut && ((unsigned char)text[cut] & 0xc0) == 0x80) cut--;
      if (cut + 4 > capacity) { high = middle - 1; continue; }
      memcpy(out, text, cut);
      memcpy(out + cut, "...", 4);
    }
    if ((int)getLineWidth(out) <= width) {
      best = cut;
      if (tail) high = middle - 1; else low = middle + 1;
    } else {
      if (tail) low = middle + 1; else high = middle - 1;
    }
  }
  if (tail) snprintf(out, capacity, "...%s", text + best);
  else { memcpy(out, text, best); memcpy(out + best, "...", 4); }
}

static void drawFileManagerMark(int x, int y, int marked) {
  int size = 12;
  uint64_t edge = glassPresetColor(0x78, 0xBD, 0xD7, marked ? 0x78 : 0x38);
  gsKit_prim_sprite(gsGlobal, x, y, x + size, y + 1, 5, edge);
  gsKit_prim_sprite(gsGlobal, x, y + size - 1, x + size, y + size, 5, edge);
  gsKit_prim_sprite(gsGlobal, x, y, x + 1, y + size, 5, edge);
  gsKit_prim_sprite(gsGlobal, x + size - 1, y, x + size, y + size, 5, edge);
  if (marked)
    gsKit_prim_sprite(gsGlobal, x + 3, y + 3, x + size - 3, y + size - 3, 5, edge);
}


typedef struct {
  int from, to;
  uint32_t startMs;
} FileManagerTrayAnimation;
static FileManagerTrayAnimation fileManagerTray;

// This transition belongs only to the File Explorer drawer.
static int fileManagerTrayWidth(int target) {
  uint32_t now = uiNowMs(), elapsed = now - fileManagerTray.startMs;
  int progress = elapsed >= 140 ? 1000 : lunaNavEase((int)(elapsed * 1000U / 140));
  int width = fileManagerTray.from +
      (fileManagerTray.to - fileManagerTray.from) * progress / 1000;
  if (target != fileManagerTray.to) {
    fileManagerTray.from = width;
    fileManagerTray.to = target;
    fileManagerTray.startMs = now;
  }
  return width;
}

static void drawFileManagerContents(const BrowserRoot *roots, int rootCount,
                                 const BrowserPane *browser, const CopyQueue *queue,
                                 int trayCollapsed, const char *status, PromptBar footer,
                                 int progressPercent) {
  int left = keepoutArea + 10, right = gsGlobal->Width - left;
  int line = getFontLineHeight();
  FileManagerLayout layout = fileManagerLayout();
  int target = queue->count && !trayCollapsed ? (right - left) / 4 : 0;
  int trayWidth = fileManagerTrayWidth(target);
  int browserRight = right - (trayWidth > 0 ? trayWidth + 10 : 0);
  int total = browser->root < 0 ? rootCount : browser->visibleCount;
  int x1 = left + 10;
  int x2 = browserRight - 10 - (total > layout.visible ? 14 : 0);
  int selected = browser->root < 0 ? browser->rootSelected : browser->selected;
  int first = selected / layout.visible * layout.visible;
  char text[PATH_MAX + 160], fitted[PATH_MAX + 160], summary[128];
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(1);
  drawTextWindow(left, headerHeight - line, right, headerHeight + 2,
                 0, HeaderTextColor, ALIGN_HCENTER, lunaText("File Explorer"));
  drawFileManagerPane(left, layout.paneTop, browserRight, layout.paneBottom, 1);
  // A quiet parent trail and a stronger current folder form one location header.
  const char *folder = lunaText("Drives");
  text[0] = '\0';
  if (browser->root >= 0) {
    const char *relative = browser->path + strlen(roots[browser->root].path);
    while (*relative == '/') relative++;
    char path[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s", relative);
    size_t length = strlen(path);
    while (length && path[length - 1] == '/') path[--length] = '\0';
    const char *last = strrchr(path, '/');
    if (length) {
      // folder points into text's separate storage below after parent fitting.
      snprintf(fitted, sizeof(fitted), "%s", last ? last + 1 : path);
      if (last) path[last - path] = '\0'; else path[0] = '\0';
      snprintf(text, sizeof(text), "%s%s%s", roots[browser->root].label,
               path[0] ? "  >  " : "", path);
    } else {
      snprintf(fitted, sizeof(fitted), "%s", roots[browser->root].label);
      snprintf(text, sizeof(text), "%s", lunaText("Drives"));
    }
    char trail[PATH_MAX + 160], current[PATH_MAX + 160];
    snprintf(current, sizeof(current), "%s", fitted);
    // Convert parent separators into a readable, font-safe breadcrumb trail.
    for (char *c = text; *c; c++) if (*c == '/') *c = '>';
    fileManagerFitText(text, x2 - x1, trail, sizeof(trail), 1);
    drawTextWindow(x1 + 4, layout.paneTop + 4, x2 - 4,
                   layout.paneTop + 4 + line, 5, HeaderTextColor, ALIGN_LEFT, trail);
    fileManagerFitText(current, x2 - x1, fitted, sizeof(fitted), 0);
    folder = fitted;
  }
  drawTextWindow(x1 + 4, layout.paneTop + line + 6, x2 - 4,
                 layout.paneTop + 2 * line + 6, 5,
                 FontMainColor, ALIGN_LEFT, folder);
  gsKit_prim_sprite(gsGlobal, x1, layout.listTop - 3, browserRight - 10,
                    layout.listTop - 2, 4,
                    glassPresetColor(0x78, 0x9A, 0xB0, 0x24));
  for (int index = first; index < total && index < first + layout.visible; index++) {
    char name[PATH_MAX + 16], detail[48] = "";
    int y = layout.listTop + (index - first) * layout.rowStep;
    int isRoot = browser->root < 0, directory = 0, marked = 0;
    if (isRoot) {
      snprintf(name, sizeof(name), "%s", roots[index].label);
      snprintf(detail, sizeof(detail), "%s", lunaText("Drive"));
    } else {
      const BrowserEntry *entry = browserPaneEntry(browser, index);
      directory = entry->isDirectory;
      char path[PATH_MAX + 1];
      marked = joinPath(path, sizeof(path), browser->path, entry->name) &&
               queueFind(queue, path) >= 0;
      snprintf(name, sizeof(name), "%s", entry->name);
      if (!directory && entry->hasSize)
        snprintf(detail, sizeof(detail), entry->size >= 1048576 ? "%llu MB" : "%llu KB",
                 (unsigned long long)(entry->size >= 1048576 ?
                   entry->size / 1048576 : (entry->size + 1023) / 1024));
    }
    if (index == selected)
      drawFileManagerSelection(x1 - 4, y, x2 + 4, line, 1);
    if (!isRoot && (marked || index == selected))
      drawFileManagerMark(x1, y + (line - 12) / 2, marked);
    drawFileManagerItemIcon(x1 + (isRoot ? 0 : 20), y, isRoot, directory, index == selected);
    int nameLeft = x1 + (isRoot ? 23 : 43);
    int detailWidth = detail[0] ? (int)getLineWidth(detail) + 10 : 0;
    if (detailWidth > (x2 - nameLeft) / 3) { detail[0] = '\0'; detailWidth = 0; }
    fileManagerFitText(name, x2 - nameLeft - detailWidth - 4, fitted, sizeof(fitted), 0);
    drawTextWindow(nameLeft, y, x2 - detailWidth - 4, y + line, 5,
                   index == selected ? ColorSelectedText : FontMainColor,
                   ALIGN_LEFT, fitted);
    if (detail[0])
      drawTextWindow(x2 - detailWidth, y, x2, y + line, 5,
                     HeaderTextColor, ALIGN_RIGHT, detail);
  }
  drawListScrollbar(browserRight - 16, layout.listTop, layout.listBottom,
                     total, first, layout.visible, 5);
  if (!total && !browser->scanning)
    drawTextWindow(x1 + 4, layout.listTop, x2, layout.listTop + line, 5,
                   FontMainColor, ALIGN_LEFT, lunaText("Nothing to show"));
  if (trayWidth > 0) {
    int trayLeft = right - trayWidth;
    drawFileManagerPane(trayLeft, layout.paneTop, right, layout.paneBottom, 0);
    // Avoid squeezing text while the drawer is entering or leaving.
    if (trayWidth >= target && trayWidth >= 90 && queue->count) {
      int tx1 = trayLeft + 10, tx2 = right - 10, y = layout.paneTop + 8;
      fileManagerFitText(lunaText("Selected items"), tx2 - tx1, fitted, sizeof(fitted), 0);
      drawTextWindow(tx1, y, tx2, y + line, 5, FontMainColor, ALIGN_LEFT, fitted);
      y += line + 5;
      fileManagerFitText(fileManagerScene.move && *fileManagerScene.move ?
          lunaText("Ready to move") : lunaText("Ready to copy"), tx2 - tx1, fitted, sizeof(fitted), 0);
      drawTextWindow(tx1, y, tx2, y + line, 5, HeaderTextColor, ALIGN_LEFT, fitted);
      y += line + 12;
      int visible = (layout.paneBottom - line - 12 - y) / layout.rowStep;
      if (visible > 4) visible = 4;
      if (visible < 1) visible = 1;
      int firstItem = queue->count > visible ? queue->count - visible : 0;
      for (int i = firstItem; i < queue->count; i++) {
        const char *path = queue->items[i].path, *base = strrchr(path, '/');
        fileManagerFitText(base ? base + 1 : path, tx2 - tx1, fitted, sizeof(fitted), 0);
        drawTextWindow(tx1, y, tx2, y + line, 5, FontMainColor, ALIGN_LEFT, fitted);
        y += layout.rowStep;
      }
      if (firstItem) {
        snprintf(text, sizeof(text), lunaText("+%d more"), firstItem);
        fileManagerFitText(text, tx2 - tx1, fitted, sizeof(fitted), 0);
        drawTextWindow(tx1, layout.paneBottom - line - 6, tx2, layout.paneBottom - 6,
                       5, HeaderTextColor, ALIGN_LEFT, fitted);
      }
    }
  }
  int actionLeft = right - (right - left) * 2 / 5;
  fileManagerItemSummary(queue->count ? queue->count : total, queue->count > 0,
      fileManagerScene.move && *fileManagerScene.move, summary, sizeof(summary));
  if (progressPercent < 0 && footer.count == 0 && queue->count) {
    const ButtonPrompt ready[] = {
        {ICON_R1, lunaText("Tray")}, {ICON_START,
         fileManagerScene.move && *fileManagerScene.move ? lunaText("Move here") : lunaText("Paste here")}};
    drawMenuPromptBar(&fileFooterTransitions[5],actionLeft, layout.summaryTop, right - 8, layout.summaryTop + line,
                   5, HeaderTextColor, (PromptBar){NULL, ready, browser->root >= 0 ? 2 : 1},1);
  }
  const char *message = status;
  if (progressPercent < 0 && footer.count == 0) {
    if (browser->root >= 0 && browser->truncated)
      message = browser->truncated == 1 ? lunaText("Listing limited by memory; some items are hidden.") :
                                         lunaText("Folder read failed; listing is incomplete.");
    else if ((!message || !*message) && browser->scanning) {
      snprintf(text, sizeof(text), lunaText("Reading folder... %d items"), browser->count);
      message = text;
    } else if ((!message || !*message) && browser->filter[0]) {
      snprintf(text, sizeof(text), lunaText("Filter: %s"), browser->filter);
      message = text;
    }
  }
  if (!message || !*message) message = summary;
  int statusRight = progressPercent < 0 && footer.count == 0 && queue->count ?
      actionLeft - 10 : right - 8;
  fileManagerFitText(message, statusRight - left - 8, fitted, sizeof(fitted), 0);
  drawTextWindow(left + 8, layout.summaryTop, statusRight, layout.summaryTop + line, 5,
                 HeaderTextColor, ALIGN_LEFT, fitted);
  if (progressPercent >= 0) {
    if (progressPercent > 100) progressPercent = 100;
    int filled = (right - left - 16) * progressPercent / 100;
    int y = gsGlobal->Height - footerHeight - 5;
    gsKit_prim_sprite(gsGlobal, left + 8, y, right - 8, y + 3, 5,
                      glassPresetColor(0x28, 0x38, 0x50, 0x60));
    if (filled > 0)
      gsKit_prim_sprite(gsGlobal, left + 8, y, left + 8 + filled, y + 3, 5,
                        glassPresetColor(0x7C, 0xD8, 0xF0, 0x70));
  }
  drawFileManagerFooter(left, right, footer);
}

static void drawFileManagerFrame(const BrowserRoot *roots, int rootCount,
    const BrowserPane *browser, const CopyQueue *queue, int trayCollapsed,
    const char *status, PromptBar footer, int progressPercent) {
  drawFileManagerContents(roots, rootCount, browser, queue, trayCollapsed,
                         status, footer, progressPercent);
  presentBrowserFrame();
}

typedef struct {
  const BrowserRoot *roots;
  int rootCount;
  BrowserPane *browser;
  const CopyQueue *queue;
  int trayCollapsed;
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
  snprintf(status, sizeof(status), lunaText("%d/%d items  %d%%  %llu/%llu MiB  %llu.%llu MiB/s"),
           progress->itemIndex, progress->itemCount, percent,
           (unsigned long long)(progress->bytes / 1048576),
           (unsigned long long)((progress->totalBytes + 1048575) / 1048576),
           (unsigned long long)(speedTenths / 10),
           (unsigned long long)(speedTenths % 10));
  snprintf(footer, sizeof(footer), "ETA %llu:%02llu  %s",
           (unsigned long long)(secondsLeft / 60),
           (unsigned long long)(secondsLeft % 60), name);
  drawFileManagerFrame(progress->roots, progress->rootCount, progress->browser,
                       progress->queue, progress->trayCollapsed, status,
                       PROMPT_NOTE(footer, ICON_CIRCLE, lunaText("Cancel")),
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
    while (1) {
      errno = 0;
      item = readdir(directory);
      if (!item) {
        int failed = errno != 0;
        if (closedir(directory) < 0) failed = 1;
        return failed ? -1 : 0;
      }
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
  }
  if (!S_ISREG(info.st_mode))
    return -1;
  progress->totalFiles++;
  if (info.st_size > 0)
    progress->scannedBytes += (uint64_t)info.st_size;
  uint32_t now = uiNowMs();
  if (now - progress->lastDrawMs >= 300) {
    char status[120];
    snprintf(status, sizeof(status), lunaText("Measuring queue: %d files, %llu MiB"),
             progress->totalFiles,
             (unsigned long long)(progress->scannedBytes / 1048576));
    drawFileManagerFrame(progress->roots, progress->rootCount, progress->browser,
                         progress->queue, progress->trayCollapsed, status,
                         PROMPT_ONE(ICON_CIRCLE, lunaText("Cancel measurement")), -1);
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
    while (1) {
      errno = 0;
      item = readdir(directory);
      if (!item) {
        int failed = errno != 0;
        if (closedir(directory) < 0) failed = 1;
        return failed ? -1 : 0;
      }
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
  uint64_t copiedBytes = 0;
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
      copiedBytes += bytes;
      progress->bytes += bytes;
      showCopyProgress(progress, source, 0);
    }
  }
  if (progress->cancelled || copiedBytes != (uint64_t)info.st_size)
    result = -1;
  free(buffer);
  if (close(output) != 0)
    result = -1;
  if (close(input) != 0)
    result = -1;
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
    while (1) {
      errno = 0;
      item = readdir(directory);
      if (!item) {
        if (errno) result = -1;
        break;
      }
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
    if (closedir(directory) < 0) result = -1;
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
  uint64_t verifiedBytes = 0;
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
    if (result == 0) {
      if (memcmp(buffers, buffers + COPY_BUFFER_SIZE, count))
        result = -1;
      else
        verifiedBytes += count;
    }
  }
  if (progress->cancelled || verifiedBytes != (uint64_t)sourceInfo.st_size)
    result = -1;
  free(buffers);
  if (close(sourceFd)) result = -1;
  if (close(destinationFd)) result = -1;
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
  while (!result) {
    errno = 0;
    item = readdir(directory);
    if (!item) {
      if (errno) result = -1;
      break;
    }
    char *child;
    if (!strcmp(item->d_name, ".") || !strcmp(item->d_name, ".."))
      continue;
    child = malloc(PATH_MAX + 1);
    if (child == NULL || !joinPath(child, PATH_MAX + 1, path, item->d_name) ||
        removeCopyTree(child, depth + 1) != 0)
      result = -1;
    free(child);
  }
  if (closedir(directory) < 0) result = -1;
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
                        void *context) {
  (void)context;
  snprintf(name, nameSize, "%s", index == 0 ? lunaText("Skip") :
           index == 1 ? lunaText("Keep Both") : lunaText("Replace"));
  detail[0] = '\0';
  (void)detailSize;
  return 1;
}

static int replaceConfirmRow(int index, char *name, size_t nameSize,
                              char *detail, size_t detailSize,
                              void *context) {
  (void)index;
  (void)context;
  (void)detailSize;
  snprintf(name, nameSize, lunaText("Replace existing item"));
  detail[0] = '\0';
  return 1;
}

static int chooseConflict(const char *name, int samePath,
                           int existingDirectory) {
  int selected = 0;
  int count = samePath ? 2 : 3;
  while (1) {
    drawFileManagerList(lunaText("Name conflict"), name,
        lunaText("The destination already contains this name."),
        count, selected, conflictRow, NULL,
        PROMPT_TWO(ICON_CROSS, lunaText("Choose"), ICON_CIRCLE, lunaText("Cancel")));
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
      drawFileManagerList(lunaText("Confirm Replace"), name,
          existingDirectory ? lunaText("The existing folder and its contents will be removed.") :
                              lunaText("The existing file will be removed."),
          1, 0, replaceConfirmRow, NULL,
          PROMPT_TWO(ICON_CROSS, lunaText("Replace"), ICON_CIRCLE, lunaText("Cancel")));
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


static void drawTransferReview(const BrowserRoot *roots, int rootCount,
                                const BrowserPane *browser, const CopyQueue *queue,
                                int move, uint64_t bytes) {
  int left = keepoutArea + 32, right = gsGlobal->Width - left;
  int top = 62, bottom = gsGlobal->Height - 28;
  int line = getFontLineHeight(), y = top + line + 32;
  char text[PATH_MAX + 160], fitted[PATH_MAX + 160];
  drawFileManagerModal(left, top, right, bottom,
                       move ? lunaText("Review move") : lunaText("Review copy"));
  fileManagerItemSummary(queue->count, 1, move, text, sizeof(text));
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 FontMainColor, ALIGN_LEFT, text);
  y += line + 4;
  formatBytes(bytes, text, sizeof(text));
  fileManagerFitText(text, right - left - 32, fitted, sizeof(fitted), 0);
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 HeaderTextColor, ALIGN_LEFT, fitted);
  y += line + 14;
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 HeaderTextColor, ALIGN_LEFT, lunaText("Destination"));
  y += line + 6;
  snprintf(text, sizeof(text), "%s", roots[browser->root].label);
  fileManagerFitText(text, right - left - 32, fitted, sizeof(fitted), 0);
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 ColorSelectedText, ALIGN_LEFT, fitted);
  y += line + 4;
  const char *relative = browser->path + strlen(roots[browser->root].path);
  while (*relative == '/') relative++;
  fileManagerFitText(*relative ? relative : "/", right - left - 32,
                     fitted, sizeof(fitted), 1);
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 FontMainColor, ALIGN_LEFT, fitted);
  y += line + 12;
  const char *base = strrchr(queue->items[0].path, '/');
  snprintf(text, sizeof(text), "%s%s", base ? base + 1 : queue->items[0].path,
           queue->count > 1 ? " ..." : "");
  fileManagerFitText(text, right - left - 32, fitted, sizeof(fitted), 0);
  drawTextWindow(left + 16, y, right - 16, y + line, 14,
                 HeaderTextColor, ALIGN_LEFT, fitted);
  y += line + 8;
  drawTextWindow(left + 16, y, right - 16, bottom - line - 24, 14,
                 HeaderTextColor, ALIGN_LEFT, lunaText("Verify after copy. Ask before replacing."));
  drawFileManagerModalHelp(left, right, bottom, PROMPT_TWO(ICON_CROSS,
      move ? lunaText("Confirm move") : lunaText("Confirm copy"), ICON_CIRCLE, lunaText("Cancel")));
  presentBrowserFrame();
  (void)rootCount;
}

static void runCopyQueue(const BrowserRoot *roots, int rootCount,
                         BrowserPane *browser, CopyQueue *queue,
                         int trayCollapsed, int move,
                         char *status, size_t statusSize) {
  if (queue->count == 0) {
    snprintf(status, statusSize, lunaText("Mark items before copying."));
    return;
  }
  BrowserPane *destinationPane = browser;
  if (destinationPane->root < 0) {
    snprintf(status, statusSize, lunaText("Open the destination folder, then paste here."));
    return;
  }
  for (int index = 0; index < queue->count; index++) {
    const CopyItem *item = &queue->items[index];
    if (item->isDirectory && pathWithin(destinationPane->path, item->path)) {
      snprintf(status, statusSize, lunaText("Destination is inside a marked folder."));
      return;
    }
  }
  CopyProgress progress = {0};
  progress.roots = roots;
  progress.rootCount = rootCount;
  progress.browser = browser;
  progress.queue = queue;
  progress.trayCollapsed = trayCollapsed;
  progress.itemCount = queue->count;
  progress.lastDrawMs = uiNowMs();
  drawFileManagerFrame(roots, rootCount, browser, queue, trayCollapsed,
                       lunaText("Measuring selected files..."),
                       PROMPT_ONE(ICON_CIRCLE, lunaText("Cancel measurement")),
                       -1);
  for (int index = 0; index < queue->count; index++) {
    uint64_t before = progress.scannedBytes;
    if (measureTree(queue->items[index].path, 0, &progress) != 0) {
      snprintf(status, statusSize, progress.cancelled ?
               lunaText("Measurement cancelled. Queue kept.") :
               lunaText("Cannot read a queued item. Queue kept."));
      return;
    }
    queue->items[index].measuredBytes = progress.scannedBytes - before;
  }
  progress.totalBytes = progress.scannedBytes;
  drawTransferReview(roots, rootCount, browser, queue, move, progress.totalBytes);
  if (!(waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS)) {
    snprintf(status, statusSize, lunaText("Queue kept. %s cancelled."),
             move ? lunaText("Move") : lunaText("Copy"));
    return;
  }
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
      snprintf(status, statusSize, lunaText("%d completed; destination path too long."),
               completed);
      stopped = 1;
      break;
    }
    if (item->isDirectory &&
        pathWithin(destinationPane->path, item->path)) {
      snprintf(status, statusSize, lunaText("%d completed; choose another destination."),
               completed);
      stopped = 1;
      break;
    }
    int samePath = !strcmp(item->path, destination);
    int replace = 0;
    if (stat(destination, &info) == 0) {
      int choice = chooseConflict(name, samePath, S_ISDIR(info.st_mode));
      if (choice < 0) {
        snprintf(status, statusSize, lunaText("%d completed; queue kept."), completed);
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
        snprintf(status, statusSize, lunaText("No available name for Keep Both."));
        stopped = 1;
        break;
      }
      replace = choice == 2;
    } else if (samePath) {
      snprintf(status, statusSize, lunaText("Cannot use the source as its destination."));
      stopped = 1;
      break;
    }
    if (!findCopyStaging(destinationPane->path, destination, staging,
                         sizeof(staging))) {
      snprintf(status, statusSize, lunaText("No temporary name on destination drive."));
      stopped = 1;
      break;
    }
    progress.itemIndex = completed + skipped + 1;
    showCopyProgress(&progress, name, 1);
    int result = copyTree(item->path, staging, 0, &progress);
    int verificationFailed = 0;
    if (result == 0) {
      drawFileManagerFrame(roots, rootCount, browser, queue, trayCollapsed, lunaText("Verifying copied data..."),
                           PROMPT_ONE(ICON_CIRCLE, lunaText("Cancel verification")), -1);
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
                 lunaText("Replace rollback failed; check LUNAxx.BAK and LUNAxx.TMP."));
      else if (cleanup != 0)
        snprintf(status, statusSize, lunaText("Check LUNAxx.TMP; queue kept."));
      else
        snprintf(status, statusSize, lunaText("%d completed; %s. Queue kept."),
                 completed, progress.cancelled ? lunaText("cancelled") :
                 verificationFailed ? lunaText("verification failed") : lunaText("copy failed"));
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
             lunaText("%d completed; inspect source or LUNAxx.BAK for %d warnings."),
             completed, warnings);
  else if (!stopped && skipped > 0)
    snprintf(status, statusSize, lunaText("%s %d; skipped %d (still marked)."),
             move ? lunaText("Moved and verified") : lunaText("Copied and verified"), completed, skipped);
  else if (!stopped)
    snprintf(status, statusSize, lunaText("%s %d items (%llu MiB, %d files)."),
             move ? lunaText("Moved and verified") : lunaText("Copied and verified"), completed,
             (unsigned long long)(progress.bytes / 1048576), progress.files);
  if (completed > 0) {
    if (browser->root >= 0) selectPaneEntry(browser, NULL);
  }
}

static int queueRow(int index, char *name, size_t nameSize,
                    char *detail, size_t detailSize, void *context) {
  const CopyQueue *queue = context;
  const char *path = queue->items[index].path;
  const char *base = strrchr(path, '/');
  snprintf(name, nameSize, "%s%s", base ? base + 1 : path,
           queue->items[index].isDirectory ? "/" : "");
  snprintf(detail, detailSize, "%s", path);
  return 1;
}

static void reviewQueue(CopyQueue *queue) {
  int selected = 0;
  while (queue->count > 0) {
    char status[96];
    snprintf(status, sizeof(status), lunaText("%d items queued. Square removes the highlighted mark."), queue->count);
    drawFileManagerList(lunaText("Review tray"), queue->items[selected].path,
        status, queue->count, selected, queueRow, queue,
        PROMPT_TWO(ICON_SQUARE, lunaText("Unmark"), ICON_CIRCLE, lunaText("Back")));
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) return;
    if (input & PAD_UP) selected = (selected + queue->count - 1) % queue->count;
    else if (input & PAD_DOWN) selected = (selected + 1) % queue->count;
    else if (input & PAD_SQUARE) {
      queueRemove(queue, selected);
      if (selected >= queue->count) selected = queue->count - 1;
    }
  }
}

// Returns 1 on success, 0 for an overlapping mark, -1 for capacity/allocation.
static int markEntry(const BrowserPane *pane, CopyQueue *queue,
                      int index, int toggle) {
  BrowserEntry *entry = browserPaneEntry(pane, index);
  char path[PATH_MAX + 1];
  if (!entry || !joinPath(path, sizeof(path), pane->path, entry->name)) return -1;
  int marked = queueFind(queue, path);
  if (marked >= 0) {
    if (toggle) queueRemove(queue, marked);
    return 1;
  }
  for (int i = 0; i < queue->count; i++)
    if (pathWithin(path, queue->items[i].path) || pathWithin(queue->items[i].path, path)) return 0;
  if (queue->count >= COPY_MAX_QUEUE) return -1;
  char *copy = strdup(path);
  if (!copy) return -1;
  queue->items[queue->count++] = (CopyItem){copy, entry->isDirectory, 0};
  return 1;
}

static void markListed(const BrowserPane *pane, CopyQueue *queue,
                        int invert, char *status, size_t statusSize) {
  CopyQueue trial = {0};
  for (int i = 0; i < queue->count; i++) {
    trial.items[i] = queue->items[i];
    trial.items[i].path = strdup(queue->items[i].path);
    if (!trial.items[i].path) goto full;
    trial.count++;
  }
  for (int i = 0; i < pane->visibleCount; i++) {
    char path[PATH_MAX + 1];
    if (!joinPath(path, sizeof(path), pane->path, browserPaneEntry(pane, i)->name)) goto full;
    int marked = queueFind(queue, path);
    if (invert && marked >= 0) queueRemove(&trial, queueFind(&trial, path));
  }
  for (int i = 0; i < pane->visibleCount; i++) {
    char path[PATH_MAX + 1];
    if (!joinPath(path, sizeof(path), pane->path, browserPaneEntry(pane, i)->name)) goto full;
    if (invert && queueFind(queue, path) >= 0) continue;
    if (markEntry(pane, &trial, i, 0) < 0) goto full;
  }
  queueClear(queue);
  *queue = trial;
  snprintf(status, statusSize, lunaText("%d items queued."), queue->count);
  return;
full:
  queueClear(&trial);
  snprintf(status, statusSize, "%s", lunaText("Selection exceeds queue or memory limits. Existing marks kept."));
}

static int deleteProgress(const char *path, void *context) {
  CopyProgress *progress = context;
  if (copyCancelled(progress)) return 1;
  uint32_t now = uiNowMs();
  if (now - progress->lastDrawMs >= 100) {
    const char *base = strrchr(path, '/');
    char status[160];
    snprintf(status, sizeof(status), lunaText("Deleting %d/%d: %s"),
        progress->itemIndex, progress->itemCount, base ? base + 1 : path);
    drawFileManagerFrame(progress->roots, progress->rootCount, progress->browser,
        progress->queue, progress->trayCollapsed, status,
        PROMPT_ONE(ICON_CIRCLE, lunaText("Stop deletion")), -1);
    progress->lastDrawMs = now;
  }
  return 0;
}

static void deleteItems(const BrowserRoot *roots, int rootCount,
                        BrowserPane *browser, CopyQueue *queue,
                        int trayCollapsed, int marked, char *status, size_t statusSize) {
  CopyQueue single = {0};
  CopyQueue *items = marked ? queue : &single;
  if (!marked) {
    if (browser->root < 0 || !browser->visibleCount ||
        markEntry(browser, &single, browser->selected, 0) != 1) return;
  }
  if (!items->count) return;
  int selected = 0, confirmed = 0;
  while (1) {
    snprintf(status, statusSize, lunaText("Permanently delete %d items? This cannot be undone."), items->count);
    drawFileManagerList(lunaText("Delete items"), items->items[selected].path,
        status, items->count, selected, queueRow, items,
        PROMPT_TWO(ICON_CROSS, lunaText("Delete permanently"), ICON_CIRCLE, lunaText("Cancel")));
    int input = waitForInput(-1);
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) break;
    if (input & PAD_UP) selected = (selected + items->count - 1) % items->count;
    else if (input & PAD_DOWN) selected = (selected + 1) % items->count;
    else if (input & PAD_CROSS) { confirmed = 1; break; }
  }
  if (!confirmed) {
    snprintf(status, statusSize, "%s", lunaText("Deletion cancelled. Nothing removed."));
    queueClear(&single);
    return;
  }
  // Stop the current directory enumerator before modifying its contents.
  browserPaneStop(browser);
  CopyProgress progress = {0};
  progress.roots = roots; progress.rootCount = rootCount;
  progress.browser = browser; progress.queue = queue;
  progress.trayCollapsed = trayCollapsed;
  progress.itemCount = items->count;
  int completed = 0, result = 0;
  while (items->count > 0) {
    const char *path = items->items[0].path;
    const char *root = NULL;
    for (int i = 0; i < rootCount; i++)
      if (pathWithin(path, roots[i].path)) { root = roots[i].path; break; }
    progress.itemIndex = completed + 1;
    result = root ? browserDeleteTree(path, root, deleteProgress, &progress) : -1;
    if (result) break;
    // Deleting one row also invalidates queued parents or descendants of it.
    if (!marked) for (int i = 0; i < queue->count;)
      if (pathWithin(queue->items[i].path, path) || pathWithin(path, queue->items[i].path))
        queueRemove(queue, i);
      else i++;
    queueRemove(items, 0);
    completed++;
  }
  if (result)
    snprintf(status, statusSize, lunaText("Deleted %d items; stopped. Some contents may already be removed."), completed);
  else
    snprintf(status, statusSize, lunaText("Deleted %d items."), completed);
  if (browser->root >= 0) selectPaneEntry(browser, NULL);
  queueClear(&single);
}

typedef struct {
  const char *path;
  uint32_t lastDraw;
  int application;
} FileOpenProgress;

static int drawFileOpenProgress(size_t completed, size_t total, void *context) {
  FileOpenProgress *progress = context;
  if (readInput() & PAD_CIRCLE) return 1;
  uint32_t now = uiNowMs();
  if (progress->lastDraw && now - progress->lastDraw < 150) return 0;
  progress->lastDraw = now;
  char status[96];
  if (progress->application)
    snprintf(status, sizeof(status), lunaText("Preparing application... %u%%"),
        total ? (unsigned)((uint64_t)completed * 100 / total) : 0);
  else snprintf(status, sizeof(status), "%s", lunaText("Reading text file..."));
  drawBrowserFrame(lunaText("File Explorer"), progress->path, status,
      PROMPT_ONE(ICON_CIRCLE, lunaText("Cancel")), 0, 0, 0, NULL, NULL);
  return 0;
}

static int textReadCancelled(void *context) {
  return drawFileOpenProgress(0, 0, context);
}

static int textGlyphWidth(const char *glyph, void *context) {
  (void)context;
  return (int)getLineWidth(glyph);
}

static void viewTextFile(const char *path, char *status, size_t statusSize) {
  LunaTextFile file = {0};
  FileOpenProgress progress = {.path = path};
  const char *error = lunaTextFileRead(path, &file, textReadCancelled, &progress);
  int left = keepoutArea + 24, right = gsGlobal->Width - left;
  if (!error) error = lunaTextFileLayout(&file, right - left - 20, textGlyphWidth, NULL);
  if (error) {
    snprintf(status, statusSize, "%s", lunaText(error));
    lunaTextFileFree(&file);
    return;
  }
  int lineHeight = getFontLineHeight();
  int top = 111, bottom = gsGlobal->Height - footerHeight - lineHeight;
  int visible = (bottom - top) / lineHeight;
  if (visible < 1) visible = 1;
  int first = 0;
  while (1) {
    char position[120];
    gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
    drawBrowserSheet(1);
    drawTextWindow(left, 22, right, 50, 0, HeaderTextColor,
                   ALIGN_HCENTER, lunaText("Text viewer"));
    drawTextWindow(left + 8, 73, right - 8, 100, 0, HeaderTextColor,
                   ALIGN_LEFT, path);
    for (int row = 0; row < visible && (uint32_t)(first + row) < file.rowCount; row++) {
      char text[LUNA_TEXT_ROW_BYTES + 1];
      lunaTextFileRow(&file, (uint32_t)(first + row), text, sizeof(text));
      drawTextWindow(left + 10, top + row * lineHeight, right - 10,
          top + (row + 1) * lineHeight, 0, FontMainColor, ALIGN_LEFT, text);
    }
    snprintf(position, sizeof(position), lunaText("Line %u  Row %u/%u  %s"),
        file.rows[first].line, (unsigned)first + 1, file.rowCount,
        file.partial ? lunaText("Limited preview") : lunaText("Read only"));
    drawTextWindow(left + 8, bottom, right - 8, bottom + lineHeight,
        0, HeaderTextColor, ALIGN_LEFT, position);
    const ButtonPrompt controls[] = {
        {ICON_DPAD, lunaText("Scroll")}, {ICON_L1, lunaText("Page up")},
        {ICON_R1, lunaText("Page down")}, {ICON_CIRCLE, lunaText("Back")}};
    drawMenuPromptBar(&fileFooterTransitions[4],left + 8, gsGlobal->Height - footerHeight, right - 8,
        gsGlobal->Height - 8, 0, HeaderTextColor, (PromptBar){NULL, controls, 4},1);
    presentBrowserFrame();
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) break;
    if (input & PAD_UP) first--;
    else if (input & PAD_DOWN) first++;
    else if (input & (PAD_L1 | PAD_L2)) first -= visible;
    else if (input & (PAD_R1 | PAD_R2)) first += visible;
    else if (input & PAD_SELECT) first = 0;
    else if (input & PAD_START) first = (int)file.rowCount - visible;
    int last = file.rowCount > (uint32_t)visible ? (int)file.rowCount - visible : 0;
    if (first < 0) first = 0;
    if (first > last) first = last;
  }
  lunaTextFileFree(&file);
  status[0] = '\0';
}

static void openPaneFile(const BrowserPane *pane, int forceText,
                          char *status, size_t statusSize) {
  const BrowserEntry *entry = browserPaneEntry(pane, pane->selected);
  char path[PATH_MAX + 1];
  if (!entry || entry->isDirectory ||
      !joinPath(path, sizeof(path), pane->path, entry->name)) return;
  LunaFileKind kind = lunaFileKind(entry->name);
  if (forceText || kind == LUNA_FILE_TEXT) {
    viewTextFile(path, status, statusSize);
  } else if (kind == LUNA_FILE_ELF) {
    drawFileManagerList(lunaText("Launch ELF"), entry->name,
        lunaText("Launch this application and leave LUNA?"), 0, 0, NULL, NULL,
        PROMPT_TWO(ICON_CROSS, lunaText("Launch"), ICON_CIRCLE, lunaText("Cancel")));
    if (!(waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS)) return;
    FileOpenProgress progress = {.path = path, .application = 1};
    const char *error = lunaHomebrewPrepare(path, drawFileOpenProgress, &progress);
    if (error) snprintf(status, statusSize, "%s", lunaText(error));
    else lunaHomebrewExecute();
  }
}

static void uiFileManagerLoop(void) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserPane *browser = calloc(1, sizeof(*browser));
  if (!roots || !browser) { free(roots); free(browser); return; }
  browser->root = -1;
  int rootCount = collectRoots(roots);
  CopyQueue queue = {0};
  int trayCollapsed = 1, queuedMove = 0;
  fileManagerScene.roots = roots;
  fileManagerScene.rootCount = rootCount;
  fileManagerScene.browser = browser;
  fileManagerScene.queue = &queue;
  fileManagerScene.trayCollapsed = &trayCollapsed;
  fileManagerScene.move = &queuedMove;
  memset(&fileManagerTray, 0, sizeof(fileManagerTray));
  int previousHeld = pollInput();
  char status[160];
  status[0] = '\0';
  while (1) {
    BrowserPane *pane = browser;
    if (pane->root >= 0) browserPaneStep(pane);
    if (!strcmp(status, lunaText("Loading details for sorting; folders first.")) &&
        !pane->scanning && pane->metadataNext > pane->count) status[0] = '\0';
    drawFileManagerFrame(roots, rootCount, browser, &queue, trayCollapsed, status, (PromptBar){0}, -1);
    int held = pollInput();
    int input = held & ~previousHeld;
    previousHeld = held;
    if (input & (PAD_UP | PAD_DOWN | PAD_CIRCLE)) status[0] = '\0';
    if ((input & PAD_CIRCLE) && pane->root < 0) break;
    if (!queue.count) queuedMove = 0;
    if (input & PAD_R1) {
      if (queue.count) trayCollapsed = !trayCollapsed;
      continue;
    }
    if (input & PAD_L1) {
      browserPaneDestroy(pane);
      pane->root = -1;
      status[0] = '\0';
      continue;
    }
    if (input & (PAD_TRIANGLE | PAD_R2)) {
      int action = chooseFileAction(pane, &queue, (input & PAD_R2) != 0);
      if (action == FILE_ACTION_COPY_ITEM || action == FILE_ACTION_MOVE_ITEM) {
        CopyQueue single = {0};
        if (markEntry(pane, &single, pane->selected, 0) == 1) {
          queueClear(&queue);
          queue = single;
          queuedMove = action == FILE_ACTION_MOVE_ITEM;
          trayCollapsed = 1;
          snprintf(status, sizeof(status), "%s", lunaText("Open a destination folder, then press Start."));
        } else snprintf(status, sizeof(status), "%s", lunaText("Could not select this item."));
      } else if (action == FILE_ACTION_COPY_MARKED || action == FILE_ACTION_MOVE_MARKED) {
        queuedMove = action == FILE_ACTION_MOVE_MARKED;
        trayCollapsed = 1;
        snprintf(status, sizeof(status), "%s", lunaText("Open a destination folder, then press Start."));
      } else if (action == FILE_ACTION_CLEAR_QUEUE) {
        queueClear(&queue);
        queuedMove = 0;
        snprintf(status, sizeof(status), "%s", lunaText("Tray cleared."));
      } else if (action == FILE_ACTION_DETAILS || action == FILE_ACTION_FOLDER_DETAILS)
        showFileDetails(roots, rootCount, pane, action == FILE_ACTION_FOLDER_DETAILS);
      else if (action == FILE_ACTION_VIEW_TEXT || action == FILE_ACTION_LAUNCH_ELF)
        openPaneFile(pane, action == FILE_ACTION_VIEW_TEXT, status, sizeof(status));
      else if (action == FILE_ACTION_RENAME)
        renamePaneEntry(pane, roots, &queue, status, sizeof(status));
      else if (action == FILE_ACTION_NEW_FOLDER)
        createPaneFolder(pane, roots, status, sizeof(status));
      else if (action == FILE_ACTION_MOVE || action == FILE_ACTION_COPY)
        runCopyQueue(roots, rootCount, browser, &queue, trayCollapsed, action == FILE_ACTION_MOVE, status, sizeof(status));
      else if (action == FILE_ACTION_QUEUE) reviewQueue(&queue);
      else if (action == FILE_ACTION_DELETE || action == FILE_ACTION_DELETE_MARKED)
        deleteItems(roots, rootCount, browser, &queue, trayCollapsed,
                    action == FILE_ACTION_DELETE_MARKED, status, sizeof(status));
      else if (action == FILE_ACTION_REFRESH) {
        if (!browserPaneRefresh(pane, NULL))
          snprintf(status, sizeof(status), "%s", lunaText("Could not open this folder."));
      } else if (action == FILE_ACTION_FILTER) {
        char filter[BROWSER_FILTER_MAX + 1];
        snprintf(filter, sizeof(filter), "%s", pane->filter);
        if (editBrowserText(lunaText("Filter names"), filter, sizeof(filter), BROWSER_FILTER_MAX, 1)) {
          browserPaneFilter(pane, filter);
          status[0] = '\0';
        }
      } else if (action == FILE_ACTION_CLEAR_FILTER) browserPaneFilter(pane, "");
      else if (action >= FILE_ACTION_SORT_NAME && action <= FILE_ACTION_SORT_DATE) {
        browserPaneSort(pane, (BrowserSort)(action - FILE_ACTION_SORT_NAME));
        snprintf(status, sizeof(status), "%s", action == FILE_ACTION_SORT_NAME ?
            lunaText("Sorted by name; folders first.") :
            lunaText("Loading details for sorting; folders first."));
      } else if (action == FILE_ACTION_MARK_ALL || action == FILE_ACTION_INVERT) {
        markListed(pane, &queue, action == FILE_ACTION_INVERT, status, sizeof(status));
      }
      previousHeld = pollInput();
      if (action != FILE_ACTION_OPEN) continue;
      input = PAD_CROSS;
    }
    if (input & PAD_CIRCLE) {
      if (pane->root >= 0) {
        if (!strcmp(pane->path, roots[pane->root].path)) {
          browserPaneDestroy(pane);
          pane->root = -1;
        } else if (!browserPaneUp(pane, roots[pane->root].path))
          snprintf(status, sizeof(status), "%s", lunaText("Could not open this folder."));
      }
    } else if (input & (PAD_UP | PAD_DOWN)) {
      int total = pane->root < 0 ? rootCount : pane->visibleCount;
      int *focus = pane->root < 0 ? &pane->rootSelected : &pane->selected;
      if (total > 0) {
        if (held & PAD_L2) {
          *focus += (input & PAD_UP ? -1 : 1) * fileManagerLayout().visible;
          if (*focus < 0) *focus = 0;
          if (*focus >= total) *focus = total - 1;
        } else *focus = (*focus + (input & PAD_UP ? total - 1 : 1)) % total;
      }
      pane->restoreName[0] = '\0';
    } else if (input & PAD_CROSS) {
      if (pane->root < 0 && rootCount > 0) {
        int root = pane->rootSelected;
        if (browserPaneOpen(pane, roots[root].path)) {
          pane->root = root;
          status[0] = '\0';
        } else snprintf(status, sizeof(status), "%s", lunaText("Could not open this device."));
      } else {
        BrowserEntry *entry = browserPaneEntry(pane, pane->selected);
        if (entry && entry->isDirectory) {
          if (!browserPaneEnter(pane, entry->name))
            snprintf(status, sizeof(status), "%s", lunaText("Could not open this folder."));
          else status[0] = '\0';
        } else if (entry) {
          if (lunaFileKind(entry->name) == LUNA_FILE_OTHER) showFileDetails(roots, rootCount, pane, 0);
          else openPaneFile(pane, 0, status, sizeof(status));
          previousHeld = pollInput();
        }
      }
    } else if (input & PAD_SQUARE) {
      if (pane->root < 0 || !pane->visibleCount) {
        snprintf(status, sizeof(status), "%s", lunaText("Open a folder and select an item to mark."));
      } else {
        int result = markEntry(pane, &queue, pane->selected, 1);
        if (result == 1) status[0] = '\0';
        else snprintf(status, sizeof(status), "%s", result == 0 ?
            lunaText("A parent or child is already marked.") :
            lunaText("Selection exceeds queue or memory limits. Existing marks kept."));
      }
    } else if (input & PAD_SELECT) {
      if (!queue.count) continue;
      snprintf(status, sizeof(status), lunaText("Clear all %d marked items?"), queue.count);
      drawFileManagerList(lunaText("Clear tray"), "", status, 0, 0, NULL, NULL,
          PROMPT_TWO(ICON_CROSS, lunaText("Clear tray"), ICON_CIRCLE, lunaText("Keep tray")));
      if (waitForInput(PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE) & PAD_CROSS) {
        queueClear(&queue);
        snprintf(status, sizeof(status), "%s", lunaText("Tray cleared."));
      } else snprintf(status, sizeof(status), "%s", lunaText("Queue kept."));
      previousHeld = pollInput();
    } else if (input & PAD_START) {
      runCopyQueue(roots, rootCount, browser, &queue, trayCollapsed,
                   queuedMove, status, sizeof(status));
      previousHeld = pollInput();
    }
  }
  memset(&fileManagerScene, 0, sizeof(fileManagerScene));
  queueClear(&queue);
  browserPaneDestroy(browser);
  free(browser);
  free(roots);
}

#include "vmc_common.inc"
#ifdef LUNA_ENABLE_PSXCORE
#include "ps1_vmc.inc"
#include "ps1_discs.inc"
#endif
#include "vmc_manager.inc"

#define STORAGE_MENU_ROWS 10
static int storageRow(int index, char *name, size_t nameSize,
                      char *detail, size_t detailSize, void *context) {
  StorageSettings *settings = context;
  if (index < STORAGE_SOURCE_COUNT) {
    ModeType mode = storageSourceMode(index);
    snprintf(name, nameSize, "%s", storageSourceName(index));
    if (!(settings->enabled & mode)) snprintf(detail, detailSize, lunaText("Disabled"));
    else if (!(STORAGE_SETTINGS.enabled & mode)) snprintf(detail, detailSize, lunaText("Pending scan"));
    else if (STORAGE_STATUS[index].incomplete ||
             (STORAGE_STATUS[index].error && STORAGE_STATUS[index].games))
      snprintf(detail, detailSize, lunaText("Incomplete - %d games"), STORAGE_STATUS[index].games);
    else if (STORAGE_STATUS[index].error) snprintf(detail, detailSize, lunaText("Scan failed"));
    else if (!STORAGE_STATUS[index].devices) snprintf(detail, detailSize, lunaText("No device found"));
    else snprintf(detail, detailSize, lunaText("Ready - %d games"), STORAGE_STATUS[index].games);
  } else {
    snprintf(name, nameSize, "%s", index == 7 ? lunaText("UDPFS console IP address") :
             index == 8 ? lunaText("Apply changes & scan") : lunaText("Rescan all enabled devices"));
    snprintf(detail, detailSize, "%s", index == 7 ?
             (settings->ip[0] ? settings->ip : lunaText("Use IPCONFIG.DAT")) : "");
  }
  return 1;
}

typedef struct {
  StorageSettings settings;
  char status[160];
  int selected;
  BrowserSelector selector;
} StorageEditor;

static void storageSubmenuBegin(void *context) {
  StorageEditor *editor = context;
  editor->settings = STORAGE_SETTINGS;
  editor->selected = 0;
  editor->selector = (BrowserSelector){.selected = -1};
  snprintf(editor->status, sizeof(editor->status), "%s",
           lunaText("Select a source to enable or disable it."));
}

static int storageX(int x) { return x * gsGlobal->Width / 640; }
static int storageY(int y) { return y * gsGlobal->Height / 480; }
static uint64_t storageInk(uint64_t color, float opacity) {
  unsigned alpha = (unsigned)((color >> 24) & 255);
  return (color & ~(0xffULL << 24)) | ((uint64_t)(alpha * opacity) << 24);
}

static void storageSubmenuDraw(void *context, float opacity) {
  StorageEditor *editor = context;
  int left = storageX(32), right = storageX(506);
  int line = getFontLineHeight(), step = line + line / 3;
  int top = storageY(127), bottom = storageY(370);
  int visible = (bottom - top) / step;
  if (visible < 1) visible = 1;
  int first = editor->selected / visible * visible;
  uint64_t ink = storageInk(HeaderTextColor, opacity);
  uint64_t bright = storageInk(GS_SETREG_RGBA(0xF0, 0xFA, 0xFF, 0x80), opacity);
  drawTextWindow(left, storageY(22), right, storageY(22) + line, 6, ink,
                 ALIGN_LEFT, lunaText("System Configuration"));
  drawTextWindow(left, storageY(50), right, storageY(50) + line, 6,
                 storageInk(GS_SETREG_RGBA(232, 232, 70, 128), opacity),
                 ALIGN_LEFT, lunaText("Storage Devices"));
  /* The expanded crystal supplies the pane's glass and rim. */
  gsKit_prim_sprite(gsGlobal,storageX(22),storageY(99),storageX(518),storageY(383),5,
      glassPresetColor(5,13,34,(int)(48*opacity)));
  drawTextWindow(left, storageY(100), right, storageY(100) + line, 6, ink,
                 ALIGN_LEFT, lunaText("Choose devices for your library."));
  int focusY = browserSelectorY(&editor->selector, editor->selected, first,
                                top + (editor->selected - first) * step);
  uiCubeSubmenuFocusDraw(left-4,focusY-1,right-10,focusY+line+2,opacity);
  int detailWidth=0,maxLabel=0;
  for(int index=first;index<STORAGE_MENU_ROWS&&index<first+visible;index++) {
    char name[PATH_MAX+3],detail[32];
    storageRow(index,name,sizeof(name),detail,sizeof(detail),&editor->settings);
    int width=(int)ceilf(getLineDrawWidth(name));if(width>maxLabel)maxLabel=width;
    width=(int)ceilf(getLineDrawWidth(detail));if(width>detailWidth)detailWidth=width;
  }
  int content=right-left-16;
  int maximum=content-maxLabel-16;if(maximum<content/2)maximum=content/2;
  if(detailWidth>maximum)detailWidth=maximum;
  int detailLeft=right-12-detailWidth;
  for (int index = first; index < STORAGE_MENU_ROWS && index < first + visible; index++) {
    char name[PATH_MAX + 3], detail[32];
    storageRow(index, name, sizeof(name), detail, sizeof(detail), &editor->settings);
    int nameRight = detailWidth ? detailLeft-16 : right-12;
    int y = top + (index - first) * step;
    uint64_t color = index == editor->selected ? bright : ink;
    uiCubeMenuTextDraw(left+4,y,nameRight,7,color,name,0);
    if(detail[0])uiCubeMenuTextDraw(detailLeft,y,right-12,7,color,detail,1);
  }
  if (opacity >= .999f)
    drawListScrollbar(right - 6, top, top + visible * step, STORAGE_MENU_ROWS, first, visible, 7);
  uiCubeMenuHelpDraw(left,storageX(608),ink,editor->status);
  const ButtonPrompt prompts[] = {{ICON_DPAD, lunaText("Navigate")},{ICON_CROSS, lunaText("Select")},
      {ICON_SQUARE, lunaText("Rescan")}, {ICON_CIRCLE, lunaText("Back")}};
  uiCubeSubmenuFooterDraw(&(PromptBar){NULL,prompts,4},opacity);
}

static int storageSubmenuInput(void *context, int input) {
  StorageEditor *editor = context;
  StorageSettings *settings = &editor->settings;
  char *status = editor->status;
  int selected = editor->selected;
  BrowserSelector *selector = &editor->selector;
    if (input & PAD_CIRCLE) return LUNA_CUBE_SUBMENU_BACK; // Discard pending edits.
    if (input & PAD_UP) selected = (selected + STORAGE_MENU_ROWS - 1) % STORAGE_MENU_ROWS;
    else if (input & PAD_DOWN) selected = (selected + 1) % STORAGE_MENU_ROWS;
    else if ((input & PAD_CROSS) && selected < STORAGE_SOURCE_COUNT) {
      StorageSettings before = *settings;
      ModeType mode = storageSourceMode(selected);
      settings->enabled ^= mode;
      if ((settings->enabled & MODE_MX4SIO) && (settings->enabled & MODE_MMCE)) {
        settings->enabled &= ~(mode == MODE_MX4SIO ? MODE_MMCE : MODE_MX4SIO);
        snprintf(status, sizeof(editor->status), lunaText("MX4SIO and MMCE cannot be enabled together."));
      } else snprintf(status, sizeof(editor->status), lunaText("Choose Apply changes & scan to save your selection."));
      if (storageRequiredConflict(settings)) {
        *settings = before;
        snprintf(status, sizeof(editor->status), lunaText("This conflicts with the device LUNA was started from."));
      }
    } else if ((input & PAD_CROSS) && selected == 7) {
      char ip[16];
      snprintf(ip, sizeof(ip), "%s", settings->ip);
      // The shared keyboard supports digits and dots; an empty value uses IPCONFIG.
      if (editFileName(lunaText("UDPFS console IP (empty uses IPCONFIG.DAT)"), ip, sizeof(ip), 15)) {
        if (storageValidIP(ip)) {
          strcpy(settings->ip, ip);
          snprintf(status, sizeof(editor->status), lunaText("Apply changes to use this address for scanning and games."));
        } else snprintf(status, sizeof(editor->status), lunaText("Enter a valid unicast IPv4 address, such as 192.168.1.10."));
      }
    } else if ((input & PAD_CROSS) && selected == 8) {
      if (storageRequiredConflict(settings) || !storageValidSettings(settings)) {
        snprintf(status, sizeof(editor->status), lunaText("These device settings conflict with the startup device."));
        return LUNA_CUBE_SUBMENU_STAY;
      }
      if (storageNeedsRestart(settings)) {
        drawBrowserFrameSelected(lunaText("Restart storage drivers"), lunaText("Apply changes"),
                         lunaText("LUNA will reopen the library after restarting its drivers."),
                         PROMPT_TWO(ICON_CROSS, lunaText("Apply"), ICON_CIRCLE, lunaText("Cancel")),
                         STORAGE_MENU_ROWS, selected, browserFirstRow(selected), storageRow, settings, selector);
        if (!(waitForInput(PAD_CROSS | PAD_CIRCLE) & PAD_CROSS)) return LUNA_CUBE_SUBMENU_STAY;
      }
      if (storageSave(settings)) {
        snprintf(status, sizeof(editor->status), lunaText("Could not save storage.cfg beside LUNA. Changes were not applied."));
        return LUNA_CUBE_SUBMENU_STAY;
      }
      if (!storageRequest(settings, settings->enabled)) return LUNA_CUBE_SUBMENU_DONE;
    } else if (((input & PAD_CROSS) && selected == 9) ||
               ((input & PAD_SQUARE) && selected < STORAGE_SOURCE_COUNT)) {
      if (settings->enabled != STORAGE_SETTINGS.enabled || strcmp(settings->ip, STORAGE_SETTINGS.ip)) {
        snprintf(status, sizeof(editor->status), lunaText("Apply changes before rescanning, or go Back to discard them."));
        return LUNA_CUBE_SUBMENU_STAY;
      }
      ModeType scan = selected == 9 ? settings->enabled : storageSourceMode(selected);
      if (!(scan & settings->enabled)) {
        snprintf(status, sizeof(editor->status), lunaText("Enable this source and apply changes before scanning it."));
        return LUNA_CUBE_SUBMENU_STAY;
      }
      if (!storageRequest(settings, scan)) return LUNA_CUBE_SUBMENU_DONE;
    }
  editor->selected = selected;
  return LUNA_CUBE_SUBMENU_STAY;
}

typedef struct {
  int selected;
  const char *status;
  LunaCubePageFocus focus;
} LanguageEditor;
static void languageSubmenuBegin(void *context) {
  LanguageEditor *editor=context;
  editor->selected=lunaLanguage();
  editor->status="Choose the language used throughout LUNA.";
  editor->focus=(LunaCubePageFocus){.selected=-1,.glideMs=110};
}
static const char *languageSubmenuLabel(void *context,int row) {
  (void)context;return lunaLanguageName((LunaLanguage)row);
}
static const char *languageSubmenuValue(void *context,int row) {
  (void)context;return row==(int)lunaLanguage()?"Selected":"";
}
static void languageSubmenuDraw(void *context,float opacity) {
  LanguageEditor *editor=context;
  LunaCubePage page={.title="Language",.count=LUNA_LANGUAGE_COUNT,
      .label=languageSubmenuLabel,.value=languageSubmenuValue,
      .context=editor,.status=editor->status};
  uiCubeSubmenuPageDraw(&page,editor->selected,&editor->focus,opacity);
}
static int languageSubmenuInput(void *context,int input) {
  LanguageEditor *editor=context;
  if(input&(PAD_CIRCLE|PAD_TRIANGLE))return LUNA_CUBE_SUBMENU_BACK;
  if(input&PAD_UP)editor->selected=(editor->selected+LUNA_LANGUAGE_COUNT-1)%LUNA_LANGUAGE_COUNT;
  else if(input&PAD_DOWN)editor->selected=(editor->selected+1)%LUNA_LANGUAGE_COUNT;
  else if(input&PAD_CROSS) {
    if(!lunaLanguageSave((LunaLanguage)editor->selected))return LUNA_CUBE_SUBMENU_BACK;
    editor->status="Could not save language. Check LUNA's folder.";
  }
  return LUNA_CUBE_SUBMENU_STAY;
}

static const char *startMenuStatus(int hasLibrary) {
  int scanFailed = 0, scanIncomplete = 0;
  for (int source = 0; source < STORAGE_SOURCE_COUNT; source++) {
    if (STORAGE_STATUS[source].error) scanFailed = 1;
    if (STORAGE_STATUS[source].incomplete ||
        (STORAGE_STATUS[source].error && STORAGE_STATUS[source].games)) scanIncomplete = 1;
  }
  if (scanIncomplete) return lunaText("Some items could not be scanned");
  if (!hasLibrary) return lunaText(scanFailed ? "Scan failed" : "No games found");
  return NULL;
}

static void waitMenuButtonRelease(void) {
  while (pollInput() & (PAD_CROSS | PAD_CIRCLE | PAD_TRIANGLE | PAD_START))
    usleep(1000);
}

static int uiSystemSettingsLoop(int hasLibrary, Target *target) {
  int selected = 0;
  StorageEditor storage = {0};
  VmcEditor vmc = {0};
  LanguageEditor language = {0};
  const LunaCubeSubmenu submenus[4] = {
    {.context=&vmc,.begin=vmcSubmenuBegin,.draw=vmcSubmenuDraw,
     .input=vmcSubmenuInput,.end=vmcSubmenuEnd},
    {.context=&storage,.begin=storageSubmenuBegin,.draw=storageSubmenuDraw,
     .input=storageSubmenuInput},
    {.context=&language,.begin=languageSubmenuBegin,.draw=languageSubmenuDraw,
     .input=languageSubmenuInput},
    uiDefaultsSubmenu(target)
  };
  for (;;) {
    int action = uiCubeSettingsLoop(&selected, startMenuStatus(hasLibrary), submenus);
    waitMenuButtonRelease();
    switch (action) {
      case LUNA_CUBE_BACK: return -1;
      case LUNA_CUBE_STORAGE:
        return STORAGE_UI_REFRESH;
      case LUNA_CUBE_EXIT: return 1;
      case LUNA_CUBE_SHUTDOWN:
        // The cube menu requires a separate confirmation press before returning.
        powerOffConsole();
        while (1) sleep(1);
      default: return -1;
    }
  }
}

int uiMainMenuLoop(int hasLibrary, Target *target) {
  // Empty libraries still have a metadata device; no selected game is needed.
  Target settingsTarget = {0};
  if (!target) {
    for (int i=0;i<MAX_DEVICES;i++) {
      if (deviceModeMap[i].mountpoint && deviceModeMap[i].mode!=MODE_NONE &&
          deviceModeMap[i].mode!=MODE_ALL) {settingsTarget.device=&deviceModeMap[i];break;}
    }
    target=&settingsTarget;
  }
  LunaAppearance appearance;
  loadAppearance(target,&appearance);
  lunaSpaceSetSpeed(appearance.spaceSpeed);
  lunaSpaceSetDepth(appearance.spaceDepth);
  uiRingSetColor((LunaRingColor)appearance.ringColor);
  for (;;) {
    int action = uiRingMenuLoop(hasLibrary, startMenuStatus(hasLibrary));
    // Consume the activation press before entering another interactive screen.
    waitMenuButtonRelease();
    if (action == LUNA_RING_LIBRARY) return 0;
    if (action == LUNA_RING_BROWSER) uiFileManagerLoop();
    else if (action == LUNA_MENU_APPEARANCE) uiAppearanceLoop(target);
    else {
      int result = uiSystemSettingsLoop(hasLibrary,target);
      if (result >= 0) return result;
    }
  }
}
