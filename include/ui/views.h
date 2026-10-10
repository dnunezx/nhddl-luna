// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_VIEWS_H
#define LUNA_UI_VIEWS_H

#include "target.h"
#include <gsKit.h>
#include <stdint.h>

typedef enum { PS1_CASE_TALL, PS1_CASE_SQUARE } Ps1CaseStyle;
void setPs1CaseStyle(Ps1CaseStyle style);
Ps1CaseStyle getPs1CaseStyle(void);

void calculateCoverArtGeometry(void);
void setClassicArtOverlap(int overlap);
// titles is the active indexed list; flags use canonical Target.idx even in Favorites.
void drawTitleList(TargetList *titles, int selectedTitleIdx, int maxTitlesPerPage,
                   const Target *displayedArtTarget,
                   GSTEXTURE *selectedTitleCover, GSTEXTURE *selectedTitleDisc,
                   const uint8_t *favoriteFlags, int favoritesOnly, int coverPending,
                   int listEntryProgress,
                   uint32_t frameNowMs,
                   const char *nextViewLabel);
void drawPSBBNCollection(TargetList *titles, int selectedTitleIdx, GSTEXTURE **covers,
                         int flowOffset, int outgoingTitleIdx, int favoritesOnly, int fastScrolling,
                         int entryProgress, int entryFade, uint32_t frameNowMs,
                         const char *nextViewLabel);
void drawOrbit(TargetList *titles, int selectedTitleIdx, GSTEXTURE **covers,
               int flowOffset, int randomActive, int entryProgress,
               uint32_t frameNowMs, const char *nextViewLabel);
void resetCaseGrid(void);
void beginCaseGridExit(uint32_t now);
int caseGridExitFinished(uint32_t now);
void setCaseGridPageDirection(int direction);
int caseGridVisibleTitleIndex(void);
void drawCaseGrid(TargetList *titles, int selectedTitleIdx, uint32_t frameNowMs);

#endif
