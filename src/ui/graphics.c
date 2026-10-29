// LUNA 2026
#include "ui/graphics.h"
#include "ui/ui.h"
#include "ui/dejavu_sans.h"
#include "ui/psbbn_font.h"
#include "ui/utf8.h"
#include "ui/navigation.h"
#include "ui/icons.h"
#include "ui/grid_selector.h"
#include "ui/view_internal.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <malloc.h>
#include <math.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

// Loads 32-bit RGBA PNG texture from memory into GSTEXTURE and uploads it to GS VRAM.
static int gsKit_texture_png_mem(GSGLOBAL *gsGlobal, GSTEXTURE *texture, void *buf, size_t size, int whiteTransparentRgb,
                                 int upload);

// Array of initialized GS textures containing font pages
GSTEXTURE **fontPages;
// Graphics textures
GSTEXTURE *icons;
GSTEXTURE *gridSelector;

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
static u32 fontVram = GSKIT_ALLOC_ERROR;
static u32 fontVramSize;
static int graphicsInitialized;

static void finishGraphicsTransfers(void) {
  if (gsGlobal == NULL)
    return;
  // Inline uploads keep their source pixels until the queued DMA completes.
  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  dmaKit_wait_fast();
}

static void releaseGraphicsTexture(GSTEXTURE *texture) {
  if (texture == NULL)
    return;
  if (gsGlobal != NULL && texture->Vram != 0)
    gsKit_TexManager_free(gsGlobal, texture);
  free(texture->Mem);
  free(texture->Clut);
  memset(texture, 0, sizeof(*texture));
}

int reserveUIFontVRAM(void) {
  if (gsGlobal == NULL)
    return -1;
  if (fontVramSize != 0)
    return 0;
  const BMFont *const fonts[] = {&BMFONT_DEJAVU_SANS, &BMFONT_PSBBN};
  u32 largest = 0;

  for (int i = 0; i < UI_FONT_COUNT; i++) {
    const u32 bytes = gsKit_texture_size(fonts[i]->scaleW, fonts[i]->scaleH,
                                          GS_PSM_CT32) * fonts[i]->pageCount;
    if (bytes > largest)
      largest = bytes;
  }
  u32 address = gsKit_vram_alloc(gsGlobal, largest, GSKIT_ALLOC_USERBUFFER);
  if (address == GSKIT_ALLOC_ERROR)
    return -1;
  fontVram = address;
  fontVramSize = largest;
  return 0;
}

static void releaseFontPages(void) {
  if (fontPages == NULL)
    return;
  for (int i = 0; i < font->pageCount; i++) {
    if (fontPages[i] == NULL)
      continue;
    free(fontPages[i]->Mem);
    free(fontPages[i]->Clut);
    free(fontPages[i]);
  }
  free(fontPages);
  fontPages = NULL;
}

static void releaseGraphicsResources(void) {
  finishGraphicsTransfers();
  // Font pages use the reserved user buffer, not texture-manager allocations.
  releaseFontPages();
  releaseGraphicsTexture(icons);
  free(icons);
  icons = NULL;
  releaseGraphicsTexture(gridSelector);
  free(gridSelector);
  gridSelector = NULL;
  graphicsInitialized = 0;
}

static void prepareFontPageAlpha(GSTEXTURE *page) {
  // The shared PNG decoder uses inverted GS alpha for artwork. Fonts need
  // zero for transparent texels so vertex opacity can fade the glyph without
  // turning its transparent rectangle into an opaque, depth-writing block.
  u8 *pixels = (u8 *)page->Mem;
  for (size_t i = 0; i < (size_t)page->Width * page->Height; i++)
    pixels[i * 4 + 3] = 0x80 - pixels[i * 4 + 3];
}

static void prepareIconsAlpha(void) {
  // Use source alpha for the whole atlas so buttons can fade with their labels.
  // At full opacity this is equivalent to the old inverse-alpha icon blend.
  u8 *pixels=(u8 *)icons->Mem;
  for(size_t i=0;i<(size_t)icons->Width*icons->Height;i++)
    pixels[i*4+3]=0x80-pixels[i*4+3];
}

