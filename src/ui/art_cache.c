// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "devices/devices.h"
#include "ui/art_cache.h"
#include "ui/graphics.h"
#include "ui/navigation.h"
#include "ui/collection_art.h"
#include "ui/ui.h"
#include "dprintf.h"

#include <malloc.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <stdio.h>
#include <string.h>

#define PSBBN_THUMBNAIL_SIZE 64
#define PSBBN_PREVIEW_SIZE 128
#define CASE_THUMBNAIL_WIDTH 64
#define CASE_THUMBNAIL_HEIGHT 96
#define GRID_THUMBNAIL_CACHE_COUNT (GRID_PAGE_BUFFERS * GRID_PAGE_SIZE)

typedef struct {
  uint8_t r, g, b, a;
} PSBBNPixel;

GSTEXTURE *coverTexture;
GSTEXTURE *classicPreviousCoverTexture;
GSTEXTURE *discTexture;
GSTEXTURE *psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT];
uint8_t psbbnCoverLoaded[PSBBN_COVER_CACHE_COUNT];
static uint8_t psbbnCoverFullResolution[PSBBN_COVER_CACHE_COUNT];
static void *psbbnCoverSourcePixels[PSBBN_COVER_CACHE_COUNT];
static int psbbnCoverSourceWidth[PSBBN_COVER_CACHE_COUNT];
static int psbbnCoverSourceHeight[PSBBN_COVER_CACHE_COUNT];
static void *collectionCoverThumbnailPixels[PSBBN_COVER_CACHE_COUNT];
static void *collectionCoverPreviewPixels[PSBBN_COVER_CACHE_COUNT];
static uint8_t collectionCoverResidentLevel[PSBBN_COVER_CACHE_COUNT];
static char collectionCoverKeys[PSBBN_COVER_CACHE_COUNT][255];
static CollectionArtReuseCache collectionReuseCache;
static int collectionNavigationDirection;
static struct {
  uint32_t reportMs;
  uint32_t completed, adopted, late, failed, cacheHits, pendingFocusFrames;
  uint32_t readMs, decodeMs, featherMs, resizeMs, maxJobMs, bindMs, binds;
} collectionArtStats;
GSTEXTURE *gridCoverTextures[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
uint8_t gridCoverLoaded[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
static uint8_t gridCoverAttempted[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
static uint8_t gridCoverResolved[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
static uint32_t gridPageGeneration[GRID_PAGE_BUFFERS];
typedef struct {
  char path[255];
  PSBBNPixel *pixels;
  int width, height;
  uint32_t lastUsed;
  uint8_t state; // 0: empty, 1: thumbnail, 2: missing file
  uint8_t caseArtwork;
} GridThumbnailCacheEntry;
static GridThumbnailCacheEntry gridThumbnailCache[GRID_THUMBNAIL_CACHE_COUNT];
static uint32_t gridThumbnailCacheClock;
GSTEXTURE *gridSelectedTextures[GRID_SELECTED_BUFFERS];
uint8_t gridSelectedLoaded[GRID_SELECTED_BUFFERS];
static uint32_t gridSelectedGeneration[GRID_SELECTED_BUFFERS];
static uint8_t gridSelectedStatus[GRID_SELECTED_BUFFERS]; // 0: idle, 1: loading, 2: ready, 3: missing
static char gridSelectedPath[GRID_SELECTED_BUFFERS][255];
GSTEXTURE *orbsLogoTextures[ORBS_LOGO_CACHE_COUNT];
uint8_t orbsLogoLoaded[ORBS_LOGO_CACHE_COUNT];
GSTEXTURE *orbsBackgroundTexture;
uint8_t orbsBackgroundLoaded;
static int orbsLogoTargets[ORBS_LOGO_CACHE_COUNT];
static uint8_t orbsLogoResolved[ORBS_LOGO_CACHE_COUNT];
static int orbsBackgroundTarget = -1;
static uint8_t orbsBackgroundResolved;

static const char artPath[] = "/ART";
static const char psbbnArtPath[] = "/ART/PSBBN";
static int gridCaseArtwork;

extern void *_gp;
static int classicArtThreadId = -1;
static int classicArtWakeSema = -1;
static int classicArtDoneSema = -1;
static volatile int classicArtStopping;
static uint8_t classicArtStack[16384] __attribute__((aligned(16)));
static volatile uint32_t classicArtGeneration;
static volatile int classicArtPending;
static char classicCoverPath[255];
static char classicDiscPath[255];
static struct {
  volatile int state; // 0: idle, 1: decoding, 2: ready
  uint32_t generation;
  char coverPath[255];
  char discPath[255];
  GSTEXTURE cover;
  GSTEXTURE disc;
  int coverResult;
  int discResult;
} classicArtJob;
static int collectionArtThreadId = -1;
static int collectionArtWakeSema = -1;
static int collectionArtDoneSema = -1;
static volatile int collectionArtStopping;
static uint8_t collectionArtStack[16384] __attribute__((aligned(16)));
static uint32_t collectionArtGeneration;
static uint8_t collectionCoverAttempted[PSBBN_COVER_CACHE_COUNT];
static uint8_t collectionCoverResolved[PSBBN_COVER_CACHE_COUNT];
typedef struct {
  volatile int state; // 0: idle, 1: decoding, 2: ready
  uint32_t generation;
  char path[255];
  GSTEXTURE texture;
  void *sourcePixels;
  void *previewPixels;
  int sourceWidth;
  int sourceHeight;
  int result;
  uint32_t requestMs, readyMs, readMs, decodeMs, featherMs, resizeMs;
} PSBBNArtJob;
static PSBBNArtJob collectionArtJob;
static int collectionFarArtThreadId = -1;
static int collectionFarArtWakeSema = -1;
static int collectionFarArtDoneSema = -1;
static volatile int collectionFarArtStopping;
static int collectionFarArtStartAttempted;
static uint8_t collectionFarArtStack[16384] __attribute__((aligned(16)));
static PSBBNArtJob collectionFarArtJob;
static int orbitArtThreadId = -1;
static int orbitArtWakeSema = -1;
static int orbitArtDoneSema = -1;
static volatile int orbitArtStopping;
static uint8_t orbitArtStack[16384] __attribute__((aligned(16)));
static uint32_t orbitArtGeneration;
static PSBBNArtJob orbitArtJob;
static int gridArtThreadId = -1;
static int gridArtWakeSema = -1;
static int gridArtDoneSema = -1;
static volatile int gridArtStopping;
static uint8_t gridArtStack[16384] __attribute__((aligned(16)));
static struct {
  volatile int state; // 0: idle, 1: decoding, 2: ready
  int thumbnail;
  int buffer;
  int slot;
  uint32_t generation;
  char path[255];
  GSTEXTURE texture;
  int result;
  int missingFile;
} gridArtJob;
static int scrollArtThreadId = -1;
static int scrollArtWakeSema = -1;
static int scrollArtDoneSema = -1;
static volatile int scrollArtStopping;
static uint8_t scrollArtStack[16384] __attribute__((aligned(16)));
static volatile uint32_t scrollArtGeneration;
static struct {
  volatile int state; // 0: idle, 1: decoding, 2: ready
  int background;
  int targetIdx;
  uint32_t generation;
  char path[255];
  GSTEXTURE texture;
  int result;
} scrollArtJob;

static void classicArtWorker(void);
static void stopClassicArtWorker(void);
static void collectionArtWorker(void);
static void stopCollectionArtWorker(void);
static void collectionFarArtWorker(void);
static int collectionCoverPath(TargetList *titles, int selectedTitleIdx, int cacheIdx,
                               char *path, size_t capacity);
static void orbitArtWorker(void);
static void stopOrbitArtWorker(void);
static void gridArtWorker(void);
static void stopGridArtWorker(void);
static void scrollArtWorker(void);
static void stopScrollArtWorker(void);

void setGridCaseArtwork(int enabled) {
  enabled = enabled != 0;
  if (gridCaseArtwork && !enabled) {
    // Case thumbnails are larger than Grid's and have no use after exit.
    for (int i = 0; i < GRID_THUMBNAIL_CACHE_COUNT; i++) {
      GridThumbnailCacheEntry *entry = &gridThumbnailCache[i];
      if (!entry->caseArtwork)
        continue;
      free(entry->pixels);
      memset(entry, 0, sizeof(*entry));
    }
  }
  gridCaseArtwork = enabled;
}

static const char orbsArtPath[] = "/ART/ORBS";
static char artPathBuffer[255];

int artCacheInit(void) {
  coverTexture = calloc(sizeof(GSTEXTURE), 1);
  classicPreviousCoverTexture = calloc(sizeof(GSTEXTURE), 1);
  discTexture = calloc(sizeof(GSTEXTURE), 1);
  if (coverTexture == NULL || classicPreviousCoverTexture == NULL || discTexture == NULL) {
    artCacheShutdown();
    return -1;
  }
  coverTexture->Delayed = 1;
  classicPreviousCoverTexture->Delayed = 1;
  discTexture->Delayed = 1;

  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    psbbnCoverTextures[i] = calloc(sizeof(GSTEXTURE), 1);
    if (psbbnCoverTextures[i] == NULL) {
      artCacheShutdown();
      return -1;
    }
    psbbnCoverTextures[i]->Delayed = 1;
  }
  for (int buffer = 0; buffer < GRID_PAGE_BUFFERS; buffer++) {
    for (int i = 0; i < GRID_CACHE_PAGE_SIZE; i++) {
      gridCoverTextures[buffer][i] = calloc(sizeof(GSTEXTURE), 1);
      if (gridCoverTextures[buffer][i] == NULL) {
        artCacheShutdown();
        return -1;
      }
      gridCoverTextures[buffer][i]->Delayed = 1;
    }
  }
  for (int buffer = 0; buffer < GRID_SELECTED_BUFFERS; buffer++) {
    gridSelectedTextures[buffer] = calloc(sizeof(GSTEXTURE), 1);
    if (gridSelectedTextures[buffer] == NULL) {
      artCacheShutdown();
      return -1;
    }
    gridSelectedTextures[buffer]->Delayed = 1;
  }
  orbsBackgroundTexture = calloc(sizeof(GSTEXTURE), 1);
  if (orbsBackgroundTexture == NULL) {
    artCacheShutdown();
    return -1;
  }
  orbsBackgroundTexture->Delayed = 1;
  orbsBackgroundTarget = -1;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    orbsLogoTextures[i] = calloc(sizeof(GSTEXTURE), 1);
    if (orbsLogoTextures[i] == NULL) {
      artCacheShutdown();
      return -1;
    }
    orbsLogoTextures[i]->Delayed = 1;
    orbsLogoTargets[i] = -1;
  }
  ee_sema_t classicSemaphore;
  ee_thread_t classicThread;
  memset(&classicSemaphore, 0, sizeof(classicSemaphore));
  classicSemaphore.init_count = 0;
  classicSemaphore.max_count = 1;
  classicArtWakeSema = CreateSema(&classicSemaphore);
  classicArtDoneSema = CreateSema(&classicSemaphore);
  if (classicArtWakeSema >= 0 && classicArtDoneSema >= 0) {
    memset(&classicThread, 0, sizeof(classicThread));
    classicThread.func = classicArtWorker;
    classicThread.stack = classicArtStack;
    classicThread.stack_size = sizeof(classicArtStack);
    classicThread.gp_reg = &_gp;
    // Stay below the UI thread so decoding never interrupts a drawn frame.
    classicThread.initial_priority = 0x21;
    classicArtStopping = 0;
    classicArtThreadId = CreateThread(&classicThread);
    if (classicArtThreadId < 0 || StartThread(classicArtThreadId, NULL) < 0) {
      if (classicArtThreadId >= 0)
        DeleteThread(classicArtThreadId);
      classicArtThreadId = -1;
      stopClassicArtWorker();
    }
  } else {
    stopClassicArtWorker();
  }
  ee_sema_t orbitSemaphore;
  ee_thread_t orbitThread;
  memset(&orbitSemaphore, 0, sizeof(orbitSemaphore));
  orbitSemaphore.init_count = 0;
  orbitSemaphore.max_count = 1;
  orbitArtWakeSema = CreateSema(&orbitSemaphore);
  orbitArtDoneSema = CreateSema(&orbitSemaphore);
  if (orbitArtWakeSema >= 0 && orbitArtDoneSema >= 0) {
    memset(&orbitThread, 0, sizeof(orbitThread));
    orbitThread.func = orbitArtWorker;
    orbitThread.stack = orbitArtStack;
    orbitThread.stack_size = sizeof(orbitArtStack);
    orbitThread.gp_reg = &_gp;
    orbitThread.initial_priority = 0x1f;
    orbitArtStopping = 0;
    orbitArtThreadId = CreateThread(&orbitThread);
    if (orbitArtThreadId < 0 || StartThread(orbitArtThreadId, NULL) < 0) {
      if (orbitArtThreadId >= 0)
        DeleteThread(orbitArtThreadId);
      orbitArtThreadId = -1;
      stopOrbitArtWorker();
    }
  } else {
    stopOrbitArtWorker();
  }
  ee_sema_t gridSemaphore;
  ee_thread_t gridThread;
  memset(&gridSemaphore, 0, sizeof(gridSemaphore));
  gridSemaphore.init_count = 0;
  gridSemaphore.max_count = 1;
  gridArtWakeSema = CreateSema(&gridSemaphore);
  gridArtDoneSema = CreateSema(&gridSemaphore);
  if (gridArtWakeSema >= 0 && gridArtDoneSema >= 0) {
    memset(&gridThread, 0, sizeof(gridThread));
    gridThread.func = gridArtWorker;
    gridThread.stack = gridArtStack;
    gridThread.stack_size = sizeof(gridArtStack);
    gridThread.gp_reg = &_gp;
    gridThread.initial_priority = 0x1f;
    gridArtStopping = 0;
    gridArtThreadId = CreateThread(&gridThread);
    if (gridArtThreadId < 0 || StartThread(gridArtThreadId, NULL) < 0) {
      if (gridArtThreadId >= 0)
        DeleteThread(gridArtThreadId);
      gridArtThreadId = -1;
      stopGridArtWorker();
    }
  } else {
    stopGridArtWorker();
  }
  ee_sema_t scrollSemaphore;
  ee_thread_t scrollThread;
  memset(&scrollSemaphore, 0, sizeof(scrollSemaphore));
  scrollSemaphore.init_count = 0;
  scrollSemaphore.max_count = 1;
  scrollArtWakeSema = CreateSema(&scrollSemaphore);
  scrollArtDoneSema = CreateSema(&scrollSemaphore);
  if (scrollArtWakeSema >= 0 && scrollArtDoneSema >= 0) {
    memset(&scrollThread, 0, sizeof(scrollThread));
    scrollThread.func = scrollArtWorker;
    scrollThread.stack = scrollArtStack;
    scrollThread.stack_size = sizeof(scrollArtStack);
    scrollThread.gp_reg = &_gp;
    scrollThread.initial_priority = 0x1f;
    scrollArtStopping = 0;
    scrollArtThreadId = CreateThread(&scrollThread);
    if (scrollArtThreadId < 0 || StartThread(scrollArtThreadId, NULL) < 0) {
      if (scrollArtThreadId >= 0)
        DeleteThread(scrollArtThreadId);
      scrollArtThreadId = -1;
      stopScrollArtWorker();
    }
  } else {
    stopScrollArtWorker();
  }
  ee_sema_t semaphore;
  ee_thread_t thread;
  memset(&semaphore, 0, sizeof(semaphore));
  semaphore.init_count = 0;
  semaphore.max_count = 1;
  collectionArtWakeSema = CreateSema(&semaphore);
  collectionArtDoneSema = CreateSema(&semaphore);
  if (collectionArtWakeSema < 0 || collectionArtDoneSema < 0) {
    stopCollectionArtWorker();
    return 0; // Collection can still use the synchronous loader.
  }
  memset(&thread, 0, sizeof(thread));
  thread.func = collectionArtWorker;
  thread.stack = collectionArtStack;
  thread.stack_size = sizeof(collectionArtStack);
  thread.gp_reg = &_gp;
  // Give artwork priority over the UI (0x20) and ambient audio (0x21).
  thread.initial_priority = 0x1f;
  collectionArtStopping = 0;
  collectionArtThreadId = CreateThread(&thread);
  if (collectionArtThreadId < 0 || StartThread(collectionArtThreadId, NULL) < 0) {
    if (collectionArtThreadId >= 0)
      DeleteThread(collectionArtThreadId);
    collectionArtThreadId = -1;
    stopCollectionArtWorker();
  }
  return 0;
}

static void releaseClassicTextureVRAM(GSTEXTURE *texture) {
  // Invalidate only marks an upload dirty; the manager keeps the old block.
  // A replacement PNG may be larger, so release that block before decoding.
  gsKit_TexManager_free(gsGlobal, texture);
  texture->Vram = 0;
  texture->VramClut = 0;
}

static int loadCoverArtInto(struct DeviceMapEntry *device, char *titleID,
                            GSTEXTURE *texture) {
  if (device->metadev) { // Fallback to metadata device
    device = device->metadev;
  }
  // Reuse line buffer for building texture path
  snprintf(artPathBuffer, 255, "%s%s/%s_COV.png", device->mountpoint, artPath, titleID);
  releaseClassicTextureVRAM(texture);
  free(texture->Mem);
  texture->Mem = NULL;
  free(texture->Clut);
  texture->Clut = NULL;
  if (gsKit_texture_png(gsGlobal, texture, artPathBuffer)) {
    return -1;
  }
  gsKit_TexManager_bind(gsGlobal, texture);
  // Retain the decoded source in EE RAM. If the texture manager evicts the
  // cover while binding fonts or disc art, a later bind can safely re-upload it.
  return 0;
}

int loadCoverArt(struct DeviceMapEntry *device, char *titleID) {
  return loadCoverArtInto(device, titleID, coverTexture);
}

int loadNextClassicCoverArt(struct DeviceMapEntry *device, char *titleID) {
  GSTEXTURE *previous;
  int result = loadCoverArtInto(device, titleID, classicPreviousCoverTexture);
  previous = coverTexture;
  coverTexture = classicPreviousCoverTexture;
  classicPreviousCoverTexture = previous;
  return result;
}

void releaseClassicArtVRAM(void) {
  GSTEXTURE *textures[] = {coverTexture, classicPreviousCoverTexture, discTexture};
  for (int i = 0; i < 3; i++) {
    if (textures[i] != NULL)
      releaseClassicTextureVRAM(textures[i]);
  }
}

// OPL Manager stores transparent disc-label artwork as ART/<TITLE_ID>_ICO.png.
int loadDiscArt(struct DeviceMapEntry *device, char *titleID) {
  if (device->metadev) {
    device = device->metadev;
  }
  snprintf(artPathBuffer, 255, "%s%s/%s_ICO.png", device->mountpoint, artPath, titleID);
  releaseClassicTextureVRAM(discTexture);
  free(discTexture->Mem);
  discTexture->Mem = NULL;
  free(discTexture->Clut);
  discTexture->Clut = NULL;
  if (loadPNGTextureRGBA(gsGlobal, discTexture, artPathBuffer)) {
    return -1;
  }
  discTexture->Filter = GS_FILTER_LINEAR;
  return 0;
}

static void clearClassicArtJob(void) {
  free(classicArtJob.cover.Mem);
  free(classicArtJob.cover.Clut);
  free(classicArtJob.disc.Mem);
  free(classicArtJob.disc.Clut);
  memset(&classicArtJob.cover, 0, sizeof(classicArtJob.cover));
  memset(&classicArtJob.disc, 0, sizeof(classicArtJob.disc));
  classicArtJob.state = 0;
}

static void classicArtWorker(void) {
  while (1) {
    WaitSema(classicArtWakeSema);
    if (classicArtStopping)
      break;
    classicArtJob.coverResult = decodePNGTextureRGBA(gsGlobal, &classicArtJob.cover,
                                                     classicArtJob.coverPath);
    if (!classicArtPending || classicArtJob.generation != classicArtGeneration) {
      clearClassicArtJob();
      continue;
    }
    classicArtJob.discResult = decodePNGTextureRGBA(gsGlobal, &classicArtJob.disc,
                                                    classicArtJob.discPath);
    if (classicArtJob.coverResult != 0 || classicArtJob.cover.Mem == NULL ||
        classicArtJob.cover.Width <= 0 || classicArtJob.cover.Height <= 0) {
      free(classicArtJob.cover.Mem);
      free(classicArtJob.cover.Clut);
      memset(&classicArtJob.cover, 0, sizeof(classicArtJob.cover));
      classicArtJob.coverResult = -1;
    }
    if (classicArtJob.discResult != 0 || classicArtJob.disc.Mem == NULL ||
        classicArtJob.disc.Width <= 0 || classicArtJob.disc.Height <= 0) {
      free(classicArtJob.disc.Mem);
      free(classicArtJob.disc.Clut);
      memset(&classicArtJob.disc, 0, sizeof(classicArtJob.disc));
      classicArtJob.discResult = -1;
    }
    if (!classicArtPending || classicArtJob.generation != classicArtGeneration) {
      clearClassicArtJob();
      continue;
    }
    __asm__ __volatile__("" ::: "memory");
    classicArtJob.state = 2;
  }
  SignalSema(classicArtDoneSema);
  ExitThread();
}

static void stopClassicArtWorker(void) {
  if (classicArtThreadId >= 0) {
    classicArtStopping = 1;
    SignalSema(classicArtWakeSema);
    WaitSema(classicArtDoneSema);
    DeleteThread(classicArtThreadId);
    classicArtThreadId = -1;
  }
  if (classicArtWakeSema >= 0)
    DeleteSema(classicArtWakeSema);
  if (classicArtDoneSema >= 0)
    DeleteSema(classicArtDoneSema);
  classicArtWakeSema = classicArtDoneSema = -1;
  clearClassicArtJob();
}

static void queueClassicArtJob(void) {
  classicArtJob.generation = classicArtGeneration;
  snprintf(classicArtJob.coverPath, sizeof(classicArtJob.coverPath), "%s", classicCoverPath);
  snprintf(classicArtJob.discPath, sizeof(classicArtJob.discPath), "%s", classicDiscPath);
  classicArtJob.state = 1;
  SignalSema(classicArtWakeSema);
}

int requestClassicArt(struct DeviceMapEntry *device, char *titleID) {
  char coverPath[sizeof(classicCoverPath)];
  char discPath[sizeof(classicDiscPath)];
  int coverLength;
  int discLength;
  if (classicArtThreadId < 0 || device == NULL || titleID == NULL)
    return -1;
  if (device->metadev)
    device = device->metadev;
  if (device->mountpoint == NULL)
    return -1;
  coverLength = snprintf(coverPath, sizeof(coverPath), "%s%s/%s_COV.png",
                         device->mountpoint, artPath, titleID);
  discLength = snprintf(discPath, sizeof(discPath), "%s%s/%s_ICO.png",
                        device->mountpoint, artPath, titleID);
  if (coverLength < 0 || coverLength >= (int)sizeof(coverPath) ||
      discLength < 0 || discLength >= (int)sizeof(discPath))
    return -1;
  // Keep a decoded prefetch when Classic requests the same title on entry.
  if (classicArtPending && !strcmp(classicCoverPath, coverPath) &&
      !strcmp(classicDiscPath, discPath))
    return 0;
  if (classicArtJob.state == 2)
    clearClassicArtJob();
  strcpy(classicCoverPath, coverPath);
  strcpy(classicDiscPath, discPath);
  classicArtGeneration++;
  classicArtPending = 1;
  if (classicArtJob.state == 0)
    queueClassicArtJob();
  return 0;
}

void pumpClassicArtPrefetch(void) {
  if (classicArtPending && classicArtJob.state == 0)
    queueClassicArtJob();
}

void cancelClassicArt(void) {
  classicArtGeneration++;
  classicArtPending = 0;
  if (classicArtJob.state == 2)
    clearClassicArtJob();
}

static void adoptClassicTexture(GSTEXTURE *destination, GSTEXTURE *decoded) {
  releaseClassicTextureVRAM(destination);
  free(destination->Mem);
  free(destination->Clut);
  *destination = *decoded;
  memset(decoded, 0, sizeof(*decoded));
  destination->Delayed = 1;
  destination->Vram = 0;
  destination->VramClut = 0;
}

int serviceClassicArt(int *coverAvailable, int *discAvailable) {
  int finished = 0;
  if (classicArtJob.state == 2) {
    __asm__ __volatile__("" ::: "memory");
    if (classicArtPending && classicArtJob.generation == classicArtGeneration) {
      GSTEXTURE *previous = coverTexture;
      *coverAvailable = classicArtJob.coverResult == 0;
      *discAvailable = classicArtJob.discResult == 0;
      adoptClassicTexture(classicPreviousCoverTexture, &classicArtJob.cover);
      coverTexture = classicPreviousCoverTexture;
      classicPreviousCoverTexture = previous;
      adoptClassicTexture(discTexture, &classicArtJob.disc);
      discTexture->Filter = GS_FILTER_LINEAR;
      classicArtPending = 0;
      finished = 1;
    }
    clearClassicArtJob();
  }
  if (classicArtPending && classicArtJob.state == 0)
    queueClassicArtJob();
  return finished;
}

static void releasePSBBNCoverCacheEntry(int cacheIdx) {
  GSTEXTURE *texture = psbbnCoverTextures[cacheIdx];
  void *sourcePixels = psbbnCoverSourcePixels[cacheIdx];
  void *thumbnailPixels = collectionCoverThumbnailPixels[cacheIdx];
  void *previewPixels = collectionCoverPreviewPixels[cacheIdx];

  if (texture->Vram != 0)
    gsKit_TexManager_free(gsGlobal, texture);
  texture->Vram = 0;
  if (texture->Mem != NULL && texture->Mem != sourcePixels &&
      texture->Mem != thumbnailPixels && texture->Mem != previewPixels)
    free(texture->Mem);
  texture->Mem = NULL;
  CollectionArtPixels pixels = {
      sourcePixels, previewPixels, thumbnailPixels,
      psbbnCoverSourceWidth[cacheIdx], psbbnCoverSourceHeight[cacheIdx],
      sourcePixels != NULL ? (size_t)gsKit_texture_size(psbbnCoverSourceWidth[cacheIdx],
          psbbnCoverSourceHeight[cacheIdx], GS_PSM_CT32) +
          (PSBBN_PREVIEW_SIZE * PSBBN_PREVIEW_SIZE +
           PSBBN_THUMBNAIL_SIZE * PSBBN_THUMBNAIL_SIZE) * sizeof(PSBBNPixel) : 0};
  if (psbbnCoverLoaded[cacheIdx] &&
      collectionArtReusePut(&collectionReuseCache, collectionCoverKeys[cacheIdx], &pixels)) {
    sourcePixels = thumbnailPixels = previewPixels = NULL;
  }
  free(sourcePixels);
  free(thumbnailPixels);
  free(previewPixels);
  psbbnCoverSourcePixels[cacheIdx] = NULL;
  collectionCoverThumbnailPixels[cacheIdx] = NULL;
  collectionCoverPreviewPixels[cacheIdx] = NULL;
  psbbnCoverSourceWidth[cacheIdx] = 0;
  psbbnCoverSourceHeight[cacheIdx] = 0;
  psbbnCoverLoaded[cacheIdx] = 0;
  psbbnCoverFullResolution[cacheIdx] = 0;
  collectionCoverAttempted[cacheIdx] = 0;
  collectionCoverResolved[cacheIdx] = 0;
  collectionCoverResidentLevel[cacheIdx] = 0;
  collectionCoverKeys[cacheIdx][0] = '\0';
}

// PSBBN jacket art fades into the scene instead of sitting inside a hard
// rectangular card. Apply the feather once to the decoded EE-RAM pixels so it
// is inherited by both the focused texture and the runtime thumbnails.
static void featherPSBBNCoverEdges(GSTEXTURE *texture) {
  PSBBNPixel *pixels = (PSBBNPixel *)texture->Mem;
  int shortestSide = (texture->Width < texture->Height) ? texture->Width : texture->Height;
  int featherWidth = shortestSide * 7 / 100;

  if (pixels == NULL || featherWidth < 2)
    return;

  for (int y = 0; y < texture->Height; y++) {
    int edgeY = y;
    if (texture->Height - 1 - y < edgeY)
      edgeY = texture->Height - 1 - y;

    for (int x = 0; x < texture->Width; x++) {
      int edgeDistance = x;
      int inverseAlpha;
      int progress;
      int feather;

      if (texture->Width - 1 - x < edgeDistance)
        edgeDistance = texture->Width - 1 - x;
      if (edgeY < edgeDistance)
        edgeDistance = edgeY;
      if (edgeDistance >= featherWidth)
        continue;

      // Smoothstep avoids a visible straight threshold at the inside edge.
      progress = edgeDistance * 1000 / featherWidth;
      feather = (int)(((int64_t)progress * progress * (3000 - 2 * progress)) / 1000000LL);
      inverseAlpha = pixels[y * texture->Width + x].a;
      pixels[y * texture->Width + x].a = 128 - (((128 - inverseAlpha) * feather) / 1000);
    }
  }
}

static PSBBNPixel *createArtThumbnail(const PSBBNPixel *source, int width, int height,
                                      int thumbWidth, int thumbHeight) {
  PSBBNPixel *thumbnail = memalign(128, thumbWidth * thumbHeight * sizeof(*thumbnail));
  if (thumbnail == NULL)
    return NULL;
  for (int y = 0; y < thumbHeight; y++) {
    int sourceY1 = y * height / thumbHeight;
    int sourceY2 = (y + 1) * height / thumbHeight;
    if (sourceY2 <= sourceY1)
      sourceY2 = sourceY1 + 1;
    for (int x = 0; x < thumbWidth; x++) {
      int sourceX1 = x * width / thumbWidth;
      int sourceX2 = (x + 1) * width / thumbWidth;
      int red = 0, green = 0, blue = 0, alpha = 0, samples = 0;
      if (sourceX2 <= sourceX1)
        sourceX2 = sourceX1 + 1;
      for (int sourceY = sourceY1; sourceY < sourceY2; sourceY++) {
        for (int sourceX = sourceX1; sourceX < sourceX2; sourceX++) {
          PSBBNPixel pixel = source[sourceY * width + sourceX];
          red += pixel.r;
          green += pixel.g;
          blue += pixel.b;
          alpha += pixel.a;
          samples++;
        }
      }
      thumbnail[y * thumbWidth + x].r = red / samples;
      thumbnail[y * thumbWidth + x].g = green / samples;
      thumbnail[y * thumbWidth + x].b = blue / samples;
      thumbnail[y * thumbWidth + x].a = alpha / samples;
    }
  }
  return thumbnail;
}

static PSBBNPixel *createPSBBNThumbnail(const PSBBNPixel *source, int width, int height,
                                      int size) {
  return createArtThumbnail(source, width, height, size, size);
}

// Orbit keeps the decoded source and both smaller sizes in EE RAM, so a cover
// can change resolution without resampling it on the drawing thread.
static int loadPSBBNCoverArt(struct DeviceMapEntry *device, char *titleID, int cacheIdx, int selected) {
  GSTEXTURE *texture = psbbnCoverTextures[cacheIdx];

  if (device->metadev) { // Fallback to metadata device
    device = device->metadev;
  }
  releasePSBBNCoverCacheEntry(cacheIdx);
  snprintf(artPathBuffer, 255, "%s%s/%s.png", device->mountpoint, psbbnArtPath, titleID);
  if (decodePNGTextureRGBA(gsGlobal, texture, artPathBuffer) ||
      texture->Mem == NULL || texture->Width <= 0 || texture->Height <= 0) {
    releasePSBBNCoverCacheEntry(cacheIdx);
    return -1;
  }

  featherPSBBNCoverEdges(texture);
  PSBBNPixel *preview = createPSBBNThumbnail((const PSBBNPixel *)texture->Mem,
                                               texture->Width, texture->Height,
                                               PSBBN_PREVIEW_SIZE);
  PSBBNPixel *thumbnail = preview != NULL ?
      createPSBBNThumbnail(preview, PSBBN_PREVIEW_SIZE, PSBBN_PREVIEW_SIZE,
                           PSBBN_THUMBNAIL_SIZE) : NULL;
  if (thumbnail == NULL) {
    free(preview);
    releasePSBBNCoverCacheEntry(cacheIdx);
    return -1;
  }
  psbbnCoverSourcePixels[cacheIdx] = texture->Mem;
  psbbnCoverSourceWidth[cacheIdx] = texture->Width;
  psbbnCoverSourceHeight[cacheIdx] = texture->Height;
  collectionCoverThumbnailPixels[cacheIdx] = thumbnail;
  collectionCoverPreviewPixels[cacheIdx] = preview;
  texture->Mem = selected ? psbbnCoverSourcePixels[cacheIdx] : thumbnail;
  texture->Width = selected ? psbbnCoverSourceWidth[cacheIdx] : PSBBN_THUMBNAIL_SIZE;
  texture->Height = selected ? psbbnCoverSourceHeight[cacheIdx] : PSBBN_THUMBNAIL_SIZE;
  texture->PSM = GS_PSM_CT32;
  texture->Filter = GS_FILTER_LINEAR;
  texture->Delayed = 1;
  texture->VramClut = 0;
  psbbnCoverLoaded[cacheIdx] = 1;
  collectionCoverResidentLevel[cacheIdx] = selected ? 2 : 0;
  snprintf(collectionCoverKeys[cacheIdx], sizeof(collectionCoverKeys[cacheIdx]), "%s", artPathBuffer);
  psbbnCoverFullResolution[cacheIdx] = selected != 0;
  gsKit_TexManager_bind(gsGlobal, texture);
  return 0;
}

static int collectionCoverPath(TargetList *titles, int selectedTitleIdx, int cacheIdx,
                               char *path, size_t capacity) {
  int targetIdx = lunaNavWrap(titles->total, selectedTitleIdx + cacheIdx - PSBBN_COVER_CACHE_FOCUS);
  Target *target = getTargetByIdx(titles, targetIdx);
  struct DeviceMapEntry *device;
  if (target == NULL || target->id == NULL || target->device == NULL)
    return -1;
  device = target->device->metadev ? target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -1;
  int length = snprintf(path, capacity, "%s%s/%s.png", device->mountpoint,
                        psbbnArtPath, target->id);
  return length >= 0 && length < (int)capacity ? 0 : -1;
}

static void decodePSBBNArtJob(PSBBNArtJob *job) {
  GSTEXTURE decoded = {0};
  job->result = decodePNGTextureRGBATimed(gsGlobal, &decoded, job->path,
                                         &job->readMs, &job->decodeMs);
  if (job->result == 0 && decoded.Mem != NULL &&
      decoded.Width > 0 && decoded.Height > 0) {
    uint32_t stageMs = uiNowMs();
    featherPSBBNCoverEdges(&decoded);
    job->featherMs = uiNowMs() - stageMs;
    stageMs = uiNowMs();
    PSBBNPixel *preview = createPSBBNThumbnail((const PSBBNPixel *)decoded.Mem,
                                                decoded.Width, decoded.Height,
                                                PSBBN_PREVIEW_SIZE);
    PSBBNPixel *thumbnail = preview != NULL ?
        createPSBBNThumbnail(preview, PSBBN_PREVIEW_SIZE, PSBBN_PREVIEW_SIZE,
                             PSBBN_THUMBNAIL_SIZE) : NULL;
    job->resizeMs = uiNowMs() - stageMs;
    if (thumbnail != NULL && preview != NULL) {
      job->sourcePixels = decoded.Mem;
      job->previewPixels = preview;
      job->sourceWidth = decoded.Width;
      job->sourceHeight = decoded.Height;
      decoded.Mem = (u32 *)thumbnail;
      decoded.Width = PSBBN_THUMBNAIL_SIZE;
      decoded.Height = PSBBN_THUMBNAIL_SIZE;
      decoded.PSM = GS_PSM_CT32;
      decoded.Filter = GS_FILTER_LINEAR;
      decoded.Delayed = 1;
      decoded.Vram = 0;
      decoded.VramClut = 0;
      job->texture = decoded;
    } else {
      free(thumbnail);
      free(preview);
      job->result = -1;
    }
  } else {
    job->result = -1;
  }
  if (job->result != 0) {
    free(decoded.Mem);
    free(decoded.Clut);
  }
  job->readyMs = uiNowMs();
  __asm__ __volatile__("" ::: "memory");
  job->state = 2;
}

static void collectionArtWorker(void) {
  while (1) {
    WaitSema(collectionArtWakeSema);
    if (collectionArtStopping)
      break;
    decodePSBBNArtJob(&collectionArtJob);
  }
  SignalSema(collectionArtDoneSema);
  ExitThread();
}

static void collectionFarArtWorker(void) {
  while (1) {
    WaitSema(collectionFarArtWakeSema);
    if (collectionFarArtStopping)
      break;
    decodePSBBNArtJob(&collectionFarArtJob);
  }
  SignalSema(collectionFarArtDoneSema);
  ExitThread();
}

static void orbitArtWorker(void) {
  while (1) {
    WaitSema(orbitArtWakeSema);
    if (orbitArtStopping)
      break;
    decodePSBBNArtJob(&orbitArtJob);
  }
  SignalSema(orbitArtDoneSema);
  ExitThread();
}

static void clearPSBBNArtJob(PSBBNArtJob *job) {
  free(job->texture.Mem);
  free(job->texture.Clut);
  free(job->sourcePixels);
  free(job->previewPixels);
  memset(job, 0, sizeof(*job));
}

static void stopCollectionArtWorker(void) {
  if (collectionArtThreadId >= 0) {
    collectionArtStopping = 1;
    SignalSema(collectionArtWakeSema);
    WaitSema(collectionArtDoneSema);
    DeleteThread(collectionArtThreadId);
    collectionArtThreadId = -1;
  }
  if (collectionArtWakeSema >= 0)
    DeleteSema(collectionArtWakeSema);
  if (collectionArtDoneSema >= 0)
    DeleteSema(collectionArtDoneSema);
  collectionArtWakeSema = collectionArtDoneSema = -1;
  clearPSBBNArtJob(&collectionArtJob);
}

void stopCollectionFarArtWorker(TargetList *titles, int selectedTitleIdx) {
  if (collectionFarArtThreadId >= 0) {
    collectionFarArtStopping = 1;
    SignalSema(collectionFarArtWakeSema);
    WaitSema(collectionFarArtDoneSema);
    DeleteThread(collectionFarArtThreadId);
    collectionFarArtThreadId = -1;
  }
  if (collectionFarArtWakeSema >= 0)
    DeleteSema(collectionFarArtWakeSema);
  if (collectionFarArtDoneSema >= 0)
    DeleteSema(collectionFarArtDoneSema);
  collectionFarArtWakeSema = collectionFarArtDoneSema = -1;
  collectionFarArtStartAttempted = 0;
  // Its unfinished request is eligible for the persistent worker on re-entry.
  if (collectionFarArtJob.state != 0 && titles != NULL && titles->total > 0) {
    for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
      char path[255];
      if (collectionCoverAttempted[i] && !collectionCoverResolved[i] &&
          collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) == 0 &&
          strcmp(path, collectionFarArtJob.path) == 0)
        collectionCoverAttempted[i] = 0;
    }
  }
  clearPSBBNArtJob(&collectionFarArtJob);
}

