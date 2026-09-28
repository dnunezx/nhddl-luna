#ifndef LUNA_UI_FILE_MANAGER_H
#define LUNA_UI_FILE_MANAGER_H

// Returns 1 when the user chooses Exit, or 0 to return to the library.
// With no library, the menu remains open until Exit is chosen.
int uiMainMenuLoop(int hasLibrary);

#endif
