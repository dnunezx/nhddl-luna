#ifndef LUNA_HOMEBREW_ELF_H
#define LUNA_HOMEBREW_ELF_H
#include <stddef.h>
#include <stdint.h>

#define LUNA_HOMEBREW_SEGMENTS 32
#define LUNA_HOMEBREW_FILE_MAX (16U * 1024U * 1024U)
#define LUNA_HOMEBREW_ELF_HEADER_SIZE 52
typedef struct {
  uint32_t offset, address, files, memory;
} LunaHomebrewSegment;
typedef struct {
  uint32_t entry, gp, count;
  LunaHomebrewSegment segments[LUNA_HOMEBREW_SEGMENTS];
} LunaHomebrewElf;

int lunaHomebrewElfHeaderValid(const void *data, size_t size);
// The header must already have passed lunaHomebrewElfHeaderValid.
int lunaHomebrewElfProgramTableFits(const void *data, size_t fileSize);
// Validate a static little-endian MIPS ELF, then order copies so a destination
// never destroys another segment's staged source. BSS is cleared in a second pass.
// Error strings are English translation keys; output is unchanged on failure.
const char *lunaHomebrewCheck(const void *data, size_t size, uint32_t staging,
                             uint32_t memorySize, LunaHomebrewElf *output);
#endif
