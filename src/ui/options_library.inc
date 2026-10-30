/* Artwork poses stay captured; the wallpaper always uses the live clock. */
#include <math.h>
typedef struct {
  UILibraryView view;
  TargetList *titles;
  int selected, maxTitles, favorites, flow, outgoing, fast, entry, fade, visualFocus;
  int coverPending, listEntry;
  uint32_t now;
  const Target *artTarget;
  GSTEXTURE *cover, *disc;
  const uint8_t *flags;
  const char *nextLabel;
} OptionsLibrarySnapshot;

static void drawOptionsLibraryScene(void *context, float progress) {
  OptionsLibrarySnapshot *s=context;
  float pose=fminf(1,fmaxf(0,progress));
  float offsetX=-pose*gsGlobal->Width;
#ifdef LUNA_CAMERA_ANIMATIONS
  lunaCameraLayerBegin(1-.22f*pose,1,offsetX,0);
#endif
  beginOptionsLibraryBackground(uiNowMs(),s->view==UI_VIEW_ORBIT);
  // The library moves out intact; it does not crossfade with the workspace.
  if (pose<1) {
#ifndef LUNA_CAMERA_ANIMATIONS
  int savedOffsetX=gsGlobal->OffsetX;
  gsGlobal->OffsetX+=(int)lroundf(offsetX*16.0f);
#endif
  switch (s->view) {
    case UI_VIEW_COLLECTION_V2:
      drawPSBBNCollectionV2(s->titles,s->selected,s->flow,s->favorites,
                            s->entry,s->now); break;
    case UI_VIEW_3D:
      drawCaseGrid(s->titles,s->selected,s->now); break;
    case UI_VIEW_PSBBN:
      drawPSBBNCollection(s->titles,s->selected,psbbnCoverTextures,s->flow,s->outgoing,
          s->favorites,s->fast,s->entry,s->fade,s->now,s->nextLabel); break;
    case UI_VIEW_ORBIT:
      drawOrbit(s->titles,s->selected,psbbnCoverTextures,s->flow,0,s->entry,s->now,s->nextLabel); break;
    case UI_VIEW_ORBS:
      drawOrbsView(s->titles,s->selected,s->flow,s->visualFocus,s->fast,s->entry,s->now,s->nextLabel); break;
    default:
      drawTitleList(s->titles,s->selected,s->maxTitles,s->artTarget,s->cover,s->disc,
          s->flags,s->favorites,s->coverPending,s->listEntry,s->now,s->nextLabel); break;
  }
#ifndef LUNA_CAMERA_ANIMATIONS
  gsGlobal->OffsetX=savedOffsetX;
#endif
  }
  endOptionsLibraryBackground();
#ifdef LUNA_CAMERA_ANIMATIONS
  lunaCameraLayerEnd();
#endif
}
