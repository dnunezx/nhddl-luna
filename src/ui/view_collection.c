// Original LUNA code: Danny Nunez (dnunezx) 2026
#include "ui/view_internal.h"
#include "ui/language.h"
#include <stdio.h>
#include <string.h>
#ifdef LUNA_CAMERA_ANIMATIONS
#include <math.h>
#endif
#ifdef LUNA_CAMERA_ANIMATIONS
#include "ui/camera_transitions.h"
#endif

enum {
  PSBBN_COVER_BACKGROUND_Z = 4,
  PSBBN_COVER_FOREGROUND_Z = 7,
  COLLECTION_TITLE_SCROLL_SPEED = 24,
  COLLECTION_TITLE_START_PAUSE_MS = 1000,
  COLLECTION_TITLE_END_PAUSE_MS = 1400,
  COLLECTION_TITLE_SLIDE_MS = 1200,
};

static const Target *collectionVisibleTitle;
static uint32_t collectionTitleStartMs;
static int collectionGameList;
static int collectionCoverEcho;
static const Target *collectionListTitle;
static uint32_t collectionListTitleStartMs;
static uint32_t collectionListLastFrameMs;

void setCollectionGameList(int enabled) {
  collectionGameList = enabled != 0;
  collectionListTitle = NULL;
}

static int collectionListScrollOffset(int overflow, uint32_t elapsed) {
  if (overflow <= 0)
    return 0;
  const uint32_t travelMs = (uint32_t)overflow * 1000U / COLLECTION_TITLE_SCROLL_SPEED;
  const uint32_t cycleMs = COLLECTION_TITLE_START_PAUSE_MS +
                          COLLECTION_TITLE_END_PAUSE_MS + 2 * travelMs;
  uint32_t phase = elapsed % cycleMs;
  if (phase <= COLLECTION_TITLE_START_PAUSE_MS)
    return 0;
  phase -= COLLECTION_TITLE_START_PAUSE_MS;
  if (phase < travelMs)
    return (int)((uint64_t)phase * overflow / travelMs);
  if (phase < travelMs + COLLECTION_TITLE_END_PAUSE_MS)
    return overflow;
  return overflow - (int)((uint64_t)(phase - travelMs -
                         COLLECTION_TITLE_END_PAUSE_MS) * overflow / travelMs);
}

static uint64_t collectionListOpacity(uint64_t color, int opacity) {
  const uint64_t alpha = ((color >> 24) & 0xFF) * opacity / 1000;
  return (color & ~(0xFFULL << 24)) | (alpha << 24);
}

static int collectionListFirst(int selected, int visible, int total) {
  int first = selected - visible / 2;
  if (first > total - visible)
    first = total - visible;
  return first < 0 ? 0 : first;
}

static void drawCollectionListSelection(int left, int y, int right, int lineHeight,
                                        int opacity) {
  // Feather the cyan light beyond the selection without a solid gray bar.
  gsKit_prim_sprite(gsGlobal, left + 5, y - 5, right - 5, y + lineHeight + 5, 11,
                    collectionListOpacity(glassPresetColor(0x18, 0x84, 0xC8, 0x0A), opacity));
  gsKit_prim_sprite(gsGlobal, left + 6, y - 3, right - 6, y + lineHeight + 3, 12,
                    collectionListOpacity(glassPresetColor(0x28, 0xA8, 0xE8, 0x14), opacity));
  const uint64_t bright = collectionListOpacity(glassPresetColor(0x38, 0xB8, 0xF0, 0x44), opacity);
  const uint64_t faded = collectionListOpacity(glassPresetColor(0x18, 0x74, 0xB0, 0x08), opacity);
  gsKit_prim_triangle_gouraud(gsGlobal, left + 7, y - 1, right - 7, y - 1,
                             left + 7, y + lineHeight + 1, 13, bright, faded, bright);
  gsKit_prim_triangle_gouraud(gsGlobal, right - 7, y - 1,
                             right - 7, y + lineHeight + 1,
                             left + 7, y + lineHeight + 1, 13, faded, faded, bright);
  gsKit_prim_sprite(gsGlobal, left + 6, y - 2, left + 8, y + lineHeight + 2, 14,
                    collectionListOpacity(glassPresetColor(0xB0, 0xF0, 0xFF, 0x80), opacity));
}

