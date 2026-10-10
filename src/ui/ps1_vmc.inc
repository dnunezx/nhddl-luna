// Uses the same browser rows, controller prompts and keyboard as PS2 VMCs.
#include "psxcore_file.h"
#include "psxcore_shared_store.h"
#include <io_common.h>

typedef struct {
  const char *const *labels;
  int art;
} Ps1Actions;

static int ps1ActionRow(int index, char *name, size_t nameSize, char *detail,
    size_t detailSize, CardArtType *art, void *context) {
  Ps1Actions *menu = context;
  snprintf(name, nameSize, "%s", lunaText(menu->labels[index])); detail[0] = 0;
  *art = menu->art ? CARD_ART_SLOT_1 : CARD_ART_NONE;
  (void)detailSize; return 1;
}
static int ps1Action(const char *title, const char *subtitle, const char *status,
    const char *const *labels, int count, int selected) {
  Ps1Actions menu = {labels, 0}; BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText(title), subtitle, lunaText(status),
        PROMPT_TWO(ICON_CROSS, lunaText("Select"), ICON_TRIANGLE, lunaText("Back")),
        count, selected, browserFirstRow(selected), 0, ps1ActionRow, &menu, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) return -1;
    if (input & PAD_UP) selected = (selected + count - 1) % count;
    else if (input & PAD_DOWN) selected = (selected + 1) % count;
    else if (input & PAD_CROSS) return selected;
  }
}
static void ps1Message(const char *message) {
  static const char *const back[] = {"Back"};
  ps1Action("PlayStation memory cards", "", message, back, 1, 0);
}
static int ps1Confirm(const char *title, const char *detail) {
  static const char *const actions[] = {"Cancel", "Continue"};
  return ps1Action(title, "PlayStation memory cards", detail, actions, 2, 0) == 1;
}
int uiPs1VmcRetry(const char *error) {
  static const char *const actions[] = {"Retry", "Back"};
  return ps1Action("Memory cards unavailable", "", error, actions, 2, 0) == 0;
}
int uiPs1VmcChooseSaves(const char *const *labels, unsigned count) {
  return ps1Action("Choose existing saves", "Automatic memory cards",
      "These cards contain different saves. Choose the pair to use. Originals will be kept and backed up.",
      labels, count, 0);
}
static void ps1StatusFrame(const char *message, PromptBar footer) {
  int left = keepoutArea + 30, right = gsGlobal->Width - left;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  drawBrowserSheet(0);
  drawTextWindow(left, headerHeight - getFontLineHeight(), right, headerHeight + 2,
      0, HeaderTextColor, ALIGN_HCENTER, lunaText("PlayStation memory cards"));
  drawTextWindow(left + 18, headerHeight + 40, right - 18,
      gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_LEFT, lunaText(message));
  drawPromptBar(left + 18, gsGlobal->Height - footerHeight, right,
      gsGlobal->Height, 0, HeaderTextColor, footer);
  presentBrowserFrame();
}
static void ps1Busy(const char *message) {
  ps1StatusFrame(message, PROMPT_TEXT(lunaText("Please wait until the operation finishes.")));
}
static int ps1CancelReleased;
static int ps1Progress(uint64_t done, uint64_t total) {
  char message[128];
  snprintf(message, sizeof(message), "%s %u%%", lunaText("Checking PS1 save identity..."),
      total ? (unsigned)(done * 100 / total) : 0);
  ps1StatusFrame(message, PROMPT_ONE(ICON_TRIANGLE, lunaText("Cancel")));
  int held = pollInput() & (PAD_CIRCLE | PAD_TRIANGLE);
  if (!held) ps1CancelReleased = 1;
  return ps1CancelReleased && held;
}
const char *uiPs1VmcLoad(Target *target, LunaPsxVmcSettings *settings) {
  // The button that opened Options must be released before it can cancel.
  ps1CancelReleased = !(pollInput() & (PAD_CIRCLE | PAD_TRIANGLE));
  ps1Busy("Checking this disc's memory card settings...");
  const char *error = lunaPsxVmcLoad(target, settings, ps1Progress);
  return error ? error : lunaPsxVmcAutomatic(target, settings, 0, ps1Progress, NULL);
}
const char *uiPs1VmcPrepare(Target *target, LunaPsxVmcSettings *settings) {
  ps1CancelReleased = !(pollInput() & (PAD_CIRCLE | PAD_TRIANGLE));
  ps1Busy("Preparing this game's memory cards...");
  const char *error = lunaPsxVmcAutomatic(target, settings, 1, ps1Progress, uiPs1VmcChooseSaves);
  if (!error && !settings->key[0]) error = lunaPsxVmcPrivate(settings, 1);
  return error;
}
int uiPs1VmcPolicy(Target *target, LunaPsxVmcSettings *settings) {
  static const char *const labels[] = {"Automatic (recommended)", "Private cards for this disc", "Choose shared cards"};
  int choice = ps1Action("Memory card behavior", target->name,
      "Automatic keeps private cards for ordinary games and shares cards across verified discs of the same game.",
      labels, 3, settings->mode);
  if (choice < 0) return 0;
  if (choice == LUNA_PS1_CARDS_SHARED) return uiPs1VmcPicker(target, settings);
  int changed = settings->mode != (unsigned)choice;
  settings->mode = choice; memset(settings->key, 0, sizeof(settings->key)); memset(settings->volume, 0, 16);
  return changed;
}
const char *uiPs1VmcCreatePrivate(const LunaPsxVmcSettings *settings) {
  ps1Busy("Creating and verifying missing private cards...");
  return lunaPsxVmcPrivate(settings, 1);
}
static int ps1CollectRoots(BrowserRoot *roots) {
  int count = 0;
  for (int i = 0; i < MAX_DEVICES; ++i) {
    char root[8];
    if (!lunaPsxVmcDevice(&deviceModeMap[i], root)) continue;
    char label[64]; snprintf(label, sizeof(label), "%s (%s)",
        deviceModeMap[i].mode == MODE_USB ? "USB" : "ATA HDD", root);
    count = addRoot(roots, count, root, label);
  }
  return count;
}
static int ps1ChooseRoot(char root[8]) {
  BrowserRoot *roots = calloc(BROWSER_MAX_ROOTS, sizeof(*roots));
  if (!roots) return 0;
  int count = ps1CollectRoots(roots), selected = 0, result = 0;
  BrowserSelector selector = {.selected = -1};
  for (;;) {
    drawBrowserFrameSelected(lunaText("PlayStation memory cards"), lunaText("Choose a drive"),
        count ? lunaText("Choose where the card set will be stored.") : lunaText("No supported local drives found."),
        PROMPT_TWO(ICON_CROSS, lunaText("Select"), ICON_TRIANGLE, lunaText("Back")),
        count, selected, 0, 0, rootRow, roots, &selector);
    int input = readInput();
    if (input & (PAD_CIRCLE | PAD_TRIANGLE)) break;
    if (count && (input & PAD_UP)) selected = (selected + count - 1) % count;
    else if (count && (input & PAD_DOWN)) selected = (selected + 1) % count;
    else if (count && (input & PAD_CROSS)) { snprintf(root, 8, "%.6s", roots[selected].path); result = 1; break; }
  }
  free(roots); return result;
}

