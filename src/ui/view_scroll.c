// LUNA visual rendering extracted from gui.c.
#include "ui/view_internal.h"
#include "ui/view_scroll.h"
#include "ui/ambient_orbs.h"
#include "ui/ps2_menu_scene.h"
#include "options.h"
#include "dprintf.h"
#include <gsInline.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static GlassColorPreset glassColorPreset = GLASS_COLOR_ORIGINAL;
static LibraryBackground libraryBackground = LIBRARY_BACKGROUND_STARS;
static u32 fogPixels[128 * 128] __attribute__((aligned(128)));
static u32 bumpPixels[64 * 64] __attribute__((aligned(128)));
static GSTEXTURE fogTexture;
static GSTEXTURE bumpTexture;
static u32 openingRefPixels[128 * 128] __attribute__((aligned(128)));
static u32 openingBlpPixels[64 * 64] __attribute__((aligned(128)));
static u32 openingBlprPixels[64 * 64] __attribute__((aligned(128)));
static GSTEXTURE openingRefTexture;
static GSTEXTURE openingBlpTexture;
static GSTEXTURE openingBlprTexture;
static GSTEXTURE openingCaptureTexture;
static GSTEXTURE openingWorkTexture;
static u32 configWallPixels[128 * 128] __attribute__((aligned(128)));
static u32 configFlowPixels[64 * 64] __attribute__((aligned(128)));
static u32 configRefPixels[64 * 64] __attribute__((aligned(128)));
static GSTEXTURE configWallTexture;
static GSTEXTURE configFlowTexture;
static GSTEXTURE configRefTexture;
static u32 configInversePixels[64 * 64] __attribute__((aligned(128)));
static GSTEXTURE configInverseTexture;
static GSTEXTURE configWorkTexture[2];
static u32 configWorkZ;
static int configCaptureReady;
static float configRodVerts[16][4][4];
static float configRodNormals[16][4];
static float configRodUvs[16][4][4];
static int biosFogTextureLoaded;
static int biosCubeTextureLoaded;
static int biosOpeningCubeTexturesLoaded;
static int biosConfigTexturesLoaded;
static int biosConfigGeometryLoaded;
static int configClockInitialized;
static uint32_t configClockAnchorMs;
static uint32_t configClockStartMs;
static float configFrontSplit;
static int configSplitHour = -1;
static uint32_t configSplitFrame;
static int openingCaptureReady;

#define OPENING_CAPTURE_SIZE 128
#define CONFIG_WORK_SIZE 256

void initOpeningCubeCapture(void) {
  // A library refresh creates a new GS context; ROM textures must be rebound.
  biosFogTextureLoaded = biosCubeTextureLoaded = 0;
  biosOpeningCubeTexturesLoaded = biosConfigTexturesLoaded = 0;
  configCaptureReady = configClockInitialized = 0;
  configFrontSplit = 0.0f;
  configSplitHour = -1;
  configSplitFrame = 0;
  memset(&openingCaptureTexture, 0, sizeof(openingCaptureTexture));
  memset(&openingWorkTexture, 0, sizeof(openingWorkTexture));
  memset(configWorkTexture, 0, sizeof(configWorkTexture));
  openingCaptureReady = 0;
  // The scenes are mutually exclusive. Share two work buffers with the
  // opening cube, reserving them BEFORE the texture manager starts. Full
  // screen wb3/wb4 do not fit beside LUNA's two framebuffers, Z and artwork
  // in 4 MB, so resample the whole backdrop into two small CT32 pages.
  // PAL needs 256x128 to retain room for the 256x256 font and artwork;
  // NTSC/480p can use 256x256. Both axes stay powers of two for REPEAT.
  // CT32 preserves bloom alpha; a separate small Z buffer keeps scaled
  // work passes from overwriting the screen's differently pitched depth.
  const int workHeight = gsGlobal->Mode == GS_MODE_PAL ? 128 : CONFIG_WORK_SIZE;
  const u32 workSize = gsKit_texture_size(CONFIG_WORK_SIZE, workHeight, GS_PSM_CT32);
  const u32 zSize = gsKit_texture_size(CONFIG_WORK_SIZE, workHeight, GS_PSM_CT16S);
  u32 captureVram = gsKit_vram_alloc(gsGlobal, workSize * 2 + zSize,
                                    GSKIT_ALLOC_SYSBUFFER);
  if (captureVram == GSKIT_ALLOC_ERROR) {
    DPRINTF("LUNA: no VRAM for opening cube or System Configuration capture\n");
    return;
  }
  for (int i = 0; i < 2; i++) {
    configWorkTexture[i].Width = CONFIG_WORK_SIZE;
    configWorkTexture[i].Height = workHeight;
    configWorkTexture[i].PSM = GS_PSM_CT32;
    configWorkTexture[i].TBW = CONFIG_WORK_SIZE / 64;
    configWorkTexture[i].Vram = captureVram + i * workSize;
    configWorkTexture[i].Filter = GS_FILTER_LINEAR;
  }
  configWorkZ = captureVram + workSize * 2;
  configCaptureReady = 1;
  openingCaptureTexture.Width = OPENING_CAPTURE_SIZE;
  openingCaptureTexture.Height = OPENING_CAPTURE_SIZE;
  openingCaptureTexture.PSM = GS_PSM_CT24;
  openingCaptureTexture.TBW = OPENING_CAPTURE_SIZE / 64;
  openingCaptureTexture.Vram = captureVram;
  openingCaptureTexture.Filter = GS_FILTER_LINEAR;
  openingWorkTexture = openingCaptureTexture;
  openingWorkTexture.Vram = configWorkTexture[1].Vram;
  openingCaptureReady = 1;
}

static uint32_t tunnelReadLE32(const unsigned char *bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
         (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

// TEXIMAGE is a ROMDIR; file offsets are 16-byte aligned from its start.
static int tunnelFindResource(FILE *rom, const char *name,
                              long *offset, uint32_t *size) {
  unsigned char entry[16];
  long fileOffset = 0;
  if (fseek(rom, 0, SEEK_SET))
    return -1;
  for (int index = 0; index < 256; index++) {
    if (fread(entry, 1, sizeof(entry), rom) != sizeof(entry) || !entry[0])
      break;
    const uint32_t fileSize = tunnelReadLE32(entry + 12);
    if (!memcmp(entry, name, strlen(name)) && entry[strlen(name)] == 0) {
      *offset = fileOffset;
      *size = fileSize;
      return 0;
    }
    fileOffset += (fileSize + 15U) & ~15U;
  }
  return -1;
}

// The OSDSYS expand format uses 30 literal/back-reference flags per word.
static int tunnelExpandResource(FILE *rom, long offset, uint32_t size,
                                unsigned char *output, uint32_t outputSize) {
  if (size < 8 || size > 1024 * 1024 || fseek(rom, offset, SEEK_SET))
    return -1;
  unsigned char *source = malloc(size);
  if (source == NULL)
    return -1;
  int result = -1;
  if (fread(source, 1, size, rom) != size ||
      tunnelReadLE32(source) != outputSize)
    goto done;
  uint32_t position = 4;
  uint32_t produced = 0;
  uint32_t descriptor = 0;
  int flagsLeft = 0;
  int shift = 0, backMask = 0;
  while (produced < outputSize) {
    if (!flagsLeft) {
      if (position + 4 > size)
        goto done;
      descriptor = ((uint32_t)source[position] << 24) |
                   ((uint32_t)source[position + 1] << 16) |
                   ((uint32_t)source[position + 2] << 8) |
                   source[position + 3];
      position += 4;
      const int mode = descriptor & 3;
      shift = 14 - mode;
      backMask = 0x3fff >> mode;
      flagsLeft = 30;
    }
    if (position >= size)
      goto done;
    const int value = source[position++];
    if (descriptor & 0x80000000U) {
      if (position >= size)
        goto done;
      const int code = (value << 8) | source[position++];
      const int back = 1 + (code & backMask);
      int count = 3 + (code >> shift);
      if (back > produced || count > (int)(outputSize - produced))
        goto done;
      while (count--) {
        output[produced] = output[produced - back];
        produced++;
      }
    } else {
      output[produced++] = value;
    }
    descriptor <<= 1;
    flagsLeft--;
  }
  result = 0;
done:
  free(source);
  return result;
}

static void tunnelInitTexture(GSTEXTURE *texture, u32 *pixels,
                              int width, int height) {
  memset(texture, 0, sizeof(*texture));
  texture->Width = width;
  texture->Height = height;
  texture->PSM = GS_PSM_CT32;
  texture->Mem = pixels;
  texture->Filter = GS_FILTER_LINEAR;
  texture->Delayed = GS_SETTING_ON;
}

static int loadBiosFogTexture(void) {
  if (biosFogTextureLoaded)
    return 0;
  FILE *rom = fopen("rom0:TEXIMAGE", "rb");
  if (rom == NULL)
    return -1;
  long fogOffset;
  uint32_t fogSize;
  // TEXOFOG0 expands to a 20-byte container header, 16-bit pixels,
  // and four spare bytes. The opening renderer reads pixels at +20.
  static unsigned char expanded[20 + 128 * 128 * 2 + 4];
  int result = tunnelFindResource(rom, "TEXOFOG0", &fogOffset, &fogSize) ||
               tunnelExpandResource(rom, fogOffset, fogSize, expanded, sizeof(expanded));
  if (!result) {
    for (int i = 0; i < 128 * 128; i++) {
      const uint16_t pixel = (uint16_t)expanded[20 + i * 2] |
                             (uint16_t)expanded[20 + i * 2 + 1] << 8;
      const u32 r5 = pixel & 31U;
      const u32 g5 = (pixel >> 5) & 31U;
      const u32 b5 = (pixel >> 10) & 31U;
      fogPixels[i] = ((r5 << 3) | (r5 >> 2)) |
                     (((g5 << 3) | (g5 >> 2)) << 8) |
                     (((b5 << 3) | (b5 >> 2)) << 16) | 0x7f000000U;
    }
  }
  fclose(rom);
  if (result) {
    DPRINTF("LUNA: failed to load BIOS TEXOFOG0\n");
    return -1;
  }
  tunnelInitTexture(&fogTexture, fogPixels, 128, 128);
  biosFogTextureLoaded = 1;
  DPRINTF("LUNA: loaded BIOS TEXOFOG0\n");
  return 0;
}

static int loadBiosCubeTexture(void) {
  if (biosCubeTextureLoaded)
    return 0;
  FILE *rom = fopen("rom0:TEXIMAGE", "rb");
  if (rom == NULL)
    return -1;
  long bumpOffset;
  uint32_t bumpSize;
  static unsigned char expanded[64 * 64];
  int result = tunnelFindResource(rom, "TEXCBUMP", &bumpOffset, &bumpSize) ||
               tunnelExpandResource(rom, bumpOffset, bumpSize, expanded, sizeof(expanded));
  fclose(rom);
  if (result) {
    DPRINTF("LUNA: failed to load BIOS TEXCBUMP\n");
    return -1;
  }
  for (int i = 0; i < 64 * 64; i++) {
    const u32 gray = expanded[i];
    bumpPixels[i] = gray | gray << 8 | gray << 16 | 0x7f000000U;
  }
  tunnelInitTexture(&bumpTexture, bumpPixels, 64, 64);
  biosCubeTextureLoaded = 1;
  DPRINTF("LUNA: loaded BIOS TEXCBUMP\n");
  return 0;
}

// The System Configuration tunnel and rod passes sample the console's own
// TEXIMAGE resources. No BIOS pixels are copied into the LUNA executable.
static int loadBiosConfigTextures(void) {
  if (biosConfigTexturesLoaded)
    return 0;
  if (loadBiosCubeTexture())
    return -1;
  FILE *rom = fopen("rom0:TEXIMAGE", "rb");
  if (rom == NULL)
    return -1;
  static unsigned char expanded[64 * 64 * 3];
  static const char *const names[3] = {"TEXCKABE", "TEXCFLOW", "TEXCREFA"};
  u32 *const pixels[3] = {configWallPixels, configFlowPixels, configRefPixels};
  int result = 0;
  for (int resource = 0; resource < 3; resource++) {
    long offset;
    uint32_t size;
    const uint32_t expandedSize = resource == 0 ? sizeof(expanded) : 64 * 64;
    if (tunnelFindResource(rom, names[resource], &offset, &size) ||
        tunnelExpandResource(rom, offset, size, expanded, expandedSize)) {
      result = -1;
      break;
    }
    if (resource == 0) {
      // The original TEXCKABE decoder repeats each 64x64 RGB pixel into a
      // 2x2 tile of a 128x128 page.
      for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
          const int source = ((y & 63) * 64 + (x & 63)) * 3;
          pixels[resource][y * 128 + x] =
              (u32)expanded[source] | (u32)expanded[source + 1] << 8 |
              (u32)expanded[source + 2] << 16 | 0x7f000000U;
        }
      }
    } else {
      for (int i = 0; i < 64 * 64; i++) {
        const u32 gray = expanded[i];
        pixels[resource][i] = gray | gray << 8 | gray << 16 | 0x7f000000U;
      }
    }
  }
  fclose(rom);
  if (result) {
    DPRINTF("LUNA: failed to load BIOS System Configuration textures\n");
    return -1;
  }
  tunnelInitTexture(&configWallTexture, configWallPixels, 128, 128);
  tunnelInitTexture(&configFlowTexture, configFlowPixels, 64, 64);
  tunnelInitTexture(&configRefTexture, configRefPixels, 64, 64);
  for (int i = 0; i < 64 * 64; i++) {
    const u32 gray = 255U - (bumpPixels[i] & 255U);
    configInversePixels[i] = gray | gray << 8 | gray << 16 | 0x7f000000U;
  }
  tunnelInitTexture(&configInverseTexture, configInversePixels, 64, 64);
  biosConfigTexturesLoaded = 1;
  DPRINTF("LUNA: loaded BIOS TEXCKABE, TEXCFLOW, TEXCBUMP, TEXCREFA\n");
  return 0;
}

