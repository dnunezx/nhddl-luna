// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/navigation.h"
#include <stddef.h>

int lunaNavEntryInputBlocked(int *pending, int viewReady, int controlsHeld) {
  if (!*pending)
    return 0;
  if (viewReady && !controlsHeld)
    *pending = 0;
  return 1;
}
int lunaQuickMenuUpdate(LunaQuickMenu *menu, int held, int controlsHeld,
                        int count, int shortcut) {
  if (!held) {
    menu->open = 0;
    if (!controlsHeld) {
      menu->captured = 0;
      menu->consumed = 0;
    }
    return -1;
  }
  menu->captured = 1;
  if (menu->consumed)
    return -1;
  menu->open = 1;
  if (shortcut < 0 || shortcut >= count)
    return -1;
  menu->open = 0;
  menu->consumed = 1;
  return shortcut;
}

int lunaCollectionScanUpdate(LunaCollectionScan *scan, int direction, uint32_t now,
                             uint32_t stepMs) {
  if (direction != scan->heldDirection) {
    int keepScanning = scan->active && direction != 0;
    scan->heldDirection = direction;
    scan->active = keepScanning;
    scan->holdStartMs = now;
    scan->nextStepMs = now;
  }
  if (!direction)
    return 0;
  if (!scan->active) {
    if (now - scan->holdStartMs < COLLECTION_SCAN_HOLD_MS)
      return 0;
    scan->active = 1;
  }
  if ((int32_t)(now - scan->nextStepMs) < 0)
    return 0;
  // Skip missed steps after a slow artwork load instead of jumping past covers.
  scan->nextStepMs = now + stepMs;
  return direction;
}

int lunaScrollFastUpdate(LunaScrollFast *fast, int direction, uint32_t now) {
  if (direction != fast->heldDirection) {
    fast->heldDirection = direction;
    fast->active = 0;
    fast->holdStartMs = now;
    fast->nextStepMs = now;
  }
  if (!direction)
    return 0;
  if (!fast->active) {
    if (now - fast->holdStartMs < SCROLL_FAST_HOLD_MS)
      return 0;
    fast->active = 1;
  }
  if ((int32_t)(now - fast->nextStepMs) < 0)
    return 0;
  fast->nextStepMs = now + SCROLL_FAST_STEP_MS;
  return direction;
}

int lunaNavWrap(int total, int index) {
  if (total <= 0)
    return -1;
  index %= total;
  return (index < 0) ? index + total : index;
}

int lunaNavRepeatStep(LunaNavRepeatState *state, int direction, uint32_t now,
                      uint32_t initialDelayMs, uint32_t intervalMs) {
  if (direction == 0) {
    state->direction = 0;
    return 0;
  }
  if (direction != state->direction) {
    state->direction = direction;
    state->nextStepMs = now + initialDelayMs;
    return 1;
  }
  if ((int32_t)(now - state->nextStepMs) < 0)
    return 0;
  // Do not queue missed repeats after a slow artwork decode or frame.
  state->nextStepMs = now + intervalMs;
  return 1;
}

static int gridVertical(int total, int index, int direction, int columns) {
  int candidate;
  int column;

  if (total <= 0 || index < 0 || index >= total || direction == 0)
    return index;
  candidate = index + direction * columns;
  column = index % columns;
  if (candidate >= 0 && candidate < total)
    return candidate;
  if (direction < 0) {
    int last = total - 1;
    candidate = last - ((last - column) % columns);
    return (candidate >= 0) ? candidate : index;
  }
  return (column < total) ? column : total - 1;
}

int lunaNavCaseGridVertical(int total, int index, int direction) {
  return gridVertical(total, index, direction, CASE_GRID_COLUMNS);
}

static int gridPage(int total, int index, int direction, int pageSize) {
  int pageCount;
  int page;
  int cell;
  int targetPage;
  int candidate;

  if (total <= 0 || index < 0 || index >= total)
    return index;
  pageCount = (total + pageSize - 1) / pageSize;
  page = index / pageSize;
  cell = index % pageSize;
  targetPage = lunaNavWrap(pageCount, page + direction);
  candidate = targetPage * pageSize + cell;
  return (candidate < total) ? candidate : total - 1;
}

int lunaNavCaseGridPage(int total, int index, int direction) {
  return gridPage(total, index, direction, CASE_GRID_PAGE_SIZE);
}

int lunaNavDirection(int total, int fromIdx, int toIdx) {
  int forwardDistance;
  int backwardDistance;

  if (total <= 0 || fromIdx < 0 || fromIdx == toIdx)
    return 0;
  forwardDistance = lunaNavWrap(total, toIdx - fromIdx);
  backwardDistance = lunaNavWrap(total, fromIdx - toIdx);
  return (forwardDistance <= backwardDistance) ? 1 : -1;
}

