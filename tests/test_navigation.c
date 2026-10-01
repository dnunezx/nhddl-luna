// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/navigation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void testWrapping(void) {
  assert(lunaNavWrap(5, -1) == 4);
  assert(lunaNavWrap(5, 5) == 0);
  assert(lunaNavWrap(5, 12) == 2);
  assert(lunaNavWrap(0, 1) == -1);
  assert(lunaNavDirection(10, 9, 0) == 1);
  assert(lunaNavDirection(10, 0, 9) == -1);
  assert(lunaNavDirection(10, 3, 3) == 0);
}

static void testGridNavigation(void) {
  assert(lunaNavGridVertical(18, 1, -1) == 17);
  assert(lunaNavGridVertical(18, 17, 1) == 1);
  assert(lunaNavGridVertical(18, 3, 1) == 7);
  assert(lunaNavGridPage(34, 2, 1) == 18);
  assert(lunaNavGridPage(34, 18, 1) == 33);
  assert(lunaNavGridPage(34, 33, 1) == 1);
  assert(lunaNavPageBase(34, 32, 1) == 0);
  assert(lunaNavPageBase(34, 0, -1) == 32);
}

static void testBufferSelection(void) {
  const int pages[GRID_PAGE_BUFFERS] = {0, 16, -1};
  assert(lunaNavFindBuffer(pages, GRID_PAGE_BUFFERS, 16) == 1);
  assert(lunaNavFindBuffer(pages, GRID_PAGE_BUFFERS, 32) == -1);
  assert(lunaNavChooseBuffer(pages, GRID_PAGE_BUFFERS, 0, 1, -1) == 2);
  assert(lunaNavChooseBuffer(pages, GRID_PAGE_BUFFERS, 0, 1, 2) == -1);
}

static void testTiming(void) {
  LunaNavRepeatState repeat = {0};
  assert(lunaNavRepeatStep(&repeat, 1, 100, 260, 105) == 1);
  assert(lunaNavRepeatStep(&repeat, 1, 359, 260, 105) == 0);
  assert(lunaNavRepeatStep(&repeat, 1, 360, 260, 105) == 1);
  assert(lunaNavRepeatStep(&repeat, 1, 900, 260, 105) == 1);
  assert(lunaNavRepeatStep(&repeat, 1, 901, 260, 105) == 0);
  assert(lunaNavRepeatStep(&repeat, -1, 902, 260, 105) == 1);
  assert(lunaNavRepeatStep(&repeat, 0, 903, 260, 105) == 0);
  assert(lunaNavRepeatStep(&repeat, -1, 904, 260, 105) == 1);
  assert(lunaNavEase(0) == 0);
  assert(lunaNavEase(500) == 500);
  assert(lunaNavEase(1000) == 1000);
  assert(lunaNavAnimatedOffset(1000, 100, 420, 100) == 1000);
  assert(lunaNavAnimatedOffset(1000, 100, 420, 310) == 500);
  assert(lunaNavAnimatedOffset(1000, 100, 420, 520) == 0);
  assert(lunaNavDurationFrames(420, 60) == 25);
  assert(lunaNavDurationFrames(420, 50) == 21);
  assert(lunaNavDurationFrames(1, 60) == 1);
  assert(lunaNavCubicGlideFrameOffset(1000, 0, 20) == 1000);
  assert(lunaNavCubicGlideFrameOffset(1000, 5, 20) == 421);
  assert(lunaNavCubicGlideFrameOffset(1000, 10, 20) == 125);
  assert(lunaNavCubicGlideFrameOffset(-1000, 10, 20) == -125);
  assert(lunaNavCubicGlideFrameOffset(1000, 20, 20) == 0);
  assert(lunaNavGridCascadeProgress(89, 1, 0) == 0);
  assert(lunaNavGridCascadeProgress(415, 1, 0) == 500);
  assert(lunaNavGridCascadeProgress(1000, 3, 1) == 1000);
}