// OSDSYS carries the original 16-face rod model in its compressed .data.
// Locate its scene descriptor by face count and the three adjacent arrays,
// since their absolute addresses vary between BIOS versions.
static int findBiosRodGeometry(const unsigned char *image, uint32_t imageSize) {
  const uint32_t base = 0x200000U;
  for (uint32_t i = 0; i + 20 < imageSize; i += 4) {
    if (tunnelReadLE32(image + i) != 16)
      continue;
    const uint32_t verts = tunnelReadLE32(image + i + 4);
    const uint32_t normals = tunnelReadLE32(image + i + 8);
    const uint32_t uvs = tunnelReadLE32(image + i + 12);
    if (verts < base || uvs != verts + sizeof(configRodVerts) ||
        normals != uvs + sizeof(configRodUvs) ||
        normals - base > imageSize - sizeof(configRodNormals))
      continue;
    memcpy(configRodVerts, image + verts - base, sizeof(configRodVerts));
    memcpy(configRodNormals, image + normals - base, sizeof(configRodNormals));
    memcpy(configRodUvs, image + uvs - base, sizeof(configRodUvs));
    if (configRodVerts[0][0][1] > 25.0f &&
        configRodVerts[0][0][1] < 27.0f)
      return 0;
  }
  return -1;
}

static int loadBiosConfigGeometry(void) {
  if (biosConfigGeometryLoaded)
    return 0;
  FILE *rom = fopen("rom0:OSDSYS", "rb");
  if (rom == NULL || fseek(rom, 0, SEEK_END)) {
    if (rom != NULL)
      fclose(rom);
    DPRINTF("LUNA: failed to open BIOS OSDSYS geometry\n");
    return -1;
  }
  const long fileSize = ftell(rom);
  int result = -1;
  if (fileSize > 0 && fileSize < 1024 * 1024) {
    unsigned char header[4];
    for (long offset = 0; offset < 0x2000 && offset + 4 < fileSize; offset += 16) {
      if (fseek(rom, offset, SEEK_SET) || fread(header, 1, 4, rom) != 4)
        break;
      const uint32_t expandedSize = tunnelReadLE32(header);
      if (expandedSize < 0x80000 || expandedSize > 0x180000)
        continue;
      unsigned char *expanded = malloc(expandedSize);
      if (expanded == NULL)
        break;
      if (!tunnelExpandResource(rom, offset, fileSize - offset,
                                expanded, expandedSize) &&
          !findBiosRodGeometry(expanded, expandedSize)) {
        result = 0;
        free(expanded);
        break;
      }
      free(expanded);
    }
  }
  fclose(rom);
  if (result) {
    DPRINTF("LUNA: failed to extract BIOS OSDSYS rod mesh\n");
    return -1;
  }
  biosConfigGeometryLoaded = 1;
  DPRINTF("LUNA: loaded 16-face BIOS OSDSYS rod mesh\n");
  return 0;
}

// The opening cube is built from these three ROM textures in osdbits/opening.c.
// Keep them in the BIOS at runtime; no Sony texture data is stored in LUNA.
static int loadBiosOpeningCubeTextures(void) {
  if (biosOpeningCubeTexturesLoaded)
    return 0;
  FILE *rom = fopen("rom0:TEXIMAGE", "rb");
  if (rom == NULL)
    return -1;
  static unsigned char expanded[20 + 128 * 128 * 2 + 4];
  static const char *const names[3] = {"TEXOREF", "TEXOBLP", "TEXOBLPR"};
  u32 *const pixels[3] = {openingRefPixels, openingBlpPixels, openingBlprPixels};
  int result = 0;
  for (int resource = 0; resource < 3; resource++) {
    long offset;
    uint32_t size;
    const uint32_t expandedSize = resource == 0 ? sizeof(expanded) : 64 * 64;
    if (tunnelFindResource(rom, names[resource], &offset, &size) ||
        tunnelExpandResource(rom, offset, size, expanded, expandedSize)) {
      result = -1;
      break;
    }
    if (resource == 0) {
      for (int i = 0; i < 128 * 128; i++) {
        const uint16_t pixel = (uint16_t)expanded[20 + i * 2] |
                               (uint16_t)expanded[20 + i * 2 + 1] << 8;
        const u32 r = pixel & 31U;
        const u32 g = (pixel >> 5) & 31U;
        const u32 b = (pixel >> 10) & 31U;
        pixels[resource][i] = ((r << 3) | (r >> 2)) |
                              (((g << 3) | (g >> 2)) << 8) |
                              (((b << 3) | (b >> 2)) << 16) | 0x7f000000U;
      }
    } else {
      for (int i = 0; i < 64 * 64; i++)
        pixels[resource][i] = (u32)expanded[i] << 24;
    }
  }
  fclose(rom);
  if (result) {
    DPRINTF("LUNA: failed to load BIOS opening cube textures\n");
    return -1;
  }
  tunnelInitTexture(&openingRefTexture, openingRefPixels, 128, 128);
  tunnelInitTexture(&openingBlpTexture, openingBlpPixels, 64, 64);
  tunnelInitTexture(&openingBlprTexture, openingBlprPixels, 64, 64);
  biosOpeningCubeTexturesLoaded = 1;
  DPRINTF("LUNA: loaded BIOS TEXOREF, TEXOBLP, TEXOBLPR\n");
  return 0;
}

int setLibraryBackground(LibraryBackground background) {
  if (background == LIBRARY_BACKGROUND_RED_CLOUDS &&
      (loadBiosFogTexture() || loadBiosOpeningCubeTextures()))
    return -1;
  if (background == LIBRARY_BACKGROUND_MIDNIGHT_CUBES && loadBiosCubeTexture())
    return -1;
  if (background == LIBRARY_BACKGROUND_SYSTEM_CONFIG &&
      (!configCaptureReady || loadBiosConfigTextures() || loadBiosConfigGeometry() ||
       loadAmbientOrbsSystemConfigAssets()))
    return -1;
  libraryBackground = background >= LIBRARY_BACKGROUND_STARS &&
                      background < LIBRARY_BACKGROUND_COUNT ?
                      background : LIBRARY_BACKGROUND_STARS;
  setAmbientOrbsBackgroundStyle(libraryBackground == LIBRARY_BACKGROUND_ORBS);
  return 0;
}

LibraryBackground getLibraryBackground(void) {
  return libraryBackground;
}

void setGlassColorPreset(GlassColorPreset preset) {
  glassColorPreset = (preset >= GLASS_COLOR_ORIGINAL && preset < GLASS_COLOR_COUNT)
                        ? preset : GLASS_COLOR_ORIGINAL;
}

GlassColorPreset getGlassColorPreset(void) {
  return glassColorPreset;
}

uint64_t glassPresetColor(int red, int green, int blue, int alpha) {
  static const int palette[GLASS_COLOR_COUNT][3] = {
      {0, 0, 0}, {0xE0, 0xE0, 0xE0}, {0x78, 0x78, 0x78}};
  if (glassColorPreset == GLASS_COLOR_ORIGINAL)
    return GS_SETREG_RGBA(red, green, blue, alpha);
  int brightness = red;
  if (green > brightness)
    brightness = green;
  if (blue > brightness)
    brightness = blue;
  return GS_SETREG_RGBA(palette[glassColorPreset][0] * brightness / 255,
                        palette[glassColorPreset][1] * brightness / 255,
                        palette[glassColorPreset][2] * brightness / 255, alpha);
}

uint64_t glassCoverAccentColor(int alpha) {
  return glassPresetColor(0x18, 0x78, 0xC8, alpha);
}

uint64_t glassMissingCoverColor(int alpha) {
  return GS_SETREG_RGBA(0x04, 0x14, 0x34, alpha);
}

uint64_t glassMissingCoverTextColor(void) {
  if (glassColorPreset == GLASS_COLOR_ORIGINAL)
    return HeaderTextColor;
  if (glassColorPreset == GLASS_COLOR_BLACK)
    return GS_SETREG_RGBA(0xC8, 0xC8, 0xC8, 0x80);
  return glassPresetColor(0xE0, 0xF0, 0xFF, 0x80);
}

uint64_t glassMissingCoverDiamondColor(int alpha) {
  if (glassColorPreset == GLASS_COLOR_BLACK)
    return GS_SETREG_RGBA(0xC8, 0xC8, 0xC8, alpha);
  return glassPresetColor(0x70, 0xD8, 0xFF, alpha);
}

static uint32_t glassStartMs = 0;

typedef struct {
  float x;
  float y;
  int depth;
} GlassPoint;

typedef struct {
  float yawSine, yawCosine;
  float pitchSine, pitchCosine;
  float rollSine, rollCosine;
} GlassRotation;

static const int glassSin[32] = {0,   25,  49,  71,  90,  106, 117, 125, 127, 125, 117, 106, 90,  71,  49,  25,
                                 0,  -25, -49, -71, -90, -106, -117, -125, -127, -125, -117, -106, -90, -71, -49, -25};

#define GLASS_STAR_TILE_SIZE 16
#define GLASS_STAR_ATLAS_WIDTH 64
#define GLASS_STAR_ATLAS_HEIGHT 32

typedef struct {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
  uint8_t alpha;
} GlassStarPixel;

static GSTEXTURE glassStarAtlas;
static GlassStarPixel glassStarAtlasPixels[GLASS_STAR_ATLAS_WIDTH * GLASS_STAR_ATLAS_HEIGHT] __attribute__((aligned(128)));

static uint64_t glassColor(int red, int green, int blue, int alpha, int brightness);
static float orbWave(uint32_t phase);
void drawOrbitalDisc(int centerX, int centerY, int radius, int z, uint64_t centerColor, uint64_t edgeColor);

void resetGlassVisuals(uint32_t startMs) {
  glassStartMs = startMs;
  resetAmbientOrbs(startMs);
}

static uint32_t glassElapsedMs(uint32_t frameNowMs) {
  return frameNowMs - glassStartMs;
}

uint64_t glassLightColor(int red, int green, int blue, int alpha) {
  if (glassColorPreset == GLASS_COLOR_BLACK) {
    int brightness = red;
    if (green > brightness)
      brightness = green;
    if (blue > brightness)
      brightness = blue;
    brightness = 0xC8 * brightness / 255;
    return GS_SETREG_RGBA(brightness, brightness, brightness, alpha);
  }
  return glassPresetColor(red, green, blue, alpha);
}

static uint32_t glassPhase(uint32_t elapsedMs, uint32_t periodMs, uint32_t offsetMs) {
  // All animation periods are below 36 seconds. Reduce first so the scaled
  // numerator fits in 32 bits and phase calculation avoids 64-bit division.
  uint32_t phaseMs = (elapsedMs + offsetMs) % periodMs;
  return (phaseMs << 16) / periodMs;
}