typedef struct {
  char key[33], name[64], detail[32];
} Ps1SetEntry;
static int ps1SetRow(int index, char *name, size_t nameSize, char *detail,
    size_t detailSize, CardArtType *art, void *context) {
  Ps1SetEntry *sets = context;
  if (!index) { snprintf(name, nameSize, "%s", lunaText("Create new set")); snprintf(detail, detailSize, "2 x 128 KB"); }
  else { snprintf(name, nameSize, "%s", sets[index - 1].name); snprintf(detail, detailSize, "%s", sets[index - 1].detail); }
  *art = CARD_ART_SLOT_1; return 1;
}
static int ps1LoadSets(const char *root, Ps1SetEntry *sets) {
  char directory[128]; snprintf(directory, sizeof(directory), "%s/POPS/SAVES", root);
  DIR *dir = opendir(directory); if (!dir) return 0;
  int count = 0; struct dirent *entry;
  while (count < 128 && (entry = readdir(dir))) {
    if (!np_shared_key_valid(entry->d_name, NP_SHARED_FILE)) continue;
    char base[128], path[160]; iox_stat_t st;
    snprintf(base, sizeof(base), "%s/%s", directory, entry->d_name);
    int found = !strncmp(entry->d_name, "LUNA_PS1_", 9);
    const char *files[] = {"shared.set", "shared.set.new", "shared.set.prev", "shared.set.bak"};
    for (unsigned i = 0; i < 4 && !found; ++i) {
      snprintf(path, sizeof(path), "%s/%s", base, files[i]); found = fileXioGetStat(path, &st) >= 0;
    }
    if (!found) continue;
    snprintf(sets[count].key, sizeof(sets[count].key), "%s", entry->d_name);
    snprintf(sets[count].name, sizeof(sets[count].name), "%s", entry->d_name);
    NpSharedSet set;
    if (np_shared_store_read(base, &set)) snprintf(sets[count].detail, sizeof(sets[count].detail), "%s", lunaText("Recovery needed"));
    else {
      snprintf(sets[count].name, sizeof(sets[count].name), "%s", set.name);
      snprintf(sets[count].detail, sizeof(sets[count].detail), "%u / 32 discs", (unsigned)set.count);
    }
    ++count;
  }
  closedir(dir); return count;
}
static int ps1Assign(Target *target, LunaPsxVmcSettings *settings, const char *root, const char *key) {
  if (!ps1Confirm("Share these memory cards?", "Both slots will use this set. Other enrolled games use the same cards.")) return 0;
  ps1Busy("Checking and enrolling this disc...");
  unsigned char discVolume[16], saveVolume[16];
  const char *error = lunaPsxVmcVolume(settings->gameRoot, discVolume, 1);
  if (!error) error = lunaPsxVmcVolume(root, saveVolume, 0);
  if (!error) error = lunaPsxVmcEnroll(root, key, settings->disc, 1);
  if (error) { ps1Message(error); return 0; }
  snprintf(settings->key, sizeof(settings->key), "%s", key); memcpy(settings->volume, saveVolume, 16);
  settings->mode = LUNA_PS1_CARDS_SHARED;
  lunaPsxVmcRememberDisc(root, settings->disc, target->name);
  return 1;
}
static int ps1CreateAt(const char *root, Target *target, LunaPsxVmcSettings *settings) {
  static const char *const choices[] = {"Blank cards", "Copy this disc's private cards"};
  int choice = ps1Action("Create new set", root, "Each set contains two 128 KB cards.", choices, target ? 2 : 1, 0);
  if (choice < 0) return 0;
  char name[63] = "LUNA PS1 Cards";
  if (!editFileName(lunaText("Card set name"), name, sizeof(name), sizeof(name) - 1)) return 0;
  if (!ps1Confirm("Create card set?", "LUNA will identify this drive and create a new pair. Existing cards are preserved.")) return 0;
  ps1Busy(choice ? "Copying and verifying both private cards..." : "Creating and verifying both cards...");
  NpSharedSet set;
  const char *error = lunaPsxVmcCreate(root, name, choice ? settings : NULL, &set);
  if (error) { ps1Message(error); return 0; }
  if (target) return ps1Assign(target, settings, root, set.key);
  ps1Message("Created two formatted 128 KB cards."); return 0;
}
int uiPs1VmcCreate(Target *target, LunaPsxVmcSettings *settings) {
  char root[8]; if (!ps1ChooseRoot(root)) return 0;
  return ps1CreateAt(root, target, settings);
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
        status[0] ? status : folder ? lunaText("Square chooses this folder. Cross opens a folder.") : lunaText("Choose a raw 128 KB memory card image."),
        folder && active >= 0 ? PROMPT_TWO(ICON_SQUARE, lunaText("Choose folder"), ICON_TRIANGLE, lunaText("Back")) :
        PROMPT_TWO(ICON_CROSS, lunaText("Open/Select"), ICON_TRIANGLE, lunaText("Back")),
        active < 0 ? rootCount : count, selected, browserFirstRow(selected), 0,
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
    else if (folder && active >= 0 && (input & PAD_SQUARE)) {
      if (strlen(path) >= 256) snprintf(status, sizeof(status), "%s", lunaText("The current path is too long."));
      else { snprintf(out, 256, "%s", path); result = 1; break; }
    } else if (total && (input & PAD_CROSS)) {
      if (active < 0) { active = selected; snprintf(path, sizeof(path), "%s", roots[active].path); }
      else {
        char child[PATH_MAX + 1];
        if (!joinPath(child, sizeof(child), path, entries[selected].name)) continue;
        if (!entries[selected].isDirectory) {
          if (folder) continue;
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

static void ps1CardActions(const char *base, int slot) {
  static const char *const actions[] = {"Export card", "Import card", "Back up both cards"};
  char title[64], source[256], destination[256];
  snprintf(title, sizeof(title), "Slot %d - 128 KB", slot + 1);
  for (;;) {
    int action = ps1Action("PlayStation memory cards", title,
        "Import replaces this slot for every game using the set. A backup is made first.", actions, 3, 0);
    if (action < 0) return;
    const char *error = NULL;
    if (action == 0) {
      if (!ps1PickPath(1, "Export card", destination)) continue;
      char name[64]; snprintf(name, sizeof(name), "%s_slot%d.bin", strrchr(base, '/') + 1, slot + 1);
      if (!editFileName(lunaText("Export filename"), name, sizeof(name), sizeof(name) - 1)) continue;
      char path[256];
      if (!joinPath(path, sizeof(path), destination, name)) { ps1Message("The export path is too long."); continue; }
      snprintf(source, sizeof(source), "%s/card%d", base, slot);
      ps1Busy("Exporting and verifying the memory card..."); error = lunaPsxVmcExport(source, path);
    } else if (action == 1) {
      if (!ps1PickPath(0, "Import card", source)) continue;
      if (!ps1Confirm("Replace this card?", "This affects every game using the set. Both current cards will be backed up first.")) continue;
      ps1Busy("Backing up and importing the memory card..."); error = lunaPsxVmcReplace(base, source, slot);
    } else { ps1Busy("Backing up both cards and their metadata..."); error = lunaPsxVmcBackup(base, destination); }
    ps1Message(error ? error : action == 0 ? "Card exported and verified." : action == 1 ? "Card imported. The previous cards were backed up." : "Both cards and their metadata were backed up.");
  }
}

static void ps1Members(const char *base) {
  NpSharedSet set;
  if (np_shared_store_read(base, &set)) { ps1Message("The set is invalid or needs recovery."); return; }
  char root[8]; snprintf(root, sizeof(root), "%.6s", base);
  while (set.count) {
    char names[NP_SHARED_MEMBERS][128]; const char *labels[NP_SHARED_MEMBERS];
    for (unsigned i = 0; i < set.count; ++i) { lunaPsxVmcDiscName(root, set.members[i], names[i], sizeof(names[i])); labels[i] = names[i]; }
    int selected = ps1Action("Enrolled games", set.name,
        "Select a disc to remove its enrollment. Its saved card contents are preserved.", labels, set.count, 0);
    if (selected < 0) return;
    if (!ps1Confirm("Remove this enrollment?", "This disc will need another card selection before launching. Its saves remain on these cards.")) continue;
    ps1Busy("Updating the enrolled games...");
    const char *error = lunaPsxVmcEnroll(root, set.key, set.members[selected], 0);
    if (error) { ps1Message(error); return; }
    if (np_shared_store_read(base, &set)) { ps1Message("The set needs recovery."); return; }
  }
  ps1Message("No games are enrolled in this set yet.");
}
static void ps1ManageBase(const char *base, int initialSlot, int shared) {
  if (initialSlot >= 0) { ps1CardActions(base, initialSlot); return; }
  static const char *const actions[] = {"Slot 1", "Slot 2", "Back up both cards", "Restore backup", "Rename set", "Enrolled games"};
  for (;;) {
    char display[64]; snprintf(display, sizeof(display), "%s", lunaText("Private cards"));
    if (shared) {
      NpSharedSet set;
      if (!np_shared_store_read(base, &set)) snprintf(display, sizeof(display), "%s", set.name);
      else snprintf(display, sizeof(display), "%s", lunaText("Recovery needed"));
    }
    int action = ps1Action("PlayStation memory cards", display,
        shared ? "Both cards are shared by every enrolled game." : "These cards belong to this disc.", actions, shared ? 6 : 4, 0);
    if (action < 0) return;
    if (action < 2) { ps1CardActions(base, action); continue; }
    char path[256]; const char *error = NULL;
    if (action == 2) { ps1Busy("Backing up both cards and their metadata..."); error = lunaPsxVmcBackup(base, path); }
    else if (action == 3) {
      if (!ps1PickPath(1, "Choose backup folder", path)) continue;
      char complete[280]; iox_stat_t st; snprintf(complete, sizeof(complete), "%s/backup.ok", path);
      if (fileXioGetStat(complete, &st) < 0) { ps1Message("Choose a completed LUNA backup folder."); continue; }
      if (!ps1Confirm("Restore both cards?", "Current cards will be backed up. This replaces both slots; the current enrollment stays unchanged.")) continue;
      ps1Busy("Backing up and restoring both cards..."); error = lunaPsxVmcReplace(base, path, -1);
    } else if (action == 4) {
      NpSharedSet set;
      if (np_shared_store_read(base, &set)) { ps1Message("The set is invalid or needs recovery."); continue; }
      char name[63]; snprintf(name, sizeof(name), "%s", set.name);
      if (!editFileName(lunaText("Card set name"), name, sizeof(name), sizeof(name) - 1)) continue;
      ps1Busy("Saving the card set name..."); error = lunaPsxVmcRename(base, name);
    } else { ps1Members(base); continue; }
    ps1Message(error ? error : action == 2 ? "Both cards and their metadata were backed up." : action == 3 ? "Both cards were restored. The previous cards were backed up." : "Card set renamed.");
  }
}
void uiPs1VmcManage(const LunaPsxVmcSettings *settings, int slot) {
  char base[128]; const char *error = lunaPsxVmcBase(settings, base);
  if (error) { ps1Message(error); return; }
  ps1ManageBase(base, slot, settings->key[0] != 0);
}
void uiPs1VmcDescribe(const LunaPsxVmcSettings *settings, char name[64], char slots[2][32]) {
  snprintf(name, 64, "%s", settings->key[0] ? settings->key : lunaText("Private cards"));
  char base[128]; const char *error = lunaPsxVmcBase(settings, base);
  if (!error && settings->key[0]) {
    NpSharedSet set;
    if (!np_shared_store_read(base, &set)) {
      snprintf(name, 64, "%s", set.name);
      if (np_shared_match(&set, NP_SHARED_FILE, settings->key, settings->volume, settings->disc)) error = "Not enrolled";
    } else error = "Recovery needed";
  }
  for (unsigned i = 0; i < 2; ++i) {
    char path[160]; iox_stat_t st; unsigned char hash[32];
    snprintf(path, sizeof(path), "%s/card%u", error ? "" : base, i);
    const char *status = error ? "Unavailable" : fileXioGetStat(path, &st) < 0 ? "Missing" :
        lunaPsxVmcCard(path, hash) ? "Invalid" : "Ready";
    if (!error) {
      snprintf(path, sizeof(path), "%s/card%u.new", base, i); if (fileXioGetStat(path, &st) >= 0) status = "Recovery needed";
      snprintf(path, sizeof(path), "%s/card%u.bak", base, i); if (fileXioGetStat(path, &st) >= 0) status = "Recovery needed";
    }
    if (settings->mode == LUNA_PS1_CARDS_AUTO && !strcmp(status, "Missing"))
      snprintf(slots[i], 32, "%s", lunaText("Created at launch"));
    else snprintf(slots[i], 32, "128 KB - %s", lunaText(status));
  }
  if (settings->mode == LUNA_PS1_CARDS_AUTO) {
    snprintf(name, 64, "%s", lunaText("Automatic"));
  }
}
static int ps1BrowseSets(Target *target, LunaPsxVmcSettings *settings) {
  char root[8];
  while (ps1ChooseRoot(root)) {
    Ps1SetEntry *sets = calloc(128, sizeof(*sets)); if (!sets) return 0;
    ps1Busy("Reading the memory card sets..."); int count = ps1LoadSets(root, sets), selected = 0;
    BrowserSelector selector = {.selected = -1};
    for (;;) {
      drawBrowserFrameSelected(lunaText("PlayStation memory cards"), root,
          target ? lunaText("Choose a shared pair for both slots. Press Start in game settings to save.") : lunaText("Create a pair, or open one to manage its cards and enrolled games."),
          PROMPT_TWO(ICON_CROSS, target ? lunaText("Assign/Create") : lunaText("Open/Create"), ICON_TRIANGLE, lunaText("Back")),
          count + 1, selected, browserFirstRow(selected), 128, ps1SetRow, sets, &selector);
      int input = readInput();
      if (input & (PAD_CIRCLE | PAD_TRIANGLE)) break;
      if (input & PAD_UP) selected = (selected + count) % (count + 1);
      else if (input & PAD_DOWN) selected = (selected + 1) % (count + 1);
      else if (input & PAD_CROSS) {
        int changed = 0;
        if (!selected) changed = ps1CreateAt(root, target, settings);
        else if (target) changed = ps1Assign(target, settings, root, sets[selected - 1].key);
        else {
          char base[128]; snprintf(base, sizeof(base), "%s/POPS/SAVES/%s", root, sets[selected - 1].key);
          ps1ManageBase(base, -1, 1);
        }
        if (changed) { free(sets); return 1; }
        ps1Busy("Reading the memory card sets..."); count = ps1LoadSets(root, sets);
        if (selected > count) selected = count; selector.selected = -1;
      }
    }
    free(sets);
  }
  return 0;
}
int uiPs1VmcPicker(Target *target, LunaPsxVmcSettings *settings) {
  return ps1BrowseSets(target, settings);
}

static void uiVMCPlatformLoop(void) {
  static const char *const platforms[] = {"PlayStation", "PlayStation 2"};
  for (;;) {
    int selected = ps1Action("Virtual Memory Cards", "", "Choose a console.", platforms, 2, 0);
    if (selected < 0) return;
    if (!selected) ps1BrowseSets(NULL, NULL);
    else uiVMCManagerLoop();
  }
}
