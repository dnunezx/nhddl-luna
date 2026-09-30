// LUNA 2026
#include "ui/graphics.h"
#include "dprintf.h"
#include "ui/dejavu_sans.h"
#include "ui/psbbn_font.h"
#include "ui/icons.h"
#include "ui/grid_selector.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <malloc.h>
#include <png.h>
#include <stdlib.h>

extern unsigned char virtual_memory_card_png[];
extern unsigned int size_virtual_memory_card_png;
extern unsigned char ps1_memory_card_1_png[];
extern unsigned int size_ps1_memory_card_1_png;
extern unsigned char ps1_memory_card_2_png[];
extern unsigned int size_ps1_memory_card_2_png;
extern unsigned char memory_card_menu_png[];
extern unsigned int size_memory_card_menu_png;
extern unsigned char file_explorer_png[];
extern unsigned int size_file_explorer_png;

// Loads 32-bit RGBA PNG texture from memory into GSTEXTURE and uploads it to GS VRAM.
static int gsKit_texture_png_mem(GSGLOBAL *gsGlobal, GSTEXTURE *texture, void *buf, size_t size, int whiteTransparentRgb,
                                 int upload);

// Array of initialized GS textures containing font pages
GSTEXTURE **fontPages;
// Graphics textures
GSTEXTURE *icons;
GSTEXTURE *gridSelector;
static GSTEXTURE cardArtTextures[CARD_ART_COUNT];
static uint8_t cardArtAttempted[CARD_ART_COUNT];
static uint8_t cardArtLoaded[CARD_ART_COUNT];

// The supplied selector is opaque, with its glow composited on black. Convert
// it once into a transparent overlay so grid covers remain visible beneath it.
static void prepareGridSelector(GSTEXTURE *texture) {
  struct pixel {
    uint8_t r, g, b, a;
  };
  struct pixel *pixels = (struct pixel *)texture->Mem;

  for (int y = 0; y < texture->Height; y++) {
    for (int x = 0; x < texture->Width; x++) {
      struct pixel *pixel = &pixels[y * texture->Width + x];
      int max = pixel->r;
      if (pixel->g > max)
        max = pixel->g;
      if (pixel->b > max)
        max = pixel->b;

      if (x >= 14 && x <= 49 && y >= 14 && y <= 49) {
        // Let the cover or fallback tile show through the selector's center.
        pixel->a = 0;
      } else {
        // Source RGB is already darkened against black; recover its hue as
        // brightness becomes alpha, including the soft outer glow.
        pixel->a = 128 - max * 128 / 255;
        if (max > 0) {
          pixel->r = pixel->r * 255 / max;
          pixel->g = pixel->g * 255 / max;
          pixel->b = pixel->b * 255 / max;
        }
      }
    }
  }
}

// Keep only the selected font's texture decoded and resident.
static const BMFont *font = &BMFONT_DEJAVU_SANS;
static UIFont activeUIFont = UI_FONT_DEJAVU;

static void releaseFontPages(void) {
  if (fontPages == NULL)
    return;
  for (int i = 0; i < font->pageCount; i++) {
    if (fontPages[i] == NULL)
      continue;
    if (fontPages[i]->Vram != 0)
      gsKit_TexManager_free(gsGlobal, fontPages[i]);
    free(fontPages[i]->Mem);
    free(fontPages[i]);
  }
  free(fontPages);
  fontPages = NULL;
}