static int clampColor(int value) {
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return value;
}

static int glassIntegerSqrt(int value) {
  int root = 0;
  while ((root + 1) * (root + 1) <= value)
    root++;
  return root;
}

static void compositeGlassStarDisc(GlassStarPixel *pixel, int distance, int radius, int red, int green, int blue,
                                   int centerAlpha) {
  if (distance >= radius)
    return;

  const int sourceAlpha = centerAlpha * (radius - distance) / radius;
  const int oldAlpha = pixel->alpha;
  const int combinedAlpha = sourceAlpha + (oldAlpha * (0x80 - sourceAlpha) + 0x40) / 0x80;
  if (combinedAlpha <= 0)
    return;

  const int denominator = combinedAlpha * 0x80;
  pixel->red = (red * sourceAlpha * 0x80 + pixel->red * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) /
               denominator;
  pixel->green =
      (green * sourceAlpha * 0x80 + pixel->green * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) / denominator;
  pixel->blue =
      (blue * sourceAlpha * 0x80 + pixel->blue * oldAlpha * (0x80 - sourceAlpha) + denominator / 2) / denominator;
  pixel->alpha = combinedAlpha;
}

void initGlassStarAtlas(void) {
  static const int starRed[3] = {0x82, 0xA4, 0xC6};
  static const int starGreen[3] = {0xAA, 0xC4, 0xDE};
  static const int starBlue[3] = {0xD0, 0xE2, 0xF4};
  static const int discBrightness[4] = {-28, -12, 12, 32};
  static const int discAlpha[4] = {0x80 / 7, 0x80 / 3, 0x80, 0x80 / 2};
  // Normalized radii preserve the two existing star profiles: 4/2/1/1 and 5/3/2/1.
  static const int discRadius[2][4] = {{128, 64, 32, 32}, {128, 77, 51, 26}};

  for (int i = 0; i < GLASS_STAR_ATLAS_WIDTH * GLASS_STAR_ATLAS_HEIGHT; i++) {
    glassStarAtlasPixels[i].red = 0;
    glassStarAtlasPixels[i].green = 0;
    glassStarAtlasPixels[i].blue = 0;
    glassStarAtlasPixels[i].alpha = 0;
  }

  for (int sizeIndex = 0; sizeIndex < 2; sizeIndex++) {
    for (int layer = 0; layer < 3; layer++) {
      const int tileX = layer * GLASS_STAR_TILE_SIZE;
      const int tileY = sizeIndex * GLASS_STAR_TILE_SIZE;
      for (int y = 0; y < GLASS_STAR_TILE_SIZE; y++) {
        for (int x = 0; x < GLASS_STAR_TILE_SIZE; x++) {
          GlassStarPixel *pixel = &glassStarAtlasPixels[(tileY + y) * GLASS_STAR_ATLAS_WIDTH + tileX + x];
          const int dx = (x * 2 + 1 - GLASS_STAR_TILE_SIZE) * 8;
          const int dy = (y * 2 + 1 - GLASS_STAR_TILE_SIZE) * 8;
          const int distance = glassIntegerSqrt(dx * dx + dy * dy);

          // Give transparent edge texels the outer glow color to avoid dark linear-filter fringes.
          pixel->red = clampColor(starRed[layer] + discBrightness[0]);
          pixel->green = clampColor(starGreen[layer] + discBrightness[0]);
          pixel->blue = clampColor(starBlue[layer] + discBrightness[0]);
          for (int disc = 0; disc < 4; disc++)
            compositeGlassStarDisc(pixel, distance, discRadius[sizeIndex][disc],
                                   clampColor(starRed[layer] + discBrightness[disc]),
                                   clampColor(starGreen[layer] + discBrightness[disc]),
                                   clampColor(starBlue[layer] + discBrightness[disc]), discAlpha[disc]);
        }
      }
    }
  }

  glassStarAtlas.Width = GLASS_STAR_ATLAS_WIDTH;
  glassStarAtlas.Height = GLASS_STAR_ATLAS_HEIGHT;
  glassStarAtlas.PSM = GS_PSM_CT32;
  glassStarAtlas.ClutPSM = 0;
  glassStarAtlas.TBW = 0;
  glassStarAtlas.Mem = (u32 *)glassStarAtlasPixels;
  glassStarAtlas.Clut = NULL;
  glassStarAtlas.Vram = 0;
  glassStarAtlas.VramClut = 0;
  glassStarAtlas.Filter = GS_FILTER_LINEAR;
  glassStarAtlas.ClutStorageMode = 0;
  glassStarAtlas.Delayed = GS_SETTING_ON;
  gsKit_TexManager_bind(gsGlobal, &glassStarAtlas);
}

static void drawGlassStarDisc(int x, int y, int size, int red, int green, int blue, int alpha, int brightness) {
  drawOrbitalDisc(x, y, size, 0, glassColor(red, green, blue, alpha, brightness),
                  glassColor(red, green, blue, 0, brightness));
}

static void drawGlassStarSprite(int x, int y, int size, int red, int green, int blue, int alpha) {
  if (alpha <= 0)
    return;
  drawGlassStarDisc(x, y, size + 3, red, green, blue, alpha / 7, -28);
  drawGlassStarDisc(x, y, size + 1, red, green, blue, alpha / 3, -12);
  drawGlassStarDisc(x, y, size, red, green, blue, alpha, 12);
  drawGlassStarDisc(x, y, 1, red, green, blue, alpha / 2, 32);
}

static void drawGlassBackgroundStarSprite(float x, float y, int size, int layer, int alpha) {
  if (alpha <= 0)
    return;
  if (alpha > 0x80)
    alpha = 0x80;

  const int radius = size + 3;
  const int tileX = layer * GLASS_STAR_TILE_SIZE;
  const int tileY = (size - 1) * GLASS_STAR_TILE_SIZE;
  gsKit_prim_sprite_texture(gsGlobal, &glassStarAtlas, x - radius, y - radius, tileX, tileY, x + radius, y + radius,
                            tileX + GLASS_STAR_TILE_SIZE - 1, tileY + GLASS_STAR_TILE_SIZE - 1, 0,
                            GS_SETREG_RGBA(0x80, 0x80, 0x80, alpha));
}

static uint64_t glassColor(int red, int green, int blue, int alpha, int brightness) {
  return glassLightColor(clampColor(red + brightness), clampColor(green + brightness),
                         clampColor(blue + brightness), alpha);
}

void drawGlassDiamond(int centerX, int centerY, int radius, int z, uint64_t color) {
  gsKit_prim_line(gsGlobal, centerX, centerY - radius, centerX + radius, centerY, z, color);
  gsKit_prim_line(gsGlobal, centerX + radius, centerY, centerX, centerY + radius, z, color);
  gsKit_prim_line(gsGlobal, centerX, centerY + radius, centerX - radius, centerY, z, color);
  gsKit_prim_line(gsGlobal, centerX - radius, centerY, centerX, centerY - radius, z, color);
}

void drawGlassPanelWithFillAlpha(int x1, int y1, int x2, int y2, int z, int fillAlpha) {
  const uint64_t glass = glassPresetColor(0x05, 0x0D, 0x22, fillAlpha);
  const uint64_t glassInner = glassPresetColor(0x18, 0x46, 0x70,
                                               (0x14 * fillAlpha) / 0x54);
  const uint64_t rim = glassPresetColor(0x58, 0xA0, 0xC8, 0x28);
  const uint64_t rimFade = glassPresetColor(0x38, 0x78, 0xA0, 0x14);
  const uint64_t edge = glassPresetColor(0x24, 0x68, 0x98, 0x24);

  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y2, z, glass);
  gsKit_prim_sprite(gsGlobal, x1 + 4, y1 + 4, x2 - 4, y1 + 8, z + 1, glassInner);
  // Two matching scanlines per band keep the rim stable in interlaced output.
  gsKit_prim_sprite(gsGlobal, x1, y1, x2, y1 + 2, z + 2, rim);
  gsKit_prim_sprite(gsGlobal, x1 + 2, y1 + 2, x2 - 2, y1 + 4, z + 2, rimFade);
  gsKit_prim_sprite(gsGlobal, x1, y1 + 2, x1 + 2, y2, z + 2, rim);
  gsKit_prim_sprite(gsGlobal, x1, y2 - 2, x2, y2, z + 1, edge);
  gsKit_prim_sprite(gsGlobal, x2 - 2, y1, x2, y2, z + 1, edge);
}

void drawGlassPanel(int x1, int y1, int x2, int y2, int z) {
  drawGlassPanelWithFillAlpha(x1, y1, x2, y2, z, 0x54);
}

static void projectCrystalPointRotated(GlassPoint *point, float centerX, float centerY,
                                       int size, const GlassRotation *rotation,
                                       int sourceX, int sourceY, int sourceZ) {
  const float yawX = sourceX * rotation->yawCosine - sourceZ * rotation->yawSine;
  const float yawZ = sourceX * rotation->yawSine + sourceZ * rotation->yawCosine;
  const float pitchY = sourceY * rotation->pitchCosine - yawZ * rotation->pitchSine;
  const float pitchZ = sourceY * rotation->pitchSine + yawZ * rotation->pitchCosine;
  const float rollX = yawX * rotation->rollCosine - pitchY * rotation->rollSine;
  const float rollY = yawX * rotation->rollSine + pitchY * rotation->rollCosine;
  const float perspective = 640.0f - pitchZ;

  point->x = centerX + rollX * size * 640.0f / (127.0f * perspective);
  point->y = centerY - rollY * size * 640.0f / (127.0f * perspective);
  point->depth = (int)pitchZ;
}

