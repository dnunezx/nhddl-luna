// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include "ui/collection_v2.h"
#include "ui/language.h"
#include <gsInline.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static u64 *packet(int count) {
  u64 *p = gsKit_heap_alloc(gsGlobal, count, count * 16, GIF_AD);
  p[0] = GIF_TAG_AD(count); p[1] = GIF_AD;
  return p + 2;
}

// RGB texture mode leaves the cache's inverse-alpha feather untouched. The
// recovered FDEG effect is drawn as vertex alpha, with standard source blending.
static void bindJacket(GSTEXTURE *texture) {
  int tw = 0, th = 0;
  while ((1 << tw) < texture->Width) tw++;
  while ((1 << th) < texture->Height) th++;
  gsKit_set_texfilter(gsGlobal, GS_FILTER_LINEAR);
  u64 *p = packet(1);
  p[0] = GS_SETREG_TEX0(texture->Vram / 256, texture->TBW, texture->PSM,
                        tw, th, 0, 0, 0, 0, 0, 0, 0);
  p[1] = gsGlobal->PrimContext ? GS_TEX0_2 : GS_TEX0_1;
}

typedef struct { float x, y, alpha; } JacketVertex;

static void quad(GSTEXTURE *texture, const CollectionV2Pose *pose,
                 JacketVertex a, JacketVertex b, JacketVertex c, JacketVertex d) {
  const JacketVertex v[] = {a, b, c, d};
  u64 *p = packet(13);
  p[0] = GS_SETREG_PRIM(GS_PRIM_PRIM_TRISTRIP, 1, 1, 0, 1, 0,
                         1, gsGlobal->PrimContext, 0);
  p[1] = GS_PRIM;
  for (int i = 0; i < 4; i++, p += 6) {
    float x = pose->x + v[i].x * pose->width;
    float y = pose->y + v[i].y * pose->height;
    float alpha = v[i].alpha * pose->opacity;
#ifdef LUNA_CAMERA_ANIMATIONS
    x = lunaCameraDrawX(gsGlobal, x);
    y = lunaCameraDrawY(gsGlobal, y);
    alpha *= lunaCameraDrawOpacity;
#endif
    p[2] = GS_SETREG_RGBA(0x80, 0x80, 0x80, (int)(128 * alpha));
    p[3] = GS_RGBAQ;
    p[4] = GS_SETREG_UV((int)(v[i].x * (texture->Width - 1) * 16) + 8,
                         (int)(v[i].y * (texture->Height - 1) * 16) + 8);
    p[5] = GS_UV;
    p[6] = GS_SETREG_XYZ2(gsKit_float_to_int_x(gsGlobal, x),
                          gsKit_float_to_int_y(gsGlobal, y), 4);
    p[7] = GS_XYZ2;
  }
}

static void jacket(GSTEXTURE *texture, const CollectionV2Pose *pose) {
  bindJacket(texture);
  // Native Plane constructor requests a 0.1 edge width, two interior feather
  // levels and eight corner subdivisions. Its alpha samples follow a cosine.
  static const float radius[] = {0.1f, 0.06666667f, 0.03333333f, 0};
  static const float alpha[] = {0, 127.0f / 255, 220.0f / 255, 1};
  static const float arcX[] = {1,.98078528f,.92387953f,.83146961f,.70710678f,
                               .55557023f,.38268343f,.19509032f,0};
  static const float arcY[] = {0,.19509032f,.38268343f,.55557023f,.70710678f,
                               .83146961f,.92387953f,.98078528f,1};
  quad(texture, pose, (JacketVertex){.1f,.1f,1}, (JacketVertex){.9f,.1f,1},
                      (JacketVertex){.1f,.9f,1}, (JacketVertex){.9f,.9f,1});
  for (int band = 0; band < 3; band++) {
    float r = radius[band], s = radius[band + 1];
    float a = alpha[band], b = alpha[band + 1];
    for (int side = 0; side < 4; side++) {
      JacketVertex v[] = {{.1f,.1f-r,a},{.9f,.1f-r,a},
                           {.1f,.1f-s,b},{.9f,.1f-s,b}};
      for (int i = 0; i < 4; i++)
        for (int turn = 0; turn < side; turn++) {
          float x = v[i].x; v[i].x = 1 - v[i].y; v[i].y = x;
        }
      quad(texture, pose, v[0],v[1],v[2],v[3]);
    }
    for (int corner = 0; corner < 4; corner++) {
      for (int step = 0; step < 8; step++) {
        float cx = corner & 1 ? .9f : .1f, cy = corner & 2 ? .9f : .1f;
        float dx = corner & 1 ? 1 : -1, dy = corner & 2 ? 1 : -1;
        float x0 = dx*arcX[step], y0 = dy*arcY[step];
        float x1 = dx*arcX[step+1], y1 = dy*arcY[step+1];
        quad(texture,pose,(JacketVertex){cx+r*x0,cy+r*y0,a},
                          (JacketVertex){cx+r*x1,cy+r*y1,a},
                          (JacketVertex){cx+s*x0,cy+s*y0,b},
                          (JacketVertex){cx+s*x1,cy+s*y1,b});
      }
    }
  }
}