int setUIFont(UIFont selection) {
  if (selection < UI_FONT_DEJAVU || selection >= UI_FONT_COUNT)
    return -1;
  if (gsGlobal == NULL || fontVram == GSKIT_ALLOC_ERROR || fontVramSize == 0)
    return -1;
  if (selection == activeUIFont && fontPages != NULL)
    return 0;
  const BMFont *next = selection == UI_FONT_PSBBN ?
                           &BMFONT_PSBBN : &BMFONT_DEJAVU_SANS;
  const u32 pageSize = gsKit_texture_size(next->scaleW, next->scaleH,
                                           GS_PSM_CT32);
  if (pageSize * next->pageCount > fontVramSize)
    return -1;
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
          free(nextPages[j]->Clut);
          free(nextPages[j]);
        }
      }
      free(nextPages);
      return -1;
    }
  }
  finishGraphicsTransfers();
  releaseFontPages();
  font = next;
  fontPages = nextPages;
  activeUIFont = selection;
  for (int i = 0; i < next->pageCount; i++) {
    GSTEXTURE *page = fontPages[i];
    prepareFontPageAlpha(page);
    page->Vram = fontVram + pageSize * i;
    gsKit_setup_tbw(page);
    SyncDCache(page->Mem, (u8 *)page->Mem + pageSize);
    gsKit_texture_send_inline(gsGlobal, page->Mem, page->Width, page->Height,
                              page->Vram, page->PSM, page->TBW, GS_CLUT_NONE);
  }
  return 0;
}

// Initializes and uploads graphics resources to GS VRAM
int initGraphics() {
  if (graphicsInitialized)
    return 0;
  if (font->pageCount == 0) {
    return -1;
  }
  if (setUIFont(activeUIFont))
    goto fail;

  // Upload icons texture to GS
  icons = calloc(sizeof(GSTEXTURE), 1);
  if (icons == NULL ||
      gsKit_texture_png_mem(gsGlobal, icons, ICONS_PNG, SIZE_ICONS_PNG, 0, 0)) {
    goto fail;
  }
  prepareIconsAlpha();
  if(!bindTextureSafe(gsGlobal,icons))goto fail;

  gridSelector = calloc(sizeof(GSTEXTURE), 1);
  if (gridSelector != NULL &&
      gsKit_texture_png_mem(gsGlobal, gridSelector, (void *)GRID_SELECTOR_PNG,
                            SIZE_GRID_SELECTOR_PNG, 0, 0) == 0) {
    prepareGridSelector(gridSelector);
    gridSelector->Filter = GS_FILTER_LINEAR;
    if (!bindTextureSafe(gsGlobal, gridSelector)) {
      releaseGraphicsTexture(gridSelector);
      free(gridSelector);
      gridSelector = NULL;
    }
  } else {
    if (gridSelector != NULL) {
      releaseGraphicsTexture(gridSelector);
      free(gridSelector);
      gridSelector = NULL;
    }
  }

  graphicsInitialized = 1;
  return 0;
fail:
  // Keep the font reservation so this context can retry after a heap failure.
  releaseGraphicsResources();
  return -1;
}

// Frees memory used by font pages and icon textures
void closeFont() {
  releaseGraphicsResources();
  fontVram = GSKIT_ALLOC_ERROR;
  fontVramSize = 0;
}

// Returns icon width
int getIconWidth(IconType iconType) { return ICONS[iconType].width; }

// Draws the icon at specified coordinates
void drawIcon(float x, float y, int z, uint64_t color, IconType iconType) {
  drawIconScaled(x, y, z, color, iconType, 1.0f);
}

static void drawIconScaledOpacity(float x, float y, int z, uint64_t color,
                                  IconType iconType, float scale, float opacity) {
  Icon icon = ICONS[iconType];
  if(opacity<=0)return;

  // Preserve the artwork colors for every controller button.
  if (iconType != ICON_ENABLED)
    color = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);
  unsigned alpha=(unsigned)((color>>24)&255);
  color=(color&~(0xffULL<<24))|((uint64_t)(alpha*fminf(1,opacity))<<24);

  if (!bindTextureSafe(gsGlobal, icons))
    return;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0,1,0,1,0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, icons,          // font page
                            x,                        // x1 (destination)
                            y,                        // y1
                            icon.x,                   // u1 (source texture)
                            icon.y,                   // v1
                            x + icon.width * scale,   // x2 (destination)
                            y + icon.height * scale,  // y2
                            icon.x + icon.width + 1,  // u2 (source texture)
                            icon.y + icon.height + 1, // v2
                            z, color);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void drawIconScaled(float x, float y, int z, uint64_t color, IconType iconType, float scale) {
  drawIconScaledOpacity(x,y,z,color,iconType,scale,1);
}