static void drawCollectionGameList(TargetList *titles, int selected, int target,
                                   int flowOffset, int right, int bottom,
                                   uint32_t frameNowMs, int moving) {
  const int left = 36;
  const int textLeft = left + 16;
  const int textRight = right - 12;
  const int top = psbbnFieldStableY(headerHeight + 32);
  const int lineHeight = getFontLineHeight();
  const int rowStep = (lineHeight + 9) & ~1;
  const int contentHeight = bottom - top - 24;
  if (contentHeight < lineHeight || titles->total <= 0 || right <= left + 40)
    return;
  // The last row needs only its glyph height, with no trailing row gap.
  int visible = 1 + (contentHeight - lineHeight) / rowStep;
  const int textTop = psbbnFieldStableY(top + 12 +
      (contentHeight - ((visible - 1) * rowStep + lineHeight)) / 2);
  if (visible > titles->total)
    visible = titles->total;

  // Sample the existing cover glide; the list owns no new animation clock.
  int visualRank = target * 1000 - flowOffset;
  int lower = visualRank / 1000;
  int fraction = visualRank % 1000;
  if (fraction < 0) {
    lower--;
    fraction += 1000;
  }
  lower = lunaNavWrap(titles->total, lower);
  const int upper = lunaNavWrap(titles->total, lower + 1);
  int first = collectionListFirst(lower, visible, titles->total);
  int nextFirst = collectionListFirst(upper, visible, titles->total);
  // At the library boundary, blend the two end windows rather than sweeping
  // through every intervening game. Small libraries always draw one copy.
  const int wrapping = upper == 0 && first != nextFirst && fraction != 0;
  const int windowFirst[2] = {
      wrapping ? first * 1000 : first * 1000 + (nextFirst - first) * fraction,
      nextFirst * 1000};

  const Target *focus = getTargetByIdx(titles, selected);
  if (collectionListTitle != focus || moving ||
      frameNowMs - collectionListLastFrameMs > 250) {
    collectionListTitle = focus;
    collectionListTitleStartMs = frameNowMs;
  }
  collectionListLastFrameMs = frameNowMs;

  const int fadeTop = textTop - rowStep / 4;
  const int fadeBottom = textTop + (visible - 1) * rowStep + lineHeight + rowStep / 4;
  gsKit_set_scissor(gsGlobal, GS_SETREG_SCISSOR(left, right - 1, top + 4, bottom - 5));
  for (int page = 0; page < (wrapping ? 2 : 1); page++) {
    const int pageOpacity = wrapping ? (page ? fraction : 1000 - fraction) : 1000;
    first = windowFirst[page] / 1000;
    for (int row = -1; row <= visible; row++) {
      const int index = first + row;
      if (index < 0 || index >= titles->total)
        continue;
      const int y = psbbnFieldStableY(textTop + row * rowStep -
          (windowFirst[page] % 1000) * rowStep / 1000);
      const Target *title = getTargetByIdx(titles, index);
      const int active = index == selected;
      int opacity = 1000;
      if (!active && titles->total > visible) {
        int edgeDistance = y + lineHeight / 2 - fadeTop;
        const int bottomDistance = fadeBottom - y - lineHeight / 2;
        if (bottomDistance < edgeDistance)
          edgeDistance = bottomDistance;
        if (edgeDistance <= 0)
          continue;
        int fade = edgeDistance * 1000 / (rowStep * 2);
        if (fade < 1000)
          opacity = fade * fade / 1000 * (3000 - 2 * fade) / 1000;
      }
      opacity = opacity * pageOpacity / 1000;
      if (opacity <= 0)
        continue;
      int scrollX = 0;
      if (active) {
        drawCollectionListSelection(left, y, right, lineHeight, opacity);
        if (!moving)
          scrollX = collectionListScrollOffset(
              (int)(getLineWidth(title->name) + 0.99f) - (textRight - textLeft),
              frameNowMs - collectionListTitleStartMs);
      }
      drawTextMarquee(textLeft, y, textRight, 15,
                      collectionListOpacity(active ? FontMainColor :
                          GS_SETREG_RGBA(0xA8, 0xC4, 0xE0, 0x78), opacity),
                      title->name, scrollX);
    }
  }
  gsKit_set_scissor(gsGlobal, GS_SETREG_SCISSOR(0, gsGlobal->Width - 1, 0, gsGlobal->Height - 1));
}

static int collectionCornerAlpha(int x, int opacity) {
  // Feather into the background at the left, with full coverage top to bottom.
  int horizontal = (int)((int64_t)x * x * (3000 - 2 * x) / 1000000);
  return opacity * horizontal / 1000;
}

enum { COLLECTION_CORNER_SIZE = 64 };
static GSTEXTURE collectionCornerTexture;
static uint32_t collectionCornerPixels[COLLECTION_CORNER_SIZE * COLLECTION_CORNER_SIZE]
    __attribute__((aligned(128)));
static uint32_t collectionCornerScratch[COLLECTION_CORNER_SIZE * COLLECTION_CORNER_SIZE];
static const Target *collectionCornerTarget;
static const void *collectionCornerSource;
static int collectionCornerSourceWidth, collectionCornerSourceHeight;

void releaseCollectionCornerArt(void) {
  if (gsGlobal != NULL && collectionCornerTexture.Vram != 0)
    gsKit_TexManager_free(gsGlobal, &collectionCornerTexture);
  memset(&collectionCornerTexture, 0, sizeof(collectionCornerTexture));
  collectionCornerTarget = NULL;
  collectionCornerSource = NULL;
}

