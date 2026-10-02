// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/case_page.h"
#include <assert.h>
#include <stdio.h>

static void checkRow(const LunaCasePage *page, int row, uint32_t now,
                      int expectedOffset, int expectedVisibility) {
  int offset, visibility;
  lunaCasePageRow(page, row, now, &offset, &visibility);
  assert(offset == expectedOffset);
  assert(visibility == expectedVisibility);
}

int main(void) {
  LunaCasePage page;
  lunaCasePageReset(&page);
  lunaCasePageUpdate(&page, 5, 1, 100);
  assert(page.stage == CASE_PAGE_IDLE && page.visibleIdx == 5);
  lunaCasePageUpdate(&page, 6, 1, 101);
  assert(page.stage == CASE_PAGE_IDLE && page.visibleIdx == 6);

  // Keep the visible title actionable until the outgoing shelf is hidden.
  lunaCasePageUpdate(&page, 30, 1, 200);
  assert(page.stage == CASE_PAGE_OUT && page.visibleIdx == 6);
  checkRow(&page, 0, 200, 0, 1000);
  checkRow(&page, 0, 280, -500, 500);
  checkRow(&page, CASE_GRID_ROWS - 1, 236, 0, 1000);
  assert(lunaCasePagePreviewVisibility(&page, 280) == 500);
  lunaCasePageUpdate(&page, 30, 1, 360);
  assert(page.stage == CASE_PAGE_IN && page.visibleIdx == 30);
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    checkRow(&page, row, 360, 1000, 0);
  lunaCasePageUpdate(&page, 30, 1, 550);
  assert(page.stage == CASE_PAGE_IDLE);
  checkRow(&page, 3, 550, 0, 1000);

  // Explicit input direction handles both two-page and end-to-start wraps.
  lunaCasePageUpdate(&page, 6, -1, 600);
  checkRow(&page, 0, 680, 500, 500);
  lunaCasePageUpdate(&page, 6, -1, 760);
  checkRow(&page, 0, 760, -1000, 0);
  lunaCasePageUpdate(&page, 6, -1, 950);

  // Multiple requests replace the destination without delaying the swap.
  lunaCasePageUpdate(&page, 30, 1, 1000);
  lunaCasePageUpdate(&page, 54, 1, 1040);
  lunaCasePageUpdate(&page, 77, 1, 1080);
  assert(page.startMs == 1000 && page.visibleIdx == 6);
  lunaCasePageUpdate(&page, 77, 1, 1160);
  assert(page.visibleIdx == 77);
  lunaCasePageUpdate(&page, 77, 1, 1350);

  // Reverse before the midpoint without a position or opacity jump.
  lunaCasePageUpdate(&page, 6, 1, 1400);
  int offset[CASE_GRID_ROWS], visibility[CASE_GRID_ROWS];
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    lunaCasePageRow(&page, row, 1470, &offset[row], &visibility[row]);
  int preview = lunaCasePagePreviewVisibility(&page, 1470);
  lunaCasePageUpdate(&page, 77, -1, 1470);
  assert(page.stage == CASE_PAGE_IN && page.visibleIdx == 77);
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    checkRow(&page, row, 1470, offset[row], visibility[row]);
  assert(lunaCasePagePreviewVisibility(&page, 1470) == preview);
  lunaCasePageUpdate(&page, 77, -1, 1660);
  assert(page.stage == CASE_PAGE_IDLE);

  // A request during entrance begins at the partially visible positions.
  lunaCasePageUpdate(&page, 6, 1, 1700);
  lunaCasePageUpdate(&page, 6, 1, 1860);
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    lunaCasePageRow(&page, row, 1920, &offset[row], &visibility[row]);
  lunaCasePageUpdate(&page, 30, 1, 1920);
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    checkRow(&page, row, 1920, offset[row], visibility[row]);
  lunaCasePageUpdate(&page, 30, 1, 2080);
  assert(page.visibleIdx == 30);

  // Frame stalls and the unsigned millisecond timer wrap do not queue motion.
  lunaCasePageReset(&page);
  lunaCasePageUpdate(&page, 0, 1, UINT32_MAX - 100U);
  lunaCasePageUpdate(&page, 24, 1, UINT32_MAX - 90U);
  lunaCasePageUpdate(&page, 24, 1, 300);
  assert(page.stage == CASE_PAGE_IDLE && page.visibleIdx == 24);
  lunaCasePageUpdate(&page, -1, -1, 301);
  assert(page.visibleIdx == 24);
  puts("3D page transition tests passed");
  return 0;
}