int setUIFont(UIFont selection) {
  if (selection < UI_FONT_DEJAVU || selection >= UI_FONT_COUNT)
    return -1;
  if (selection == activeUIFont && fontPages != NULL)
    return 0;
  const BMFont *next = selection == UI_FONT_PSBBN ?
                           &BMFONT_PSBBN : &BMFONT_DEJAVU_SANS;
  GSTEXTURE **nextPages = calloc(next->pageCount, sizeof(*nextPages));
  if (nextPages == NULL)
    return -1;
  for (int i = 0; i < next->pageCount; i++) {
    nextPages[i] = calloc(1, sizeof(*nextPages[i]));
    if (nextPages[i] == NULL ||
        gsKit_texture_png_mem(gsGlobal, nextPages[i], next->pages[i].data,
                              next->pages[i].size, 0, 0)) {
      for (int j = 0; j <= i; j++) {
        if (nextPages[j] != NULL) {
          free(nextPages[j]->Mem);
          free(nextPages[j]);
        }
      }
      free(nextPages);
      return -1;
    }
  }
  releaseFontPages();
  font = next;
  fontPages = nextPages;
  activeUIFont = selection;
  return 0;
}

// Initializes and uploads graphics resources to GS VRAM
int initGraphics() {
  if (font->pageCount == 0) {
    DPRINTF("ERROR: Invalid number of font pages\n");
    return -1;
  }
  if (setUIFont(activeUIFont))
    return -1;

  // Upload icons texture to GS
  icons = calloc(sizeof(GSTEXTURE), 1);
  if (gsKit_texture_png_mem(gsGlobal, icons, ICONS_PNG, SIZE_ICONS_PNG, 0, 1)) {
    DPRINTF("ERROR: Failed to load icons texture\n");
    return -1;
  }

  gridSelector = calloc(sizeof(GSTEXTURE), 1);
  if (gridSelector != NULL &&
      gsKit_texture_png_mem(gsGlobal, gridSelector, (void *)GRID_SELECTOR_PNG,
                            SIZE_GRID_SELECTOR_PNG, 0, 0) == 0) {
    prepareGridSelector(gridSelector);
    gridSelector->Filter = GS_FILTER_LINEAR;
    gsKit_TexManager_bind(gsGlobal, gridSelector);
  } else {
    DPRINTF("WARNING: Failed to load Grid selector texture\n");
    if (gridSelector != NULL) {
      free(gridSelector->Mem);
      free(gridSelector);
      gridSelector = NULL;
    }
  }

  return 0;
}

// Frees memory used by font pages and icon textures
void closeFont() {
  for (int i = 0; i < CARD_ART_COUNT; i++) {
    free(cardArtTextures[i].Mem);
    cardArtTextures[i].Mem = NULL;
    cardArtTextures[i].Vram = 0;
    cardArtLoaded[i] = 0;
    cardArtAttempted[i] = 0;
  }
  releaseFontPages();

  free(icons->Mem);
  free(icons);
  if (gridSelector != NULL) {
    free(gridSelector->Mem);
    free(gridSelector);
    gridSelector = NULL;
  }
  return;
}

// Returns icon width
int getIconWidth(IconType iconType) { return ICONS[iconType].width; }