void drawLunaLogo(float centerX,float top,float width,int z,uint64_t color) {
  if(icons==NULL||width<=0||((color>>24)&255)==0)return;
  int filter=icons->Filter;
  icons->Filter=GS_FILTER_LINEAR;
  if(!bindTextureSafe(gsGlobal,icons)) {icons->Filter=filter;return;}
  GSTEST savedTest=*gsGlobal->Test;
  uint64_t savedBlend=gsGlobal->PrimAlpha;
  int savedAlpha=gsGlobal->PrimAlphaEnable,savedPabe=gsGlobal->PABE;
  gsKit_set_test(gsGlobal,GS_ATEST_OFF);
  gsGlobal->PrimAlphaEnable=GS_SETTING_ON;
  gsKit_set_primalpha(gsGlobal,GS_SETREG_ALPHA(0,1,0,1,0),0);
  float left=roundf(centerX-width*.5f),y=roundf(top);
  // Texture modulation uses 128 as neutral white; callers use standard RGB.
  uint64_t tint=GS_SETREG_RGBA((color&255)/2,((color>>8)&255)/2,
      ((color>>16)&255)/2,(color>>24)&255);
  gsKit_prim_sprite_texture(gsGlobal,icons,left,y,LUNA_LOGO.x+.5f,LUNA_LOGO.y+.5f,
      left+width,y+width*109.0f/710.0f,LUNA_LOGO.x+LUNA_LOGO.width-.5f,
      LUNA_LOGO.y+LUNA_LOGO.height-.5f,z,tint);
  icons->Filter=filter;
  gsKit_set_primalpha(gsGlobal,savedBlend,savedPabe);
  gsGlobal->PrimAlphaEnable=savedAlpha;
  *gsGlobal->Test=savedTest;
  gsKit_set_test(gsGlobal,savedTest.ATE?GS_ATEST_ON:GS_ATEST_OFF);
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

void drawMenuRowSelector(int left, int top, int right) {
  const int plateTop = psbbnFieldStableY(top - 3);
  const int plateBottom = psbbnFieldStableY(top + getFontLineHeight() + 3);
  const int insetLeft = left + 6;
  const int insetRight = right - 6;

  // Cover both interlaced fields without thin horizontal rules that shimmer.
  gsKit_prim_sprite(gsGlobal, insetLeft, plateTop, insetRight, plateBottom, 2,
                    glassPresetColor(0x16, 0x54, 0x82, 0x38));
  gsKit_prim_sprite(gsGlobal, insetLeft - 4, plateTop, insetLeft + 6, plateBottom, 3,
                    GS_SETREG_RGBA(0x48, 0xB8, 0xF0, 0x10));
  gsKit_prim_sprite(gsGlobal, insetLeft - 2, plateTop, insetLeft + 4, plateBottom, 4,
                    GS_SETREG_RGBA(0x70, 0xD0, 0xFF, 0x24));
  gsKit_prim_sprite(gsGlobal, insetLeft, plateTop, insetLeft + 2, plateBottom, 5,
                    GS_SETREG_RGBA(0xB0, 0xE8, 0xFF, 0x78));
}

void drawListScrollbar(int x, int top, int bottom, int total, int first,
                       int visible, int z) {
  if (visible <= 0 || total <= visible || bottom <= top)
    return;
  const int height = bottom - top;
  int thumb = (int)((int64_t)height * visible / total);
  if (thumb < 16) thumb = 16;
  if (thumb > height) thumb = height;
  if (first < 0) first = 0;
  if (first > total - visible) first = total - visible;
  const int y = top + (int)((int64_t)(height - thumb) * first /
                             (total - visible));
  // Vertical edges and a substantial thumb stay legible on interlaced output.
  gsKit_prim_sprite(gsGlobal, x + 2, top, x + 4, bottom, z,
                    glassPresetColor(0x78, 0x9A, 0xB0, 0x24));
  gsKit_prim_sprite(gsGlobal, x, y, x + 6, y + thumb, z + 1,
                    glassPresetColor(0x78, 0xBD, 0xD7, 0x58));
  gsKit_prim_sprite(gsGlobal, x + 2, y, x + 4, y + thumb, z + 2,
                    glassPresetColor(0xB0, 0xE8, 0xFF, 0x70));
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
    if (prompt->label == NULL)
      continue;
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

static uint64_t promptFade(uint64_t color,float opacity) {
  unsigned alpha=(unsigned)((color>>24)&255);
  return (color&~(0xffULL<<24))|((uint64_t)(alpha*opacity)<<24);
}

static int promptMatch(PromptBar bar,IconType icon) {
  for(int i=0;i<bar.count;i++)
    if(bar.items[i].label && bar.items[i].icon==icon)return i;
  return -1;
}

static float promptLeft(int left,int right,PromptBar bar,int index) {
  int slot=(right-left)/bar.count,start=left+index*slot;
  int end=index==bar.count-1?right:start+slot;
  int width=getIconWidth(bar.items[index].icon)+6+(int)getLineWidth(bar.items[index].label);
  return fmaxf(start+2,start+((end-start)-width)/2);
}

static void drawPromptTransitionItem(int x,int top,int right,int bottom,int z,
                                     uint64_t color,const ButtonPrompt *from,
                                     const ButtonPrompt *to,float progress,float opacity) {
  if(opacity<=0)return;
  const ButtonPrompt *button=to?to:from;
  Icon icon=ICONS[button->icon];
  drawIconScaledOpacity(x,top+((bottom-top)-icon.height)/2,z,FontMainColor,
                        button->icon,1,opacity);
  int textX=x+icon.width+6;
  if(from && to && strcmp(from->label,to->label)) {
    drawTextWindow(textX,top,right,bottom,z,promptFade(color,opacity*(1-progress)),
                    ALIGN_VCENTER,from->label);
    drawTextWindow(textX,top,right,bottom,z,promptFade(color,opacity*progress),
                    ALIGN_VCENTER,to->label);
  } else drawTextWindow(textX,top,right,bottom,z,promptFade(color,opacity),
                          ALIGN_VCENTER,button->label);
}

void drawPromptBarTransition(int left,int top,int right,int bottom,int z,
                            uint64_t labelColor,PromptBar from,PromptBar to,
                            float progress,float opacity) {
  if(right<=left || bottom<=top || opacity<=0)return;
  progress=fminf(1,fmaxf(0,progress));opacity=fminf(1,opacity);
  for(int i=0;i<from.count;i++) {
    const ButtonPrompt *source=&from.items[i];
    if(!source->label)continue;
    int match=promptMatch(to,source->icon);
    float x=promptLeft(left,right,from,i);
    if(match>=0) {
      float destination=promptLeft(left,right,to,match);
      x+=(destination-x)*progress;
      drawPromptTransitionItem((int)lroundf(x),top,right,bottom,z,labelColor,
          source,&to.items[match],progress,opacity);
    } else drawPromptTransitionItem((int)lroundf(x),top,right,bottom,z,labelColor,
                                     source,NULL,0,opacity*(1-progress));
  }
  for(int i=0;i<to.count;i++) {
    const ButtonPrompt *destination=&to.items[i];
    if(!destination->label || promptMatch(from,destination->icon)>=0)continue;
    drawPromptTransitionItem((int)lroundf(promptLeft(left,right,to,i)),top,right,bottom,z,
        labelColor,NULL,destination,0,opacity*progress);
  }
}

static void menuPromptText(char *out,size_t capacity,const char *text) {
  if(!text)text="";
  size_t n=strlen(text);
  if(n>=capacity)n=capacity-1;
  while(n&&((unsigned char)text[n]&0xc0)==0x80)n--;
  memcpy(out,text,n);out[n]=0;
}
static void menuPromptCopy(MenuPromptSnapshot *out,PromptBar bar) {
  out->count=bar.items&&bar.count>0?(bar.count>MENU_PROMPT_MAX?MENU_PROMPT_MAX:bar.count):0;
  out->hasNote=bar.note!=NULL;menuPromptText(out->note,sizeof(out->note),bar.note);
  for(int i=0;i<out->count;i++) {
    menuPromptText(out->labels[i],sizeof(out->labels[i]),bar.items[i].label);
    out->items[i]=(ButtonPrompt){bar.items[i].icon,bar.items[i].label?out->labels[i]:NULL};
  }
}
static PromptBar menuPromptBar(MenuPromptSnapshot *snapshot) {
  return (PromptBar){snapshot->hasNote?snapshot->note:NULL,snapshot->items,snapshot->count};
}
static int menuPromptSame(const MenuPromptSnapshot *snapshot,PromptBar bar) {
  int count=bar.items&&bar.count>0?(bar.count>MENU_PROMPT_MAX?MENU_PROMPT_MAX:bar.count):0;
  if(snapshot->count!=count||snapshot->hasNote!=(bar.note!=NULL))return 0;
  for(int i=0;i<count;i++) {
    char label[96];menuPromptText(label,sizeof(label),bar.items[i].label);
    if(snapshot->items[i].icon!=bar.items[i].icon||
       (snapshot->items[i].label!=NULL)!=(bar.items[i].label!=NULL)||
       strcmp(snapshot->labels[i],label))return 0;
  }
  return 1;
}
void drawMenuPromptBar(MenuPromptTransition *state,int left,int top,int right,
                       int bottom,int z,uint64_t color,PromptBar bar,float opacity) {
  if(opacity<=0||right<=left||bottom<=top)return;
  uint32_t now=uiNowMs();
  if(!state->initialized||(uint32_t)(now-state->lastDraw)>300U) {
    menuPromptCopy(&state->from,(PromptBar){0});menuPromptCopy(&state->to,bar);
    state->started=now;state->initialized=1;
  } else if(!menuPromptSame(&state->to,bar)) {
    menuPromptCopy(&state->from,menuPromptBar(&state->to));menuPromptCopy(&state->to,bar);
    state->started=now;
  } else menuPromptText(state->to.note,sizeof(state->to.note),bar.note);
  state->lastDraw=now;
  unsigned elapsed=now-state->started;
  float progress=lunaNavEase(elapsed>=180U?1000:(int)(elapsed*1000U/180U))/1000.0f;
  PromptBar from=menuPromptBar(&state->from),to=menuPromptBar(&state->to);
  if(!from.note&&!to.note) {
    drawPromptBarTransition(left,top,right,bottom,z,color,from,to,progress,opacity);return;
  }
  int fromLeft=from.note&&from.count?left+(right-left)/2:left;
  int toLeft=to.note&&to.count?left+(right-left)/2:left;
  if(from.note&&to.note&&!strcmp(from.note,to.note))
    drawTextWindow(left,top,to.count?toLeft-6:right,bottom,z,promptFade(color,opacity),
                   to.count?ALIGN_VCENTER:ALIGN_CENTER,to.note);
  else {
    if(from.note)drawTextWindow(left,top,from.count?fromLeft-6:right,bottom,z,
        promptFade(color,opacity*(1-progress)),from.count?ALIGN_VCENTER:ALIGN_CENTER,from.note);
    if(to.note)drawTextWindow(left,top,to.count?toLeft-6:right,bottom,z,
        promptFade(color,opacity*progress),to.count?ALIGN_VCENTER:ALIGN_CENTER,to.note);
  }
  for(int i=0;i<from.count;i++) {
    const ButtonPrompt *source=&from.items[i];if(!source->label)continue;
    int match=promptMatch(to,source->icon);
    float x=promptLeft(fromLeft,right,from,i);
    if(match>=0) {
      x+=(promptLeft(toLeft,right,to,match)-x)*progress;
      drawPromptTransitionItem((int)lroundf(x),top,right,bottom,z,color,source,&to.items[match],progress,opacity);
    } else drawPromptTransitionItem((int)lroundf(x),top,right,bottom,z,color,source,NULL,0,opacity*(1-progress));
  }
  for(int i=0;i<to.count;i++)if(to.items[i].label&&promptMatch(from,to.items[i].icon)<0)
    drawPromptTransitionItem((int)lroundf(promptLeft(toLeft,right,to,i)),top,right,bottom,z,
        color,NULL,&to.items[i],0,opacity*progress);
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

static uint64_t glassTextColor(uint64_t color) {
  if (getGlassColorPreset() != GLASS_COLOR_CYBER)
    return color;
  const int red = color & 0xFF;
  const int green = (color >> 8) & 0xFF;
  const int blue = (color >> 16) & 0xFF;
  // Keep warning/error hues and text opacity; only neutral/cool ink is green.
  if (red > green && red > blue)
    return color;
  return glassPresetColor(red, green, blue, (color >> 24) & 0xFF);
}

// Draws glyph at specified coordinates
static void drawGlyph(const BMFontChar *glyph, float x, float y, int z, uint64_t color) {
  // Match the menu renderer: the extra atlas texel needs a destination pixel
  // too. Squeezing width+1 texels into width pixels changes stroke spacing.
  gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],   // font page
                            x + glyph->xoffset,                 // x1 (destination)
                            y + glyph->yoffset,                 // y1
                            glyph->x,                           // u1 (source texture)
                            glyph->y,                           // v1
                            x + glyph->xoffset + glyph->width + 1,  // x2 (destination)
                            y + glyph->yoffset + glyph->height + 1, // y2
                            glyph->x + glyph->width + 1,        // u2 (source texture, without +1 all characters are cut off on real hardware)
                            glyph->y + glyph->height + 1,       // v2
                            z, color);
}

static void beginFontDraw(void) {
  // Conventional source alpha: (source - destination) * alpha + destination.
  // Reject zero alpha after modulation, including a fully faded glyph.
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsGlobal->Test->ATST = 6; // GREATER
  gsGlobal->Test->AREF = 0;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
}

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawText(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text) {
  color = glassTextColor(color);
  int curX = x;
  const BMFontChar *glyph;
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  // Transparent font-atlas texels must be rejected rather than merely blended.
  // Otherwise their invisible quads still write depth and appear as boxes when
  // animated artwork passes behind text.
  beginFontDraw();

  int curHeight = 0;
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') {
      curX = x;
      curHeight += font->lineHeight;
      continue;
    }

    if (maxWidth && (curX > maxWidth)) {
      continue;
    }

    glyph = getGlyph(character);
    if (glyph == NULL) {
      continue;
    }

    if (maxHeight && ((curHeight + font->lineHeight) > maxHeight)) {
      break;
    }

    drawGlyph(glyph, curX, y + curHeight, z, color);
    curX += glyph->xadvance;

    // Account for kerning if kernings are present and next char is not a null terminator
    if (glyph->kernings && (*cursor != '\0')) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++) {
        if (glyph->kernings[k].secondChar == nextCharacter) {
          curX += glyph->kernings[k].amount;
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
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') {
      return lineWidth;
    }

    glyph = getGlyph(character);
    if (glyph == NULL) {
      continue;
    }

    lineWidth += glyph->xadvance;
    // Account for kerning
    if (glyph->kernings && (*cursor != '\0')) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++) {
        if (glyph->kernings[k].secondChar == nextCharacter) {
          lineWidth += glyph->kernings[k].amount;
        }
      }
    }
  }
  return lineWidth;
}