static void testCollectionLayering(void) {
  int positions[PSBBN_COVER_CACHE_COUNT];
  uint8_t drawable[PSBBN_COVER_CACHE_COUNT];
  memset(drawable, 1, sizeof(drawable));
  uint32_t duration = lunaNavDurationFrames(PSBBN_ANIMATION_DURATION_MS, 60);

  // The second held step starts before the first jacket has cleared focus.
  int remaining = lunaNavClassicGlideFrameOffset(1000,
                                               PSBBN_REPEAT_FRAMES_NTSC, duration);
  assert(remaining > 0 && remaining < 1000);
  for (int direction = -1; direction <= 1; direction += 2) {
    int offset = direction * (1000 + remaining);
    for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++)
      positions[i] = (i - PSBBN_COVER_CACHE_FOCUS) * 1000 + offset;
    int foreground = lunaNavCollectionForeground(positions, drawable,
                                                PSBBN_COVER_CACHE_COUNT);
    assert(foreground == PSBBN_COVER_CACHE_FOCUS + (direction > 0 ? -2 : 1));
    assert(positions[foreground] <= 0);
    assert(positions[foreground + 1] > 0); // Up Next has not reached focus.
    drawable[foreground] = 0; // Duplicate or hidden jackets cannot own depth.
    assert(lunaNavCollectionForeground(positions, drawable,
                                      PSBBN_COVER_CACHE_COUNT) == foreground - 1);
    drawable[foreground] = 1;
  }

  // Preserve a single forward tap's outgoing jacket throughout its glide.
  for (uint32_t frame = 0; frame <= duration; frame++) {
    int offset = lunaNavClassicGlideFrameOffset(1000, frame, duration);
    for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++)
      positions[i] = (i - PSBBN_COVER_CACHE_FOCUS) * 1000 + offset;
    assert(lunaNavCollectionForeground(positions, drawable,
                                      PSBBN_COVER_CACHE_COUNT) ==
           PSBBN_COVER_CACHE_FOCUS - (offset > 0));
  }
  memset(drawable, 0, sizeof(drawable));
  assert(lunaNavCollectionForeground(positions, drawable,
                                    PSBBN_COVER_CACHE_COUNT) == -1);
}

static void testCollectionFastScan(void) {
  LunaCollectionScan scan = {0};
  assert(lunaCollectionScanUpdate(&scan, 1, 100, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, 549, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, 550, COLLECTION_SCAN_STEP_MS) == 1);
  assert(lunaCollectionScanUpdate(&scan, 1, 649, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, 650, COLLECTION_SCAN_STEP_MS) == 1);
  assert(lunaCollectionScanUpdate(&scan, 1, 1000, COLLECTION_SCAN_STEP_MS) == 1); // No catch-up burst.
  assert(lunaCollectionScanUpdate(&scan, -1, 1001, COLLECTION_SCAN_STEP_MS) == -1); // Reverse an active scan.
  assert(lunaCollectionScanUpdate(&scan, 0, 1010, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, -1, 1020, COLLECTION_SCAN_STEP_MS) == 0); // A tap is inert.
  assert(lunaCollectionScanUpdate(&scan, 0, 1030, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, UINT32_MAX - 200, COLLECTION_SCAN_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, 249, COLLECTION_SCAN_STEP_MS) == 1); // Timer wrap.
}

static void testOrbitFastScan(void) {
  LunaCollectionScan scan = {0};
  assert(lunaCollectionScanUpdate(&scan, -1, 100, ORBIT_RANDOM_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, -1, 549, ORBIT_RANDOM_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, -1, 550, ORBIT_RANDOM_STEP_MS) == -1);
  assert(lunaCollectionScanUpdate(&scan, -1, 634, ORBIT_RANDOM_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, -1, 635, ORBIT_RANDOM_STEP_MS) == -1);
  assert(lunaCollectionScanUpdate(&scan, 0, 640, ORBIT_RANDOM_STEP_MS) == 0);
  assert(lunaCollectionScanUpdate(&scan, 1, 650, ORBIT_RANDOM_STEP_MS) == 0); // A tap is inert.
  assert(lunaCollectionScanUpdate(&scan, 0, 660, ORBIT_RANDOM_STEP_MS) == 0);
}