// Draws the icon at specified coordinates
void drawIcon(float x, float y, int z, uint64_t color, IconType iconType) {
  Icon icon = ICONS[iconType];

  // Preserve the artwork colors for every controller button.
  if (iconType != ICON_ENABLED)
    color = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);

  gsKit_TexManager_bind(gsGlobal, icons);
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, icons,          // font page
                            x,                        // x1 (destination)
                            y,                        // y1
                            icon.x,                   // u1 (source texture)
                            icon.y,                   // v1
                            x + icon.width,           // x2 (destination)
                            y + icon.height,          // y2
                            icon.x + icon.width + 1,  // u2 (source texture)
                            icon.y + icon.height + 1, // v2
                            z, color);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void drawCardArt(CardArtType card, float x, float y, float size) {
  static const unsigned char *const pngData[] = {
      virtual_memory_card_png, ps1_memory_card_1_png,
      ps1_memory_card_2_png, memory_card_menu_png, file_explorer_png};
  static const unsigned int *const pngSizes[] = {
      &size_virtual_memory_card_png, &size_ps1_memory_card_1_png,
      &size_ps1_memory_card_2_png, &size_memory_card_menu_png,
      &size_file_explorer_png};
  if (card < CARD_ART_VIRTUAL || card >= CARD_ART_COUNT || size <= 0)
    return;

  GSTEXTURE *texture = &cardArtTextures[card];
  if (!cardArtAttempted[card]) {
    cardArtAttempted[card] = 1;
    texture->Delayed = 1;
    if (gsKit_texture_png_mem(gsGlobal, texture, (void *)pngData[card],
                              *pngSizes[card], 0, 0) == 0) {
      // The main-menu artwork is displayed at 200 pixels. Keep its texture
      // within 256 pixels to avoid spending a quarter of PS2 VRAM on mc.png.
      if (texture->Width > 256 || texture->Height > 256) {
        int sourceWidth = texture->Width;
        int sourceHeight = texture->Height;
        int width = sourceWidth > 256 ? 256 : sourceWidth;
        int height = sourceHeight > 256 ? 256 : sourceHeight;
        u32 *source = texture->Mem;
        u32 *scaled = memalign(128, gsKit_texture_size(width, height,
                                                       texture->PSM));
        if (scaled == NULL) {
          free(texture->Mem);
          texture->Mem = NULL;
          return;
        }
        for (int row = 0; row < height; row++)
          for (int column = 0; column < width; column++)
            scaled[row * width + column] =
                source[(row * sourceHeight / height) * sourceWidth +
                       column * sourceWidth / width];
        free(texture->Mem);
        texture->Mem = scaled;
        texture->Width = width;
        texture->Height = height;
      }
      texture->Filter = GS_FILTER_LINEAR;
      gsKit_TexManager_bind(gsGlobal, texture);
      cardArtLoaded[card] = 1;
    } else {
      free(texture->Mem);
      texture->Mem = NULL;
    }
  }
  if (!cardArtLoaded[card])
    return;

  gsKit_TexManager_bind(gsGlobal, texture);
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, texture, x, y, 0, 0, x + size,
                            y + size, texture->Width - 1,
                            texture->Height - 1, 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

// Draws the icon in [x1,y1],[x2,y2] window.
void drawIconWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, IconType iconType) {
  Icon icon = ICONS[iconType];

  // Apply vertical alignment
  if (y2) {
    if (alignment & ALIGN_VCENTER) {
      y1 += ((y2 - y1) - icon.height) / 2;
    } else if (alignment & ALIGN_BOTTOM) {
      y1 = y2 - icon.height;
    }
  }

  // Apply horizontal alignment
  if (x2) {
    if (alignment & ALIGN_HCENTER) {
      x1 = x1 + (((x2 - x1) - icon.width) / 2);
    } else if (alignment & ALIGN_RIGHT) {
      x1 = x2 - icon.width;
    }
  }

  drawIcon(x1, y1, z, color, iconType);
}

void drawPromptBar(int left, int top, int right, int bottom, int z,
                   uint64_t labelColor, PromptBar bar) {
  if (right <= left || bottom <= top)
    return;
  if (bar.note != NULL) {
    int noteRight = bar.count > 0 ? left + (right - left) / 2 : right;
    drawTextWindow(left, top, noteRight - 6, bottom, z, labelColor,
                   bar.count > 0 ? ALIGN_VCENTER : ALIGN_CENTER, bar.note);
    if (bar.count == 0)
      return;
    left = noteRight;
  }
  if (bar.items == NULL || bar.count <= 0)
    return;
  int slot = (right - left) / bar.count;
  for (int i = 0; i < bar.count; i++) {
    const ButtonPrompt *prompt = &bar.items[i];
    int slotLeft = left + i * slot;
    int slotRight = i == bar.count - 1 ? right : slotLeft + slot;
    int labelWidth = (int)getLineWidth(prompt->label);
    int contentWidth = getIconWidth(prompt->icon) + 6 + labelWidth;
    int x = slotLeft + ((slotRight - slotLeft) - contentWidth) / 2;
    if (x < slotLeft + 2)
      x = slotLeft + 2;
    drawIconWindow(x, top, 0, bottom, z, FontMainColor,
                   ALIGN_VCENTER, prompt->icon);
    drawTextWindow(x + getIconWidth(prompt->icon) + 6, top, slotRight - 2,
                   bottom, z, labelColor, ALIGN_VCENTER, prompt->label);
  }
}

