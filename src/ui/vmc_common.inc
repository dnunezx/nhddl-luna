static int vmcVcdFilename(const char *path) { const char *ext=strrchr(path,'.');return ext && !strcasecmp(ext,".vcd"); }
static int ps1ActionRow(int index, char *name, size_t nameSize, char *detail,
    size_t detailSize, void *context) {
  const char *const *labels = context;
  snprintf(name, nameSize, "%s", lunaText(labels[index])); detail[0] = 0;
  (void)detailSize; return 1;
}
static int ps1Action(const char *title, const char *subtitle, const char *status,
    const char *const *labels, int count, int selected) {
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText(title), subtitle, lunaText(status),
        PROMPT_TWO(ICON_CROSS, lunaText("Select"), ICON_TRIANGLE, lunaText("Back")),
        count, selected, browserFirstRow(selected), ps1ActionRow, (void *)labels, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) return -1;
    if (input & PAD_UP) selected = (selected + count - 1) % count;
    else if (input & PAD_DOWN) selected = (selected + 1) % count;
    else if (input & PAD_CROSS) return selected;
  }
}
static void ps1Message(const char *message) {
  static const char *const back[] = {"Back"};
  ps1Action("Virtual Memory Cards", "", message, back, 1, 0);
}
static int ps1Confirm(const char *title, const char *detail) {
  static const char *const actions[] = {"Cancel", "Continue"};
  return ps1Action(title, "Virtual Memory Cards", detail, actions, 2, 0) == 1;
}
static void ps1StatusFrame(const char *message, PromptBar footer) {
  if(browserPageSection) {
    LunaCubePageFocus focus={.selected=-1};
    LunaCubePage page={.title="Virtual Memory Cards",.sectionTitle=browserPageSection,
        .count=-1,.status=message,.libraryFooter=1};
    uiCubeHubPageDraw(&page,0,&focus);
    drawBrowserPageFooter(footer);
    presentBrowserFrame();
    return;
  }
  int left = keepoutArea + 30, right = gsGlobal->Width - left;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(0);
  drawTextWindow(left, headerHeight - getFontLineHeight(), right, headerHeight + 2,
      0, HeaderTextColor, ALIGN_HCENTER, lunaText("Virtual Memory Cards"));
  drawTextWindow(left + 18, headerHeight + 40, right - 18,
      gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT, lunaText(message));
  drawPromptBar(left + 18, gsGlobal->Height - footerHeight, right,
      gsGlobal->Height, 0, HeaderTextColor, footer);
  presentBrowserFrame();
}
static void ps1Busy(const char *message) {
  ps1StatusFrame(message, PROMPT_TEXT(lunaText("Please wait until the operation finishes.")));
}

// A bounded, ordinary storage picker; imports are validated after selection.
static int ps1PickPath(int folder, const char *heading, char out[256]) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  BrowserEntry *entries = calloc(BROWSER_MAX_ENTRIES, sizeof(*entries));
  if (!roots || !entries) { free(roots); free(entries); return 0; }
  int rootCount = collectRoots(roots), active = -1, selected = 0, count = 0, truncated = 0, result = 0;
  char path[PATH_MAX + 1] = "", status[128] = "";
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText(heading), active < 0 ? lunaText("Choose a drive") : path,
        status[0] ? status : folder==1 ? lunaText("Square chooses this folder. Cross opens a folder.") :
        folder==2 ? lunaText("Choose a VCD on the game's drive.") : lunaText("Choose a raw memory card image."),
        folder==1 && active >= 0 ? PROMPT_TWO(ICON_SQUARE, lunaText("Choose folder"), ICON_TRIANGLE, lunaText("Back")) :
        PROMPT_TWO(ICON_CROSS, lunaText("Open/Select"), ICON_TRIANGLE, lunaText("Back")),
        active < 0 ? rootCount : count, selected, browserFirstRow(selected),
        active < 0 ? rootRow : fileRow, active < 0 ? (void *)roots : (void *)entries, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) {
      if (active < 0) break;
      size_t length = strlen(path); while (length && path[length - 1] == '/') path[--length] = 0;
      char *slash = strrchr(path, '/');
      if (!slash || slash < strchr(path, ':')) { clearEntries(entries, count); count = 0; active = -1; }
      else { *slash = 0; loadDirectory(path, entries, &count, &truncated); }
      selected = 0; selector.selected = -1; status[0] = 0; continue;
    }
    int total = active < 0 ? rootCount : count;
    if (total && (input & PAD_UP)) selected = (selected + total - 1) % total;
    else if (total && (input & PAD_DOWN)) selected = (selected + 1) % total;
    else if (folder==1 && active >= 0 && (input & PAD_SQUARE)) {
      if (strlen(path) >= 256) snprintf(status, sizeof(status), "%s", lunaText("The current path is too long."));
      else { snprintf(out, 256, "%s", path); result = 1; break; }
    } else if (total && (input & PAD_CROSS)) {
      if (active < 0) { active = selected; snprintf(path, sizeof(path), "%s", roots[active].path); }
      else {
        char child[PATH_MAX + 1];
        if (!joinPath(child, sizeof(child), path, entries[selected].name)) continue;
        if (!entries[selected].isDirectory) {
          if (folder==1 || (folder==2 && !vmcVcdFilename(child))) continue;
          if (strlen(child) >= 256) { snprintf(status, sizeof(status), "%s", lunaText("The current path is too long.")); continue; }
          snprintf(out, 256, "%s", child); result = 1; break;
        }
        snprintf(path, sizeof(path), "%s", child);
      }
      loadDirectory(path, entries, &count, &truncated); selected = 0; selector.selected = -1;
      snprintf(status, sizeof(status), "%s", truncated ? lunaText("Only the first 512 entries are shown.") : "");
    }
  }
  clearEntries(entries, count); free(entries); free(roots); return result;
}



typedef int (*VmcAssignCard)(const char *path, void *context);
static int vmcCardMenu(int platform,const char *path,VmcAssignCard assign,void *context);
static int vmcBrowse(int platform,const char *root,VmcAssignCard assign,void *context);