void drawPSBBNCollectionV2(TargetList *titles, int selectedTitleIdx, int flowOffset,
                           int favoritesOnly, int entryProgress, uint32_t frameNowMs) {
  drawSharedLibraryBackground(frameNowMs);
  int left = gsGlobal->Width * 6 / 100;
  int labelY = psbbnFieldStableY(gsGlobal->Height / 2 - getFontLineHeight() / 2);
  drawPSBBNFocusGlow(left,labelY,gsGlobal->Width * 30 / 100,
      left + getLineWidth(lunaText("Collection")));
  drawTextWindow(left,labelY,gsGlobal->Width * 30 / 100,0,6,
      FontMainColor,ALIGN_LEFT,lunaText("Collection"));
  if (favoritesOnly)
    drawTextWindow(left,labelY + getFontLineHeight() + 10,gsGlobal->Width / 3,0,6,
        HeaderTextColor,ALIGN_LEFT,lunaText("Favorites"));
  if (titles->total <= 0) {
    drawTextWindow(gsGlobal->Width / 3,headerHeight,gsGlobal->Width - keepoutArea,
        gsGlobal->Height-footerHeight,6,HeaderTextColor,ALIGN_CENTER,
        lunaText("NO FAVORITES YET\nAdd favorites in List"));
    return;
  }
  if (entryProgress < 0) return;

  GSTEST savedTest = *gsGlobal->Test;
  u64 savedBlend = gsGlobal->PrimAlpha;
  int savedEnable = gsGlobal->PrimAlphaEnable, savedPabe = gsGlobal->PABE;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0),0);
  // Positive slots are behind focus. Paint far-to-near; position, rather than
  // the queued logical selection, determines the foreground during retargets.
  for (int cacheIdx = PSBBN_COVER_CACHE_COUNT - 1; cacheIdx >= 0; cacheIdx--) {
    int targetIdx = lunaNavWrap(titles->total,selectedTitleIdx + cacheIdx - PSBBN_COVER_CACHE_FOCUS);
    int duplicate = 0;
    int position = (cacheIdx - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
    for (int other = 0; other < PSBBN_COVER_CACHE_COUNT; other++) {
      if (other == cacheIdx || lunaNavWrap(titles->total,selectedTitleIdx + other - PSBBN_COVER_CACHE_FOCUS) != targetIdx)
        continue;
      int otherPosition = (other - PSBBN_COVER_CACHE_FOCUS) * 1000 + flowOffset;
      if (abs(otherPosition) < abs(position) ||
          (abs(otherPosition) == abs(position) && other < cacheIdx)) duplicate = 1;
    }
    CollectionV2Pose pose;
    if (duplicate || !collectionV2Pose(position / 1000.0f,gsGlobal->Width,gsGlobal->Height,&pose))
      continue;
    GSTEXTURE *cover = psbbnCoverTextures[cacheIdx];
    if (psbbnCoverLoaded[cacheIdx] && prepareCollectionCoverTexture(cacheIdx) &&
        bindTextureSafe(gsGlobal,cover)) {
      jacket(cover,&pose);
    } else if (collectionCoverMissing(cacheIdx)) {
      int cx = (int)(pose.x + pose.width*.5f), cy = (int)(pose.y + pose.height*.5f);
      drawGlassDiamond(cx,cy,pose.width / 12,4,HeaderTextColor);
      if (abs(position) < 500)
        drawTextWindow(pose.x,cy + 20,pose.x + pose.width,0,6,
            FontMainColor,ALIGN_HCENTER,getTargetByIdx(titles,targetIdx)->name);
    }
  }
  gsGlobal->PrimAlphaEnable = savedEnable;
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);
  *gsGlobal->Test = savedTest;
  gsKit_set_test(gsGlobal,savedTest.ATE ? GS_ATEST_ON : GS_ATEST_OFF);
  gsKit_set_test(gsGlobal,savedTest.ZTE ? GS_ZTEST_ON : GS_ZTEST_OFF);
  int visual = lunaNavWrap(titles->total,selectedTitleIdx +
      (flowOffset >= 500 ? -1 : flowOffset < -500 ? 1 : 0));
  char counter[32];
  snprintf(counter,sizeof(counter),"%d/%d",visual + 1,titles->total);
  drawTextWindow(gsGlobal->Width / 2,gsGlobal->Height-footerHeight-getFontLineHeight()-12,
      gsGlobal->Width-keepoutArea-gsGlobal->Width*COLLECTION_V2_LEFT_SHIFT/640,
      0,6,FontMainColor,ALIGN_RIGHT,counter);
}
