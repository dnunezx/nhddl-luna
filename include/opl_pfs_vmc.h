// Original LUNA code: Danny Nunez (dnunezx) 2026
#ifndef LUNA_OPL_PFS_VMC_H
#define LUNA_OPL_PFS_VMC_H
#include <stdint.h>
#include <usbhdfsd-common.h>
// Derived from the existing Neutrino PFS mapper; verifies each extent before handoff.
int pfs_vmc_map_file(const char *path, const char *partition,
                     bd_fragment_t *extents, uint32_t *fragment_count, uint64_t *file_size);
#endif