void setCollectionCoverEcho(int enabled) {
  collectionCoverEcho = enabled != 0;
  if (!collectionCoverEcho)
    releaseCollectionCornerArt();
}

static void prepareCollectionCornerArt(const GSTEXTURE *cover, const Target *target) {
  if (collectionCornerTarget == target && collectionCornerSource == cover->Mem &&
      collectionCornerSourceWidth == cover->Width &&
      collectionCornerSourceHeight == cover->Height)
    return;
  releaseCollectionCornerArt();
  const int size = COLLECTION_CORNER_SIZE;
  const uint8_t *source = (const uint8_t *)cover->Mem;
  uint8_t *pixels = (uint8_t *)collectionCornerPixels;
  uint8_t *scratch = (uint8_t *)collectionCornerScratch;
  // Sample inside the jacket's existing edge feather. Copy only RGB: jacket
  // textures use inverse alpha, while this echo owns a normal transparency mask.
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      int rgb[3] = {0};
      for (int sy = 0; sy < 2; sy++) {
        int sourceY = (y * 2 + sy + 8) * cover->Height / (size * 2 + 16);
        for (int sx = 0; sx < 2; sx++) {
          int sourceX = (x * 2 + sx + 8) * cover->Width / (size * 2 + 16);
          const uint8_t *sample = source + 4 * (sourceY * cover->Width + sourceX);
          for (int channel = 0; channel < 3; channel++)
            rgb[channel] += sample[channel];
        }
      }
      for (int channel = 0; channel < 3; channel++)
        pixels[4 * (y * size + x) + channel] = (rgb[channel] + 2) / 4;
    }
  }
  // Separable seven-tap blur runs only when the selected artwork changes.
  static const int weights[] = {1, 2, 3, 4, 3, 2, 1};
  for (int pass = 0; pass < 2; pass++) {
    const uint8_t *input = pass == 0 ? pixels : scratch;
    uint8_t *output = pass == 0 ? scratch : pixels;
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++) {
        int rgb[3] = {0};
        for (int tap = -3; tap <= 3; tap++) {
          int sampleX = x + (pass == 0 ? tap : 0);
          int sampleY = y + (pass == 1 ? tap : 0);
          if (sampleX < 0) sampleX = 0;
          if (sampleY < 0) sampleY = 0;
          if (sampleX >= size) sampleX = size - 1;
          if (sampleY >= size) sampleY = size - 1;
          for (int channel = 0; channel < 3; channel++)
            rgb[channel] += input[4 * (sampleY * size + sampleX) + channel] * weights[tap + 3];
        }
        for (int channel = 0; channel < 3; channel++)
          output[4 * (y * size + x) + channel] = (rgb[channel] + 8) / 16;
        if (pass == 1)
          output[4 * (y * size + x) + 3] =
              collectionCornerAlpha(x * 1000 / (size - 1), 0x5C);
      }
    }
  }
  collectionCornerTexture.Width = collectionCornerTexture.Height = size;
  collectionCornerTexture.PSM = GS_PSM_CT32;
  collectionCornerTexture.Filter = GS_FILTER_LINEAR;
  collectionCornerTexture.Delayed = 1;
  collectionCornerTexture.Mem = (u32 *)collectionCornerPixels;
  collectionCornerTarget = target;
  collectionCornerSource = cover->Mem;
  collectionCornerSourceWidth = cover->Width;
  collectionCornerSourceHeight = cover->Height;
}

static void drawCollectionCornerArt(GSTEXTURE *cover, int cacheIdx, int entryProgress,
                                     const Target *target) {
  if (cover == NULL || !psbbnCoverLoaded[cacheIdx] || entryProgress <= 0 ||
      cover->Mem == NULL || cover->Width <= 0 || cover->Height <= 0 ||
      cover->PSM != GS_PSM_CT32 || cover->Clut != NULL)
    return;
  prepareCollectionCornerArt(cover, target);
  if (!bindTextureSafe(gsGlobal, &collectionCornerTexture))
    return;
  const float left = gsGlobal->Width * 0.5f;
  const int previousAlphaEnable = gsGlobal->PrimAlphaEnable;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  // A single filtered sprite avoids the triangular alpha interpolation of the
  // old mesh. Stretch the blurred echo across the full height of the right half.
  gsKit_prim_sprite_texture(gsGlobal, &collectionCornerTexture,
      left, 0, 0, 0,
      gsGlobal->Width, gsGlobal->Height, COLLECTION_CORNER_SIZE - 1, COLLECTION_CORNER_SIZE - 1,
      0, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80 * entryProgress / 1000));
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  gsGlobal->PrimAlphaEnable = previousAlphaEnable;
}

#ifdef LUNA_CAMERA_ANIMATIONS
typedef struct {
  float x, y, size;
  int cacheIdx, emphasis, visibility;
} ChannelCover;
static ChannelCover channelCovers[PSBBN_COVER_CACHE_COUNT];
static int channelCoverCount, channelFrameTarget = -1;