float getLineDrawWidth(const char *text) {
  float pen = 0, extent = 0;
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') break;
    const BMFontChar *glyph = getGlyph(character);
    if (!glyph) continue;
    float right = pen + glyph->xoffset + glyph->width + 1;
    if (right > extent) extent = right;
    pen += glyph->xadvance;
    if (glyph->kernings && *cursor) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++)
        if (glyph->kernings[k].secondChar == nextCharacter)
          pen += glyph->kernings[k].amount;
    }
  }
  return extent > pen ? extent : pen;
}

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text) {
  color = glassTextColor(color);
  if (!x2 && !y2) {
    // If window limits are not set, use faster drawing function
    return drawText(x1, y1, z, 0, 0, color, text);
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
  beginFontDraw();

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
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') {
      curX = x1;
      curY += font->lineHeight;
      // Get the width of the next line
      lineWidth = getLineWidth(cursor);
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

    glyph = getGlyph(character);
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
    if (glyph->kernings && (*cursor != '\0')) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++) {
        if (glyph->kernings[k].secondChar == nextCharacter) {
          curX += glyph->kernings[k].amount;
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

void drawTextLineScaled(int x, int y, int right, int z, float scale,
                        uint64_t color, const char *text) {
  color = glassTextColor(color);
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;
  float curX = x;
  beginFontDraw();
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') break;
    const BMFontChar *glyph = getGlyph(character);
    if (!glyph) continue;
    float left = curX + glyph->xoffset * scale;
    float top = y + glyph->yoffset * scale;
    if (left + (glyph->width + 1) * scale > right) break;
    gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],
        left, top, glyph->x, glyph->y,
        left + (glyph->width + 1) * scale, top + (glyph->height + 1) * scale,
        glyph->x + glyph->width + 1, glyph->y + glyph->height + 1, z, color);
    curX += glyph->xadvance * scale;
    if (glyph->kernings && *cursor) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++)
        if (glyph->kernings[k].secondChar == nextCharacter)
          curX += glyph->kernings[k].amount * scale;
    }
  }
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void drawTextLineScaledCrisp(int x, int y, int right, int z, float scale,
                        uint64_t color, const char *text) {
  color = glassTextColor(color);
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;
  float curX = x;
  beginFontDraw();
  const char *cursor = text;
  while (*cursor) {
    uint32_t character = lunaUTF8Next(&cursor);
    if (character == '\n') break;
    const BMFontChar *glyph = getGlyph(character);
    if (!glyph) continue;
    float left = curX + glyph->xoffset * scale;
    float top = y + glyph->yoffset * scale;
    if (left + glyph->width * scale > right) break;
    float endX=left+glyph->width*scale,endY=top+glyph->height*scale;
    GSTEXTURE *page=fontPages[glyph->page];
    int savedFilter=page->Filter;
    {
      /* Source UVs include the hardware-required extra atlas texel. Give
       * that transparent padding its matching destination extent too,
       * instead of compressing width+1 texels into width pixels. */
      endX=roundf(endX+scale);endY=roundf(endY+scale);
      left=roundf(left);top=roundf(top);
      page->Filter=GS_FILTER_NEAREST;
    }
    gsKit_prim_sprite_texture(gsGlobal, page,
        left, top, glyph->x, glyph->y,
        endX, endY,
        glyph->x + glyph->width + 1, glyph->y + glyph->height + 1, z, color);
    page->Filter=savedFilter;
    curX += glyph->xadvance * scale;
    if (glyph->kernings && *cursor) {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++)
        if (glyph->kernings[k].secondChar == nextCharacter)
          curX += glyph->kernings[k].amount * scale;
    }
  }
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