// Returns line height for used font
uint8_t getFontLineHeight() { return font->lineHeight; }

// Returns pointer to the glyph or NULL if the font doesn't have a glyph for this character
const BMFontChar *getGlyph(uint32_t character) {
  for (int i = 0; i < font->bucketCount; i++) {
    if ((font->buckets[i].startChar <= character) && (font->buckets[i].endChar >= character)) {
      return &font->buckets[i].chars[character - font->buckets[i].startChar];
    }
  }
  return NULL;
}

// Draws glyph at specified coordinates
static void drawGlyph(const BMFontChar *glyph, float x, float y, int z, uint64_t color) {
  gsKit_TexManager_bind(gsGlobal, fontPages[glyph->page]);
  gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],   // font page
                            x + glyph->xoffset,                 // x1 (destination)
                            y + glyph->yoffset,                 // y1
                            glyph->x,                           // u1 (source texture)
                            glyph->y,                           // v1
                            x + glyph->xoffset + glyph->width,  // x2 (destination)
                            y + glyph->yoffset + glyph->height, // y2
                            glyph->x + glyph->width + 1,        // u2 (source texture, without +1 all characters are cut off on real hardware)
                            glyph->y + glyph->height + 1,       // v2
                            z, color);
}

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawText(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text) {
  int curX = x;
  const BMFontChar *glyph;
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  // Transparent font-atlas texels must be rejected rather than merely blended.
  // Otherwise their invisible quads still write depth and appear as boxes when
  // animated artwork passes behind text.
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);

  int curHeight = 0;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      curX = x;
      curHeight += font->lineHeight;
      continue;
    }

    if (maxWidth && (curX > maxWidth)) {
      continue;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    if (maxHeight && ((curHeight + font->lineHeight) > maxHeight)) {
      break;
    }

    drawGlyph(glyph, curX, y + curHeight, z, color);
    curX += glyph->xadvance;

    // Account for kerning if kernings are present and next char is not a null terminator
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          curX += glyph->kernings[i].amount;
        }
      }
    }
  }

  // Reset alpha
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  return (y + curHeight + font->lineHeight);
}

// Gets the line width for the first line in text
float getLineWidth(const char *text) {
  float lineWidth = 0;
  const BMFontChar *glyph;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      return lineWidth;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    lineWidth += glyph->xadvance;
    // Account for kerning
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          lineWidth += glyph->kernings[i].amount;
        }
      }
    }
  }
  return lineWidth;
}

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text) {
  if (!x2 && !y2) {
    // If window limits are not set, use faster drawing function
    return drawText(x1, x2, z, 0, 0, color, text);
  }
  float curX = x1;
  float curY = y1;
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  // Determine text height
  int maxHeight = font->lineHeight;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n')
      maxHeight += font->lineHeight;
  }

  // Apply vertical alignment if text fits within set y2
  if (y2) {
    if ((alignment & ALIGN_VCENTER) && (maxHeight < y2)) {
      curY += ((y2 - y1) - maxHeight) / 2;
    } else if ((alignment & ALIGN_BOTTOM) && (maxHeight < y2)) {
      curY = y2 - maxHeight;
    }
  }

  // Reject fully transparent atlas texels so glyph bounds cannot mask moving
  // artwork through depth writes.
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);

  // Get the width of the first line
  int lineWidth = getLineWidth(text);
  // Determine line offset according to alignment
  if (x2) {
    if (alignment & ALIGN_HCENTER) {
      curX = x1 + (((x2 - x1) - lineWidth) / 2);
    } else if (alignment & ALIGN_RIGHT) {
      curX = x2 - lineWidth;
    }
  }

  const BMFontChar *glyph;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      curX = x1;
      curY += font->lineHeight;
      // Get the width of the next line
      lineWidth = getLineWidth(&text[i + 1]);
      // Set line offset according to alignment
      if (x2) {
        if (alignment & ALIGN_HCENTER) {
          curX = x1 + (((x2 - x1) - lineWidth) / 2);
        } else if (alignment & ALIGN_RIGHT) {
          curX = x2 - lineWidth;
        }
      }
      continue;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    if (y2 && ((curY + font->lineHeight) > y2)) {
      // If window bottom border has been reached, break
      break;
    }

    // Skip drawing glyph if doesn't fit in the window
    if (!((curY < y1) || (curX < x1) || (x2 && (curX + 1 >= x2)))) {
      drawGlyph(glyph, curX, curY, z, color);
    }

    curX += glyph->xadvance;
    // Account for kerning if kernings are present and next char is not a null terminator
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          curX += glyph->kernings[i].amount;
        }
      }
    }
  }

  // Reset alpha
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  return curY + font->lineHeight;
}

