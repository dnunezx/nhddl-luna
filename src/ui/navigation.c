// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/navigation.h"
#include "ui/language.h"
#include <stddef.h>

int lunaNavEntryInputBlocked(int *pending, int viewReady, int controlsHeld) {
  if (!*pending)
    return 0;
  if (viewReady && !controlsHeld)
    *pending = 0;
  return 1;
}
int lunaQuickMenuUpdate(LunaQuickMenu *menu, int held, int controlsHeld,
                        int count, int shortcut, uint32_t now) {
  if (menu->releasePending &&
      now - menu->releaseStartMs >= QUICK_MENU_CLOSE_DELAY_MS) {
    menu->open = 0;
    menu->releasePending = 0;
  }
  if (menu->consumed) {
    if (!controlsHeld) {
      menu->captured = 0;
      menu->consumed = 0;
    }
    return -1;
  }
  if (held && menu->releasePending) {
    // Consume the dismissing press until all buttons are released, so a
    // held R1 cannot reopen the menu or let a simultaneous shortcut leak.
    menu->open = 0;
    menu->releasePending = 0;
    menu->consumed = 1;
    menu->captured = 1;
    return -1;
  }
  if (held) {
    menu->open = 1;
    menu->captured = 1;
  } else if (menu->open && !menu->releasePending) {
    menu->releasePending = 1;
    menu->releaseStartMs = now;
  }
  if (!menu->open) {
    if (!controlsHeld)
      menu->captured = 0;
    return -1;
  }
  if (shortcut < 0 || shortcut >= count)
    return -1;
  menu->open = 0;
  menu->releasePending = 0;
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

uint32_t lunaNavDurationFrames(uint32_t durationMs, int framesPerSecond) {
  uint32_t frames = (uint32_t)(((uint64_t)durationMs * framesPerSecond + 500) / 1000);
  return frames > 0 ? frames : 1;
}

int lunaNavClassicGlideFrameOffset(int startOffset, uint32_t elapsedFrames,
                                   uint32_t durationFrames) {
  int progress = (durationFrames == 0 || elapsedFrames >= durationFrames)
                     ? 1000
                     : (int)((elapsedFrames * 1000ULL) / durationFrames);
  return (startOffset * (1000 - lunaNavEase(progress))) / 1000;
}

// A normal glide uses the original integer easing, byte-for-byte. Only a
// fresh same-direction tap during a moving glide supplies an initial speed.
int lunaNavGlideOffset(int start, uint32_t frame, uint32_t frames,
                                 float velocity) {
  if (velocity == 0.0f)
    return lunaNavClassicGlideFrameOffset(start, frame, frames);
  if (frames == 0 || frame >= frames)
    return 0;
  float t = (float)frame / (float)frames;
  float remaining = 1.0f - t;
  return (int)(start * remaining * remaining * (1.0f + 2.0f * t) +
               velocity * frames * t * remaining * remaining);
}

float lunaNavGlideVelocity(int start, uint32_t frame, uint32_t frames,
                                     float velocity) {
  if (frames == 0 || frame >= frames)
    return 0.0f;
  float t = (float)frame / (float)frames;
  return (start * (6.0f * t * t - 6.0f * t)) / frames +
         velocity * (3.0f * t * t - 4.0f * t + 1.0f);
}

float lunaNavGlideCarry(int start, uint32_t frames, float velocity) {
  if (frames == 0 || start == 0 || start * velocity >= 0.0f)
    return 0.0f;
  // Endpoint velocity is zero. This tangent bound keeps the entire cubic
  // moving toward its destination without overshooting or turning around.
  float limit = 3.0f * (start < 0 ? -(float)start : (float)start) / frames;
  if (velocity > limit) velocity = limit;
  if (velocity < -limit) velocity = -limit;
  return velocity;
}

// Orbit and Scroll keep their elapsed-millisecond clock. Zero incoming speed
// calls their original integer easing, including its quantization and wrap.
int lunaNavTimedGlideOffset(int start, uint32_t startTime, uint32_t duration,
                               uint32_t now, float velocity) {
  if (velocity == 0.0f)
    return lunaNavAnimatedOffset(start, startTime, duration, now);
  return lunaNavGlideOffset(start, now - startTime, duration, velocity);
}

int lunaNavRandomTarget(int total, int selectedIndex, uint32_t randomSeed) {
  int jump;
  if (total <= 1)
    return selectedIndex;
  jump = 1 + (int)(randomSeed % (uint32_t)(total - 1));
  return lunaNavWrap(total, selectedIndex + jump);
}

const UILibraryView lunaViewCycleOrder[UI_VIEW_COUNT] = {
    UI_VIEW_CLASSIC, UI_VIEW_PSBBN, UI_VIEW_COLLECTION_V2, UI_VIEW_ORBIT,
    UI_VIEW_3D, UI_VIEW_ORBS};

const char *lunaNavViewLabel(UILibraryView view) {
  static const char *const labels[UI_VIEW_ID_LIMIT] = {
      [UI_VIEW_CLASSIC] = "List", [UI_VIEW_PSBBN] = "Collections",
      [UI_VIEW_COLLECTION_V2] = "Collections Version 2",
      [UI_VIEW_ORBIT] = "Orbit",
      [UI_VIEW_ORBS] = "Scroll", [UI_VIEW_3D] = "3D"};
  return view >= UI_VIEW_CLASSIC && view < UI_VIEW_ID_LIMIT && lunaText(labels[view]) ?
         lunaText(labels[view]) : lunaText("View");
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