static void drawGlassCube(float centerX, float centerY, int size, uint32_t yawPhase, int red, int green, int blue,
                          int stableOutline, GSTEXTURE *surface, int clearCore) {
  // Clean-room crystal renderer based only on observation of the stock System
  // Configuration animation: a tumbling translucent shell around a dark core.
  static const int source[8][3] = {{-127, -127, -127}, {127, -127, -127}, {127, 127, -127}, {-127, 127, -127},
                                    {-127, -127, 127},  {127, -127, 127},  {127, 127, 127},  {-127, 127, 127}};
  static const int faceVertices[6][4] = {{3, 2, 0, 1}, {7, 6, 4, 5}, {3, 7, 0, 4},
                                         {2, 6, 1, 5}, {3, 2, 7, 6}, {0, 1, 4, 5}};
  static const int faceShade[6] = {-18, 8, -26, -4, 28, -34};
  static const int edgeVertices[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                          {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  const uint32_t pitchPhase = ((yawPhase * 5) / 7) + (5 << 11);
  const uint32_t rollPhase = ((yawPhase * 3) / 11) + (2 << 11);
  const GlassRotation rotation = {
      orbWave(yawPhase) / 127.0f, orbWave(yawPhase + (8 << 11)) / 127.0f,
      orbWave(pitchPhase) / 127.0f, orbWave(pitchPhase + (8 << 11)) / 127.0f,
      orbWave(rollPhase) / 127.0f, orbWave(rollPhase + (8 << 11)) / 127.0f};
  GlassPoint shell[8];
  GlassPoint core[8];
  const float coreScale = (float)((size * 68) / 100) / size;
  int faceOrder[6] = {0, 1, 2, 3, 4, 5};
  int faceDepth[6];

  for (int i = 0; i < 8; i++) {
    projectCrystalPointRotated(&shell[i], centerX, centerY, size, &rotation,
                               source[i][0], source[i][1], source[i][2]);
    // The inner volume shares the shell's rotation and perspective.
    core[i].x = centerX + (shell[i].x - centerX) * coreScale;
    core[i].y = centerY + (shell[i].y - centerY) * coreScale;
  }

  for (int face = 0; face < 6; face++) {
    faceDepth[face] = 0;
    for (int vertex = 0; vertex < 4; vertex++)
      faceDepth[face] += shell[faceVertices[face][vertex]].depth;
  }

  // Draw rear faces first so the transparent front faces retain their depth.
  for (int i = 0; i < 5; i++) {
    for (int j = i + 1; j < 6; j++) {
      if (faceDepth[faceOrder[i]] > faceDepth[faceOrder[j]]) {
        int swap = faceOrder[i];
        faceOrder[i] = faceOrder[j];
        faceOrder[j] = swap;
      }
    }
  }

  if (surface != NULL)
    gsKit_TexManager_bind(gsGlobal, surface);

  // The low-alpha shell remains visible behind the central smoked volume.
  for (int order = 0; order < 6; order++) {
    int face = faceOrder[order];
    int a = faceVertices[face][0];
    int b = faceVertices[face][1];
    int c = faceVertices[face][2];
    int d = faceVertices[face][3];
    int shade = faceShade[face] + faceDepth[face] / 18;
    uint64_t light = glassColor(red, green, blue, 0x25, shade + 34);
    uint64_t mid = glassColor(red, green, blue, 0x1D, shade + 8);
    uint64_t dark = glassColor(red, green, blue, 0x16, shade - 24);
    gsKit_prim_quad_gouraud(gsGlobal, shell[a].x, shell[a].y, shell[b].x, shell[b].y, shell[c].x, shell[c].y, shell[d].x,
                            shell[d].y, 0, light, mid, dark, mid);
    if (surface != NULL)
      gsKit_prim_quad_texture(gsGlobal, surface,
                              shell[a].x, shell[a].y, 0, 0,
                              shell[b].x, shell[b].y, 63, 0,
                              shell[c].x, shell[c].y, 0, 63,
                              shell[d].x, shell[d].y, 63, 63, 0,
                              GS_SETREG_RGBA(red, green, blue, 0x22));
  }

  // The clear variant shows the clouds through its glass shell.
  if (!clearCore) {
    for (int order = 0; order < 6; order++) {
      int face = faceOrder[order];
      int a = faceVertices[face][0];
      int b = faceVertices[face][1];
      int c = faceVertices[face][2];
      int d = faceVertices[face][3];
      int shade = faceShade[face] + faceDepth[face] / 22;
      uint64_t light = glassColor(red / 2, green / 2, blue / 2, 0x42, shade + 8);
      uint64_t mid = glassColor(red / 3, green / 3, blue / 3, 0x48, shade - 12);
      uint64_t dark = glassColor(red / 4, green / 4, blue / 4, 0x50, shade - 28);
      gsKit_prim_quad_gouraud(gsGlobal, core[a].x, core[a].y, core[b].x, core[b].y, core[c].x, core[c].y, core[d].x, core[d].y, 0,
                              light, mid, dark, mid);
    }
  }

  // Keep the large loading cube's outline subdued so interlaced output does
  // not turn its moving one-pixel edges into a white shimmer.
  for (int i = 0; i < 12; i++) {
    int a = edgeVertices[i][0];
    int b = edgeVertices[i][1];
    int edgeDepth = (shell[a].depth + shell[b].depth) / 2;
    int edgeAlpha = stableOutline ? 0x30 + (edgeDepth + 180) / 14 : 0x38 + (edgeDepth + 180) / 8;
    int edgeBrightness = stableOutline ? 18 + (edgeDepth + 180) / 12 : 46 + (edgeDepth + 180) / 5;
    int maxAlpha = stableOutline ? 0x50 : 0x78;
    if (edgeAlpha > maxAlpha)
      edgeAlpha = maxAlpha;
    gsKit_prim_line(gsGlobal, shell[a].x, shell[a].y, shell[b].x, shell[b].y, 0,
                    glassColor(red, green, blue, edgeAlpha, edgeBrightness));
  }

  if (!stableOutline) {
    int nearest = 0;
    for (int i = 1; i < 8; i++) {
      if (shell[i].depth > shell[nearest].depth)
        nearest = i;
    }
    drawGlassStarSprite((int)(shell[nearest].x + 0.5f), (int)(shell[nearest].y + 0.5f), 1, clampColor(red + 90),
                        clampColor(green + 90), clampColor(blue + 90), 0x58);
  }
}

// Snapshot the current cloud frame before drawing glass. osdbits/opening.c
// captures the screen for each cube; a 128x128 local copy covers LUNA's one
// cube without reserving another full framebuffer in the PS2's 4 MB VRAM.
static void copyCapturePixels(u32 sourceVram, int sourceWidth,
                              int left, int top, u32 destVram, int size) {
  u64 *packet = gsKit_heap_alloc(gsGlobal, 5, 5 * 16, GIF_AD);
  packet[0] = GIF_TAG_AD(5);
  packet[1] = GIF_AD;
  packet[2] = GS_SETREG_BITBLTBUF(sourceVram / 256,
                                  sourceWidth / 64, GS_PSM_CT24,
                                  destVram / 256,
                                  size / 64, GS_PSM_CT24);
  packet[3] = GS_BITBLTBUF;
  packet[4] = GS_SETREG_TRXPOS(left, top, 0, 0, 0);
  packet[5] = GS_TRXPOS;
  packet[6] = GS_SETREG_TRXREG(size, size);
  packet[7] = GS_TRXREG;
  packet[8] = GS_SETREG_TRXDIR(2);
  packet[9] = GS_TRXDIR;
  packet[10] = 0;
  packet[11] = GS_TEXFLUSH;
}

static void setOpeningCubeTarget(int work) {
  const u32 vram = work ? openingWorkTexture.Vram :
                    gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer];
  const int width = work ? OPENING_CAPTURE_SIZE : gsGlobal->Width;
  const int height = work ? OPENING_CAPTURE_SIZE : gsGlobal->Height;
  const int frameRegister = gsGlobal->PrimContext ? GS_FRAME_2 : GS_FRAME_1;
  const int scissorRegister = gsGlobal->PrimContext ? GS_SCISSOR_2 : GS_SCISSOR_1;
  u64 *packet = gsKit_heap_alloc(gsGlobal, 3, 3 * 16, GIF_AD);
  packet[0] = GIF_TAG_AD(3);
  packet[1] = GIF_AD;
  packet[2] = GS_SETREG_FRAME(vram / 8192, width / 64, GS_PSM_CT24, 0);
  packet[3] = frameRegister;
  packet[4] = GS_SETREG_SCISSOR(0, width - 1, 0, height - 1);
  packet[5] = scissorRegister;
  packet[6] = 0;
  packet[7] = GS_TEXFLUSH;
}

static float openingCubeUv(float value, float maximum) {
  if (value < 1.0f)
    return 1.0f;
  if (value > maximum - 1.0f)
    return maximum - 1.0f;
  return value;
}

static float openingCubeWrapUv(float value, float maximum) {
  while (value < 0.0f)
    value += maximum;
  while (value >= maximum)
    value -= maximum;
  return value;
}

static void drawOpeningGlassCube(uint32_t elapsedMs, int width, int height) {
  if (!openingCaptureReady || !biosOpeningCubeTexturesLoaded)
    return;
  const float centerX = width * 0.26f;
  const float centerY = height * 0.42f +
                        orbWave(glassPhase(elapsedMs, 15000, 2800)) / 11.0f;
  const int size = 25;
  const int captureLeft = (int)centerX - OPENING_CAPTURE_SIZE / 2;
  const int captureTop = (int)centerY - OPENING_CAPTURE_SIZE / 2;
  if (captureLeft < 0 || captureTop < 0 ||
      captureLeft + OPENING_CAPTURE_SIZE > width ||
      captureTop + OPENING_CAPTURE_SIZE > height)
    return;

  // The face winding and two-layer texture sequence come from the opening
  // cube in osdbits/opening.c. This keeps the actual ROM reflection and
  // glass masks over a live copy of the moving clouds.
  static const int corners[8][3] = {
      {-127, -127, -127}, {127, -127, -127}, {127, 127, -127}, {-127, 127, -127},
      {-127, -127, 127}, {127, -127, 127}, {127, 127, 127}, {-127, 127, 127}};
  static const int faces[6][4] = {
      {3, 2, 0, 1}, {7, 6, 4, 5}, {3, 7, 0, 4},
      {2, 6, 1, 5}, {3, 2, 7, 6}, {0, 1, 4, 5}};
  static const int faceNormal[6][3] = {
      {0, 0, -1}, {0, 0, 1}, {-1, 0, 0},
      {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  const uint32_t yaw = glassPhase(elapsedMs, 19000, 4200);
  const uint32_t pitch = ((yaw * 5) / 7) + (5 << 11);
  const uint32_t roll = ((yaw * 3) / 11) + (2 << 11);
  const GlassRotation rotation = {
      orbWave(yaw) / 127.0f, orbWave(yaw + (8 << 11)) / 127.0f,
      orbWave(pitch) / 127.0f, orbWave(pitch + (8 << 11)) / 127.0f,
      orbWave(roll) / 127.0f, orbWave(roll + (8 << 11)) / 127.0f};
  GlassPoint point[8];
  int faceOrder[6] = {0, 1, 2, 3, 4, 5};
  int depth[6] = {0};
  float normalX[6], normalY[6], normalZ[6];
  for (int i = 0; i < 8; i++)
    projectCrystalPointRotated(&point[i], centerX, centerY, size, &rotation,
                               corners[i][0], corners[i][1], corners[i][2]);
  for (int face = 0; face < 6; face++) {
    for (int vertex = 0; vertex < 4; vertex++)
      depth[face] += point[faces[face][vertex]].depth;
    const float sx = faceNormal[face][0];
    const float sy = faceNormal[face][1];
    const float sz = faceNormal[face][2];
    const float yawX = sx * rotation.yawCosine - sz * rotation.yawSine;
    const float yawZ = sx * rotation.yawSine + sz * rotation.yawCosine;
    const float pitchY = sy * rotation.pitchCosine - yawZ * rotation.pitchSine;
    normalZ[face] = sy * rotation.pitchSine + yawZ * rotation.pitchCosine;
    normalX[face] = yawX * rotation.rollCosine - pitchY * rotation.rollSine;
    normalY[face] = yawX * rotation.rollSine + pitchY * rotation.rollCosine;
  }
  for (int i = 0; i < 5; i++) {
    for (int j = i + 1; j < 6; j++) {
      if (depth[faceOrder[i]] > depth[faceOrder[j]]) {
        int swap = faceOrder[i];
        faceOrder[i] = faceOrder[j];
        faceOrder[j] = swap;
      }
    }
  }

  copyCapturePixels(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer],
                    width, captureLeft, captureTop,
                    openingCaptureTexture.Vram, OPENING_CAPTURE_SIZE);
  copyCapturePixels(openingCaptureTexture.Vram, OPENING_CAPTURE_SIZE,
                    0, 0, openingWorkTexture.Vram, OPENING_CAPTURE_SIZE);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  for (int stage = 0; stage < 2; stage++) {
    if (stage == 0)
      setOpeningCubeTarget(1);
    else
      setOpeningCubeTarget(0);
    GSTEXTURE *source = stage == 0 ? &openingCaptureTexture :
                         &openingWorkTexture;
    for (int order = 0; order < 6; order++) {
      const int faceIndex = faceOrder[order];
      if ((normalZ[faceIndex] > 0.0f) != (stage == 1))
        continue;
      const int *face = faces[faceIndex];
      const float faceX = normalX[faceIndex] * 9.0f;
      const float faceY = -normalY[faceIndex] * 9.0f;
      float u[4], v[4];
      for (int vertex = 0; vertex < 4; vertex++) {
        const GlassPoint *p = &point[face[vertex]];
        const float zoom = stage == 1 ? 0.084f : 0.0f;
        u[vertex] = openingCubeUv(p->x - captureLeft + faceX -
                                  (p->x - centerX) * zoom,
                                  OPENING_CAPTURE_SIZE);
        v[vertex] = openingCubeUv(p->y - captureTop + faceY -
                                  (p->y - centerY) * zoom,
                                  OPENING_CAPTURE_SIZE);
      }
      const float outputX = stage == 0 ? captureLeft : 0.0f;
      const float outputY = stage == 0 ? captureTop : 0.0f;
      gsKit_prim_quad_texture(gsGlobal, source,
                              point[face[0]].x - outputX, point[face[0]].y - outputY, u[0], v[0],
                              point[face[1]].x - outputX, point[face[1]].y - outputY, u[1], v[1],
                              point[face[2]].x - outputX, point[face[2]].y - outputY, u[2], v[2],
                              point[face[3]].x - outputX, point[face[3]].y - outputY, u[3], v[3], 0,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80, stage == 0 ? 0x72 : 0x68));
    }
  }

  // The ROM's two black alpha masks use additive blending in opening.c;
  // ordinary source-alpha blending incorrectly paints black face stickers.
  GSTEXTURE *const layers[3] = {
      &openingBlprTexture, &openingBlpTexture, &openingRefTexture};
  for (int layer = 0; layer < 3; layer++) {
    GSTEXTURE *texture = layers[layer];
    gsKit_TexManager_bind(gsGlobal, texture);
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
    for (int order = 0; order < 6; order++) {
      const int faceIndex = faceOrder[order];
      if (normalZ[faceIndex] <= 0.0f)
        continue;
      const int *face = faces[faceIndex];
      const float textureSize = layer == 2 ? 128.0f : 64.0f;
      const float phase = (elapsedMs % 16000) / 16000.0f;
      float u[4], v[4];
      for (int vertex = 0; vertex < 4; vertex++) {
        const float cornerU = vertex == 1 || vertex == 3 ? 1.0f : 0.0f;
        const float cornerV = vertex >= 2 ? 1.0f : 0.0f;
        if (layer == 2) {
          const GlassPoint *p = &point[face[vertex]];
          const float viewX = (p->x - centerX) / (size * 2.0f);
          const float viewY = (p->y - centerY) / (size * 2.0f);
          u[vertex] = openingCubeUv((0.5f + viewY * 0.5f +
                                     normalY[faceIndex] * 0.30f) * textureSize,
                                    textureSize);
          v[vertex] = openingCubeUv((0.5f + viewX * 0.5f +
                                     normalX[faceIndex] * 0.30f) * textureSize,
                                    textureSize);
        } else {
          const float scroll = phase * (layer == 0 ? -0.5f : 0.5f);
          u[vertex] = openingCubeWrapUv((cornerU + scroll) *
                                         (textureSize - 1.0f), textureSize);
          v[vertex] = openingCubeWrapUv((cornerV - scroll) *
                                         (textureSize - 1.0f), textureSize);
        }
      }
      gsKit_prim_quad_texture(gsGlobal, texture,
                              point[face[0]].x, point[face[0]].y, u[0], v[0],
                              point[face[1]].x, point[face[1]].y, u[1], v[1],
                              point[face[2]].x, point[face[2]].y, u[2], v[2],
                              point[face[3]].x, point[face[3]].y, u[3], v[3], 0,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80,
                                             layer == 2 ? 0x18 : 0x10));
    }
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void drawOrbitalDisc(int centerX, int centerY, int radius, int z, uint64_t centerColor, uint64_t edgeColor) {
  for (int i = 0; i < 32; i++) {
    int next = (i + 1) & 31;
    int x1 = centerX + (glassSin[(i + 8) & 31] * radius) / 127;
    int y1 = centerY + (glassSin[i] * radius) / 127;
    int x2 = centerX + (glassSin[(next + 8) & 31] * radius) / 127;
    int y2 = centerY + (glassSin[next] * radius) / 127;
    gsKit_prim_triangle_gouraud(gsGlobal, centerX, centerY, x1, y1, x2, y2, z, centerColor, edgeColor, edgeColor);
  }
}

// Cubic interpolation keeps the orb path and its velocity smooth between the
// 32 entries in the shared sine table. gsKit accepts fractional coordinates.
static float orbWave(uint32_t phase) {
  const int index = (phase >> 11) & 31;
  const float p0 = glassSin[(index + 31) & 31];
  const float p1 = glassSin[index];
  const float p2 = glassSin[(index + 1) & 31];
  const float p3 = glassSin[(index + 2) & 31];
  const float t = (phase & 2047) / 2048.0f;
  return 0.5f * ((2.0f * p1) +
                 t * ((p2 - p0) +
                      t * ((2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) +
                           t * (-p0 + 3.0f * p1 - 3.0f * p2 + p3))));
}

static void drawOrbitalStars(int width, int height, uint32_t elapsedMs) {
  static const int speedPixelsPerSecond[3] = {4, 8, 13};
  float scroll[3];

  for (int layer = 0; layer < 3; layer++)
    scroll[layer] = (float)(((uint64_t)elapsedMs * speedPixelsPerSecond[layer]) %
                            ((uint64_t)width * 1000)) / 1000.0f;

  gsKit_TexManager_bind(gsGlobal, &glassStarAtlas);
  for (int i = 0; i < 58; i++) {
    int layer = i % 3;
    int baseX = (i * 97 + i * i * 13 + 31) % width;
    int baseY = (i * 53 + i * i * 7 + 19) % height;
    float y = baseY + orbWave(glassPhase(elapsedMs, 7000 + layer * 1300, i * 113)) * (layer + 1) / 160.0f;
    float x = baseX + scroll[layer];
    if (x >= width)
      x -= width;
    int twinkle = (int)(orbWave(glassPhase(elapsedMs, 2600 + layer * 900, i * 173)) / 14.0f);
    int size = ((layer == 2) && ((i % 5) == 0)) ? 2 : 1;
    int alpha = 0x20 + layer * 0x12;
    int starAlpha = alpha + twinkle;
    drawGlassBackgroundStarSprite(x, y, size, layer, starAlpha);

    // Draw the clipped half at the opposite edge with identical brightness.
    const int radius = size + 3;
    if (x < radius)
      drawGlassBackgroundStarSprite(x + width, y, size, layer, starAlpha);
    else if (x > width - radius)
      drawGlassBackgroundStarSprite(x - width, y, size, layer, starAlpha);
  }
}

static void drawColorStarBackground(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  const uint64_t black = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);

  gsKit_prim_quad_gouraud(gsGlobal, 0, 0, width, 0, 0, height, width, height, 0, black, black, black, black);

  drawOrbitalStars(width, height, elapsedMs);
}

static void drawGlassBackground(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  const int orbitX = width * 65 / 100;
  const int orbitY = height * 52 / 100;

  drawColorStarBackground(frameNowMs);

  // Two floating glass cubes retain the PS2 BIOS geometry without crowding the library.
  // Share one orbital phase and keep the crystals half a revolution apart.
  // The nested ellipses retain at least 96 pixels of center separation, so
  // their paths cannot collide even at their closest vertical alignment.
  uint32_t orbitPhase = glassPhase(elapsedMs, 36000, 0);
  uint32_t oppositePhase = orbitPhase + (16 << 11);
  float nearX = orbitX + orbWave(orbitPhase + (8 << 11)) * (146.0f / 127.0f);
  float nearY = orbitY + orbWave(orbitPhase) * (56.0f / 127.0f);
  float farX = orbitX + orbWave(oppositePhase + (8 << 11)) * (96.0f / 127.0f);
  float farY = orbitY + orbWave(oppositePhase) * (40.0f / 127.0f);
  drawGlassCube(nearX, nearY, 9, glassPhase(elapsedMs, 18000, 3000), 0x38, 0x98, 0xD8, 1, NULL, 0);
  drawGlassCube(farX, farY, 7, glassPhase(elapsedMs, 26000, 12000), 0x78, 0x68, 0xC8, 1, NULL, 0);
}

typedef struct {
  float x, y, u, v;
  uint64_t color;
} CloudVertex;

// OSDSYS fades a 3x3 fog patch to its lit center. Four quads keep that
// shape while sampling TEXOFOG0 from the running console's BIOS.
static void drawRedCloudPatch(float x, float y, float radius, int alpha,
                              int red, int green, int blue) {
  CloudVertex vertex[3][3];
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      CloudVertex *point = &vertex[row][column];
      point->x = x + (column - 1) * radius;
      point->y = y + (row - 1) * radius;
      point->u = column * 63.5f;
      point->v = row * 63.5f;
      point->color = GS_SETREG_RGBA(red, green, blue,
                                     row == 1 && column == 1 ? alpha : 0);
    }
  }
  for (int row = 0; row < 2; row++) {
    for (int column = 0; column < 2; column++) {
      CloudVertex *topLeft = &vertex[row][column];
      CloudVertex *topRight = &vertex[row][column + 1];
      CloudVertex *bottomLeft = &vertex[row + 1][column];
      CloudVertex *bottomRight = &vertex[row + 1][column + 1];
      gsKit_prim_quad_goraud_texture(gsGlobal, &fogTexture,
                                     topLeft->x, topLeft->y, topLeft->u, topLeft->v,
                                     topRight->x, topRight->y, topRight->u, topRight->v,
                                     bottomLeft->x, bottomLeft->y, bottomLeft->u, bottomLeft->v,
                                     bottomRight->x, bottomRight->y, bottomRight->u, bottomRight->v,
                                     0, topLeft->color, topRight->color,
                                     bottomLeft->color, bottomRight->color);
    }
  }
}

