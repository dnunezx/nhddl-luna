// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_NAVIGATION_H
#define LUNA_UI_NAVIGATION_H

#include <stdint.h>

#define PSBBN_COVER_CACHE_COUNT 10
#define PSBBN_COVER_CACHE_FOCUS 3
#define ORBS_LOGO_CACHE_COUNT 7
#define ORBS_LOGO_CACHE_FOCUS 3
#define PSBBN_ANIMATION_DURATION_MS 420
#define PSBBN_REPEAT_FRAMES_NTSC 11
#define PSBBN_REPEAT_FRAMES_PAL 9
#define GRID_COLUMNS 4
#define GRID_ROWS 4
#define GRID_PAGE_SIZE (GRID_COLUMNS * GRID_ROWS)
#define CASE_GRID_COLUMNS 6
#define CASE_GRID_ROWS 4
#define CASE_GRID_PAGE_SIZE (CASE_GRID_COLUMNS * CASE_GRID_ROWS)
#define GRID_CACHE_PAGE_SIZE CASE_GRID_PAGE_SIZE
#define GRID_THUMBNAIL_SIZE 64
#define GRID_PAGE_BUFFERS 3
#define GRID_SELECTED_BUFFERS 2
#define ORBIT_RANDOM_STEP_MS 85
#define CLASSIC_REPEAT_DELAY_MS 260
#define CLASSIC_REPEAT_INTERVAL_MS 105
#define CLASSIC_ART_SETTLE_MS 90
#define COLLECTION_SCAN_HOLD_MS 450
#define COLLECTION_SCAN_STEP_MS 100
#define SCROLL_FAST_HOLD_MS 1500
#define SCROLL_FAST_STEP_MS 90
#define SCROLL_FAST_ANIMATION_MS 110

typedef struct {
  int heldDirection;
  int active;
  uint32_t holdStartMs;
  uint32_t nextStepMs;
} LunaCollectionScan;

int lunaCollectionScanUpdate(LunaCollectionScan *scan, int direction, uint32_t now,
                             uint32_t stepMs);

typedef struct {
  int heldDirection;
  int active;
  uint32_t holdStartMs;
  uint32_t nextStepMs;
} LunaScrollFast;

int lunaScrollFastUpdate(LunaScrollFast *fast, int direction, uint32_t now);

typedef struct {
  int direction;
  uint32_t nextStepMs;
} LunaNavRepeatState;

typedef struct {
  int open;
  int captured;
  int consumed;
} LunaQuickMenu;

// Returns a direct action, or -1. Capture lasts until all controls are released.
int lunaQuickMenuUpdate(LunaQuickMenu *menu, int held, int controlsHeld,
                        int count, int shortcut);

typedef enum {
  UI_VIEW_CLASSIC = 0,
  UI_VIEW_PSBBN = 1,
  UI_VIEW_GRID = 2, // Retired Classic Grid ID; keep persisted IDs stable.
  UI_VIEW_ORBIT = 3,
  UI_VIEW_ORBS = 4,
  // Keep persisted view IDs stable; slots 5 and 6 are also retired.
  UI_VIEW_3D = 7,
  UI_VIEW_ID_LIMIT,
} UILibraryView;

#define UI_VIEW_COUNT 5
#define UI_VIEW_ALL_MASK (((1U << UI_VIEW_ID_LIMIT) - 1U) & \
                          ~((1U << UI_VIEW_GRID) | (1U << 5) | (1U << 6)))
#define UI_VIEW_DEFAULT_MASK ((1U << UI_VIEW_CLASSIC) | \
                              (1U << UI_VIEW_PSBBN) | \
                              (1U << UI_VIEW_ORBIT) | \
                              (1U << UI_VIEW_3D))

extern const UILibraryView lunaViewCycleOrder[UI_VIEW_COUNT];

int lunaNavWrap(int total, int index);
int lunaNavRepeatStep(LunaNavRepeatState *state, int direction, uint32_t now,
                      uint32_t initialDelayMs, uint32_t intervalMs);
int lunaNavCaseGridVertical(int total, int index, int direction);
int lunaNavCaseGridPage(int total, int index, int direction);
int lunaNavDirection(int total, int fromIdx, int toIdx);
int lunaNavCollectionForeground(const int *positions, const uint8_t *drawable,
                                int count);
int lunaNavEase(int progress);
int lunaNavAnimatedOffset(int startOffset, uint32_t startTime, uint32_t duration,
                          uint32_t now);
uint32_t lunaNavDurationFrames(uint32_t durationMs, int framesPerSecond);
int lunaNavClassicGlideFrameOffset(int startOffset, uint32_t elapsedFrames,
                                   uint32_t durationFrames);
int lunaNavCubicGlideFrameOffset(int startOffset, uint32_t elapsedFrames,
                                 uint32_t durationFrames);
int lunaNavRandomTarget(int total, int selectedIndex, uint32_t randomSeed);
int lunaNavMarkedCount(const uint8_t *marked, int total);
int lunaNavMarkedRank(const uint8_t *marked, int total, int index);
int lunaNavMarkedByRank(const uint8_t *marked, int total, int rank);
int lunaNavMarkedStep(const uint8_t *marked, int total, int index, int direction);
int lunaNavMarkedPage(const uint8_t *marked, int total, int index, int pageSize,
                      int direction);
UILibraryView lunaNavNextView(UILibraryView view, uint32_t enabledViews);
const char *lunaNavViewLabel(UILibraryView view);

#endif