static void startCollectionFarArtWorker(void) {
  if (collectionFarArtStartAttempted || collectionArtThreadId < 0)
    return;
  collectionFarArtStartAttempted = 1;
  ee_sema_t semaphore;
  ee_thread_t thread;
  memset(&semaphore, 0, sizeof(semaphore));
  semaphore.init_count = 0;
  semaphore.max_count = 1;
  collectionFarArtWakeSema = CreateSema(&semaphore);
  collectionFarArtDoneSema = CreateSema(&semaphore);
  if (collectionFarArtWakeSema < 0 || collectionFarArtDoneSema < 0) {
    stopCollectionFarArtWorker(NULL, 0);
    collectionFarArtStartAttempted = 1;
    return;
  }
  memset(&thread, 0, sizeof(thread));
  thread.func = collectionFarArtWorker;
  thread.stack = collectionFarArtStack;
  thread.stack_size = sizeof(collectionFarArtStack);
  thread.gp_reg = &_gp;
  thread.initial_priority = 0x1f;
  collectionFarArtStopping = 0;
  collectionFarArtThreadId = CreateThread(&thread);
  if (collectionFarArtThreadId < 0 || StartThread(collectionFarArtThreadId, NULL) < 0) {
    if (collectionFarArtThreadId >= 0)
      DeleteThread(collectionFarArtThreadId);
    collectionFarArtThreadId = -1;
    stopCollectionFarArtWorker(NULL, 0);
    collectionFarArtStartAttempted = 1;
  }
}