void lunaCameraCollectionBeginFrame(int target) {
  channelCoverCount = 0;
  channelFrameTarget = target;
}

int lunaCameraCollectionCoverRect(int cacheIdx, float rect[4], int *emphasis, int *visibility) {
  for (int i = 0; i < channelCoverCount; ++i) {
    const ChannelCover *item = &channelCovers[i];
    if (item->cacheIdx != cacheIdx) continue;
    rect[0] = item->x; rect[1] = item->y;
    rect[2] = rect[3] = item->size;
    if (emphasis) *emphasis = item->emphasis;
    if (visibility) *visibility = item->visibility;
    return 1;
  }
  return 0;
}

int lunaCameraCollectionHasFrame(int target) {
  float rect[4];
  return channelFrameTarget == target &&
      lunaCameraCollectionCoverRect(PSBBN_COVER_CACHE_FOCUS, rect, NULL, NULL);
}

static void recordChannelCover(float x, float y, float size, int cacheIdx,
                               int emphasis, int visibility) {
  if (channelCoverCount >= PSBBN_COVER_CACHE_COUNT) return;
  channelCovers[channelCoverCount++] =
      (ChannelCover){x, y, size, cacheIdx, emphasis, visibility};
}


#endif

void drawPSBBNCover(GSTEXTURE *cover, float x1, float y1, float size, int cacheIdx, int emphasis, int visibility, int z) {
  float x2 = x1 + size;
  float y2 = y1 + size;
  // GS texture modulation treats 0x80 as neutral. End the emphasis curve at
  // neutral instead of 0xFF, which nearly doubles and clips the jacket RGB.
  int red = (0x58 + (emphasis * 0x28) / 1000) * visibility / 1000;
  int green = (0x64 + (emphasis * 0x1C) / 1000) * visibility / 1000;
  int blue = (0x70 + (emphasis * 0x10) / 1000) * visibility / 1000;
  int alpha = (0x40 + (emphasis * 0x40) / 1000) * visibility / 1000;

  if (cover != NULL && psbbnCoverLoaded[cacheIdx] &&
      prepareCollectionCoverTexture(cacheIdx)) {
    int previousAlphaTest = gsGlobal->Test->ATST;
    int previousAlphaReference = gsGlobal->Test->AREF;
    int previousAlphaFail = gsGlobal->Test->AFAIL;
    if (!bindTextureSafe(gsGlobal, cover))
      return;
#ifdef LUNA_CAMERA_ANIMATIONS
    recordChannelCover(x1, y1, size, cacheIdx, emphasis, visibility);
#endif
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsGlobal->Test->ATST = 2;
    gsGlobal->Test->AREF = 0x80;
    gsGlobal->Test->AFAIL = 0;
    gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    // PSBBN database jackets are opaque RGB squares. Drawing the same texture
    // enlarged underneath does not create a feathered halo; it exposes a
    // second copy of the top and bottom edge. Keep one filtered jacket pass.
    gsKit_prim_sprite_texture(gsGlobal, cover, x1, y1, 0.0f, 0.0f, x2, y2, cover->Width - 1, cover->Height - 1, z,
                              GS_SETREG_RGBA(red, green, blue, alpha));
    gsGlobal->Test->ATST = previousAlphaTest;
    gsGlobal->Test->AREF = previousAlphaReference;
    gsGlobal->Test->AFAIL = previousAlphaFail;
    gsKit_set_test(gsGlobal, GS_ATEST_ON);
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  } else {
    int placeholderAlpha = (0x42 + (emphasis * 0x36) / 1000) * visibility / 1000;
    int centerX = (int)(x1 + size / 2.0f);
    int centerY = (int)(y1 + size / 2.0f);
    int radius = (int)(size / 8.0f);
    int showLabel = emphasis >= 800 && size >= 140.0f;
    if (radius > 24)
      radius = 24;
    if (radius < 4)
      radius = 4;
    if (getGlassColorPreset() == GLASS_COLOR_ORIGINAL)
      gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, z,
                        glassMissingCoverColor(placeholderAlpha));
    drawGlassDiamond(centerX, centerY - (showLabel ? getFontLineHeight() : 0),
                     radius, z + 1,
                     glassMissingCoverDiamondColor((0x48 * visibility) / 1000));
    if (showLabel)
      drawTextWindow((int)x1, centerY + radius / 2, (int)x2, 0, z + 1,
                     glassMissingCoverTextColor(), ALIGN_HCENTER,
                     lunaText("COVER\nUNAVAILABLE"));
  }
}

