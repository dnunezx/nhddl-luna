// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_GAME_INFO_H
#define LUNA_UI_GAME_INFO_H

typedef struct {
  char release[64];
  char developer[128];
  char genre[192];
  char players[64];
  char description[1024];
} LunaGameInfo;

// Read only display metadata from an OPL CFG; never apply launch settings.
// Missing, empty or unreadable files return zero and clear the result.
int lunaReadGameInfo(const char *path, LunaGameInfo *info);

#endif