static void stopOrbitArtWorker(void) {
  if (orbitArtThreadId >= 0) {
    orbitArtStopping = 1;
    SignalSema(orbitArtWakeSema);
    WaitSema(orbitArtDoneSema);
    DeleteThread(orbitArtThreadId);
    orbitArtThreadId = -1;
  }
  if (orbitArtWakeSema >= 0)
    DeleteSema(orbitArtWakeSema);
  if (orbitArtDoneSema >= 0)
    DeleteSema(orbitArtDoneSema);
  orbitArtWakeSema = orbitArtDoneSema = -1;
  clearPSBBNArtJob(&orbitArtJob);
}

void releasePSBBNCovers(void) {
  // Retain nearby artwork last, so a full window exceeds the reuse budget by
  // evicting distant covers rather than the selection and its moving partner.
  static const uint8_t releaseOrder[PSBBN_COVER_CACHE_COUNT] = {9, 8, 7, 0, 6, 1, 5, 2, 4, 3};
  collectionArtGeneration++;
  orbitArtGeneration++;
  for (int p = 0; p < PSBBN_COVER_CACHE_COUNT; p++)
    releasePSBBNCoverCacheEntry(releaseOrder[p]);
}

void suspendCollectionCovers(void) {
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    if (psbbnCoverTextures[i]->Vram != 0)
      gsKit_TexManager_free(gsGlobal, psbbnCoverTextures[i]);
    psbbnCoverTextures[i]->Vram = 0;
  }
}

