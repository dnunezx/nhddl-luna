#ifndef LUNA_CUBE_MENU_H
#define LUNA_CUBE_MENU_H
#include <stdint.h>

// Native-size menu text, fitted at UTF-8 boundaries with an ellipsis when needed.
void uiCubeMenuTextDraw(int left, int top, int right, int z, uint64_t color,
                        const char *text, int rightAligned);
void uiCubeMenuHelpDraw(int left,int right,uint64_t color,const char *text);
struct PromptBar;
void uiCubeSubmenuFooterDraw(const struct PromptBar *bar,float opacity);
enum { LUNA_CUBE_BACK, LUNA_CUBE_VMC, LUNA_CUBE_STORAGE,
       LUNA_CUBE_LANGUAGE, LUNA_CUBE_EXIT, LUNA_CUBE_SHUTDOWN, LUNA_CUBE_DEFAULTS };
typedef struct {
  const char *title;
  int count; // -1 leaves the list area free for custom controls.
  const char *(*label)(void *context, int row);
  const char *(*value)(void *context, int row);
  const char *(*description)(void *context, int row);
  void (*adjust)(void *context, int row, int direction);
  void *context;
  const char *status;
  const char *sectionTitle;
  const char *subtitle;
  int libraryFooter;
  int stackedValues;
  // Optional tab labels; the caller owns which tab is accessible.
  const char *const *tabs;
  int tabCount, selectedTab;
} LunaCubePage;
enum { LUNA_CUBE_SUBMENU_STAY, LUNA_CUBE_SUBMENU_BACK, LUNA_CUBE_SUBMENU_DONE };
typedef struct {
  void *context;
  void (*begin)(void *context);
  void (*draw)(void *context, float opacity);
  int (*input)(void *context, int buttons);
  void (*end)(void *context);
} LunaCubeSubmenu;
typedef struct {
  const char *title;
  const char *const *labels;
  int count;
  const LunaCubeSubmenu *submenus;
  int submenuCount;
  int confirmRow; // -1 when no category needs a confirmation press.
  const char *confirmText;
  void *context;
  const char *(*description)(void *context, int row);
  const char *subtitle;
  int libraryFooter;
  // Optional live scene. Return nonzero once the closing scene is restored.
  int (*background)(void *context, int closing);
  int (*input)(void *context, int buttons);
  void (*footer)(void *context, float pageProgress);
  int gameOptionsLogo;
} LunaCubeHub;
// One crystal per row, up to six. Returns the chosen row, or -1 for Back.
int uiCubeHubLoop(int *selected, const char *status, const LunaCubeHub *hub);
// Redraw the active hub or page beneath a blocking picker, without presenting.
void uiCubeHubDrawBackdrop(void);
// CubeGlass panel in screen pixels; borrows scratch pages after the live scene.
// Nested sheets borrow the active hub's materials; Close releases standalone panels only.
void uiCubePanelGlassDraw(int left, int top, int right, int bottom, int z, int fillAlpha);
void uiCubePanelGlassClose(void);
int uiCubeQuickMenuGlassDraw(int left, int top, int right, int bottom);
void uiCubeQuickMenuGlassClose(void);
// Game Options uses the library footer's margins, height, and prompt spacing.
void uiCubeOptionsFooterDraw(const char *action, float opacity);
void uiCubeOptionsFooterTransitionDraw(float libraryProgress, float pageProgress);
typedef struct {
  int selected,first,fromY,toY;
  unsigned int startMs;
  int glideMs;
} LunaCubePageFocus;
// Replace the active page's contents while retaining its expanded crystal.
void uiCubeHubPageDraw(const LunaCubePage *page, int selected,
                       LunaCubePageFocus *focus);
// Draw page contents into the shared settings scene without presenting a frame.
void uiCubeSubmenuPageDraw(const LunaCubePage *page, int selected,
                         LunaCubePageFocus *focus, float opacity);
void uiCubeSubmenuFocusDraw(int left, int top, int right, int bottom, float opacity);
// The first four settings rows supply VMC, Storage, Language, and Defaults.
int uiCubeSettingsLoop(int *selected, const char *status,
                      const LunaCubeSubmenu submenus[4]);
#endif