static float psbbnCurveValue(int distance, int selectedSize, const uint8_t *percent, int pointCount) {
  int segment;
  int fraction;
  float valuePercent;

  if (distance <= 0)
    return selectedSize * percent[0] / 100.0f;
  segment = distance / 1000;
  if (segment >= pointCount - 1)
    return selectedSize * percent[pointCount - 1] / 100.0f;

  fraction = distance - (segment * 1000);
  // Give the cover approaching focus a gentle final settle while preserving
  // the tightly compressed spacing farther down the stream.
  if (segment == 0)
    fraction = lunaNavEase(fraction);
  valuePercent = percent[segment] + ((percent[segment + 1] - percent[segment]) * fraction) / 1000.0f;
  return selectedSize * valuePercent / 100.0f;
}

static float psbbnFutureCoverSize(int distance, int selectedSize) {
  // Preserve the two nearest approved neighbors, then taper the distant tail
  // so it recedes toward a vanishing point instead of ending as a hard stack.
  static const uint8_t sizePercent[] = {100, 44, 41, 38, 30, 22, 14};
  return psbbnCurveValue(distance, selectedSize, sizePercent, sizeof(sizePercent));
}

static float psbbnFutureCenterOffset(int distance, int selectedSize) {
  static const uint8_t offsetPercent[] = {0, 50, 72, 93, 112, 127, 138};
  return psbbnCurveValue(distance, selectedSize, offsetPercent, sizeof(offsetPercent));
}

static float psbbnOutgoingCenterOffset(int distance, int selectedSize) {
  int squaredProgress;

  if (distance > 1000)
    distance = 1000;
  // Stay near the focal point while the cover grows, then accelerate to the
  // right late in the glide so the enlarged sprite clears the viewport.
  squaredProgress = distance * distance / 1000;
  return (selectedSize * 160.0f * squaredProgress) / 100000.0f;
}

static float psbbnOutgoingCoverSize(int distance, int selectedSize) {
  float progress;

  if (distance > 1000)
    distance = 1000;
  // PSBBN sends the old focal jacket toward the viewer, but it clears the
  // viewport before becoming an extreme magnification. Capping this at 1.75x
  // keeps the 256x256 jacket inside a useful filtering range.
  progress = distance / 1000.0f;
  return selectedSize * (1.0f + 0.75f * progress * progress);
}

static int psbbnAbsolute(int value) {
  return (value < 0) ? -value : value;
}


static void drawCollectionEntryFade(int entryProgress) {
  if (entryProgress >= 1000)
    return;
  // Fade the whole destination scene in after its artwork has been prepared.
  // A tinted veil keeps the shared background faintly visible at the start.
  int alpha = (1000 - entryProgress) * 0x70 / 1000;
  gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0,
                    GS_SETREG_RGBA(0x01, 0x04, 0x0D, alpha));
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
}

void formatPSBBNTitle(const char *source, char *destination, int maxWidth) {
  size_t length;

  snprintf(destination, 255, "%s", source);
  if (getLineWidth(destination) <= maxWidth)
    return;

  length = strnlen(destination, 251);
  while (length > 0) {
    destination[length] = '\0';
    if (getLineWidth(destination) + getLineWidth("...") <= maxWidth)
      break;
    length--;
  }
  strncat(destination, "...", 254 - strlen(destination));
}