void adoptOrbitCoversForCollection(void) {
  // A pending Orbit decode cannot be adopted. Collection will request that
  // slot itself while keeping finished jackets and known missing files.
  orbitArtGeneration++;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    if (orbitArtThreadId < 0)
      collectionCoverResolved[i] = 1;
    collectionCoverAttempted[i] = collectionCoverResolved[i];
  }
}

void adoptCollectionCoversForOrbit(void) {
  // Finished Collection jackets already occupy the same slots Orbit needs.
  // Let Orbit request unfinished slots while a pending Collection decode winds down.
  collectionArtGeneration++;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++)
    collectionCoverAttempted[i] = collectionCoverResolved[i];
}

void releaseGridTexture(GSTEXTURE *texture) {
  for (int buffer = 0; buffer < GRID_SELECTED_BUFFERS; buffer++) {
    if (texture == gridSelectedTextures[buffer]) {
      gridSelectedGeneration[buffer]++;
      gridSelectedStatus[buffer] = 0;
      gridSelectedPath[buffer][0] = '\0';
      gridSelectedLoaded[buffer] = 0;
      break;
    }
  }
  if (texture->Vram != 0)
    gsKit_TexManager_free(gsGlobal, texture);
  texture->Vram = 0;
  free(texture->Mem);
  free(texture->Clut);
  texture->Mem = NULL;
  texture->Clut = NULL;
}

void releaseOrbsArt(void) {
  scrollArtGeneration++;
  serviceScrollArt(); // Drop a completed result before its slots are released.
  if (orbsBackgroundTexture != NULL)
    releaseGridTexture(orbsBackgroundTexture);
  orbsBackgroundLoaded = 0;
  orbsBackgroundTarget = -1;
  orbsBackgroundResolved = 0;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    if (orbsLogoTextures[i] != NULL)
      releaseGridTexture(orbsLogoTextures[i]);
    orbsLogoLoaded[i] = 0;
    orbsLogoTargets[i] = -1;
    orbsLogoResolved[i] = 0;
  }
}

static int orbsArtworkPath(Target *target, const char *suffix,
                           char *path, size_t capacity) {
  struct DeviceMapEntry *device;
  if (target == NULL || target->device == NULL || target->id == NULL)
    return -1;
  device = target->device->metadev ? target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -1;
  int length = snprintf(path, capacity, "%s%s/%s_%s.png",
                        device->mountpoint, orbsArtPath, target->id, suffix);
  return length >= 0 && length < (int)capacity ? 0 : -1;
}

static int queueScrollArt(Target *target, int background) {
  if (scrollArtJob.state != 0)
    return 0;
  if (orbsArtworkPath(target, background ? "BG" : "LGO",
                      scrollArtJob.path, sizeof(scrollArtJob.path)) < 0)
    return -1;
  scrollArtJob.background = background;
  scrollArtJob.targetIdx = target->idx;
  scrollArtJob.generation = scrollArtGeneration;
  scrollArtJob.state = 1;
  SignalSema(scrollArtWakeSema);
  return 1;
}

static void loadOrbsLogo(TargetList *titles, int targetIdx, int cacheIdx) {
  Target *target = getTargetByIdx(titles, targetIdx);
  struct DeviceMapEntry *device = target->device;
  GSTEXTURE *texture = orbsLogoTextures[cacheIdx];

  if (device->metadev)
    device = device->metadev;
  releaseGridTexture(texture);
  snprintf(artPathBuffer, sizeof(artPathBuffer), "%s%s/%s_LGO.png",
           device->mountpoint, orbsArtPath, target->id);
  orbsLogoLoaded[cacheIdx] =
      loadPNGTextureRGBA(gsGlobal, texture, artPathBuffer) == 0;
  if (orbsLogoLoaded[cacheIdx])
    texture->Filter = GS_FILTER_LINEAR;
  orbsLogoTargets[cacheIdx] = targetIdx;
  orbsLogoResolved[cacheIdx] = 1;
}

void refreshOrbsLogos(TargetList *titles, int selectedTitleIdx) {
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    int wanted = lunaNavWrap(titles->total,
                             selectedTitleIdx + i - ORBS_LOGO_CACHE_FOCUS);
    int match = -1;
    if (orbsLogoTargets[i] == wanted)
      continue;
    for (int j = i + 1; j < ORBS_LOGO_CACHE_COUNT; j++) {
      if (orbsLogoTargets[j] == wanted) {
        match = j;
        break;
      }
    }
    if (match >= 0) {
      GSTEXTURE *texture = orbsLogoTextures[i];
      uint8_t loaded = orbsLogoLoaded[i];
      uint8_t resolved = orbsLogoResolved[i];
      int target = orbsLogoTargets[i];
      orbsLogoTextures[i] = orbsLogoTextures[match];
      orbsLogoLoaded[i] = orbsLogoLoaded[match];
      orbsLogoResolved[i] = orbsLogoResolved[match];
      orbsLogoTargets[i] = orbsLogoTargets[match];
      orbsLogoTextures[match] = texture;
      orbsLogoLoaded[match] = loaded;
      orbsLogoResolved[match] = resolved;
      orbsLogoTargets[match] = target;
    } else {
      if (scrollArtThreadId < 0) {
        loadOrbsLogo(titles, wanted, i);
      } else {
        releaseGridTexture(orbsLogoTextures[i]);
        orbsLogoLoaded[i] = 0;
        orbsLogoResolved[i] = 0;
        orbsLogoTargets[i] = wanted;
      }
    }
  }
  if (scrollArtThreadId >= 0 && scrollArtJob.state == 0) {
    static const uint8_t priority[ORBS_LOGO_CACHE_COUNT] = {3, 2, 4, 1, 5, 0, 6};
    for (int p = 0; p < ORBS_LOGO_CACHE_COUNT; p++) {
      int i = priority[p];
      if (orbsLogoResolved[i])
        continue;
      int queued = queueScrollArt(getTargetByIdx(titles, orbsLogoTargets[i]), 0);
      if (queued < 0)
        orbsLogoResolved[i] = 1;
      else
        break;
    }
  }
}

void refreshOrbsBackground(Target *target) {
  struct DeviceMapEntry *device;
  if (orbsBackgroundTarget == target->idx) {
    if (scrollArtThreadId >= 0 && !orbsBackgroundResolved &&
        queueScrollArt(target, 1) < 0)
      orbsBackgroundResolved = 1;
    return;
  }
  if (scrollArtThreadId >= 0) {
    releaseGridTexture(orbsBackgroundTexture);
    orbsBackgroundLoaded = 0;
    orbsBackgroundResolved = 0;
    orbsBackgroundTarget = target->idx;
    if (queueScrollArt(target, 1) < 0)
      orbsBackgroundResolved = 1;
    return;
  }
  device = target->device;
  if (device->metadev)
    device = device->metadev;
  releaseGridTexture(orbsBackgroundTexture);
  snprintf(artPathBuffer, sizeof(artPathBuffer), "%s%s/%s_BG.png",
           device->mountpoint, orbsArtPath, target->id);
  orbsBackgroundLoaded =
      loadPNGTextureRGBA(gsGlobal, orbsBackgroundTexture, artPathBuffer) == 0;
  if (orbsBackgroundLoaded)
    orbsBackgroundTexture->Filter = GS_FILTER_LINEAR;
  orbsBackgroundTarget = target->idx;
  orbsBackgroundResolved = 1;
}

static void scrollArtWorker(void) {
  while (1) {
    WaitSema(scrollArtWakeSema);
    if (scrollArtStopping)
      break;
    GSTEXTURE decoded = {0};
    decoded.Delayed = 1;
    scrollArtJob.result = decodePNGTextureRGBA(gsGlobal, &decoded, scrollArtJob.path);
    if (scrollArtJob.result == 0 && decoded.Mem != NULL &&
        decoded.Width > 0 && decoded.Height > 0) {
      decoded.Filter = GS_FILTER_LINEAR;
      decoded.Delayed = 1;
      decoded.Vram = 0;
      decoded.VramClut = 0;
      scrollArtJob.texture = decoded;
    } else {
      scrollArtJob.result = -1;
      free(decoded.Mem);
      free(decoded.Clut);
    }
    if (scrollArtJob.generation != scrollArtGeneration) {
      free(scrollArtJob.texture.Mem);
      free(scrollArtJob.texture.Clut);
      memset(&scrollArtJob.texture, 0, sizeof(scrollArtJob.texture));
      scrollArtJob.result = -1;
    }
    __asm__ __volatile__("" ::: "memory");
    scrollArtJob.state = 2;
  }
  SignalSema(scrollArtDoneSema);
  ExitThread();
}

static void stopScrollArtWorker(void) {
  if (scrollArtThreadId >= 0) {
    scrollArtStopping = 1;
    SignalSema(scrollArtWakeSema);
    WaitSema(scrollArtDoneSema);
    DeleteThread(scrollArtThreadId);
    scrollArtThreadId = -1;
  }
  if (scrollArtWakeSema >= 0)
    DeleteSema(scrollArtWakeSema);
  if (scrollArtDoneSema >= 0)
    DeleteSema(scrollArtDoneSema);
  scrollArtWakeSema = scrollArtDoneSema = -1;
  free(scrollArtJob.texture.Mem);
  free(scrollArtJob.texture.Clut);
  memset(&scrollArtJob, 0, sizeof(scrollArtJob));
}

void serviceScrollArt(void) {
  if (scrollArtThreadId < 0 || scrollArtJob.state != 2)
    return;
  __asm__ __volatile__("" ::: "memory");
  if (scrollArtJob.generation == scrollArtGeneration) {
    if (scrollArtJob.background) {
      if (orbsBackgroundTarget == scrollArtJob.targetIdx &&
          !orbsBackgroundResolved) {
        releaseGridTexture(orbsBackgroundTexture);
        if (scrollArtJob.result == 0) {
          *orbsBackgroundTexture = scrollArtJob.texture;
          memset(&scrollArtJob.texture, 0, sizeof(scrollArtJob.texture));
          orbsBackgroundLoaded = 1;
          gsKit_TexManager_bind(gsGlobal, orbsBackgroundTexture);
        }
        orbsBackgroundResolved = 1;
      }
    } else {
      static const uint8_t priority[ORBS_LOGO_CACHE_COUNT] = {3, 2, 4, 1, 5, 0, 6};
      for (int p = 0; p < ORBS_LOGO_CACHE_COUNT; p++) {
        int i = priority[p];
        if (orbsLogoTargets[i] != scrollArtJob.targetIdx || orbsLogoResolved[i])
          continue;
        releaseGridTexture(orbsLogoTextures[i]);
        if (scrollArtJob.result == 0) {
          *orbsLogoTextures[i] = scrollArtJob.texture;
          memset(&scrollArtJob.texture, 0, sizeof(scrollArtJob.texture));
          orbsLogoLoaded[i] = 1;
          gsKit_TexManager_bind(gsGlobal, orbsLogoTextures[i]);
        }
        orbsLogoResolved[i] = 1;
        break;
      }
    }
  }
  free(scrollArtJob.texture.Mem);
  free(scrollArtJob.texture.Clut);
  memset(&scrollArtJob, 0, sizeof(scrollArtJob));
}