int drawTextMarquee(int x1, int y, int x2, int z, uint64_t color, const char *text, int scrollX) {
  float curX = x1 - scrollX;
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);

  for (int i = 0; text[i] != '\0' && text[i] != '\n'; i++) {
    const BMFontChar *glyph = getGlyph(text[i]);
    if (glyph == NULL)
      continue;

    float glyphLeft = curX + glyph->xoffset;
    float glyphRight = glyphLeft + glyph->width;
    if (glyph->width > 0 && glyphRight > x1 && glyphLeft < x2) {
      float clippedLeft = (glyphLeft < x1) ? x1 : glyphLeft;
      float clippedRight = (glyphRight > x2) ? x2 : glyphRight;
      float textureScale = (glyph->width + 1.0f) / glyph->width;
      float u1 = glyph->x + (clippedLeft - glyphLeft) * textureScale;
      float u2 = glyph->x + (clippedRight - glyphLeft) * textureScale;
      gsKit_TexManager_bind(gsGlobal, fontPages[glyph->page]);
      gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],
                                clippedLeft, y + glyph->yoffset, u1, glyph->y,
                                clippedRight, y + glyph->yoffset + glyph->height,
                                u2, glyph->y + glyph->height + 1, z, color);
    }

    curX += glyph->xadvance;
    if (glyph->kernings && text[i + 1] != '\0') {
      for (int k = 0; k < glyph->kerningsCount; k++) {
        if (glyph->kernings[k].secondChar == text[i + 1])
          curX += glyph->kernings[k].amount;
      }
    }
  }

  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  return y + font->lineHeight;
}