#ifdef LUNA_CAMERA_ANIMATIONS
static int drawCameraRevealAnchor(GSTEXTURE *destination, int cacheIdx,
                                  float x, float y, float size) {
  GSTEXTURE *source = lunaCameraViewSource();
  if (!source) return 0;
  float rect[4];
  lunaCameraViewCoverRect(x, y, size, rect);
  int ready = destination && psbbnCoverLoaded[cacheIdx] &&
              prepareCollectionCoverTexture(cacheIdx) &&
              bindTextureSafe(gsGlobal, destination);
  lunaCameraViewReady(ready);
  const int alphaTest = gsGlobal->Test->ATST;
  const int alphaRef = gsGlobal->Test->AREF;
  const int alphaFail = gsGlobal->Test->AFAIL;
  const int alphaEnable = gsGlobal->PrimAlphaEnable;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  const float width = fmaxf(0, lunaCameraViewTurnWidth());
  const float centerX = rect[0] + rect[2] * 0.5f;
  const float centerY = rect[1] + rect[3] * 0.5f;
  const int back = lunaCameraViewTurnBack();
  GSTEXTURE *face = back ? destination : source;
  if (width < 0.01f) {
    gsKit_prim_line(gsGlobal, centerX, rect[1], centerX, rect[1] + rect[3],
        PSBBN_COVER_FOREGROUND_Z, GS_SETREG_RGBA(0x98, 0xA0, 0xB0, 0x80));
  } else if (face && (!back || ready) && bindTextureSafe(gsGlobal, face)) {
    const float halfWidth = rect[2] * width * 0.5f;
    const float tilt = (1.0f - width) * (back ? -0.12f : 0.12f);
    const float leftHeight = rect[3] * 0.5f * (1.0f + tilt);
    const float rightHeight = rect[3] * 0.5f * (1.0f - tilt);
    gsKit_prim_quad_texture(gsGlobal, face,
        centerX - halfWidth, centerY - leftHeight, 0, 0,
        centerX + halfWidth, centerY - rightHeight, face->Width - 1, 0,
        centerX - halfWidth, centerY + leftHeight, 0, face->Height - 1,
        centerX + halfWidth, centerY + rightHeight, face->Width - 1, face->Height - 1,
        PSBBN_COVER_FOREGROUND_Z, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  }
  gsGlobal->Test->ATST = alphaTest;
  gsGlobal->Test->AREF = alphaRef;
  gsGlobal->Test->AFAIL = alphaFail;
  gsGlobal->PrimAlphaEnable = alphaEnable;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  /* Keep the selected slot handled at the edge and while artwork is pending. */
  return 1;
}
#endif

void drawPSBBNCollection(TargetList *titles, int selectedTitleIdx, GSTEXTURE **covers, int flowOffset,
                         int outgoingTitleIdx, int favoritesOnly, int fastScrolling,
                         int entryProgress, int entryFade, uint32_t frameNowMs,
                         const char *nextViewLabel) {
  int top = headerHeight + 12;
  int bottom = gsGlobal->Height - footerHeight - 18;
  int selectedSize = (bottom - top) * 76 / 100;
  int maxSelectedSize = gsGlobal->Width * 38 / 100;
  int selectedX;
  int selectedY;
  int centerY;
  int panelRight;
  int focalCenterX;
  int position[PSBBN_COVER_CACHE_COUNT];
  int distance[PSBBN_COVER_CACHE_COUNT];
  int targetIndex[PSBBN_COVER_CACHE_COUNT];
  uint8_t drawable[PSBBN_COVER_CACHE_COUNT];
  uint8_t drawn[PSBBN_COVER_CACHE_COUNT] = {0};
  int visualFocus = -1;
  int visualFocusDistance = 0x7FFFFFFF;
  int foregroundCacheIdx = -1;
  int panelLineY;
  int panelLineHeight = psbbnFieldStableHeight();

#ifdef LUNA_CAMERA_ANIMATIONS
  lunaCameraCollectionBeginFrame(titles->total > 0
      ? getTargetByIdx(titles, selectedTitleIdx)->idx : -1);
#endif

  if (selectedSize > maxSelectedSize)
    selectedSize = maxSelectedSize;
  selectedSize = selectedSize * 115 / 100;
  selectedX = gsGlobal->Width - keepoutArea - selectedSize - 40;
  selectedY = headerHeight + 34;
  centerY = selectedY + selectedSize / 2;
  focalCenterX = selectedX + selectedSize / 2;
  panelRight = selectedX - 26;
#ifdef LUNA_CAMERA_ANIMATIONS
  if (lunaCameraViewActive()) {
    entryProgress = 1000;
    entryFade = 0;
  }
#endif

  // Resolve visual focus once for the corner echo, counter, list and cover stream.
  if (titles->total > 0) {
    for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
      int relativeIndex = cacheIdx - PSBBN_COVER_CACHE_FOCUS;
      position[cacheIdx] = relativeIndex * 1000 + flowOffset;
      distance[cacheIdx] = psbbnAbsolute(position[cacheIdx]);
      targetIndex[cacheIdx] = lunaNavWrap(titles->total, selectedTitleIdx + relativeIndex);
      drawable[cacheIdx] = 1;
      if (distance[cacheIdx] < visualFocusDistance) {
        visualFocus = cacheIdx;
        visualFocusDistance = distance[cacheIdx];
      }
    }
  }
  drawSharedLibraryBackground(frameNowMs);
  if (collectionCoverEcho && visualFocus >= 0)
    drawCollectionCornerArt(covers[visualFocus], visualFocus, entryProgress,
                            getTargetByIdx(titles, targetIndex[visualFocus]));

  // The original shell keeps the Collection selector on the left and lets
  // the cover stack overlap its right edge.
  panelLineY = psbbnFieldStableY(headerHeight + 42);
  gsKit_prim_sprite(gsGlobal, 24, panelLineY, panelRight, panelLineY + panelLineHeight, 1, glassPresetColor(0x2A, 0x74, 0xB8, 0x18));
  gsKit_prim_line(gsGlobal, 24, panelLineY, 24, psbbnFieldStableY(bottom), 1, glassPresetColor(0x2A, 0x8C, 0xE8, 0x14));
  // Dock the selector low on the left so the cover tail can recede through the
  // space it previously occupied without colliding with the words.
  int selectorCenterY = psbbnFieldStableY(gsGlobal->Height - footerHeight - getFontLineHeight() - 16);
  int collectionTextY = selectorCenterY - getFontLineHeight() - 8;
  int favoritesTextY = selectorCenterY + 10;
  if (!collectionGameList) {
    const char *collectionLabel = lunaText("Collection");
    const char *favoritesLabel = lunaText("Favorites");
    int activeTextY = favoritesOnly ? favoritesTextY : collectionTextY;
    int activeTextRight = 70 + getLineWidth(favoritesOnly ? favoritesLabel : collectionLabel);
    drawPSBBNFocusGlow(70, activeTextY, panelRight - 8, activeTextRight);
    drawTextWindow(70, collectionTextY, panelRight - 8, 0, 6,
                   favoritesOnly ? HeaderTextColor : FontMainColor, ALIGN_LEFT, collectionLabel);
    drawTextWindow(70, favoritesTextY, panelRight - 8, 0, 6,
                   favoritesOnly ? FontMainColor : HeaderTextColor, ALIGN_LEFT, lunaText("Favorites"));
  }

  if (titles->total <= 0) {
    collectionVisibleTitle = NULL;
    drawTextWindow(40, headerHeight + 96, panelRight, selectorCenterY - getFontLineHeight() * 2,
                   5, HeaderTextColor, ALIGN_CENTER,
                   lunaText("NO FAVORITES YET\nAdd favorites in List"));
    if (entryFade)
      drawCollectionEntryFade(entryProgress);
    return;
  }

  // On a cold entry, reveal the prepared jackets together instead of drawing
  // each one as its worker job finishes. The ambient background keeps moving.
  if (entryProgress < 0) {
    collectionVisibleTitle = NULL;
    drawCollectionEntryFade(0);
    return;
  }
  if (entryProgress == 0)
    collectionVisibleTitle = NULL;

  // Keep the position counter tied to the cover nearest the animated focal point.
  if (visualFocus >= 0) {
    char focusCounter[32];
    int counterRight = gsGlobal->Width - keepoutArea;

    // Show the title only for a confirmed missing cover after the selection
    // settles. Fast scanning keeps it hidden between individual scan steps.
    if (!fastScrolling && flowOffset == 0 && collectionCoverMissing(visualFocus)) {
      Target *focusTitle = getTargetByIdx(titles, targetIndex[visualFocus]);
      int titleY = selectedY - getFontLineHeight() - 8;
      float titleWidth = getLineWidth(focusTitle->name);
      if (collectionVisibleTitle != focusTitle) {
        collectionVisibleTitle = focusTitle;
        collectionTitleStartMs = frameNowMs;
      }
      uint32_t titleElapsedMs = frameNowMs - collectionTitleStartMs;
      int slideProgress = titleElapsedMs >= COLLECTION_TITLE_SLIDE_MS ? 1000
          : lunaNavEase((int)(titleElapsedMs * 1000U / COLLECTION_TITLE_SLIDE_MS));
      // Begin inside the selected cover area and rise above its top edge.
      int slideStartY = selectedY + getFontLineHeight() + 20;
      int slideY = titleY + (slideStartY - titleY) * (1000 - slideProgress) / 1000;
      if (titleWidth <= selectedSize) {
        drawTextWindow(selectedX, slideY, selectedX + selectedSize, 0, 3,
                       FontMainColor, ALIGN_HCENTER, focusTitle->name);
      } else {
        int overflow = (int)(titleWidth - selectedSize + 0.99f);
        uint32_t travelMs = (uint32_t)overflow * 1000 / COLLECTION_TITLE_SCROLL_SPEED;
        uint32_t cycleMs = COLLECTION_TITLE_START_PAUSE_MS +
                           COLLECTION_TITLE_END_PAUSE_MS + 2 * travelMs;
        uint32_t phase = titleElapsedMs < COLLECTION_TITLE_SLIDE_MS ? 0
            : (titleElapsedMs - COLLECTION_TITLE_SLIDE_MS) % cycleMs;
        int scrollX = 0;
        if (phase > COLLECTION_TITLE_START_PAUSE_MS) {
          phase -= COLLECTION_TITLE_START_PAUSE_MS;
          if (phase < travelMs)
            scrollX = (int)((uint64_t)phase * overflow / travelMs);
          else if (phase < travelMs + COLLECTION_TITLE_END_PAUSE_MS)
            scrollX = overflow;
          else
            scrollX = overflow - (int)((uint64_t)(phase - travelMs -
                      COLLECTION_TITLE_END_PAUSE_MS) * overflow / travelMs);
        }
        drawTextMarquee(selectedX, slideY, selectedX + selectedSize, 3,
                        FontMainColor, focusTitle->name, scrollX);
      }
    } else {
      collectionVisibleTitle = NULL;
    }

    snprintf(focusCounter, sizeof(focusCounter), "%d/%d", targetIndex[visualFocus] + 1, titles->total);
    drawTextWindow(selectedX + selectedSize - 58, favoritesTextY, counterRight, 0, 5,
                   GS_SETREG_RGBA(0xC8, 0xD4, 0xE8, 0x80), ALIGN_RIGHT, focusCounter);
  }

  // Small libraries wrap within the ten-entry cache. Keep only the closest
  // occurrence of each title so a one-game collection never draws duplicates.
  for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
    for (int priorIdx = 0; priorIdx < cacheIdx; priorIdx++) {
      if (!drawable[priorIdx] || targetIndex[priorIdx] != targetIndex[cacheIdx])
        continue;
      if (distance[priorIdx] <= distance[cacheIdx])
        drawable[cacheIdx] = 0;
      else
        drawable[priorIdx] = 0;
    }
  }

  if (flowOffset != 0 && outgoingTitleIdx >= 0) {
    // Held repeats queue the next selection before the current glide ends.
    // Keep upcoming jackets behind the cover actually leaving focus.
    foregroundCacheIdx = lunaNavCollectionForeground(position, drawable,
                                                     PSBBN_COVER_CACHE_COUNT);
  }

  // Draw the background stream far-to-near, then the foreground cover.
  // Its depth follows the transition direction instead of fading emphasis.
  for (int pass = 0; pass < PSBBN_COVER_CACHE_COUNT; pass++) {
    int futureToDraw = -1;
    int outgoingToDraw = -1;
    int cacheToDraw;

    for (int cacheIdx = 0; cacheIdx < PSBBN_COVER_CACHE_COUNT; cacheIdx++) {
      if (!drawable[cacheIdx] || drawn[cacheIdx] || cacheIdx == foregroundCacheIdx)
        continue;
      if (position[cacheIdx] < 0) {
        if (outgoingToDraw < 0 || distance[cacheIdx] < distance[outgoingToDraw])
          outgoingToDraw = cacheIdx;
      } else if (futureToDraw < 0 || distance[cacheIdx] > distance[futureToDraw]) {
        futureToDraw = cacheIdx;
      }
    }

    cacheToDraw = (futureToDraw >= 0) ? futureToDraw : outgoingToDraw;
    if (cacheToDraw < 0 && foregroundCacheIdx >= 0 && !drawn[foregroundCacheIdx])
      cacheToDraw = foregroundCacheIdx;
    if (cacheToDraw < 0)
      break;

    drawn[cacheToDraw] = 1;
    float size;
    float itemCenterX;
    float itemCenterY;

    if (position[cacheToDraw] < 0) {
      size = psbbnOutgoingCoverSize(distance[cacheToDraw], selectedSize);
      itemCenterX = focalCenterX + psbbnOutgoingCenterOffset(distance[cacheToDraw], selectedSize);
      itemCenterY = centerY;
    } else {
      size = psbbnFutureCoverSize(distance[cacheToDraw], selectedSize);
      itemCenterX = focalCenterX - psbbnFutureCenterOffset(distance[cacheToDraw], selectedSize);
      itemCenterY = centerY + (distance[cacheToDraw] * 3.0f) / 2000.0f;
    }
    // Settle the destination stack into focus without retaining the old view's
    // artwork or allocating another framebuffer texture.
#ifdef LUNA_CAMERA_ANIMATIONS
    if (!lunaCameraViewActive())
#endif
      size *= (850 + entryProgress * 150 / 1000) / 1000.0f;
    float x1 = itemCenterX - size / 2.0f;
    float y1 = itemCenterY - size / 2.0f;
    int focusProgress = 1000 - ((distance[cacheToDraw] < 1000) ? distance[cacheToDraw] : 1000);
    int emphasis = lunaNavEase(focusProgress);
    int visibility = 1000;

    if (position[cacheToDraw] >= 0 && distance[cacheToDraw] > 1000) {
      int fadeDistance = distance[cacheToDraw] - 1000;
      if (fadeDistance > 5000)
        fadeDistance = 5000;
      visibility = 1000 - (fadeDistance * 850) / 5000;
    }
    if (entryFade)
      visibility = visibility * (250 + entryProgress * 750 / 1000) / 1000;

#ifdef LUNA_CAMERA_ANIMATIONS
    if (lunaCameraViewActive()) {
      if (cacheToDraw == visualFocus &&
          drawCameraRevealAnchor(covers[cacheToDraw], cacheToDraw, x1, y1, size))
        continue;
      lunaCameraViewNeighbor(focalCenterX, centerY, &x1, &y1, &size);
    }
#endif
    if (x1 + size > 24.0f && x1 < (float)(gsGlobal->Width - keepoutArea))
      drawPSBBNCover(covers[cacheToDraw], x1, y1, size, cacheToDraw, emphasis, visibility,
                     (cacheToDraw == foregroundCacheIdx) ? PSBBN_COVER_FOREGROUND_Z
                                                      : PSBBN_COVER_BACKGROUND_Z);
  }
  if (collectionGameList && visualFocus >= 0)
    drawCollectionGameList(titles, targetIndex[visualFocus], selectedTitleIdx,
                           flowOffset, panelRight, psbbnFieldStableY(bottom), frameNowMs,
                           fastScrolling || flowOffset != 0 || entryProgress < 1000);

  if (entryFade)
    drawCollectionEntryFade(entryProgress);
}
