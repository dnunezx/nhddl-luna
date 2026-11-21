#include "homebrew_elf.h"
#include <string.h>

static uint16_t read16(const unsigned char *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}
static uint32_t read32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
      (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int overlaps(uint32_t a, uint32_t sizeA, uint32_t b, uint32_t sizeB) {
  return sizeA && sizeB && a < b + sizeB && b < a + sizeA;
}

int lunaHomebrewElfHeaderValid(const void *data, size_t size) {
  const unsigned char *p = data;
  return p && size >= LUNA_HOMEBREW_ELF_HEADER_SIZE && !memcmp(p, "\177ELF", 4) &&
      p[4] == 1 && p[5] == 1 && p[6] == 1 && read16(p + 16) == 2 &&
      read16(p + 18) == 8 && read32(p + 20) == 1 &&
      read16(p + 40) == LUNA_HOMEBREW_ELF_HEADER_SIZE;
}

int lunaHomebrewElfProgramTableFits(const void *data, size_t fileSize) {
  const unsigned char *p = data;
  uint32_t headers = read32(p + 28), count = read16(p + 44);
  return count && count <= 128 && read16(p + 42) == 32 &&
      headers >= LUNA_HOMEBREW_ELF_HEADER_SIZE && headers <= fileSize &&
      count <= (fileSize - headers) / 32;
}

const char *lunaHomebrewCheck(const void *data, size_t size, uint32_t staging,
                             uint32_t memorySize, LunaHomebrewElf *output) {
  const unsigned char *p = data;
  if (!output || !lunaHomebrewElfHeaderValid(p, size))
    return "Not a supported PS2 ELF executable.";
  if (memorySize > 0x02000000U || memorySize <= 0x00100000U ||
      size > LUNA_HOMEBREW_FILE_MAX || staging < 0x00100000U ||
      staging >= memorySize || size > memorySize - staging)
    return "The ELF staging buffer is outside available memory.";
  if (!lunaHomebrewElfProgramTableFits(p, size))
    return "The ELF program headers are invalid.";
  uint32_t headers = read32(p + 28), count = read16(p + 44);
  LunaHomebrewElf result = {0};
  result.entry = read32(p + 24);
  int executable = 0;
  if (result.entry & 3) return "The ELF entry point is invalid.";
  for (uint32_t i = 0; i < count; i++) {
    const unsigned char *h = p + headers + i * 32;
    uint32_t type = read32(h), offset = read32(h + 4), address = read32(h + 8);
    uint32_t files = read32(h + 16), memory = read32(h + 20);
    uint32_t flags = read32(h + 24), alignment = read32(h + 28);
    if (!type) continue;
    if (offset > size || files > size - offset)
      return "The ELF contains truncated segment data.";
    if (type == 0x70000000U) { // PT_MIPS_REGINFO
      if (files < 24) return "The ELF register information is invalid.";
      result.gp = read32(p + offset + 20);
      continue;
    }
    if (type == 4 || type == 6 || type == 0x70000003U || type == 0x6474e551U)
      continue; // Notes, program headers, ABI flags, and stack attributes.
    if (type != 1) return "This ELF requires an unsupported dynamic loader.";
    if (!memory) { if (files) return "The ELF segment sizes are invalid."; continue; }
    if (files > memory || address < 0x00100000U || address >= memorySize ||
        memory > memorySize - address || (flags & ~7U))
      return "The ELF loads outside PS2 application memory.";
    if (alignment > 1 && ((alignment & (alignment - 1)) ||
        (address & (alignment - 1)) != (offset & (alignment - 1))))
      return "The ELF segment alignment is invalid.";
    if (result.count == LUNA_HOMEBREW_SEGMENTS)
      return "The ELF has too many loadable segments.";
    for (uint32_t j = 0; j < result.count; j++)
      if (overlaps(address, memory, result.segments[j].address, result.segments[j].memory))
        return "The ELF loadable segments overlap.";
    result.segments[result.count++] = (LunaHomebrewSegment){offset, address, files, memory};
    if ((flags & 1) && result.entry >= address && result.entry - address < files)
      executable = 1;
  }
  if (!result.count || !executable) return "The ELF entry point is not executable.";
  if (result.gp && (result.gp < 0x00100000U || result.gp >= memorySize))
    return "The ELF global pointer is invalid.";

  LunaHomebrewSegment ordered[LUNA_HOMEBREW_SEGMENTS];
  unsigned char copied[LUNA_HOMEBREW_SEGMENTS] = {0};
  for (uint32_t next = 0; next < result.count; next++) {
    int found = -1;
    for (uint32_t i = 0; i < result.count && found < 0; i++) {
      if (copied[i]) continue;
      int blocked = 0;
      for (uint32_t j = 0; j < result.count; j++)
        if (i != j && !copied[j] && overlaps(result.segments[i].address,
            result.segments[i].files, staging + result.segments[j].offset,
            result.segments[j].files)) { blocked = 1; break; }
      if (!blocked) found = (int)i;
    }
    if (found < 0) return "Not enough separate memory to stage this ELF.";
    ordered[next] = result.segments[found];
    copied[found] = 1;
  }
  memcpy(result.segments, ordered, result.count * sizeof(ordered[0]));
  *output = result;
  return NULL;
}