int drawTextMarquee(int x1, int y, int x2, int z, uint64_t color, const char *text, int scrollX) {
  color = glassTextColor(color);
  float curX = x1 - scrollX;
  const int previousAlphaTest = gsGlobal->Test->ATST;
  const int previousAlphaReference = gsGlobal->Test->AREF;
  const int previousAlphaFail = gsGlobal->Test->AFAIL;

  beginFontDraw();

  const char *cursor = text;
  while (*cursor && *cursor != '\n') {
    uint32_t character = lunaUTF8Next(&cursor);
    const BMFontChar *glyph = getGlyph(character);
    if (glyph == NULL)
      continue;

    float glyphLeft = curX + glyph->xoffset;
    float glyphRight = glyphLeft + glyph->width + 1;
    if (glyph->width > 0 && glyphRight > x1 && glyphLeft < x2) {
      float clippedLeft = (glyphLeft < x1) ? x1 : glyphLeft;
      float clippedRight = (glyphRight > x2) ? x2 : glyphRight;
      float u1 = glyph->x + clippedLeft - glyphLeft;
      float u2 = glyph->x + clippedRight - glyphLeft;
      gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],
                                clippedLeft, y + glyph->yoffset, u1, glyph->y,
                                clippedRight, y + glyph->yoffset + glyph->height + 1,
                                u2, glyph->y + glyph->height + 1, z, color);
    }

    curX += glyph->xadvance;
    if (glyph->kernings && *cursor != '\0') {
      const char *next = cursor;
      uint32_t nextCharacter = lunaUTF8Next(&next);
      for (int k = 0; k < glyph->kerningsCount; k++) {
        if (glyph->kernings[k].secondChar == nextCharacter)
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
  if (texture == NULL || buf == NULL || size == 0)
    return -1;
  FILE *file = fmemopen(buf, size, "rb");
  if (file == NULL) {
    return -1;
  }

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  // These owners must survive libpng's longjmp on malformed/truncated input.
  png_bytep volatile pixelBuffer = NULL;
  png_bytep *volatile row_pointers = NULL;

  uint32_t sig_read = 0;
  int row, i, k = 0, j, bit_depth, color_type, interlace_type;

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

  if (!png_ptr) {
    fclose(file);
    return -1;
  }

  info_ptr = png_create_info_struct(png_ptr);

  if (!info_ptr) {
    fclose(file);
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    return -1;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    goto failed;
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

  png_set_interlace_handling(png_ptr);
  png_read_update_info(png_ptr, info_ptr);

  if (png_get_channels(png_ptr, info_ptr) != 4) {
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(file);
    return -1;
  }

  // Limit dimensions to the GS texture range before its integer size math.
  if (width == 0 || height == 0 || width > 1024 || height > 1024 ||
      png_get_rowbytes(png_ptr, info_ptr) != (size_t)width * 4)
    png_error(png_ptr, "Unsupported PNG dimensions");
  pixelBuffer = memalign(128, gsKit_texture_size(width, height, GS_PSM_CT32));
  if (pixelBuffer == NULL)
    png_error(png_ptr, "Out of memory for PNG pixels");
  row_pointers = calloc(height, sizeof(png_bytep));
  if (row_pointers == NULL)
    png_error(png_ptr, "Out of memory for PNG rows");
  // Decode straight into the DMA buffer. Per-row copies doubled peak pixel
  // memory and fragmented the EE heap during sustained Collection scrolling.
  for (row = 0; row < height; row++)
    row_pointers[row] = pixelBuffer + (size_t)row * width * 4;

  png_read_image(png_ptr, row_pointers);
  png_read_end(png_ptr, NULL);

  struct pixel {
    uint8_t r, g, b, a;
  };
  struct pixel *pixels = (struct pixel *)pixelBuffer;

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

  free(row_pointers);
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
  fclose(file);

  // Publish only complete artwork so a failed job never exposes freed pixels.
  texture->Width = width;
  texture->Height = height;
  texture->VramClut = 0;
  texture->Clut = NULL;
  texture->PSM = GS_PSM_CT32;
  texture->Filter = GS_FILTER_NEAREST;
  texture->Mem = (u32 *)pixelBuffer;

  if (upload && !bindTextureSafe(gsGlobal, texture)) {
    free(texture->Mem);
    texture->Mem = NULL;
    return -1;
  }

  return 0;

failed:
  free(row_pointers);
  free(pixelBuffer);
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
  fclose(file);
  return -1;
}

static int loadPNGTextureRGBAInternal(GSGLOBAL *gsGlobal, GSTEXTURE *texture,
                                      const char *path, int upload,
                                      const volatile int *cancelled) {
  if (cancelled != NULL && *cancelled)
    return -1;
  FILE *file = fopen(path, "rb");
  void *buffer;
  long fileSize;
  int result;

  if (file == NULL) {
    return -1;
  }
  if (fseek(file, 0, SEEK_END) != 0 || (fileSize = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return -1;
  }

  buffer = malloc(fileSize);
  if (buffer == NULL || fread(buffer, 1, fileSize, file) != (size_t)fileSize) {
    free(buffer);
    fclose(file);
    return -1;
  }
  fclose(file);

  if (cancelled != NULL && *cancelled) {
    free(buffer);
    return -1;
  }
  result = gsKit_texture_png_mem(gsGlobal, texture, buffer, fileSize, 0, 0);
  if (result == 0 &&
      ((cancelled != NULL && *cancelled) ||
       fitTextureToVram(gsGlobal, texture) < 0 ||
       (upload && !bindTextureSafe(gsGlobal, texture)))) {
    free(texture->Mem);
    texture->Mem = NULL;
    result = -1;
  }
  free(buffer);
  return result;
}

int loadPNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path) {
  return loadPNGTextureRGBAInternal(gsGlobal, texture, path, 1, NULL);
}

int decodePNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture, const char *path) {
  return loadPNGTextureRGBAInternal(gsGlobal, texture, path, 0, NULL);
}

int decodeCollectionPNGTextureRGBA(GSGLOBAL *gsGlobal, GSTEXTURE *texture,
                                   const char *path, const volatile int *cancelled) {
  return loadPNGTextureRGBAInternal(gsGlobal, texture, path, 0, cancelled);
}
