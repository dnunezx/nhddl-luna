#ifndef LUNA_UI_GENRES_MENU_H
#define LUNA_UI_GENRES_MENU_H
#include "target.h"
/* Categories and their game-image ring share one browser and Back path.
 * Membership changes are explicit; the caller handles launch after cleanup. */
typedef struct { int genre,inGames; Target *selectedGame; } LunaGenreBrowser;
enum { LUNA_GENRE_BROWSER_BACK, LUNA_GENRE_BROWSER_LAUNCH };
int uiGenresLoop(TargetList *library,Target *assignmentTarget,TargetFilter platform,
                 const unsigned char *favorites,LunaGenreBrowser *state,Target **launchTarget);
#endif
