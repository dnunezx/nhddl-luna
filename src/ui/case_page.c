// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/case_page.h"
#include <string.h>

static int motionProgress(const LunaCasePage *page, int row, uint32_t now) {
  if (page->stage == CASE_PAGE_IDLE)
    return 1000;
  uint32_t duration = page->stage == CASE_PAGE_OUT ? CASE_PAGE_OUT_MS : CASE_PAGE_IN_MS;
  uint32_t delay = row * CASE_PAGE_ROW_DELAY_MS;
  uint32_t elapsed = now - page->startMs;
  if (elapsed <= delay)
    return 0;
  if (elapsed >= duration)
    return 1000;
  return lunaNavEase((int)((elapsed - delay) * 1000U / (duration - delay)));
}

void lunaCasePageRow(const LunaCasePage *page, int row, uint32_t now,
                     int *offset, int *visibility) {
  if (page->stage == CASE_PAGE_IDLE) {
    *offset = 0;
    *visibility = 1000;
    return;
  }
  int progress = motionProgress(page, row, now);
  int endOffset = page->stage == CASE_PAGE_OUT ? -page->direction * 1000 : 0;
  int endVisibility = page->stage == CASE_PAGE_OUT ? 0 : 1000;
  *offset = page->startOffset[row] + (endOffset - page->startOffset[row]) * progress / 1000;
  *visibility = page->startVisibility[row] +
      (endVisibility - page->startVisibility[row]) * progress / 1000;
}

int lunaCasePagePreviewVisibility(const LunaCasePage *page, uint32_t now) {
  if (page->stage == CASE_PAGE_IDLE)
    return 1000;
  int target = page->stage == CASE_PAGE_OUT ? 0 : 1000;
  return page->startPreviewVisibility +
      (target - page->startPreviewVisibility) * motionProgress(page, 0, now) / 1000;
}

static void retargetMotion(LunaCasePage *page, int stage, int direction, uint32_t now) {
  for (int row = 0; row < CASE_GRID_ROWS; row++)
    lunaCasePageRow(page, row, now, &page->startOffset[row], &page->startVisibility[row]);
  page->startPreviewVisibility = lunaCasePagePreviewVisibility(page, now);
  page->stage = stage;
  page->direction = direction;
  page->startMs = now;
}

void lunaCasePageReset(LunaCasePage *page) {
  memset(page, 0, sizeof(*page));
  page->visibleIdx = page->targetIdx = -1;
}

void lunaCasePageUpdate(LunaCasePage *page, int selectedIdx, int direction, uint32_t now) {
  if (selectedIdx < 0)
    return;
  if (page->visibleIdx < 0)
    page->visibleIdx = selectedIdx;
  direction = direction < 0 ? -1 : 1;
  page->targetIdx = selectedIdx;
  if (selectedIdx / CASE_GRID_PAGE_SIZE == page->visibleIdx / CASE_GRID_PAGE_SIZE) {
    if (page->stage == CASE_PAGE_OUT)
      retargetMotion(page, CASE_PAGE_IN, direction, now);
    page->visibleIdx = selectedIdx;
  } else if (page->stage != CASE_PAGE_OUT || page->direction != direction) {
    // Retarget from the visible shelf's current position, never queue pages.
    retargetMotion(page, CASE_PAGE_OUT, direction, now);
  }
  if (page->stage == CASE_PAGE_OUT && now - page->startMs >= CASE_PAGE_OUT_MS) {
    page->visibleIdx = page->targetIdx;
    page->stage = CASE_PAGE_IN;
    page->startMs += CASE_PAGE_OUT_MS;
    for (int row = 0; row < CASE_GRID_ROWS; row++) {
      page->startOffset[row] = page->direction * 1000;
      page->startVisibility[row] = 0;
    }
    page->startPreviewVisibility = 0;
  }
  if (page->stage == CASE_PAGE_IN && now - page->startMs >= CASE_PAGE_IN_MS)
    page->stage = CASE_PAGE_IDLE;
}