static int gridArtworkPath(Target *target, char *path, size_t capacity) {
  struct DeviceMapEntry *device;
  if (target == NULL || target->device == NULL || target->id == NULL)
    return -1;
  device = target->device->metadev ? target->device->metadev : target->device;
  if (device->mountpoint == NULL)
    return -1;
  int length = gridCaseArtwork ? snprintf(path, capacity, "%s/ART/%s_COV.png", device->mountpoint, target->id)
      : snprintf(path, capacity, "%s%s/%s.png", device->mountpoint,
                 psbbnArtPath, target->id);
  return length >= 0 && length < (int)capacity ? 0 : -1;
}

static int findGridThumbnail(const char *path) {
  for (int i = 0; i < GRID_THUMBNAIL_CACHE_COUNT; i++) {
    if (gridThumbnailCache[i].state && !strcmp(gridThumbnailCache[i].path, path))
      return i;
  }
  return -1;
}

static void rememberGridThumbnail(const char *path, const PSBBNPixel *pixels,
                                   int width, int height) {
  int victim = -1;
  // Keep at most one case page in RAM; Grid can retain its smaller 64x64 tiles.
  int limit = gridCaseArtwork ? CASE_GRID_PAGE_SIZE : GRID_THUMBNAIL_CACHE_COUNT;
  for (int i = 0; i < limit; i++) {
    if (!gridThumbnailCache[i].state) {
      victim = i;
      break;
    }
    if (victim < 0 || gridThumbnailCache[i].lastUsed < gridThumbnailCache[victim].lastUsed)
      victim = i;
  }
  GridThumbnailCacheEntry *entry = &gridThumbnailCache[victim];
  if (pixels != NULL) {
    size_t bytes = (size_t)width * height * sizeof(*pixels);
    PSBBNPixel *resized = realloc(entry->pixels, bytes);
    if (resized == NULL) return;
    entry->pixels = resized;
    memcpy(entry->pixels, pixels, bytes);
  } else {
    free(entry->pixels);
    entry->pixels = NULL;
  }
  snprintf(entry->path, sizeof(entry->path), "%s", path);
  entry->width = width;
  entry->height = height;
  entry->lastUsed = ++gridThumbnailCacheClock;
  entry->state = pixels != NULL ? 1 : 2;
  entry->caseArtwork = gridCaseArtwork;
}

static int loadGridCoverArt(struct DeviceMapEntry *device, char *titleID, GSTEXTURE *texture, int thumbnail) {
  if (device->metadev)
    device = device->metadev;

  releaseGridTexture(texture);
  if (gridCaseArtwork)
    snprintf(artPathBuffer, 255, "%s/ART/%s_COV.png", device->mountpoint, titleID);
  else
    snprintf(artPathBuffer, 255, "%s%s/%s.png", device->mountpoint, psbbnArtPath, titleID);
  if (thumbnail) {
    int cached = findGridThumbnail(artPathBuffer);
    if (cached >= 0) {
      GridThumbnailCacheEntry *entry = &gridThumbnailCache[cached];
      entry->lastUsed = ++gridThumbnailCacheClock;
      if (entry->state == 2)
        return -1;
      size_t bytes = (size_t)entry->width * entry->height * sizeof(PSBBNPixel);
      texture->Mem = memalign(128, bytes);
      if (texture->Mem == NULL)
        return -1;
      memcpy(texture->Mem, entry->pixels, bytes);
      texture->Width = entry->width;
      texture->Height = entry->height;
      texture->VramClut = 0;
      texture->Clut = NULL;
      texture->PSM = GS_PSM_CT32;
      texture->Filter = GS_FILTER_LINEAR;
      gsKit_TexManager_bind(gsGlobal, texture);
      return 0;
    }
  }
  if (thumbnail ? decodePNGTextureRGBA(gsGlobal, texture, artPathBuffer) : loadPNGTextureRGBA(gsGlobal, texture, artPathBuffer)) {
    if (thumbnail) {
      // A missing file stays a placeholder on later visits; other read or
      // decode failures may be retried after the page buffer is replaced.
      FILE *file = fopen(artPathBuffer, "rb");
      if (file == NULL)
        rememberGridThumbnail(artPathBuffer, NULL, 0, 0);
      else
        fclose(file);
      releaseGridTexture(texture);
    }
    return -1;
  }
  texture->Filter = GS_FILTER_LINEAR;

  if (thumbnail) {
    int width = gridCaseArtwork ? CASE_THUMBNAIL_WIDTH : GRID_THUMBNAIL_SIZE;
    int height = gridCaseArtwork ? CASE_THUMBNAIL_HEIGHT : GRID_THUMBNAIL_SIZE;
    PSBBNPixel *pixels = createArtThumbnail((const PSBBNPixel *)texture->Mem,
        texture->Width, texture->Height, width, height);
    if (pixels == NULL) {
      releaseGridTexture(texture);
      return -1;
    }
    free(texture->Mem);
    texture->Mem = (u32 *)pixels;
    texture->Width = width;
    texture->Height = height;
    texture->VramClut = 0;
    texture->Clut = NULL;
    texture->PSM = GS_PSM_CT32;
    texture->Filter = GS_FILTER_LINEAR;
    gsKit_TexManager_bind(gsGlobal, texture);
    rememberGridThumbnail(artPathBuffer, pixels, width, height);
  }
  return 0;
}

static void gridArtWorker(void) {
  while (1) {
    WaitSema(gridArtWakeSema);
    if (gridArtStopping)
      break;
    GSTEXTURE decoded = {0};
    decoded.Delayed = 1;
    gridArtJob.result = decodePNGTextureRGBA(gsGlobal, &decoded, gridArtJob.path);
    if (gridArtJob.result == 0 && decoded.Mem != NULL &&
        decoded.Width > 0 && decoded.Height > 0) {
      if (gridArtJob.thumbnail == 1 || gridArtJob.thumbnail == 3) {
        int width = gridArtJob.thumbnail == 3 ? CASE_THUMBNAIL_WIDTH : GRID_THUMBNAIL_SIZE;
        int height = gridArtJob.thumbnail == 3 ? CASE_THUMBNAIL_HEIGHT : GRID_THUMBNAIL_SIZE;
        PSBBNPixel *pixels = createArtThumbnail((const PSBBNPixel *)decoded.Mem,
            decoded.Width, decoded.Height, width, height);
        if (pixels == NULL) {
          gridArtJob.result = -1;
        } else {
          free(decoded.Mem);
          decoded.Mem = (u32 *)pixels;
          decoded.Width = width;
          decoded.Height = height;
        }
      }
      if (gridArtJob.result == 0) {
        decoded.PSM = GS_PSM_CT32;
        decoded.Filter = GS_FILTER_LINEAR;
        decoded.Delayed = 1;
        decoded.Vram = 0;
        decoded.VramClut = 0;
        gridArtJob.texture = decoded;
      }
    } else {
      gridArtJob.result = -1;
    }
    if (gridArtJob.result != 0) {
      FILE *file = fopen(gridArtJob.path, "rb");
      gridArtJob.missingFile = file == NULL;
      if (file != NULL)
        fclose(file);
      free(decoded.Mem);
      free(decoded.Clut);
    }
    __asm__ __volatile__("" ::: "memory");
    gridArtJob.state = 2;
  }
  SignalSema(gridArtDoneSema);
  ExitThread();
}

static void stopGridArtWorker(void) {
  if (gridArtThreadId >= 0) {
    gridArtStopping = 1;
    SignalSema(gridArtWakeSema);
    WaitSema(gridArtDoneSema);
    DeleteThread(gridArtThreadId);
    gridArtThreadId = -1;
  }
  if (gridArtWakeSema >= 0)
    DeleteSema(gridArtWakeSema);
  if (gridArtDoneSema >= 0)
    DeleteSema(gridArtDoneSema);
  gridArtWakeSema = gridArtDoneSema = -1;
  free(gridArtJob.texture.Mem);
  free(gridArtJob.texture.Clut);
  memset(&gridArtJob, 0, sizeof(gridArtJob));
}

int serviceGridArt(void) {
  if (gridArtThreadId < 0 || gridArtJob.state != 2)
    return 0;
  __asm__ __volatile__("" ::: "memory");
  if (gridArtJob.thumbnail) {
    int buffer = gridArtJob.buffer;
    int slot = gridArtJob.slot;
    if (gridArtJob.generation == gridPageGeneration[buffer] &&
        gridCoverAttempted[buffer][slot] && !gridCoverResolved[buffer][slot]) {
      if (gridArtJob.result == 0) {
        GSTEXTURE *texture = gridCoverTextures[buffer][slot];
        releaseGridTexture(texture);
        *texture = gridArtJob.texture;
        memset(&gridArtJob.texture, 0, sizeof(gridArtJob.texture));
        gridCoverLoaded[buffer][slot] = 1;
        if (gridArtJob.thumbnail == 1 || gridArtJob.thumbnail == 3)
          rememberGridThumbnail(gridArtJob.path, (const PSBBNPixel *)texture->Mem,
                                 texture->Width, texture->Height);
        gsKit_TexManager_bind(gsGlobal, texture);
      } else if (gridArtJob.missingFile &&
                 (gridArtJob.thumbnail == 1 || gridArtJob.thumbnail == 3)) {
        rememberGridThumbnail(gridArtJob.path, NULL, 0, 0);
      }
      gridCoverResolved[buffer][slot] = 1;
    }
  } else {
    int buffer = gridArtJob.buffer;
    if (gridArtJob.generation == gridSelectedGeneration[buffer] &&
        gridSelectedStatus[buffer] == 1 &&
        strcmp(gridSelectedPath[buffer], gridArtJob.path) == 0) {
      GSTEXTURE *texture = gridSelectedTextures[buffer];
      releaseGridTexture(texture);
      snprintf(gridSelectedPath[buffer], sizeof(gridSelectedPath[buffer]),
               "%s", gridArtJob.path);
      if (gridArtJob.result == 0) {
        *texture = gridArtJob.texture;
        memset(&gridArtJob.texture, 0, sizeof(gridArtJob.texture));
        gridSelectedLoaded[buffer] = 1;
        gridSelectedStatus[buffer] = 2;
        gsKit_TexManager_bind(gsGlobal, texture);
      } else {
        gridSelectedStatus[buffer] = 3;
      }
    }
  }
  free(gridArtJob.texture.Mem);
  free(gridArtJob.texture.Clut);
  memset(&gridArtJob, 0, sizeof(gridArtJob));
  return 1;
}

void releaseGridCovers(void) {
  for (int buffer = 0; buffer < GRID_PAGE_BUFFERS; buffer++) {
    gridPageGeneration[buffer]++;
    for (int slot = 0; slot < GRID_CACHE_PAGE_SIZE; slot++) {
      releaseGridTexture(gridCoverTextures[buffer][slot]);
      gridCoverLoaded[buffer][slot] = 0;
      gridCoverAttempted[buffer][slot] = 0;
      gridCoverResolved[buffer][slot] = 0;
    }
  }
  for (int buffer = 0; buffer < GRID_SELECTED_BUFFERS; buffer++) {
    releaseGridTexture(gridSelectedTextures[buffer]);
    gridSelectedLoaded[buffer] = 0;
  }
}

static void resetGridPageBuffer(int buffer) {
  gridPageGeneration[buffer]++;
  for (int slot = 0; slot < GRID_CACHE_PAGE_SIZE; slot++) {
    releaseGridTexture(gridCoverTextures[buffer][slot]);
    gridCoverLoaded[buffer][slot] = 0;
    gridCoverAttempted[buffer][slot] = 0;
    gridCoverResolved[buffer][slot] = 0;
  }
}

int gridPageSlotReady(int buffer, int slot) {
  return buffer >= 0 && buffer < GRID_PAGE_BUFFERS &&
         slot >= 0 && slot < GRID_CACHE_PAGE_SIZE && gridCoverResolved[buffer][slot];
}

