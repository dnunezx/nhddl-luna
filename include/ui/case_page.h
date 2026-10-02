// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_CASE_PAGE_H
#define LUNA_UI_CASE_PAGE_H

#include "ui/navigation.h"

#define CASE_PAGE_OUT_MS 160U
#define CASE_PAGE_IN_MS 190U
#define CASE_PAGE_ROW_DELAY_MS 12U

enum { CASE_PAGE_IDLE, CASE_PAGE_OUT, CASE_PAGE_IN };

typedef struct {
  int visibleIdx, targetIdx, direction, stage;
  uint32_t startMs;
  int startOffset[CASE_GRID_ROWS], startVisibility[CASE_GRID_ROWS];
  int startPreviewVisibility;
} LunaCasePage;

void lunaCasePageReset(LunaCasePage *page);
void lunaCasePageUpdate(LunaCasePage *page, int selectedIdx, int direction, uint32_t now);
void lunaCasePageRow(const LunaCasePage *page, int row, uint32_t now,
                     int *offset, int *visibility);
int lunaCasePagePreviewVisibility(const LunaCasePage *page, uint32_t now);

#endif
