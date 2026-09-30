#ifndef LUNA_UI_FILE_MANAGER_H
#define LUNA_UI_FILE_MANAGER_H

// Returns 1 for Exit, 0 for the library, or STORAGE_UI_REFRESH for a rescan.
// With no library, the menu remains open until Exit is chosen.
int uiMainMenuLoop(int hasLibrary);

#endif