// Queue one requested tile first, then resume the remaining slots in order.
// A missing PNG still resolves its slot so the page can show a placeholder.
int loadGridPageStep(TargetList *titles, int pageBase, int buffer, int *nextSlot,
                     int prioritySlot, int *didLoadArtwork) {
  const int pageSize = gridCaseArtwork ? CASE_GRID_PAGE_SIZE : GRID_PAGE_SIZE;
  *didLoadArtwork = 0;
  if (gridArtThreadId >= 0) {
    while (1) {
      int slot = prioritySlot >= 0 && prioritySlot < pageSize &&
                 !gridCoverAttempted[buffer][prioritySlot]
          ? prioritySlot : -1;
      if (slot < 0) {
        while (*nextSlot < pageSize && gridCoverAttempted[buffer][*nextSlot])
          (*nextSlot)++;
        if (*nextSlot >= pageSize)
          break;
        slot = *nextSlot;
      }
      int targetIdx = pageBase + slot;
      if (targetIdx >= titles->total) {
        gridCoverAttempted[buffer][slot] = 1;
        gridCoverResolved[buffer][slot] = 1;
        continue;
      }
      Target *target = getTargetByIdx(titles, targetIdx);
      char path[255];
      if (gridArtworkPath(target, path, sizeof(path)) < 0) {
        gridCoverAttempted[buffer][slot] = 1;
        gridCoverResolved[buffer][slot] = 1;
        continue;
      }
      if (findGridThumbnail(path) >= 0) {
        gridCoverLoaded[buffer][slot] =
            loadGridCoverArt(target->device, target->id,
                             gridCoverTextures[buffer][slot], 1) == 0;
        gridCoverAttempted[buffer][slot] = 1;
        gridCoverResolved[buffer][slot] = 1;
        *didLoadArtwork = 1;
      } else if (gridArtJob.state == 0) {
        gridArtJob.thumbnail = gridCaseArtwork ? 3 : 1;
        gridArtJob.buffer = buffer;
        gridArtJob.slot = slot;
        gridArtJob.generation = gridPageGeneration[buffer];
        snprintf(gridArtJob.path, sizeof(gridArtJob.path), "%s", path);
        gridArtJob.state = 1;
        gridCoverAttempted[buffer][slot] = 1;
        SignalSema(gridArtWakeSema);
        *didLoadArtwork = 1;
      }
      break;
    }
    for (int slot = 0; slot < pageSize; slot++) {
      if (!gridCoverResolved[buffer][slot])
        return 0;
    }
    return 1;
  }
  if (prioritySlot >= 0 && prioritySlot < pageSize &&
      !gridCoverAttempted[buffer][prioritySlot]) {
    int targetIdx = pageBase + prioritySlot;
    gridCoverAttempted[buffer][prioritySlot] = 1;
    gridCoverResolved[buffer][prioritySlot] = 1;
    if (targetIdx < titles->total) {
      Target *target = getTargetByIdx(titles, targetIdx);
      gridCoverLoaded[buffer][prioritySlot] =
          (loadGridCoverArt(target->device, target->id,
                            gridCoverTextures[buffer][prioritySlot], 1) == 0);
      *didLoadArtwork = 1;
    }
    return 0;
  }
  while (*nextSlot < pageSize) {
    int slot = (*nextSlot)++;
    int targetIdx = pageBase + slot;

    if (gridCoverAttempted[buffer][slot])
      continue;
    gridCoverAttempted[buffer][slot] = 1;
    gridCoverResolved[buffer][slot] = 1;

    if (targetIdx < titles->total) {
      Target *target = getTargetByIdx(titles, targetIdx);
      gridCoverLoaded[buffer][slot] =
          (loadGridCoverArt(target->device, target->id, gridCoverTextures[buffer][slot], 1) == 0);
      *didLoadArtwork = 1;
      break;
    }
  }
  return *nextSlot >= pageSize;
}

int refreshGridSelectedCover(Target *target, int buffer) {
  if (gridArtThreadId >= 0) {
    char path[255];
    int pathResult = gridArtworkPath(target, path, sizeof(path));
    if (pathResult < 0)
      return 0;
    if (strcmp(gridSelectedPath[buffer], path) != 0) {
      releaseGridTexture(gridSelectedTextures[buffer]);
      snprintf(gridSelectedPath[buffer], sizeof(gridSelectedPath[buffer]), "%s", path);
    }
    if (gridSelectedStatus[buffer] == 2)
      return 1;
    if (gridSelectedStatus[buffer] == 3)
      return 0;
    if (gridSelectedStatus[buffer] == 1 || gridArtJob.state != 0)
      return -1;
    gridArtJob.thumbnail = 0;
    gridArtJob.buffer = buffer;
    gridArtJob.slot = -1;
    gridArtJob.generation = gridSelectedGeneration[buffer];
    snprintf(gridArtJob.path, sizeof(gridArtJob.path), "%s", path);
    gridArtJob.state = 1;
    gridSelectedStatus[buffer] = 1;
    SignalSema(gridArtWakeSema);
    return -1;
  }
  gridSelectedLoaded[buffer] = (loadGridCoverArt(target->device, target->id, gridSelectedTextures[buffer], 0) == 0);
  return gridSelectedLoaded[buffer];
}

void prepareGridPageBuffer(int buffer, int pageBase, int *pageBases, int *pageComplete, int *pageNextSlot) {
  resetGridPageBuffer(buffer);
  pageBases[buffer] = pageBase;
  pageComplete[buffer] = 0;
  pageNextSlot[buffer] = 0;
}

static void loadPSBBNCoverCacheEntry(TargetList *titles, int selectedTitleIdx, int cacheIdx,
                                     int useFullResolution) {
  int targetIdx = lunaNavWrap(titles->total, selectedTitleIdx + cacheIdx - PSBBN_COVER_CACHE_FOCUS);
  Target *target = getTargetByIdx(titles, targetIdx);
  psbbnCoverLoaded[cacheIdx] = (loadPSBBNCoverArt(target->device, target->id, cacheIdx,
                                                  useFullResolution &&
                                                      (cacheIdx == PSBBN_COVER_CACHE_FOCUS ||
                                                       cacheIdx == PSBBN_COVER_CACHE_FOCUS - 1)) == 0);
}

void refreshPSBBNCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx,
                        int useFullResolution) {
  int direction = lunaNavDirection(titles->total, previousTitleIdx, selectedTitleIdx);

  if (previousTitleIdx >= 0 && direction > 0 && lunaNavWrap(titles->total, previousTitleIdx + 1) == selectedTitleIdx) {
    char recycledKey[255];
    strcpy(recycledKey, collectionCoverKeys[0]);
    GSTEXTURE *recycledTexture = psbbnCoverTextures[0];
    void *recycledSource = psbbnCoverSourcePixels[0];
    void *recycledThumbnail = collectionCoverThumbnailPixels[0];
    void *recycledPreview = collectionCoverPreviewPixels[0];
    int recycledWidth = psbbnCoverSourceWidth[0];
    int recycledHeight = psbbnCoverSourceHeight[0];
    uint8_t recycledFullResolution = psbbnCoverFullResolution[0];
    uint8_t recycledLevel = collectionCoverResidentLevel[0];
    for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT - 1; cacheIdx++) {
      psbbnCoverTextures[cacheIdx] = psbbnCoverTextures[cacheIdx + 1];
      psbbnCoverLoaded[cacheIdx] = psbbnCoverLoaded[cacheIdx + 1];
      psbbnCoverSourcePixels[cacheIdx] = psbbnCoverSourcePixels[cacheIdx + 1];
      collectionCoverThumbnailPixels[cacheIdx] = collectionCoverThumbnailPixels[cacheIdx + 1];
      collectionCoverPreviewPixels[cacheIdx] = collectionCoverPreviewPixels[cacheIdx + 1];
      psbbnCoverSourceWidth[cacheIdx] = psbbnCoverSourceWidth[cacheIdx + 1];
      psbbnCoverSourceHeight[cacheIdx] = psbbnCoverSourceHeight[cacheIdx + 1];
      psbbnCoverFullResolution[cacheIdx] = psbbnCoverFullResolution[cacheIdx + 1];
      collectionCoverResidentLevel[cacheIdx] = collectionCoverResidentLevel[cacheIdx + 1];
      strcpy(collectionCoverKeys[cacheIdx], collectionCoverKeys[cacheIdx + 1]);
    }
    psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT - 1] = recycledTexture;
    psbbnCoverSourcePixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledSource;
    collectionCoverThumbnailPixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledThumbnail;
    collectionCoverPreviewPixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledPreview;
    psbbnCoverSourceWidth[PSBBN_COVER_CACHE_COUNT - 1] = recycledWidth;
    psbbnCoverSourceHeight[PSBBN_COVER_CACHE_COUNT - 1] = recycledHeight;
    psbbnCoverFullResolution[PSBBN_COVER_CACHE_COUNT - 1] = recycledFullResolution;
    collectionCoverResidentLevel[PSBBN_COVER_CACHE_COUNT - 1] = recycledLevel;
    strcpy(collectionCoverKeys[PSBBN_COVER_CACHE_COUNT - 1], recycledKey);
    releasePSBBNCoverCacheEntry(PSBBN_COVER_CACHE_COUNT - 1);
    loadPSBBNCoverCacheEntry(titles, selectedTitleIdx, PSBBN_COVER_CACHE_COUNT - 1,
                            useFullResolution);
    return;
  }

  if (previousTitleIdx >= 0 && direction < 0 && lunaNavWrap(titles->total, previousTitleIdx - 1) == selectedTitleIdx) {
    char recycledKey[255];
    strcpy(recycledKey, collectionCoverKeys[PSBBN_COVER_CACHE_COUNT - 1]);
    GSTEXTURE *recycledTexture = psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledSource = psbbnCoverSourcePixels[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledThumbnail = collectionCoverThumbnailPixels[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledPreview = collectionCoverPreviewPixels[PSBBN_COVER_CACHE_COUNT - 1];
    int recycledWidth = psbbnCoverSourceWidth[PSBBN_COVER_CACHE_COUNT - 1];
    int recycledHeight = psbbnCoverSourceHeight[PSBBN_COVER_CACHE_COUNT - 1];
    uint8_t recycledFullResolution = psbbnCoverFullResolution[PSBBN_COVER_CACHE_COUNT - 1];
    uint8_t recycledLevel = collectionCoverResidentLevel[PSBBN_COVER_CACHE_COUNT - 1];
    for (int cacheIdx = PSBBN_COVER_CACHE_COUNT - 1; cacheIdx > 0; cacheIdx--) {
      psbbnCoverTextures[cacheIdx] = psbbnCoverTextures[cacheIdx - 1];
      psbbnCoverLoaded[cacheIdx] = psbbnCoverLoaded[cacheIdx - 1];
      psbbnCoverSourcePixels[cacheIdx] = psbbnCoverSourcePixels[cacheIdx - 1];
      collectionCoverThumbnailPixels[cacheIdx] = collectionCoverThumbnailPixels[cacheIdx - 1];
      collectionCoverPreviewPixels[cacheIdx] = collectionCoverPreviewPixels[cacheIdx - 1];
      psbbnCoverSourceWidth[cacheIdx] = psbbnCoverSourceWidth[cacheIdx - 1];
      psbbnCoverSourceHeight[cacheIdx] = psbbnCoverSourceHeight[cacheIdx - 1];
      psbbnCoverFullResolution[cacheIdx] = psbbnCoverFullResolution[cacheIdx - 1];
      collectionCoverResidentLevel[cacheIdx] = collectionCoverResidentLevel[cacheIdx - 1];
      strcpy(collectionCoverKeys[cacheIdx], collectionCoverKeys[cacheIdx - 1]);
    }
    psbbnCoverTextures[0] = recycledTexture;
    psbbnCoverSourcePixels[0] = recycledSource;
    collectionCoverThumbnailPixels[0] = recycledThumbnail;
    collectionCoverPreviewPixels[0] = recycledPreview;
    psbbnCoverSourceWidth[0] = recycledWidth;
    psbbnCoverSourceHeight[0] = recycledHeight;
    psbbnCoverFullResolution[0] = recycledFullResolution;
    collectionCoverResidentLevel[0] = recycledLevel;
    strcpy(collectionCoverKeys[0], recycledKey);
    releasePSBBNCoverCacheEntry(0);
    loadPSBBNCoverCacheEntry(titles, selectedTitleIdx, 0, useFullResolution);
    return;
  }

  releasePSBBNCovers();
  for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++)
    loadPSBBNCoverCacheEntry(titles, selectedTitleIdx, cacheIdx, useFullResolution);
}