static void drawRedClouds(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  const int centerX = width * 52 / 100;
  const int centerY = height * 52 / 100;
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x05, 0x01, 0x04, 0x80));
  drawOrbitalDisc(centerX, centerY, width * 42 / 100, 0,
                  GS_SETREG_RGBA(0x62, 0x10, 0x19, 0x24),
                  GS_SETREG_RGBA(0x18, 0x03, 0x08, 0));

  gsKit_TexManager_bind(gsGlobal, &fogTexture);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  for (int i = 0; i < 36; i++) {
    const int radius = 72 + (i % 6) * 18;
    const int span = height + radius * 2;
    const int speed = 5 + (i % 7) * 3;
    const float baseX = (i * 107 + i * i * 19 + 37) % (width + 160) - 80;
    const float x = baseX + orbWave(glassPhase(elapsedMs, 15000 + i * 270, i * 313)) / 8.0f;
    const float y = (float)(((uint64_t)elapsedMs * speed +
                              (uint64_t)((i * 73 + i * i * 11) % span) * 1000) %
                             ((uint64_t)span * 1000)) / 1000.0f - radius;
    const int alpha = 0x1A + (i % 4) * 4;
    drawRedCloudPatch(x, y, radius, alpha,
                      0x68 + (i % 5) * 8, 0x20 + (i % 3) * 7,
                      0x25 + (i % 4) * 5);
  }
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  drawOrbitalDisc(centerX, centerY, width * 13 / 100, 0,
                  GS_SETREG_RGBA(0xD0, 0x5A, 0x64, 0x20),
                  GS_SETREG_RGBA(0x78, 0x1A, 0x28, 0));
}

static void drawMidnightCubes(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const uint32_t elapsedMs = glassElapsedMs(frameNowMs);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0x02, 0x04, 0x0B, 0x80));
  drawOrbitalDisc(width * 34 / 100, height * 49 / 100, width * 43 / 100, 0,
                  GS_SETREG_RGBA(0x15, 0x35, 0x53, 0x28),
                  GS_SETREG_RGBA(0x02, 0x08, 0x15, 0));

  // Floating cubes keep the BIOS bump texture from the original trial.
  drawGlassCube(width * 0.17f,
                height * 0.33f + orbWave(glassPhase(elapsedMs, 13000, 0)) / 10.0f,
                23, glassPhase(elapsedMs, 16000, 0), 0x26, 0xAE, 0xDC, 0, &bumpTexture, 0);
  drawGlassCube(width * 0.36f,
                height * 0.18f + orbWave(glassPhase(elapsedMs, 17000, 2300)) / 12.0f,
                18, glassPhase(elapsedMs, 22000, 5500), 0x43, 0xA0, 0xDB, 0, &bumpTexture, 0);
  drawGlassCube(width * 0.31f,
                height * 0.67f + orbWave(glassPhase(elapsedMs, 15000, 5900)) / 11.0f,
                15, glassPhase(elapsedMs, 19000, 11000), 0x7E, 0x75, 0xB9, 0, &bumpTexture, 0);
  drawGlassCube(width * 0.47f,
                height * 0.51f + orbWave(glassPhase(elapsedMs, 14000, 8100)) / 13.0f,
                11, glassPhase(elapsedMs, 26000, 17000), 0x68, 0x83, 0xB8, 0, &bumpTexture, 0);
}

typedef struct {
  float x, y, z;
  float cameraX, cameraY;
  float u, v;
} ConfigRodVertex;

typedef struct {
  ConfigRodVertex vertex[4];
  float normalX, normalY, normalZ;
  float fresnel;
  int nearFace;
} ConfigRodFace;

typedef struct {
  float slotS, slotC, orbitS, orbitC, tiltS, tiltC, spinS, spinC;
} ConfigRodRotation;

static float configSin(uint32_t phase) {
  return orbWave(phase) / 127.0f;
}

