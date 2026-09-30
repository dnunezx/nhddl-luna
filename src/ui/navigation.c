// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/navigation.h"
#include <stddef.h>

int lunaQuickMenuUpdate(LunaQuickMenu *menu, int held, int direction,
                        int confirm, int controlsHeld, int count,
                        int shortcut, uint32_t now) {
  if (!held) {
    menu->open = 0;
    if (!controlsHeld) {
      menu->captured = 0;
      menu->consumed = 0;
      menu->confirmHeld = 0;
      menu->repeat.direction = 0;
    }
    return -1;
  }
  menu->captured = 1;
  if (menu->consumed)
    return -1;
  if (!menu->open) {
    menu->open = 1;
    menu->selected = 0;
    menu->confirmHeld = confirm;
    menu->repeat.direction = 0;
    return -1;
  }
  if (lunaNavRepeatStep(&menu->repeat, direction, now, 280, 120) && count > 0)
    menu->selected = lunaNavWrap(count, menu->selected + direction);
  int direct = shortcut >= 0 && shortcut < count;
  int confirmed = direct || (confirm && !menu->confirmHeld);
  menu->confirmHeld = confirm;
  if (confirmed && count > 0) {
    if (direct)
      menu->selected = shortcut;
    menu->open = 0;
    menu->consumed = 1;
    return menu->selected;
  }
  return -1;
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

int lunaNavGridVertical(int total, int index, int direction) {
  int candidate;
  int column;

  if (total <= 0 || index < 0 || index >= total || direction == 0)
    return index;
  candidate = index + direction * GRID_COLUMNS;
  column = index % GRID_COLUMNS;
  if (candidate >= 0 && candidate < total)
    return candidate;
  if (direction < 0) {
    int last = total - 1;
    candidate = last - ((last - column) % GRID_COLUMNS);
    return (candidate >= 0) ? candidate : index;
  }
  return (column < total) ? column : total - 1;
}

int lunaNavGridPage(int total, int index, int direction) {
  int pageCount;
  int page;
  int cell;
  int targetPage;
  int candidate;

  if (total <= 0 || index < 0 || index >= total)
    return index;
  pageCount = (total + GRID_PAGE_SIZE - 1) / GRID_PAGE_SIZE;
  page = index / GRID_PAGE_SIZE;
  cell = index % GRID_PAGE_SIZE;
  targetPage = lunaNavWrap(pageCount, page + direction);
  candidate = targetPage * GRID_PAGE_SIZE + cell;
  return (candidate < total) ? candidate : total - 1;
}

int lunaNavPageBase(int total, int pageBase, int direction) {
  int pageCount;
  int page;

  if (total <= 0)
    return -1;
  pageCount = (total + GRID_PAGE_SIZE - 1) / GRID_PAGE_SIZE;
  page = pageBase / GRID_PAGE_SIZE;
  return lunaNavWrap(pageCount, page + direction) * GRID_PAGE_SIZE;
}

int lunaNavFindBuffer(const int *pageBases, int bufferCount, int pageBase) {
  int buffer;
  for (buffer = 0; buffer < bufferCount; buffer++) {
    if (pageBases[buffer] == pageBase)
      return buffer;
  }
  return -1;
}

int lunaNavChooseBuffer(const int *pageBases, int bufferCount, int activeBuffer,
                        int previousBuffer, int incomingBuffer) {
  int buffer;
  for (buffer = 0; buffer < bufferCount; buffer++) {
    if (buffer != activeBuffer && buffer != previousBuffer &&
        buffer != incomingBuffer && pageBases[buffer] < 0)
      return buffer;
  }
  for (buffer = 0; buffer < bufferCount; buffer++) {
    if (buffer != activeBuffer && buffer != previousBuffer && buffer != incomingBuffer)
      return buffer;
  }
  return -1;
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

uint32_t lunaNavDurationFrames(uint32_t durationMs, int framesPerSecond) {
  uint32_t frames = (uint32_t)(((uint64_t)durationMs * framesPerSecond + 500) / 1000);
  return frames > 0 ? frames : 1;
}

int lunaNavAnimatedFrameOffset(int startOffset, uint32_t elapsedFrames,
                               uint32_t durationFrames) {
  int progress = (durationFrames == 0 || elapsedFrames >= durationFrames)
                     ? 1000
                     : (int)((elapsedFrames * 1000ULL) / durationFrames);
  return (startOffset * (1000 - lunaNavEase(progress))) / 1000;
}

int lunaNavClassicGlideFrameOffset(int startOffset, uint32_t elapsedFrames,
                                   uint32_t durationFrames) {
  return lunaNavAnimatedFrameOffset(startOffset, elapsedFrames, durationFrames);
}

int lunaNavCubicGlideFrameOffset(int startOffset, uint32_t elapsedFrames,
                                 uint32_t durationFrames) {
  if (durationFrames == 0 || elapsedFrames >= durationFrames)
    return 0;
  int64_t remaining = durationFrames - elapsedFrames;
  int64_t duration = durationFrames;
  return (int)((startOffset * remaining * remaining * remaining) /
               (duration * duration * duration));
}

int lunaNavGridCascadeProgress(int progress, int row, int incoming) {
  int start = row * GRID_CASCADE_ROW_STAGGER +
              (incoming ? GRID_CASCADE_FOLLOW_DELAY : 0);
  int rowProgress;
  if (progress <= start)
    return 0;
  rowProgress = ((progress - start) * 1000) / GRID_CASCADE_ROW_DURATION;
  return (rowProgress > 1000) ? 1000 : rowProgress;
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

const UILibraryView lunaViewCycleOrder[UI_VIEW_SAVE_ICONS + 1] = {
    UI_VIEW_CLASSIC, UI_VIEW_PSBBN, UI_VIEW_ORBIT,
    UI_VIEW_ORBS, UI_VIEW_GRID, UI_VIEW_SAVE_ICONS};

const char *lunaNavViewLabel(UILibraryView view) {
  static const char *const labels[UI_VIEW_SAVE_ICONS + 1] = {
      "List", "Collections", "Grid", "Orbit", "Scroll", "Save Icons"};
  return view >= UI_VIEW_CLASSIC && view <= UI_VIEW_SAVE_ICONS ?
         labels[view] : "View";
}

UILibraryView lunaNavNextView(UILibraryView view, uint32_t enabledViews) {
  const int count = UI_VIEW_SAVE_ICONS + 1;
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
