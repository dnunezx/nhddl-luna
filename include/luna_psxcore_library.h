// Original LUNA PS1 library metadata reader, 2026.
#ifndef LUNA_PSXCORE_LIBRARY_H
#define LUNA_PSXCORE_LIBRARY_H
#include <stddef.h>
// Structural VCD validation and bounded SYSTEM.CNF metadata for artwork only.
// PSXCore independently measures and validates the selected disc at launch.
const char *lunaPsxDiscInfo(const char *path, char titleID[12]);
#endif