static float configCos(uint32_t phase) {
  return orbWave(phase + 16384U) / 127.0f;
}

static float configWrapUv(float value) {
  value = fmodf(value, 64.0f);
  return value < 0.0f ? value + 64.0f : value;
}

static void configTransform(float *x, float *y, float *z,
                            const ConfigRodRotation *rotation, int normal) {
  float a = *x * rotation->spinC + *z * rotation->spinS;
  float b = *z * rotation->spinC - *x * rotation->spinS;
  float c = *y + (normal ? 0.0f : 20.0f);
  float d = a * rotation->slotC - c * rotation->slotS;
  float e = a * rotation->slotS + c * rotation->slotC;
  a = d * rotation->orbitC + b * rotation->orbitS;
  b = b * rotation->orbitC - d * rotation->orbitS;
  d = a * rotation->tiltC - e * rotation->tiltS;
  e = a * rotation->tiltS + e * rotation->tiltC;
  *x = d;
  *y = e;
  *z = b;
  ps2MenuCamera(x, y, z, normal);
}

static float configRodDepth(int slot, uint32_t tilt, uint32_t orbit,
                            uint32_t spin) {
  const uint32_t slotAngle = slot * (65536U / 12U) - 32768U;
  const ConfigRodRotation rotation = {
      configSin(slotAngle), configCos(slotAngle),
      configSin(orbit), configCos(orbit),
      configSin(tilt), configCos(tilt),
      configSin(spin), configCos(spin)};
  float x = 0.0f, y = 0.0f, z = 0.0f;
  configTransform(&x, &y, &z, &rotation, 0);
  return z;
}

static void drawBiosTunnel(uint32_t elapsedMs, int centerX, int centerY) {
  // The ROM emits 16 ribbons with 33 axial rings on a radius-6000 wall.
  // Every second ring is sufficient for gsKit's independent quads.
  gsKit_TexManager_bind(gsGlobal, &configWallTexture);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  const uint32_t frame = elapsedMs * 60U / 1000U;
  for (int ring = 4; ring < 32; ring += 2) {
    const int next = ring + 2;
    const float z0 = ring * 1250.0f - 2500.0f + 103.0f;
    const float z1 = next * 1250.0f - 2500.0f + 103.0f;
    const float wave0 = 1.0f + configSin(frame * 100U + ring * 5120U) * 0.05f;
    const float wave1 = 1.0f + configSin(frame * 100U + next * 5120U) * 0.05f;
    const float r0 = 6000.0f * 512.0f * wave0 / z0;
    const float r1 = 6000.0f * 512.0f * wave1 / z1;
    const float v0 = configWrapUv(((float)ring / 32.0f +
                                  (frame % 5000U) * 0.0002f) * 384.0f);
    const float v1 = v0 + 24.0f;
    const int p0 = ((32 - ring) * (32 - ring) * (32 - ring)) >> 10;
    const int p1 = ((32 - next) * (32 - next) * (32 - next)) >> 10;
    const uint64_t color0 = GS_SETREG_RGBA(clampColor(p0 * 230 >> 5),
                                            clampColor(p0 * 260 >> 5),
                                            clampColor(p0 * 260 >> 5), 0x80);
    const uint64_t color1 = GS_SETREG_RGBA(clampColor(p1 * 230 >> 5),
                                            clampColor(p1 * 260 >> 5),
                                            clampColor(p1 * 260 >> 5), 0x80);
    for (int segment = 0; segment < 16; segment++) {
      const uint32_t a0 = segment * 4096U;
      const uint32_t a1 = (segment + 1) * 4096U;
      const float s0 = configSin(a0), c0 = configCos(a0);
      const float s1 = configSin(a1), c1 = configCos(a1);
      const float u0 = (float)((segment * 24) & 63);
      const float u1 = u0 + 24.0f;
      const float yscale = gsGlobal->Height / 480.0f;
      gsKit_prim_quad_goraud_texture(gsGlobal, &configWallTexture,
                                     centerX + s0 * r0, centerY - c0 * r0 * yscale, u0, v0,
                                     centerX + s1 * r0, centerY - c1 * r0 * yscale, u1, v0,
                                     centerX + s0 * r1, centerY - c0 * r1 * yscale, u0, v1,
                                     centerX + s1 * r1, centerY - c1 * r1 * yscale, u1, v1,
                                     0, color0, color0, color1, color1);
    }
  }
}

// Port of osdbits/menuconfig.c's rod passes (0x22D920 / 0x22E428).
// Geometry stays in the running BIOS; these are only transforms and GS packets.
// Screen positions remain native-resolution; work-target positions and sampled
// screen UVs are scaled together into our shared power-of-two pages.
typedef struct {
  ConfigRodFace face[2][16];
  int pieces, index;
  float refX, refY, split;
} ConfigRodScene;

static ConfigRodScene configRodScene[12];
static int configRenderTarget = -1;

static u64 *configPacket(int count) {
  u64 *packet = gsKit_heap_alloc(gsGlobal, count, count * 16, GIF_AD);
  packet[0] = GIF_TAG_AD(count);
  packet[1] = GIF_AD;
  return packet + 2;
}

static u32 configFloatBits(float value) {
  u32 bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static void configSetTarget(int work) {
  configRenderTarget = work;
  const int width = work < 0 ? gsGlobal->Width : CONFIG_WORK_SIZE;
  const int height = work < 0 ? gsGlobal->Height : configWorkTexture[work].Height;
  const u32 vram = work < 0 ? gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer] :
                              configWorkTexture[work].Vram;
  const u32 zram = work < 0 ? gsGlobal->ZBuffer : configWorkZ;
  u64 *p = configPacket(4);
  p[0] = GS_SETREG_FRAME(vram / 8192, width / 64,
                         work < 0 ? gsGlobal->PSM : GS_PSM_CT32, 0);
  p[1] = gsGlobal->PrimContext ? GS_FRAME_2 : GS_FRAME_1;
  p[2] = GS_SETREG_ZBUF(zram / 8192, gsGlobal->PSMZ & 15, 0);
  p[3] = gsGlobal->PrimContext ? GS_ZBUF_2 : GS_ZBUF_1;
  p[4] = GS_SETREG_SCISSOR(0, width - 1, 0, height - 1);
  p[5] = gsGlobal->PrimContext ? GS_SCISSOR_2 : GS_SCISSOR_1;
  p[6] = 0;
  p[7] = GS_TEXFLUSH;
}

static void configSetTest(int gequal) {
  u64 *p = configPacket(1);
  // Alpha zero is meaningful in work buffers: disable LUNA's UI alpha test.
  p[0] = GS_SETREG_TEST(0, 0, 0, 0, 0, 0, 1, gequal ? 2 : 1);
  p[1] = gsGlobal->PrimContext ? GS_TEST_2 : GS_TEST_1;
}

static void configBindTexture(GSTEXTURE *texture, int repeat, int resident) {
  if (!resident)
    gsKit_TexManager_bind(gsGlobal, texture);
  int tw = 0, th = 0;
  while ((1 << tw) < texture->Width) tw++;
  while ((1 << th) < texture->Height) th++;
  u64 *p = configPacket(4);
  p[0] = 0;
  p[1] = GS_TEXFLUSH;
  p[2] = GS_SETREG_TEX0(texture->Vram / 256, texture->TBW, texture->PSM,
                        tw, th, 1, 0, 0, 0, 0, 0, 0);
  p[3] = gsGlobal->PrimContext ? GS_TEX0_2 : GS_TEX0_1;
  p[4] = GS_SETREG_TEX1(1, 0, 1, 1, 0, 0, 0);
  p[5] = gsGlobal->PrimContext ? GS_TEX1_2 : GS_TEX1_1;
  p[6] = GS_SETREG_CLAMP(repeat ? 0 : 1, repeat ? 0 : 1, 0, 0, 0, 0);
  p[7] = gsGlobal->PrimContext ? GS_CLAMP_2 : GS_CLAMP_1;
}

static void configBlitRect(GSTEXTURE *texture, u64 color, int blend,
                           float width, float height,
                           float sourceWidth, float sourceHeight) {
  configBindTexture(texture, 0, 1);
  u64 *p = configPacket(6);
  p[0] = 6 | 16 | 256 | (blend ? 64 : 0) | (gsGlobal->PrimContext << 9);
  p[1] = GS_PRIM;
  p[2] = color;
  p[3] = GS_RGBAQ;
  p[4] = GS_SETREG_UV(8, 8);
  p[5] = GS_UV;
  p[6] = GS_SETREG_XYZ2(gsKit_float_to_int_x(gsGlobal, 0),
                        gsKit_float_to_int_y(gsGlobal, 0), 0);
  p[7] = GS_XYZ2;
  p[8] = GS_SETREG_UV((int)(sourceWidth * 16.0f) + 8,
                       (int)(sourceHeight * 16.0f) + 8);
  p[9] = GS_UV;
  p[10] = GS_SETREG_XYZ2(gsKit_float_to_int_x(gsGlobal, width),
                         gsKit_float_to_int_y(gsGlobal, height), 0);
  p[11] = GS_XYZ2;
}

static void configBlit(GSTEXTURE *texture, u64 color, int blend) {
  const int width = configRenderTarget < 0 ? gsGlobal->Width : CONFIG_WORK_SIZE;
  const int height = configRenderTarget < 0 ? gsGlobal->Height :
                      configWorkTexture[configRenderTarget].Height;
  configBlitRect(texture, color, blend, width, height, texture->Width, texture->Height);
}

// MenuZoomBlur -> 0x22C3C0(5): AFTER the 3D scene and bloom, BEFORE UI.
// Bilinear, opaque round trips through wb4, with progressively different
// fractional sampling grids. Adapt retail's 319.25x149.25 working rect to
// our smaller reserved page; do not allocate VRAM or blur library artwork.
static void configZoomBlur(GSTEXTURE *screen) {
  const u64 white = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);
  configSetTest(0);
  for (int pass = 0; pass < 5; pass++) {
    const float width = (319.25f - pass * 2.0f) * CONFIG_WORK_SIZE / 320.0f;
    const float height = (149.25f - pass) * configWorkTexture[1].Height / 150.0f;
    configSetTarget(1);
    configBlitRect(screen, white, 0, width, height,
                    gsGlobal->Width, gsGlobal->Height - 1.0f);
    configSetTarget(-1);
    configBlitRect(&configWorkTexture[1], white, 0,
                    gsGlobal->Width, gsGlobal->Height - 1.0f, width, height);
  }
}

static void configClearWork(void) {
  const int alpha = gsGlobal->PrimAlphaEnable;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_prim_sprite(gsGlobal, 0, 0, CONFIG_WORK_SIZE, configWorkTexture[0].Height, 0,
                    GS_SETREG_RGBA(0, 0, 0, 0x80));
  gsGlobal->PrimAlphaEnable = alpha;
}

static void configEmitVertex(u64 *p, const ConfigRodVertex *v) {
  const float sx = configRenderTarget < 0 ? 1.0f :
                    CONFIG_WORK_SIZE / (float)gsGlobal->Width;
  const float sy = configRenderTarget < 0 ? 1.0f :
                    configWorkTexture[0].Height / (float)gsGlobal->Height;
  // LUNA uses Z16S rather than retail's Z32. Preserve reversed-depth
  // ordering (near >= far) in its 16-bit range for GEQUAL and emboss.
  const u32 depth = (u32)(65535.0f / v->z);
  p[0] = GS_SETREG_XYZF2(gsKit_float_to_int_x(gsGlobal, v->x * sx),
                         gsKit_float_to_int_y(gsGlobal, v->y * sy), depth, 0);
  p[1] = GS_XYZF2;
}