static void testScrollFast(void) {
  LunaScrollFast fast = {0};
  assert(lunaScrollFastUpdate(&fast, 1, 100) == 0);
  assert(lunaScrollFastUpdate(&fast, 1, 1599) == 0);
  assert(!fast.active);
  assert(lunaScrollFastUpdate(&fast, 1, 1600) == 1);
  assert(fast.active);
  assert(lunaScrollFastUpdate(&fast, 1, 1689) == 0);
  assert(lunaScrollFastUpdate(&fast, 1, 1690) == 1);
  assert(lunaScrollFastUpdate(&fast, -1, 1691) == 0);
  assert(!fast.active);
  assert(lunaScrollFastUpdate(&fast, 0, 1700) == 0);
  assert(lunaScrollFastUpdate(&fast, -1, 1710) == 0);
  assert(lunaScrollFastUpdate(&fast, -1, 3210) == -1);
  assert(lunaScrollFastUpdate(&fast, 0, 3220) == 0);
  assert(!fast.active);
}

static void testRouting(void) {
  int selected;
  for (selected = 0; selected < 17; selected++) {
    uint32_t seed;
    for (seed = 0; seed < 64; seed++) {
      int target = lunaNavRandomTarget(17, selected, seed);
      assert(target >= 0 && target < 17);
      assert(target != selected);
    }
  }
  assert(lunaNavNextView(UI_VIEW_CLASSIC, UI_VIEW_ALL_MASK) == UI_VIEW_PSBBN);
  assert(lunaNavNextView(UI_VIEW_PSBBN, UI_VIEW_ALL_MASK) == UI_VIEW_ORBIT);
  assert(lunaNavNextView(UI_VIEW_ORBIT, UI_VIEW_ALL_MASK) == UI_VIEW_ORBS);
  assert(lunaNavNextView(UI_VIEW_ORBS, UI_VIEW_ALL_MASK) == UI_VIEW_GRID);
  assert(lunaNavNextView(UI_VIEW_GRID, UI_VIEW_ALL_MASK) == UI_VIEW_SAVE_ICONS);
  assert(lunaNavNextView(UI_VIEW_SAVE_ICONS, UI_VIEW_ALL_MASK) == UI_VIEW_CLASSIC);
  assert(lunaNavNextView(UI_VIEW_CLASSIC, UI_VIEW_DEFAULT_MASK) == UI_VIEW_PSBBN);
  assert(lunaNavNextView(UI_VIEW_PSBBN, UI_VIEW_DEFAULT_MASK) == UI_VIEW_ORBIT);
  assert(lunaNavNextView(UI_VIEW_ORBIT, UI_VIEW_DEFAULT_MASK) == UI_VIEW_CLASSIC);
  assert(lunaNavNextView(UI_VIEW_CLASSIC, (1U << UI_VIEW_GRID) | (1U << UI_VIEW_ORBS)) == UI_VIEW_ORBS);
  assert(lunaNavNextView(UI_VIEW_GRID, (1U << UI_VIEW_GRID) | (1U << UI_VIEW_ORBS)) == UI_VIEW_ORBS);
  assert(lunaNavNextView(UI_VIEW_ORBS, (1U << UI_VIEW_GRID) | (1U << UI_VIEW_ORBS)) == UI_VIEW_GRID);
  assert(lunaNavNextView(UI_VIEW_PSBBN, (1U << UI_VIEW_ORBIT) | (1U << UI_VIEW_GRID)) == UI_VIEW_ORBIT);
  assert(lunaNavNextView(UI_VIEW_ORBIT, (1U << UI_VIEW_ORBS) | (1U << UI_VIEW_SAVE_ICONS)) == UI_VIEW_ORBS);
  assert(lunaNavNextView(UI_VIEW_SAVE_ICONS, (1U << UI_VIEW_ORBIT) | (1U << UI_VIEW_GRID)) == UI_VIEW_ORBIT);
  assert(lunaNavNextView(UI_VIEW_ORBIT, 1U << UI_VIEW_GRID) == UI_VIEW_GRID);
  assert(lunaNavNextView(UI_VIEW_GRID, 1U << UI_VIEW_GRID) == UI_VIEW_GRID);
  for (int current = UI_VIEW_CLASSIC; current <= UI_VIEW_SAVE_ICONS; current++)
    for (int enabled = UI_VIEW_CLASSIC; enabled <= UI_VIEW_SAVE_ICONS; enabled++)
      assert(lunaNavNextView((UILibraryView)current, 1U << enabled) ==
             (UILibraryView)enabled);
  assert(lunaNavNextView(UI_VIEW_ORBS, 0) == UI_VIEW_CLASSIC);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_CLASSIC), "List") == 0);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_PSBBN), "Collections") == 0);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_ORBIT), "Orbit") == 0);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_ORBS), "Scroll") == 0);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_GRID), "Grid") == 0);
  assert(strcmp(lunaNavViewLabel(UI_VIEW_SAVE_ICONS), "Save Icons") == 0);
}