// Loads a 32-bit RGBA PNG texture from memory. Callers that are going to
// resize the decoded pixels can defer the GS upload and bind only the final
// texture, avoiding a redundant full-resolution transfer.
// Code based on gsToolkit.
static int gsKit_texture_png_mem(GSGLOBAL *gsGlobal, GSTEXTURE *texture, void *buf, size_t size, int whiteTransparentRgb,
                                 int upload) {
  FILE *file = fmemopen(buf, size, "rb");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to load PNG file\n");
    return -1;
  }

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  png_bytep *row_pointers;

  uint32_t sig_read = 0;
  int row, i, k = 0, j, bit_depth, color_type, interlace_type;

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

  if (!png_ptr) {
    DPRINTF("ERROR: Failed to init libpng read struct\n");
    fclose(file);
    return -1;
  }

  info_ptr = png_create_info_struct(png_ptr);

  if (!info_ptr) {
    DPRINTF("ERROR: Failed to init libpng info struct\n");
    fclose(file);
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    return -1;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    DPRINTF("ERROR: Failed to setup libpng long jump\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(file);
    return -1;
  }

  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, sig_read);
  png_read_info(png_ptr, info_ptr);
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

  if (bit_depth == 16)
    png_set_strip_16(png_ptr);

  // OPL Manager's GameArt database uses indexed (palette) PNGs. Normalize
  // those, plus the other common PNG color types, before reading rows so the
  // texture upload always receives four bytes per pixel.
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png_ptr);
  else if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png_ptr);

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png_ptr);

  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png_ptr);

  // RGB and palette PNGs without tRNS do not have an alpha channel. Add an
  // opaque filler byte; PNG alpha is preserved when the source has RGBA,
  // gray-alpha, or tRNS data.
  const int hasAlpha = (color_type == PNG_COLOR_TYPE_RGBA || color_type == PNG_COLOR_TYPE_GRAY_ALPHA ||
                        png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS));
  if (!hasAlpha)
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

  png_read_update_info(png_ptr, info_ptr);

  if (png_get_channels(png_ptr, info_ptr) != 4) {
    DPRINTF("ERROR: PNG did not normalize to RGBA\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(file);
    return -1;
  }

  texture->Width = width;
  texture->Height = height;
  texture->VramClut = 0;
  texture->Clut = NULL;
  texture->PSM = GS_PSM_CT32;
  texture->Filter = GS_FILTER_NEAREST;
  texture->Mem = memalign(128, gsKit_texture_size(texture->Width, texture->Height, texture->PSM));

  int row_bytes = png_get_rowbytes(png_ptr, info_ptr);
  row_pointers = calloc(height, sizeof(png_bytep));
  for (row = 0; row < height; row++)
    row_pointers[row] = malloc(row_bytes);

  png_read_image(png_ptr, row_pointers);

  struct pixel {
    uint8_t r, g, b, a;
  };
  struct pixel *pixels = (struct pixel *)texture->Mem;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      uint8_t sourceAlpha = row_pointers[i][4 * j + 3];
      if (whiteTransparentRgb && sourceAlpha == 0) {
        // Linear filtering interpolates RGB independently of alpha. Bleeding
        // white into invisible logo texels prevents a dark fringe at edges.
        pixels[k].r = 0xff;
        pixels[k].g = 0xff;
        pixels[k].b = 0xff;
      } else {
        pixels[k].r = row_pointers[i][4 * j];
        pixels[k].g = row_pointers[i][4 * j + 1];
        pixels[k].b = row_pointers[i][4 * j + 2];
      }
      pixels[k++].a = 128 - ((int)sourceAlpha * 128 / 255);
    }
  }

  for (row = 0; row < height; row++)
    free(row_pointers[row]);

  free(row_pointers);
  png_read_end(png_ptr, NULL);
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
  fclose(file);

  if (upload)
    gsKit_TexManager_bind(gsGlobal, texture);

  return 0;
}

static int loadPNGTextureRGBAInternal(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path, int upload) {
  FILE *file = fopen(path, "rb");
  void *buffer;
  long fileSize;
  int result;

  if (file == NULL) {
    DPRINTF("Failed to load PNG file: %s\n", path);
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (fileSize = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    DPRINTF("ERROR: Failed to size PNG file: %s\n", path);
    fclose(file);
    return -1;
  }

  buffer = malloc(fileSize);
  if (buffer == NULL || fread(buffer, 1, fileSize, file) != (size_t)fileSize) {
    DPRINTF("ERROR: Failed to read PNG file: %s\n", path);
    free(buffer);
    fclose(file);
    return -1;
  }
  fclose(file);

  result = gsKit_texture_png_mem(gsGlobal, texture, buffer, fileSize, 0, upload);
  free(buffer);
  return result;
}

int loadPNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path) {
  return loadPNGTextureRGBAInternal(gsGlobal, texture, path, 1);
}

int loadPNGTextureRGBAMemory(GSGLOBAL *gsGlobal, GSTEXTURE *texture,
                             const unsigned char *data, size_t size) {
  return gsKit_texture_png_mem(gsGlobal, texture, (void *)data, size, 0, 1);
}

int decodePNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path) {
  return loadPNGTextureRGBAInternal(gsGlobal, texture, path, 0);
}