static void refreshBackgroundCovers(TargetList *titles, int selectedTitleIdx,
                                    int previousTitleIdx) {
  int direction = lunaNavDirection(titles->total, previousTitleIdx, selectedTitleIdx);
  if (previousTitleIdx >= 0 && direction > 0 &&
      lunaNavWrap(titles->total, previousTitleIdx + 1) == selectedTitleIdx) {
    char recycledKey[255];
    strcpy(recycledKey, collectionCoverKeys[0]);
    GSTEXTURE *recycled = psbbnCoverTextures[0];
    void *recycledSource = psbbnCoverSourcePixels[0];
    void *recycledThumbnail = collectionCoverThumbnailPixels[0];
    void *recycledPreview = collectionCoverPreviewPixels[0];
    int recycledWidth = psbbnCoverSourceWidth[0];
    int recycledHeight = psbbnCoverSourceHeight[0];
    uint8_t recycledFull = psbbnCoverFullResolution[0];
    uint8_t recycledLevel = collectionCoverResidentLevel[0];
    uint8_t recycledResolved = collectionCoverResolved[0];
    for (int i = 0; i < PSBBN_COVER_CACHE_COUNT - 1; i++) {
      psbbnCoverTextures[i] = psbbnCoverTextures[i + 1];
      psbbnCoverLoaded[i] = psbbnCoverLoaded[i + 1];
      collectionCoverAttempted[i] = collectionCoverAttempted[i + 1];
      collectionCoverResolved[i] = collectionCoverResolved[i + 1];
      psbbnCoverSourcePixels[i] = psbbnCoverSourcePixels[i + 1];
      collectionCoverThumbnailPixels[i] = collectionCoverThumbnailPixels[i + 1];
      collectionCoverPreviewPixels[i] = collectionCoverPreviewPixels[i + 1];
      psbbnCoverSourceWidth[i] = psbbnCoverSourceWidth[i + 1];
      psbbnCoverSourceHeight[i] = psbbnCoverSourceHeight[i + 1];
      psbbnCoverFullResolution[i] = psbbnCoverFullResolution[i + 1];
      collectionCoverResidentLevel[i] = collectionCoverResidentLevel[i + 1];
      strcpy(collectionCoverKeys[i], collectionCoverKeys[i + 1]);
    }
    psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT - 1] = recycled;
    psbbnCoverSourcePixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledSource;
    collectionCoverThumbnailPixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledThumbnail;
    collectionCoverPreviewPixels[PSBBN_COVER_CACHE_COUNT - 1] = recycledPreview;
    psbbnCoverSourceWidth[PSBBN_COVER_CACHE_COUNT - 1] = recycledWidth;
    psbbnCoverSourceHeight[PSBBN_COVER_CACHE_COUNT - 1] = recycledHeight;
    psbbnCoverFullResolution[PSBBN_COVER_CACHE_COUNT - 1] = recycledFull;
    collectionCoverResidentLevel[PSBBN_COVER_CACHE_COUNT - 1] = recycledLevel;
    collectionCoverResolved[PSBBN_COVER_CACHE_COUNT - 1] = recycledResolved;
    strcpy(collectionCoverKeys[PSBBN_COVER_CACHE_COUNT - 1], recycledKey);
    releasePSBBNCoverCacheEntry(PSBBN_COVER_CACHE_COUNT - 1);
  } else if (previousTitleIdx >= 0 && direction < 0 &&
             lunaNavWrap(titles->total, previousTitleIdx - 1) == selectedTitleIdx) {
    char recycledKey[255];
    strcpy(recycledKey, collectionCoverKeys[PSBBN_COVER_CACHE_COUNT - 1]);
    GSTEXTURE *recycled = psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledSource = psbbnCoverSourcePixels[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledThumbnail = collectionCoverThumbnailPixels[PSBBN_COVER_CACHE_COUNT - 1];
    void *recycledPreview = collectionCoverPreviewPixels[PSBBN_COVER_CACHE_COUNT - 1];
    int recycledWidth = psbbnCoverSourceWidth[PSBBN_COVER_CACHE_COUNT - 1];
    int recycledHeight = psbbnCoverSourceHeight[PSBBN_COVER_CACHE_COUNT - 1];
    uint8_t recycledFull = psbbnCoverFullResolution[PSBBN_COVER_CACHE_COUNT - 1];
    uint8_t recycledLevel = collectionCoverResidentLevel[PSBBN_COVER_CACHE_COUNT - 1];
    uint8_t recycledResolved = collectionCoverResolved[PSBBN_COVER_CACHE_COUNT - 1];
    for (int i = PSBBN_COVER_CACHE_COUNT - 1; i > 0; i--) {
      psbbnCoverTextures[i] = psbbnCoverTextures[i - 1];
      psbbnCoverLoaded[i] = psbbnCoverLoaded[i - 1];
      collectionCoverAttempted[i] = collectionCoverAttempted[i - 1];
      collectionCoverResolved[i] = collectionCoverResolved[i - 1];
      psbbnCoverSourcePixels[i] = psbbnCoverSourcePixels[i - 1];
      collectionCoverThumbnailPixels[i] = collectionCoverThumbnailPixels[i - 1];
      collectionCoverPreviewPixels[i] = collectionCoverPreviewPixels[i - 1];
      psbbnCoverSourceWidth[i] = psbbnCoverSourceWidth[i - 1];
      psbbnCoverSourceHeight[i] = psbbnCoverSourceHeight[i - 1];
      psbbnCoverFullResolution[i] = psbbnCoverFullResolution[i - 1];
      collectionCoverResidentLevel[i] = collectionCoverResidentLevel[i - 1];
      strcpy(collectionCoverKeys[i], collectionCoverKeys[i - 1]);
    }
    psbbnCoverTextures[0] = recycled;
    psbbnCoverSourcePixels[0] = recycledSource;
    collectionCoverThumbnailPixels[0] = recycledThumbnail;
    collectionCoverPreviewPixels[0] = recycledPreview;
    psbbnCoverSourceWidth[0] = recycledWidth;
    psbbnCoverSourceHeight[0] = recycledHeight;
    psbbnCoverFullResolution[0] = recycledFull;
    collectionCoverResidentLevel[0] = recycledLevel;
    collectionCoverResolved[0] = recycledResolved;
    strcpy(collectionCoverKeys[0], recycledKey);
    releasePSBBNCoverCacheEntry(0);
  } else {
    releasePSBBNCovers();
  }
}

void refreshCollectionCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx) {
  collectionNavigationDirection = previousTitleIdx >= 0 ?
      lunaNavDirection(titles->total, previousTitleIdx, selectedTitleIdx) : 0;
  if (collectionArtThreadId < 0) {
    refreshPSBBNCovers(titles, selectedTitleIdx, previousTitleIdx, 0);
    return;
  }
  refreshBackgroundCovers(titles, selectedTitleIdx, previousTitleIdx);
}

void refreshOrbitCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx) {
  if (orbitArtThreadId < 0) {
    refreshPSBBNCovers(titles, selectedTitleIdx, previousTitleIdx, 1);
    return;
  }
  refreshBackgroundCovers(titles, selectedTitleIdx, previousTitleIdx);
}

int collectionArtBackgroundAvailable(void) {
  return collectionArtThreadId >= 0;
}

static void serviceBackgroundCovers(TargetList *titles, int selectedTitleIdx,
                                    PSBBNArtJob *job, uint32_t generation,
                                    int wakeSema, const uint8_t *priority,
                                    const PSBBNArtJob *otherJob) {
  char path[255];
  if (job->state == 2) {
    int adopted = 0;
    __asm__ __volatile__("" ::: "memory");
    if (job != &orbitArtJob) {
      uint32_t jobMs = job->readyMs - job->requestMs;
      collectionArtStats.completed++;
      collectionArtStats.readMs += job->readMs;
      collectionArtStats.decodeMs += job->decodeMs;
      collectionArtStats.featherMs += job->featherMs;
      collectionArtStats.resizeMs += job->resizeMs;
      if (jobMs > collectionArtStats.maxJobMs) collectionArtStats.maxJobMs = jobMs;
      if (job->result != 0) collectionArtStats.failed++;
    }
    if (job->generation == generation) {
      for (int p = 0; p < PSBBN_COVER_CACHE_COUNT; p++) {
        int i = priority[p];
        if (collectionCoverResolved[i] ||
            collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) < 0 ||
            strcmp(path, job->path) != 0)
          continue;
        if (job->result == 0) {
          GSTEXTURE *texture = psbbnCoverTextures[i];
          // Only the drawing thread touches the texture manager and cache slots.
          releasePSBBNCoverCacheEntry(i);
          *texture = job->texture;
          psbbnCoverSourcePixels[i] = job->sourcePixels;
          collectionCoverThumbnailPixels[i] = texture->Mem;
          collectionCoverPreviewPixels[i] = job->previewPixels;
          psbbnCoverSourceWidth[i] = job->sourceWidth;
          psbbnCoverSourceHeight[i] = job->sourceHeight;
          snprintf(collectionCoverKeys[i], sizeof(collectionCoverKeys[i]), "%s", job->path);
          memset(&job->texture, 0, sizeof(job->texture));
          job->sourcePixels = NULL;
          job->previewPixels = NULL;
          psbbnCoverLoaded[i] = 1;
        }
        collectionCoverAttempted[i] = 1;
        collectionCoverResolved[i] = 1;
        adopted = 1;
        break;
      }
    }
    if (job != &orbitArtJob) {
      if (adopted) collectionArtStats.adopted++;
      else if (job->result == 0) collectionArtStats.late++;
    }
    // A successful decode that left the window is still useful on a reversal
    // or list switch. Retain its complete artwork without touching worker memory.
    if (job->sourcePixels != NULL) {
      CollectionArtPixels pixels = {job->sourcePixels, job->previewPixels, job->texture.Mem,
          job->sourceWidth, job->sourceHeight,
          (size_t)gsKit_texture_size(job->sourceWidth, job->sourceHeight, GS_PSM_CT32) +
          (PSBBN_PREVIEW_SIZE * PSBBN_PREVIEW_SIZE +
           PSBBN_THUMBNAIL_SIZE * PSBBN_THUMBNAIL_SIZE) * sizeof(PSBBNPixel)};
      if (collectionArtReusePut(&collectionReuseCache, job->path, &pixels)) {
        job->sourcePixels = job->previewPixels = job->texture.Mem = NULL;
      }
    }
    clearPSBBNArtJob(job);
  }
  if (job->state != 0)
    return;
  for (int p = 0; p < PSBBN_COVER_CACHE_COUNT; p++) {
    int i = priority[p];
    if (collectionCoverAttempted[i] ||
        collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) < 0)
      continue;
    if (otherJob != NULL && otherJob->state != 0 &&
        strcmp(path, otherJob->path) == 0)
      continue;
    collectionCoverAttempted[i] = 1;
    snprintf(job->path, sizeof(job->path), "%s", path);
    job->generation = generation;
    job->result = -1;
    job->requestMs = uiNowMs();
    job->sourceWidth = job->sourceHeight = 0;
    job->state = 1;
    SignalSema(wakeSema);
    break;
  }
}

void serviceCollectionCoversNavigating(TargetList *titles, int selectedTitleIdx,
                                      int direction, int fastScrolling, int flowOffset) {
  uint8_t nearPriority[PSBBN_COVER_CACHE_COUNT];
  static const uint8_t farPriority[PSBBN_COVER_CACHE_COUNT] = {9, 8, 7, 6, 0, 1, 5, 2, 4, 3};
  if (direction == 0) direction = collectionNavigationDirection;
  collectionArtPriority(nearPriority, direction, fastScrolling, flowOffset);
  if (collectionArtThreadId >= 0 && titles->total > 0) {
    // Restoring ownership costs no allocation, decode, or pixel copy.
    for (int p = 0; p < PSBBN_COVER_CACHE_COUNT; p++) {
      int i = nearPriority[p];
      char path[255];
      CollectionArtPixels pixels = {0};
      if (collectionCoverResolved[i] ||
          collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) < 0 ||
          !collectionArtReuseTake(&collectionReuseCache, path, &pixels))
        continue;
      releasePSBBNCoverCacheEntry(i);
      GSTEXTURE *texture = psbbnCoverTextures[i];
      memset(texture, 0, sizeof(*texture));
      texture->Mem = pixels.thumbnail;
      texture->Width = texture->Height = PSBBN_THUMBNAIL_SIZE;
      texture->PSM = GS_PSM_CT32;
      texture->Filter = GS_FILTER_LINEAR;
      texture->Delayed = 1;
      psbbnCoverSourcePixels[i] = pixels.source;
      collectionCoverPreviewPixels[i] = pixels.preview;
      collectionCoverThumbnailPixels[i] = pixels.thumbnail;
      psbbnCoverSourceWidth[i] = pixels.width;
      psbbnCoverSourceHeight[i] = pixels.height;
      strcpy(collectionCoverKeys[i], path);
      psbbnCoverLoaded[i] = collectionCoverAttempted[i] = collectionCoverResolved[i] = 1;
      collectionArtStats.cacheHits++;
    }
    startCollectionFarArtWorker();
    serviceBackgroundCovers(titles, selectedTitleIdx, &collectionArtJob,
                            collectionArtGeneration, collectionArtWakeSema,
                            nearPriority, collectionFarArtThreadId >= 0 ? &collectionFarArtJob : NULL);
    int urgent = fastScrolling || direction != 0;
    for (int p = 0; p < 3; p++)
      if (!collectionCoverResolved[nearPriority[p]]) urgent = 1;
    if (collectionFarArtThreadId >= 0)
      serviceBackgroundCovers(titles, selectedTitleIdx, &collectionFarArtJob,
                              collectionArtGeneration, collectionFarArtWakeSema,
                              urgent ? nearPriority : farPriority, &collectionArtJob);
    if (!collectionCoverResolved[nearPriority[0]]) collectionArtStats.pendingFocusFrames++;
  }
  uint32_t now = uiNowMs();
  if (collectionArtStats.reportMs == 0) collectionArtStats.reportMs = now;
  if ((uint32_t)(now - collectionArtStats.reportMs) >= 2000) {
    if (collectionArtStats.completed || collectionArtStats.cacheHits || collectionArtStats.pendingFocusFrames)
      DPRINTF("Collection art: jobs=%u adopted=%u late=%u failed=%u hits=%u pending_frames=%u "
              "read=%ums png=%ums feather=%ums resize=%ums max_job=%ums bind=%ums/%u reuse=%uKiB\n",
              collectionArtStats.completed, collectionArtStats.adopted, collectionArtStats.late,
              collectionArtStats.failed, collectionArtStats.cacheHits, collectionArtStats.pendingFocusFrames,
              collectionArtStats.readMs, collectionArtStats.decodeMs, collectionArtStats.featherMs,
              collectionArtStats.resizeMs, collectionArtStats.maxJobMs,
              collectionArtStats.bindMs, collectionArtStats.binds,
              (unsigned)(collectionReuseCache.bytes / 1024));
    memset(&collectionArtStats, 0, sizeof(collectionArtStats));
    collectionArtStats.reportMs = now;
  }
}