static void configBuildRod(ConfigRodScene *scene, int slot, int index,
                            uint32_t tilt, uint32_t orbit, uint32_t spin,
                            float split) {
  const uint32_t slotAngle = (slot * 65536U) / 12U - 32768U;
  const ConfigRodRotation rotation = {
      configSin(slotAngle), configCos(slotAngle),
      configSin(orbit), configCos(orbit),
      configSin(tilt), configCos(tilt),
      configSin(spin), configCos(spin)};
  scene->pieces = split > 0.0f ? 2 : 1;
  scene->index = index;
  scene->split = split;
  float ox = 0, oy = 0, oz = 0;
  configTransform(&ox, &oy, &oz, &rotation, 0);
  ps2MenuProject(ox, oy, oz, gsGlobal->Width, gsGlobal->Height,
                  &scene->refX, &scene->refY);
  scene->refX = (scene->refX - gsGlobal->Width * 0.5f) * 0.9f;
  scene->refY = (scene->refY - gsGlobal->Height * 0.5f) * 0.9f;
  for (int piece = 0; piece < scene->pieces; piece++) {
    const float scale = scene->pieces == 1 ? 1.0f :
                         piece == 0 ? split : 1.0f - split;
    const float offset = piece ? 26.0f * split : 0.0f;
    for (int i = 0; i < 16; i++) {
      ConfigRodFace *f = &scene->face[piece][i];
      f->normalX = configRodNormals[i][0];
      f->normalY = configRodNormals[i][1];
      f->normalZ = configRodNormals[i][2];
      configTransform(&f->normalX, &f->normalY, &f->normalZ, &rotation, 1);
      for (int j = 0; j < 4; j++) {
        ConfigRodVertex *v = &f->vertex[j];
        float x = configRodVerts[i][j][0];
        float y = configRodVerts[i][j][1] * scale + offset;
        float z = configRodVerts[i][j][2];
        configTransform(&x, &y, &z, &rotation, 0);
        if (z < 10.0f) z = 10.0f;
        v->cameraX = x;
        v->cameraY = y;
        v->z = z;
        ps2MenuProject(x, y, z, gsGlobal->Width, gsGlobal->Height, &v->x, &v->y);
        v->u = configRodUvs[i][j][0];
        v->v = configRodUvs[i][j][1] * scale;
      }
      const float e2x = f->vertex[2].x - f->vertex[0].x;
      const float e2y = f->vertex[2].y - f->vertex[0].y;
      const float e1x = f->vertex[1].x - f->vertex[0].x;
      const float e1y = f->vertex[1].y - f->vertex[0].y;
      // osdbits projects +camera Y down; LUNA projects it up. That
      // reflection reverses winding, so its cull!=0 NEAR set is >=0 here.
      f->nearFace = e2x * e1y - e2y * e1x >= 0.0f;
      const ConfigRodVertex *v = &f->vertex[0];
      const float length = sqrtf(v->cameraX * v->cameraX +
                                 v->cameraY * v->cameraY + v->z * v->z);
      f->fresnel = 1.0f - fabsf((v->cameraX * f->normalX +
                       v->cameraY * f->normalY + v->z * f->normalZ) / length);
      if (f->fresnel < 0.0f) f->fresnel = 0.0f;
      if (f->fresnel > 1.0f) f->fresnel = 1.0f;
    }
  }
}

static int configRodFaceSelected(const ConfigRodScene *scene, int piece,
                                 int face, int near) {
  // Split meshes omit caps at the cut, exactly MESH_LOWER / MESH_UPPER.
  if (scene->pieces == 2 && (piece ? face == 8 || face == 9 : face < 8))
    return 0;
  return scene->face[piece][face].nearFace == near;
}

static void configRefractRod(const ConfigRodScene *scene, int near, int extra,
                              int front) {
  for (int piece = 0; piece < scene->pieces; piece++) {
    const int lower = scene->pieces == 2 && piece == 0;
    const int red = lower ? 167 : front ? (0x2D + 167) / 2 : 0x2D;
    const int green = lower ? 217 : front ? (0x55 + 217) / 2 : 0x55;
    const int blue = lower ? 255 : front ? (0x66 + 255) / 2 : 0x66;
    const float size = lower ? 100.0f : front ? 200.0f : 160.0f;
    for (int i = 0; i < 16; i++) {
      if (!configRodFaceSelected(scene, piece, i, near)) continue;
      const ConfigRodFace *f = &scene->face[piece][i];
      const float fres = f->fresnel;
      float bright = size * 10.0f * fres * fres * fres * fres;
      // 0x22C4E0's cosine rolloff prevents fully edge-on faces flashing.
      if (fres > 0.9f)
        bright = (int)bright * (1.0f - cosf((1.0f - fres) *
                                           3.14159265359f / 0.1f)) * 0.5f;
      u64 *p = configPacket(10);
      p[0] = 276 | (fres > 0.99f ? 0 : 128) | (gsGlobal->PrimContext << 9);
      p[1] = GS_PRIM;
      p[2] = GS_SETREG_RGBA(clampColor(red + (int)bright + extra),
                            clampColor(green + (int)bright + extra),
                            clampColor(blue + (int)bright + extra), 0x80);
      p[3] = GS_RGBAQ;
      for (int j = 0; j < 4; j++) {
        const ConfigRodVertex *v = &f->vertex[j];
        float u = (v->x - gsGlobal->Width * 0.5f - scene->refX) * 0.95f +
                   scene->refX + gsGlobal->Width * 0.5f -
                   f->normalX * 1000.0f / v->z;
        float t = (v->y - gsGlobal->Height * 0.5f - scene->refY) * 0.95f +
                   scene->refY + gsGlobal->Height * 0.5f +
                   f->normalY * 500.0f / v->z;
        if (u < 0.0f) u = 0.0f;
        // Continuous UVs; hardware REPEAT, never modulo each vertex.
        u *= CONFIG_WORK_SIZE / (float)gsGlobal->Width;
        // Retail adds +256 before packing V. Keep an equivalent whole-
        // texture-period bias AFTER resampling: a negative V must not
        // become ~1024 at one vertex and stretch interpolation across the
        // 14-bit UV-register boundary. REPEAT removes this bias per pixel.
        t = t * configWorkTexture[0].Height / (float)gsGlobal->Height +
             configWorkTexture[0].Height;
        p[4 + j * 4] = GS_SETREG_UV(((int)(u * 16.0f)) & 0x3fff,
                                    ((int)(t * 16.0f)) & 0x3fff);
        p[5 + j * 4] = GS_UV;
        configEmitVertex(p + 6 + j * 4, v);
      }
    }
  }
}

static void configEmbossRod(const ConfigRodScene *scene, int near,
                             float offset, int strength) {
  for (int piece = 0; piece < scene->pieces; piece++) {
    const float tofs = piece ? 2.0f * scene->split : 0.0f;
    for (int i = 0; i < 16; i++) {
      if (!configRodFaceSelected(scene, piece, i, near)) continue;
      const ConfigRodFace *f = &scene->face[piece][i];
      const float phase = scene->index * 0.1f + i * 0.1f + offset;
      u64 *p = configPacket(13);
      p[0] = 84 | (gsGlobal->PrimContext << 9); // ST/Q, ABE on.
      p[1] = GS_PRIM;
      for (int j = 0; j < 4; j++) {
        const ConfigRodVertex *v = &f->vertex[j];
        const float q = 1.0f / v->z;
        const float s = (v->u + phase) * q;
        const float t = (v->v + phase + tofs) * q;
        p[2 + j * 6] = GS_SETREG_STQ(configFloatBits(s), configFloatBits(t));
        p[3 + j * 6] = GS_ST;
        p[4 + j * 6] = GS_SETREG_RGBAQ(strength, strength, strength, 0x80,
                                      configFloatBits(q));
        p[5 + j * 6] = GS_RGBAQ;
        configEmitVertex(p + 6 + j * 6, v);
      }
    }
  }
}

static void configReflectRod(const ConfigRodScene *scene, int front) {
  const int color = scene->pieces == 2 || !front ? 0x3C : 0x80;
  const int alpha = scene->pieces == 2 || !front ? 0x80 : 0x1E;
  for (int piece = 0; piece < scene->pieces; piece++) {
    for (int i = 0; i < 16; i++) {
      if (!configRodFaceSelected(scene, piece, i, 1)) continue;
      const ConfigRodFace *f = &scene->face[piece][i];
      u64 *p = configPacket(10);
      p[0] = 276 | (gsGlobal->PrimContext << 9); // TEXCFLOW, ABE/AA1 off.
      p[1] = GS_PRIM;
      p[2] = GS_SETREG_RGBA(color, color, color, alpha);
      p[3] = GS_RGBAQ;
      for (int j = 0; j < 4; j++) {
        const ConfigRodVertex *v = &f->vertex[j];
        const float length = sqrtf(v->cameraX * v->cameraX +
                                   v->cameraY * v->cameraY + v->z * v->z);
        const float dot = fabsf(2.0f * (v->cameraX * f->normalX +
                                 v->cameraY * f->normalY + v->z * f->normalZ) / length);
        // A whole TEXCFLOW period also keeps reflection UVs continuous if
        // rounded transforms put an otherwise zero coordinate just below 0.
        const float u = (v->cameraX / length + f->normalX * dot + 1.0f) * 32.0f + 64.0f;
        const float t = (v->cameraY / length + f->normalY * dot + 1.0f) * 16.0f + 64.0f;
        p[4 + j * 4] = GS_SETREG_UV(((int)(u * 16.0f)) & 0x3fff,
                                    ((int)(t * 16.0f)) & 0x3fff);
        p[5 + j * 4] = GS_UV;
        configEmitVertex(p + 6 + j * 4, v);
      }
    }
  }
}

static void drawBiosRod(const ConfigRodScene *scene, int front) {
  // 1: far refraction wb3 -> wb4 (opaque except AA1 coverage).
  configSetTarget(1);
  configSetTest(0);
  configBindTexture(&configWorkTexture[0], 1, 1);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  configRefractRod(scene, 0, 0, front);
  // 2/3: same TEXCBUMP, subtract at phase then add at phase - 0.008.
  configSetTest(1);
  configBindTexture(&bumpTexture, 1, 0);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(2, 0, 0, 1, 0), 0);
  configEmbossRod(scene, 0, 0.0f, 8);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
  configEmbossRod(scene, 0, -0.008f, 8);
  // 4: near refraction wb4 -> screen. No masked work-buffer composite.
  configSetTarget(-1);
  configSetTest(1);
  configBindTexture(&configWorkTexture[1], 1, 1);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  configRefractRod(scene, 1, 0, front);
  // 5: near refraction wb4 -> wb3, washed out by extra = 255.
  configSetTarget(0);
  configRefractRod(scene, 1, 255, front);
}

static void configBloom(const int *order) {
  // Deferred 0x2267E8: clear, render ALL rods, then composite, twice.
  for (int walk = 0; walk < 2; walk++) {
    configSetTarget(1);
    configSetTest(0);
    configClearWork();
    for (int n = 0; n < 12; n++) {
      const int slot = order[n];
      const ConfigRodScene *scene = &configRodScene[slot];
      configBindTexture(&configFlowTexture, 1, 0);
      configReflectRod(scene, slot == 0);
      // Split branch swaps BUMP/BINV relative to the plain branch.
      const int inverse = scene->pieces == 2 ? walk != 0 : walk == 0;
      configBindTexture(inverse ? &configInverseTexture : &bumpTexture, 1, 0);
      gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(2, 0, 0, 1, 0), 0);
      configEmbossRod(scene, 1, walk ? -0.008f : 0.0f, 0x28);
    }
    configSetTarget(-1);
    gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
    configBlit(&configWorkTexture[1], GS_SETREG_RGBA(0x80, 0x80, 0x80, 30), 1);
  }
}