static void testMarkedNavigation(void) {
  const uint8_t marked[] = {0, 1, 0, 1, 1, 0, 0, 1};
  assert(lunaNavMarkedCount(marked, 8) == 4);
  assert(lunaNavMarkedRank(marked, 8, 1) == 0);
  assert(lunaNavMarkedRank(marked, 8, 4) == 2);
  assert(lunaNavMarkedRank(marked, 8, 2) == -1);
  assert(lunaNavMarkedByRank(marked, 8, 3) == 7);
  assert(lunaNavMarkedByRank(marked, 8, 4) == 1);
  assert(lunaNavMarkedStep(marked, 8, 1, -1) == 7);
  assert(lunaNavMarkedStep(marked, 8, 7, 1) == 1);
  assert(lunaNavMarkedPage(marked, 8, 1, 2, 1) == 4);
  assert(lunaNavMarkedPage(marked, 8, 4, 2, 1) == 7);
  assert(lunaNavMarkedPage(marked, 8, 7, 2, 1) == 1);
  assert(lunaNavMarkedPage(marked, 8, 1, 2, -1) == 7);
}

static void testQuickMenu(void) {
  LunaQuickMenu menu = {0};
  // Holding R1 opens the overlay without choosing an action.
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 4, -1) == -1);
  assert(menu.open && menu.captured);
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 4, 0) == 0);
  assert(!menu.open && menu.captured && menu.consumed);
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 4, 1) == -1);
  // Releasing R1 while an action button remains held cannot leak that press.
  assert(lunaQuickMenuUpdate(&menu, 0, 1, 4, -1) == -1);
  assert(menu.captured);
  assert(lunaQuickMenuUpdate(&menu, 0, 0, 4, -1) == -1);
  assert(!menu.captured && !menu.consumed);
  // Pressing R1 and Circle together is a valid direct chord.
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 4, 1) == 1);
  assert(!menu.open && menu.captured && menu.consumed);
  assert(lunaQuickMenuUpdate(&menu, 0, 0, 4, -1) == -1);
  // An action button already held before R1 has no fresh shortcut edge.
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 3, -1) == -1);
  assert(menu.open);
  // Random is unavailable in a three-row menu.
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 3, 3) == -1);
  assert(menu.open);
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 3, 2) == 2);
  assert(lunaQuickMenuUpdate(&menu, 0, 0, 3, -1) == -1);
  assert(lunaQuickMenuUpdate(&menu, 1, 1, 3, -1) == -1);
  assert(menu.open);
  assert(lunaQuickMenuUpdate(&menu, 0, 0, 3, -1) == -1);
  assert(!menu.open && !menu.captured);
}

int main(void) {
  testQuickMenu();
  testWrapping();
  testGridNavigation();
  testBufferSelection();
  testTiming();
  testCollectionLayering();
  testCollectionFastScan();
  testOrbitFastScan();
  testScrollFast();
  testRouting();
  testMarkedNavigation();
  puts("navigation tests passed");
  return 0;
}