void serviceCollectionCovers(TargetList *titles, int selectedTitleIdx) {
  serviceCollectionCoversNavigating(titles, selectedTitleIdx, 0, 0, 0);
}

void recordCollectionCoverBind(uint32_t elapsedMs) {
  collectionArtStats.bindMs += elapsedMs;
  collectionArtStats.binds++;
}

void serviceOrbitCovers(TargetList *titles, int selectedTitleIdx) {
  static const uint8_t priority[PSBBN_COVER_CACHE_COUNT] = {3, 4, 2, 5, 1, 6, 7, 8, 9, 0};
  if (orbitArtThreadId >= 0 && titles->total > 0) {
    // Reattach decoded covers on the UI thread before scheduling disk work.
    for (int p = 0; p < PSBBN_COVER_CACHE_COUNT; p++) {
      int i = priority[p];
      char path[255];
      CollectionArtPixels pixels = {0};
      if (collectionCoverResolved[i] ||
          collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) < 0 ||
          !collectionArtReuseTake(&collectionReuseCache, path, &pixels))
        continue;
      releasePSBBNCoverCacheEntry(i);
      GSTEXTURE *texture = psbbnCoverTextures[i];
      memset(texture, 0, sizeof(*texture));
      texture->Mem = pixels.thumbnail;
      texture->Width = texture->Height = PSBBN_THUMBNAIL_SIZE;
      texture->PSM = GS_PSM_CT32;
      texture->Filter = GS_FILTER_LINEAR;
      texture->Delayed = 1;
      psbbnCoverSourcePixels[i] = pixels.source;
      collectionCoverPreviewPixels[i] = pixels.preview;
      collectionCoverThumbnailPixels[i] = pixels.thumbnail;
      psbbnCoverSourceWidth[i] = pixels.width;
      psbbnCoverSourceHeight[i] = pixels.height;
      strcpy(collectionCoverKeys[i], path);
      psbbnCoverLoaded[i] = collectionCoverAttempted[i] = collectionCoverResolved[i] = 1;
    }
    serviceBackgroundCovers(titles, selectedTitleIdx, &orbitArtJob,
                            orbitArtGeneration, orbitArtWakeSema, priority, NULL);
  }
}

int collectionCoversReady(TargetList *titles, int selectedTitleIdx) {
  char path[255];
  if (collectionArtThreadId < 0 || titles->total <= 0)
    return 1;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    int targetIdx = lunaNavWrap(titles->total, selectedTitleIdx + i - PSBBN_COVER_CACHE_FOCUS);
    int distance = i - PSBBN_COVER_CACHE_FOCUS;
    int nearest = 1;
    if (distance < 0)
      distance = -distance;
    for (int j = 0; j < PSBBN_COVER_CACHE_COUNT; j++) {
      int otherDistance = j - PSBBN_COVER_CACHE_FOCUS;
      if (otherDistance < 0)
        otherDistance = -otherDistance;
      if (j != i &&
          lunaNavWrap(titles->total, selectedTitleIdx + j - PSBBN_COVER_CACHE_FOCUS) == targetIdx &&
          (otherDistance < distance || (otherDistance == distance && j < i))) {
        nearest = 0;
        break;
      }
    }
    if (nearest && collectionCoverPath(titles, selectedTitleIdx, i, path, sizeof(path)) == 0 &&
        !collectionCoverResolved[i])
      return 0;
  }
  return 1;
}

int collectionCoverMissing(int cacheIdx) {
  if (cacheIdx < 0 || cacheIdx >= PSBBN_COVER_CACHE_COUNT)
    return 0;
  // An unresolved asynchronous request is still loading, not missing.
  return !psbbnCoverLoaded[cacheIdx] &&
         (collectionArtThreadId < 0 || collectionCoverResolved[cacheIdx]);
}

static void setCollectionCoverResidentSize(int cacheIdx, int level) {
  GSTEXTURE *texture = psbbnCoverTextures[cacheIdx];
  if (!psbbnCoverLoaded[cacheIdx] || psbbnCoverSourcePixels[cacheIdx] == NULL ||
      collectionCoverThumbnailPixels[cacheIdx] == NULL ||
      collectionCoverPreviewPixels[cacheIdx] == NULL ||
      collectionCoverResidentLevel[cacheIdx] == level)
    return;
  if (texture->Vram != 0)
    gsKit_TexManager_free(gsGlobal, texture);
  texture->Vram = 0;
  texture->VramClut = 0;
  texture->Mem = level == 2 ? psbbnCoverSourcePixels[cacheIdx] :
                 level == 1 ? collectionCoverPreviewPixels[cacheIdx] :
                              collectionCoverThumbnailPixels[cacheIdx];
  texture->Width = level == 2 ? psbbnCoverSourceWidth[cacheIdx] :
                   level == 1 ? PSBBN_PREVIEW_SIZE : PSBBN_THUMBNAIL_SIZE;
  texture->Height = level == 2 ? psbbnCoverSourceHeight[cacheIdx] :
                    level == 1 ? PSBBN_PREVIEW_SIZE : PSBBN_THUMBNAIL_SIZE;
  collectionCoverResidentLevel[cacheIdx] = level;
  psbbnCoverFullResolution[cacheIdx] = level == 2;
}

void updateCollectionCoverResidency(int flowOffset) {
  int closest = -1, nextClosest = -1;
  int closestDistance = 0x7fffffff, nextDistance = 0x7fffffff;
  int desired[PSBBN_COVER_CACHE_COUNT] = {0};
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    int position = (i - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    if (!psbbnCoverLoaded[i])
      continue;
    if (distance < closestDistance) {
      nextClosest = closest;
      nextDistance = closestDistance;
      closest = i;
      closestDistance = distance;
    } else if (distance < nextDistance) {
      nextClosest = i;
      nextDistance = distance;
    }
  }
  // Keep the next and previous covers at 128 pixels, with full detail on the
  // focal cover and its moving partner. Demote first to limit peak GS use.
  if (PSBBN_COVER_CACHE_FOCUS > 0)
    desired[PSBBN_COVER_CACHE_FOCUS - 1] = 1;
  if (PSBBN_COVER_CACHE_FOCUS + 1 < PSBBN_COVER_CACHE_COUNT)
    desired[PSBBN_COVER_CACHE_FOCUS + 1] = 1;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    int position = (i - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    if (distance <= 1500)
      desired[i] = 1;
  }
  if (closest >= 0)
    desired[closest] = 2;
  if (flowOffset != 0 && nextClosest >= 0)
    desired[nextClosest] = 2;
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    if (collectionCoverResidentLevel[i] > desired[i])
      setCollectionCoverResidentSize(i, desired[i]);
  }
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    if (collectionCoverResidentLevel[i] < desired[i])
      setCollectionCoverResidentSize(i, desired[i]);
  }
}

// Orbit held input can leave the rendered focal point behind the logical
// selection. Keep its two nearest covers full-size and nearby covers at 128.
// Demote first to avoid a temporary third full-size GS allocation.
void updatePSBBNCoverResidency(int flowOffset) {
  int closest = -1;
  int nextClosest = -1;
  int closestDistance = 0x7FFFFFFF;
  int nextClosestDistance = 0x7FFFFFFF;
  int desired[PSBBN_COVER_CACHE_COUNT] = {0};

  for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    int position;
    int distance;

    if (!psbbnCoverLoaded[cacheIdx])
      continue;
    position = (cacheIdx - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    distance = (position < 0) ? -position : position;
    if (distance <= 1500)
      desired[cacheIdx] = 1;
    if (distance < closestDistance) {
      nextClosest = closest;
      nextClosestDistance = closestDistance;
      closest = cacheIdx;
      closestDistance = distance;
    } else if (distance < nextClosestDistance) {
      nextClosest = cacheIdx;
      nextClosestDistance = distance;
    }
  }

  if (closest >= 0)
    desired[closest] = 2;
  if (nextClosest >= 0)
    desired[nextClosest] = 2;
  for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    if (collectionCoverResidentLevel[cacheIdx] > desired[cacheIdx])
      setCollectionCoverResidentSize(cacheIdx, desired[cacheIdx]);
  }
  for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    if (collectionCoverResidentLevel[cacheIdx] < desired[cacheIdx])
      setCollectionCoverResidentSize(cacheIdx, desired[cacheIdx]);
  }
}

void artCacheShutdown(void) {
  stopClassicArtWorker();
  stopCollectionFarArtWorker(NULL, 0);
  stopCollectionArtWorker();
  stopOrbitArtWorker();
  stopGridArtWorker();
  stopScrollArtWorker();
  collectionArtReuseClear(&collectionReuseCache);
  memset(collectionCoverKeys, 0, sizeof(collectionCoverKeys));
  memset(&collectionArtStats, 0, sizeof(collectionArtStats));
  collectionNavigationDirection = 0;
  if (orbsBackgroundTexture != NULL) {
    free(orbsBackgroundTexture->Mem);
    free(orbsBackgroundTexture->Clut);
    free(orbsBackgroundTexture);
    orbsBackgroundTexture = NULL;
  }
  orbsBackgroundLoaded = 0;
  orbsBackgroundTarget = -1;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    if (orbsLogoTextures[i] != NULL) {
      free(orbsLogoTextures[i]->Mem);
      free(orbsLogoTextures[i]->Clut);
      free(orbsLogoTextures[i]);
      orbsLogoTextures[i] = NULL;
    }
    orbsLogoLoaded[i] = 0;
    orbsLogoTargets[i] = -1;
  }
  if (coverTexture != NULL) {
    free(coverTexture->Mem);
    free(coverTexture->Clut);
    free(coverTexture);
    coverTexture = NULL;
  }
  if (classicPreviousCoverTexture != NULL) {
    free(classicPreviousCoverTexture->Mem);
    free(classicPreviousCoverTexture->Clut);
    free(classicPreviousCoverTexture);
    classicPreviousCoverTexture = NULL;
  }
  if (discTexture != NULL) {
    free(discTexture->Mem);
    free(discTexture->Clut);
    free(discTexture);
    discTexture = NULL;
  }
  for (int i = 0; i < PSBBN_COVER_CACHE_COUNT; i++) {
    if (psbbnCoverTextures[i] != NULL) {
      if (psbbnCoverTextures[i]->Mem != NULL &&
          psbbnCoverTextures[i]->Mem != psbbnCoverSourcePixels[i] &&
          psbbnCoverTextures[i]->Mem != collectionCoverThumbnailPixels[i] &&
          psbbnCoverTextures[i]->Mem != collectionCoverPreviewPixels[i])
        free(psbbnCoverTextures[i]->Mem);
      free(psbbnCoverTextures[i]);
      psbbnCoverTextures[i] = NULL;
    }
    free(psbbnCoverSourcePixels[i]);
    free(collectionCoverThumbnailPixels[i]);
    free(collectionCoverPreviewPixels[i]);
    psbbnCoverSourcePixels[i] = NULL;
    collectionCoverThumbnailPixels[i] = NULL;
    collectionCoverPreviewPixels[i] = NULL;
    psbbnCoverSourceWidth[i] = 0;
    psbbnCoverSourceHeight[i] = 0;
    psbbnCoverLoaded[i] = 0;
    psbbnCoverFullResolution[i] = 0;
  }
  for (int buffer = 0; buffer < GRID_PAGE_BUFFERS; buffer++) {
    for (int i = 0; i < GRID_CACHE_PAGE_SIZE; i++) {
      if (gridCoverTextures[buffer][i] != NULL) {
        free(gridCoverTextures[buffer][i]->Mem);
        free(gridCoverTextures[buffer][i]->Clut);
        free(gridCoverTextures[buffer][i]);
        gridCoverTextures[buffer][i] = NULL;
      }
      gridCoverLoaded[buffer][i] = 0;
    }
  }
  for (int i = 0; i < GRID_THUMBNAIL_CACHE_COUNT; i++) {
    free(gridThumbnailCache[i].pixels);
    gridThumbnailCache[i].pixels = NULL;
    gridThumbnailCache[i].state = 0;
    gridThumbnailCache[i].path[0] = '\0';
    gridThumbnailCache[i].lastUsed = 0;
  }
  gridThumbnailCacheClock = 0;
  for (int buffer = 0; buffer < GRID_SELECTED_BUFFERS; buffer++) {
    if (gridSelectedTextures[buffer] != NULL) {
      free(gridSelectedTextures[buffer]->Mem);
      free(gridSelectedTextures[buffer]->Clut);
      free(gridSelectedTextures[buffer]);
      gridSelectedTextures[buffer] = NULL;
    }
    gridSelectedLoaded[buffer] = 0;
  }
}
