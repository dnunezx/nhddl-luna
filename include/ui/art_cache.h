// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_UI_ART_CACHE_H
#define LUNA_UI_ART_CACHE_H

#include "target.h"
#include "ui/navigation.h"
#include <gsKit.h>
#include <stdint.h>

extern GSTEXTURE *coverTexture;
extern GSTEXTURE *classicPreviousCoverTexture;
extern GSTEXTURE *discTexture;
extern GSTEXTURE *psbbnCoverTextures[PSBBN_COVER_CACHE_COUNT];
extern uint8_t psbbnCoverLoaded[PSBBN_COVER_CACHE_COUNT];
extern GSTEXTURE *gridCoverTextures[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
extern uint8_t gridCoverLoaded[GRID_PAGE_BUFFERS][GRID_CACHE_PAGE_SIZE];
extern GSTEXTURE *gridSelectedTextures[GRID_SELECTED_BUFFERS];
extern uint8_t gridSelectedLoaded[GRID_SELECTED_BUFFERS];
extern GSTEXTURE *orbsLogoTextures[ORBS_LOGO_CACHE_COUNT];
extern uint8_t orbsLogoLoaded[ORBS_LOGO_CACHE_COUNT];

int artCacheInit(void);
void artCacheShutdown(void);
int loadCoverArt(struct DeviceMapEntry *device, char *titleID);
int loadNextClassicCoverArt(struct DeviceMapEntry *device, char *titleID);
int loadDiscArt(struct DeviceMapEntry *device, char *titleID);
int requestClassicArt(struct DeviceMapEntry *device, char *titleID);
void pumpClassicArtPrefetch(void);
int serviceClassicArt(int *coverAvailable, int *discAvailable);
void cancelClassicArt(void);
void releaseClassicArtVRAM(void);
void releasePSBBNCovers(void);
void releaseGridCovers(void);
void setGridCaseArtwork(int enabled);
void releaseGridTexture(GSTEXTURE *texture);
int loadGridPageStep(TargetList *titles, int pageBase, int buffer, int *nextSlot,
                     int prioritySlot, int *didLoadArtwork);
int serviceGridArt(void);
void pauseGridArtRequests(void);
int gridArtIsIdle(void);
int gridPageSlotReady(int buffer, int slot);
// Returns -1 while loading, 0 for missing artwork, 1 when the cover is ready.
int refreshGridSelectedCover(Target *target, int buffer);
void prepareGridPageBuffer(int buffer, int pageBase, int *pageBases,
                           int *pageComplete, int *pageNextSlot);
void refreshPSBBNCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx,
                        int useFullResolution);
void refreshCollectionCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx);
void refreshOrbitCovers(TargetList *titles, int selectedTitleIdx, int previousTitleIdx);
int collectionArtBackgroundAvailable(void);
// Applies to running decodes as well as future requests; retains cached pixels.
void setCollectionArtForeground(int foreground);
void serviceCollectionCovers(TargetList *titles, int selectedTitleIdx);
// Returns whether entry covers were ready from memory before servicing new loads.
int serviceCollectionEntryCovers(TargetList *titles, int selectedTitleIdx);
void serviceCollectionCoversNavigating(TargetList *titles, int selectedTitleIdx,
                                      int direction, int fastScrolling, int flowOffset);
void recordCollectionCoverBind(uint32_t elapsedMs);
void stopCollectionFarArtWorker(TargetList *titles, int selectedTitleIdx);
void serviceOrbitCovers(TargetList *titles, int selectedTitleIdx);
// Entry requires current + six upcoming games; previous-only games do not block.
int collectionCoversReady(TargetList *titles, int selectedTitleIdx);
int collectionCoverMissing(int cacheIdx);
// Returns zero when no cover size can safely be bound to the texture pool.
int prepareCollectionCoverTexture(int cacheIdx);
void suspendCollectionCovers(void);
void adoptOrbitCoversForCollection(void);
void adoptCollectionCoversForOrbit(void);
void updateCollectionCoverResidency(int flowOffset);
void updatePSBBNCoverResidency(int flowOffset);
void refreshOrbsLogos(TargetList *titles, int selectedTitleIdx);
void serviceScrollArt(void);
void refreshScrollCover(Target *target, int fastScrolling, uint32_t now);
GSTEXTURE *getScrollCoverTexture(int *resolved);
void refreshScrollCarouselCovers(TargetList *titles, int selectedTitleIdx, int fastScrolling);
GSTEXTURE *getScrollCarouselCover(int cacheIdx, int *resolved);
void refreshScrollBackground(Target *target, int fastScrolling, uint32_t now);
GSTEXTURE *getScrollBackgroundTexture(void);
void releaseScrollBackground(void);
void releaseOrbsArt(void);

#endif