static void configRestoreState(void) {
  configSetTarget(-1);
  configSetTest(0);
  // Return a clean depth plane to LUNA's 2D UI without changing colour.
  u64 *p = configPacket(1);
  p[0] = GS_SETREG_FRAME(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer] / 8192,
                         gsGlobal->Width / 64, gsGlobal->PSM, 0xffffffffU);
  p[1] = gsGlobal->PrimContext ? GS_FRAME_2 : GS_FRAME_1;
  const int alpha = gsGlobal->PrimAlphaEnable;
  gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Height, 0, 0);
  gsGlobal->PrimAlphaEnable = alpha;
  configSetTarget(-1);
  const GSTEST *test = gsGlobal->Test;
  const GSCLAMP *clamp = gsGlobal->Clamp;
  p = configPacket(3);
  p[0] = GS_SETREG_TEST(test->ATE, test->ATST, test->AREF, test->AFAIL,
                       test->DATE, test->DATM, test->ZTE, test->ZTST);
  p[1] = gsGlobal->PrimContext ? GS_TEST_2 : GS_TEST_1;
  p[2] = GS_SETREG_CLAMP(clamp->WMS, clamp->WMT, clamp->MINU, clamp->MAXU,
                        clamp->MINV, clamp->MAXV);
  p[3] = gsGlobal->PrimContext ? GS_CLAMP_2 : GS_CLAMP_1;
  p[4] = GS_SETREG_ZBUF(gsGlobal->ZBuffer / 8192, gsGlobal->PSMZ & 15,
                       !gsGlobal->ZBuffering);
  p[5] = gsGlobal->PrimContext ? GS_ZBUF_2 : GS_ZBUF_1;
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void drawBiosSystemConfiguration(uint32_t frameNowMs) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  if (!configClockInitialized) {
    const uint32_t stamp = getTimestamp();
    const uint32_t hour = (stamp >> 12) & 31U;
    const uint32_t minute = (stamp >> 6) & 63U;
    const uint32_t second = stamp & 63U;
    configClockStartMs = ((hour < 24 ? hour : 0) * 3600U +
                          (minute < 60 ? minute : 0) * 60U +
                          (second < 60 ? second : 0)) * 1000U;
    configClockAnchorMs = frameNowMs;
    configClockInitialized = 1;
  }
  const uint32_t elapsedMs = frameNowMs - configClockAnchorMs;
  const uint32_t clockMs = configClockStartMs + elapsedMs;
  const uint32_t hour = (clockMs / 3600000U) % 12U;
  const uint32_t orbit = (uint32_t)(((uint64_t)(clockMs % 60000U) << 16) / 60000U);
  const uint32_t tilt = hour * (65536U / 12U);
  const uint32_t spin = orbit * 4U;
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 0,
                    GS_SETREG_RGBA(0, 0, 0, 0x80));
  drawBiosTunnel(elapsedMs, width / 2, height / 2);
  // MenuBackdrop (0x21D0A0): both pages capture the un-tinted wall ONCE,
  // before objects, then wb3 is modulated back onto the visible screen.
  GSTEXTURE screen = {0};
  screen.Width = width;
  screen.Height = height;
  screen.PSM = gsGlobal->PSM;
  screen.TBW = width / 64;
  screen.Vram = gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer];
  configSetTest(0);
  for (int work = 0; work < 2; work++) {
    configSetTarget(work);
    configBlit(&screen, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80), 0);
  }
  configSetTarget(-1);
  configBlit(&configWorkTexture[0], GS_SETREG_RGBA(0x37, 0x28, 0x3C, 0x80), 0);
  // Source CarouselClock/CarouselColors drive the front rod's split using
  // 1 - minutes/60 (the overview prose differs; the implementation wins).
  const uint32_t splitFrame = (uint32_t)((uint64_t)elapsedMs * 60U / 1000U);
  if (configSplitHour != (int)hour) {
    configFrontSplit = 0.0f;
    configSplitHour = hour;
    configSplitFrame = splitFrame;
  }
  const float splitMax = 1.0f - (clockMs % 3600000U) / 60000.0f / 60.0f;
  configFrontSplit += (splitFrame - configSplitFrame) * 0.004f;
  if (configFrontSplit > splitMax) configFrontSplit = splitMax;
  configSplitFrame = splitFrame;
  int order[12];
  float depth[12];
  for (int i = 0; i < 12; i++) {
    order[i] = i;
    depth[i] = configRodDepth(i, tilt, orbit, spin);
    configBuildRod(&configRodScene[i], i, (i + hour) % 12U,
                    tilt, orbit, spin, i == 0 ? configFrontSplit : 0.0f);
  }
  for (int i = 0; i < 11; i++) {
    for (int j = i + 1; j < 12; j++) {
      if (depth[order[i]] < depth[order[j]]) {
        const int swap = order[i];
        order[i] = order[j];
        order[j] = swap;
      }
    }
  }
  for (int i = 0; i < 12; i++)
    drawBiosRod(&configRodScene[order[i]], order[i] == 0);
  configSetTarget(-1);
  // The orb renderer owns its test/blend setup; start it from LUNA's state.
  configRestoreState();
  drawAmbientOrbsSystemConfig(clockMs, frameNowMs, elapsedMs, 2);
  configSetTest(0);
  configBloom(order);
  configZoomBlur(&screen);
  configRestoreState();
}

void drawSharedLibraryBackground(uint32_t frameNowMs) {
  if (libraryBackground == LIBRARY_BACKGROUND_RED_CLOUDS)
    drawRedClouds(frameNowMs);
  else if (libraryBackground == LIBRARY_BACKGROUND_MIDNIGHT_CUBES)
    drawMidnightCubes(frameNowMs);
  else if (libraryBackground == LIBRARY_BACKGROUND_SYSTEM_CONFIG)
    drawBiosSystemConfiguration(frameNowMs);
  else if (!drawAmbientOrbsBackground(frameNowMs))
    drawGlassBackground(frameNowMs);
}

int orbsVisualCacheIndex(int flowOffset) {
  int closest = ORBS_LOGO_CACHE_FOCUS;
  int closestDistance = 0x7FFFFFFF;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++) {
    int position = (i - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = position < 0 ? -position : position;
    if (distance < closestDistance) {
      closest = i;
      closestDistance = distance;
    }
  }
  return closest;
}

static void drawOrbsLogo(GSTEXTURE *texture, float x, float y,
                         float width, float height, int brightness) {
  int previousAlphaTest = gsGlobal->Test->ATST;
  int previousAlphaReference = gsGlobal->Test->AREF;
  int previousAlphaFail = gsGlobal->Test->AFAIL;
  gsKit_TexManager_bind(gsGlobal, texture);
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 2;
  gsGlobal->Test->AREF = 0x80;
  gsGlobal->Test->AFAIL = 0;
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_prim_sprite_texture(gsGlobal, texture, x, y, 0.0f, 0.0f,
                            x + width, y + height,
                            texture->Width - 1, texture->Height - 1, 6,
                            GS_SETREG_RGBA(brightness, brightness, brightness, 0x80));
  gsGlobal->Test->ATST = previousAlphaTest;
  gsGlobal->Test->AREF = previousAlphaReference;
  gsGlobal->Test->AFAIL = previousAlphaFail;
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

static void scrollWheelGeometry(int position, int centerY, int listHeight,
                                int logoCenterX, int entryOffset,
                                float *x, float *y, float *width,
                                float *height) {
  const int distance = abs(position);
  const float steps = distance / 1000.0f;
  const float scale = 1.0f / (1.0f + 0.20f * steps * steps);
  *width = 232.0f * scale;
  *height = 76.0f * scale;
  *x = logoCenterX + entryOffset + 12.0f * steps * steps;
  *y = centerY + position * (listHeight * 0.29f) /
                   (1000.0f + distance * 0.23f);
}

void drawOrbsView(TargetList *titles, int selectedTitleIdx,
                  int flowOffset, int visualFocus, int fastScroll,
                  int entryProgress, uint32_t now,
                  const char *nextViewLabel) {
  const int width = gsGlobal->Width;
  const int height = gsGlobal->Height;
  const int listTop = headerHeight + 12;
  const int listBottom = height - footerHeight - 12;
  const int centerY = (listTop + listBottom) / 2;
  const int logoCenterX = width - 153;
  const int logoEntryOffset = (1000 - entryProgress) * 72 / 1000;
  const int logoEntryBrightness = 350 + entryProgress * 650 / 1000;
  const int visualTitleIdx = lunaNavWrap(titles->total,
      selectedTitleIdx + visualFocus - ORBS_LOGO_CACHE_FOCUS);
  const uint32_t elapsedMs = glassElapsedMs(now);
  char title[255];

  drawSharedLibraryBackground(now);

  // Keep the orbit visible and make a quiet area for the logos.
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_prim_sprite(gsGlobal, 0, 0, width, height, 1,
                    glassPresetColor(0x00, 0x02, 0x0C, 0x20));
  gsKit_prim_quad_gouraud(gsGlobal, width - 350, 0, width, 0,
                          width - 350, height, width, height, 2,
                          glassPresetColor(0x02, 0x06, 0x12, 0x08),
                          glassPresetColor(0x02, 0x06, 0x12, 0x72),
                          glassPresetColor(0x02, 0x06, 0x12, 0x08),
                          glassPresetColor(0x02, 0x06, 0x12, 0x72));
  gsKit_prim_sprite(gsGlobal, 0, 0, width, headerHeight + 3, 3,
                    glassPresetColor(0x02, 0x05, 0x10, 0x4A));
  gsKit_prim_sprite(gsGlobal, 0, height - footerHeight, width, height, 3,
                    glassPresetColor(0x02, 0x05, 0x10, 0x62));

  drawTextWindow(keepoutArea + 10, headerHeight - getFontLineHeight(),
                 width - keepoutArea, 0, 7, FontMainColor, ALIGN_LEFT, "SCROLL");
  snprintf(lineBuffer, sizeof(lineBuffer), "%d/%d", visualTitleIdx + 1,
           titles->total);
  drawTextWindow(width - 116, headerHeight - getFontLineHeight(),
                 width - keepoutArea - 8, 0, 7, FontMainColor,
                 ALIGN_RIGHT, lineBuffer);

  drawAmbientOrbsScroll(width * 27 / 100, centerY, width * 20 / 100,
                        (listBottom - listTop) * 36 / 100, elapsedMs,
                        fastScroll, getTargetByIdx(titles, visualTitleIdx)->name,
                        5, width - 290);

  // Draw the far side of the wheel first so nearer titles stay in front.
  int wheelOrder[ORBS_LOGO_CACHE_COUNT];
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT; i++)
    wheelOrder[i] = i;
  for (int i = 0; i < ORBS_LOGO_CACHE_COUNT - 1; i++) {
    for (int j = i + 1; j < ORBS_LOGO_CACHE_COUNT; j++) {
      int a = (wheelOrder[i] - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
      int b = (wheelOrder[j] - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
      if (abs(a) < abs(b)) {
        int swap = wheelOrder[i];
        wheelOrder[i] = wheelOrder[j];
        wheelOrder[j] = swap;
      }
    }
  }

  for (int order = 0; order < ORBS_LOGO_CACHE_COUNT; order++) {
    int i = wheelOrder[order];
    int position = (i - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
    int distance = abs(position);
    int targetIdx = lunaNavWrap(titles->total,
        selectedTitleIdx + i - ORBS_LOGO_CACHE_FOCUS);
    int duplicate = 0;
    float steps;
    float rowX;
    float logoWidth;
    float logoHeight;
    float rowY;
    int brightness;
    if (distance >= 2300)
      continue;
    for (int j = 0; j < ORBS_LOGO_CACHE_COUNT; j++) {
      int otherPosition = (j - ORBS_LOGO_CACHE_FOCUS) * 1000 + flowOffset;
      int otherDistance = otherPosition < 0 ? -otherPosition : otherPosition;
      int otherTarget = lunaNavWrap(titles->total,
          selectedTitleIdx + j - ORBS_LOGO_CACHE_FOCUS);
      if (j != i && otherTarget == targetIdx &&
          (otherDistance < distance || (otherDistance == distance && j < i))) {
        duplicate = 1;
        break;
      }
    }
    if (duplicate)
      continue;
    steps = distance / 1000.0f;
    scrollWheelGeometry(position, centerY, listBottom - listTop,
                        logoCenterX, logoEntryOffset,
                        &rowX, &rowY, &logoWidth, &logoHeight);
    brightness = 0x80 - (int)(17.0f * steps * steps);
    if (distance > 2000)
      brightness = brightness * (2300 - distance) / 300;
    brightness = brightness * logoEntryBrightness / 1000;
    if (orbsLogoLoaded[i] && !fastScroll)
      drawOrbsLogo(orbsLogoTextures[i], rowX - logoWidth / 2.0f,
                   rowY - logoHeight / 2.0f, logoWidth, logoHeight,
                   brightness);
    else {
      int textWidth = (int)logoWidth - 20;
      formatPSBBNTitle(getTargetByIdx(titles, targetIdx)->name, title, textWidth);
      drawTextWindow((int)(rowX - logoWidth / 2.0f),
                     (int)rowY - getFontLineHeight() / 2,
                     (int)(rowX + logoWidth / 2.0f),
                     (int)rowY + getFontLineHeight() / 2, 6,
                     glassPresetColor(0xC8 * brightness / 0x80,
                                      0xD4 * brightness / 0x80,
                                      0xE8 * brightness / 0x80, 0x80),
                     ALIGN_CENTER, title);
    }
  }


}