int lunaNavCollectionForeground(const int *positions, const uint8_t *drawable,
                                int count) {
  int foreground = -1;
  // A queued selection can still be in the upcoming stack. The cover at
  // focus or leaving toward the viewer owns the foreground until it clears.
  for (int i = 0; i < count; i++) {
    if (drawable[i] && positions[i] <= 0 &&
        (foreground < 0 || positions[i] > positions[foreground]))
      foreground = i;
  }
  return foreground;
}

int lunaNavEase(int progress) {
  int inverse = 1000 - progress;
  return 1000 - (int)(((int64_t)inverse * inverse * (3000 - (2 * inverse))) /
                      1000000LL);
}

int lunaNavAnimatedOffset(int startOffset, uint32_t startTime, uint32_t duration,
                          uint32_t now) {
  uint32_t elapsed = now - startTime;
  int progress = (duration == 0 || elapsed >= duration)
                     ? 1000
                     : (int)((elapsed * 1000ULL) / duration);
  return (startOffset * (1000 - lunaNavEase(progress))) / 1000;
}

int lunaNavRandomTarget(int total, int selectedIndex, uint32_t randomSeed) {
  int jump;
  if (total <= 1)
    return selectedIndex;
  jump = 1 + (int)(randomSeed % (uint32_t)(total - 1));
  return lunaNavWrap(total, selectedIndex + jump);
}

int lunaNavMarkedCount(const uint8_t *marked, int total) {
  int count = 0;
  int index;
  if (marked == NULL || total <= 0)
    return 0;
  for (index = 0; index < total; index++)
    count += marked[index] != 0;
  return count;
}

int lunaNavMarkedRank(const uint8_t *marked, int total, int index) {
  int rank = 0;
  int current;
  if (marked == NULL || index < 0 || index >= total || !marked[index])
    return -1;
  for (current = 0; current < index; current++)
    rank += marked[current] != 0;
  return rank;
}

int lunaNavMarkedByRank(const uint8_t *marked, int total, int rank) {
  int count = lunaNavMarkedCount(marked, total);
  int current;
  if (count <= 0)
    return -1;
  rank = lunaNavWrap(count, rank);
  for (current = 0; current < total; current++) {
    if (marked[current] && rank-- == 0)
      return current;
  }
  return -1;
}

int lunaNavMarkedStep(const uint8_t *marked, int total, int index, int direction) {
  int rank = lunaNavMarkedRank(marked, total, index);
  if (rank < 0)
    return lunaNavMarkedByRank(marked, total, (direction < 0) ? -1 : 0);
  return lunaNavMarkedByRank(marked, total, rank + ((direction < 0) ? -1 : 1));
}

int lunaNavMarkedPage(const uint8_t *marked, int total, int index, int pageSize,
                      int direction) {
  int count = lunaNavMarkedCount(marked, total);
  int rank = lunaNavMarkedRank(marked, total, index);
  if (count <= 0 || pageSize <= 0)
    return -1;
  if (rank < 0)
    return lunaNavMarkedByRank(marked, total, (direction < 0) ? count - 1 : 0);
  if (direction > 0)
    rank = (rank == count - 1) ? 0 : ((rank + pageSize < count) ? rank + pageSize : count - 1);
  else
    rank = (rank == 0) ? count - 1 : ((rank - pageSize > 0) ? rank - pageSize : 0);
  return lunaNavMarkedByRank(marked, total, rank);
}

const UILibraryView lunaViewCycleOrder[UI_VIEW_COUNT] = {
    UI_VIEW_CLASSIC, UI_VIEW_PSBBN, UI_VIEW_ORBIT,
    UI_VIEW_3D, UI_VIEW_ORBS};

const char *lunaNavViewLabel(UILibraryView view) {
  static const char *const labels[UI_VIEW_ID_LIMIT] = {
      [UI_VIEW_CLASSIC] = "List", [UI_VIEW_PSBBN] = "Collections",
      [UI_VIEW_ORBIT] = "Orbit",
      [UI_VIEW_ORBS] = "Scroll", [UI_VIEW_3D] = "3D"};
  return view >= UI_VIEW_CLASSIC && view < UI_VIEW_ID_LIMIT && labels[view] ?
         labels[view] : "View";
}

UILibraryView lunaNavNextView(UILibraryView view, uint32_t enabledViews) {
  const int count = UI_VIEW_COUNT;
  int currentIndex = -1;
  for (int index = 0; index < count; index++) {
    if (lunaViewCycleOrder[index] == view) {
      currentIndex = index;
      break;
    }
  }
  for (int step = 1; step <= count; step++) {
    UILibraryView next = lunaViewCycleOrder[(currentIndex + step) % count];
    if (enabledViews & (1U << next))
      return next;
  }
  return UI_VIEW_CLASSIC;
}
