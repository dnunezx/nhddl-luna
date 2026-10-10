// Original LUNA PSXCore staged transfer, 2026.
#ifndef LUNA_PSXCORE_LAUNCH_H
#define LUNA_PSXCORE_LAUNCH_H
#include "psxcore_elf.h"
#include <stddef.h>
#include <stdint.h>
struct Target;
struct LunaPsxVmcSettings;
#define LUNA_PSX_STAGE 0x01000000u
#define LUNA_PSX_CAPACITY 0x00400000u
#define LUNA_PSX_CONTROL 0x01401000u
#define LUNA_PSX_STUB 0x01402000u
#define LUNA_PSX_STACK 0x01410000u
#define LUNA_PSX_MAX_ARGS 24u
#define LUNA_PSX_ARGUMENT_BYTES 1024u
typedef struct {
    NpElf elf;
    uint32_t argc, argv[LUNA_PSX_MAX_ARGS];
    char strings[LUNA_PSX_ARGUMENT_BYTES];
} LunaPsxTransfer;
const char *lunaPsxBootstrapCheck(const void *bytes,size_t size,NpElf *out);
// Standard argv transport only; PSXCore owns its launch protocol and storage.
// argumentCount excludes argv[0], which is the bootstrap path.
const char *lunaPsxArgumentsCheck(const char *bootstrap,unsigned argumentCount,
    const char *const arguments[],LunaPsxTransfer *out);
// Prepares everything while the library is usable; no worker shutdown on error.
const char *lunaPsxPrepare(const char *bootstrap,unsigned argumentCount,
    const char *const arguments[]);
void lunaPsxExecute(void) __attribute__((noreturn));
// 0: staged, 1: missing cards require explicit creation, -1: preparation error.
int lunaPsxPrepareTitle(struct Target *target, int createCards, char *error, size_t size,
                        int (*progress)(uint64_t completed, uint64_t total));
int lunaPsxPrepareTitleCards(struct Target *target, const struct LunaPsxVmcSettings *selection,
    int createCards, char *error, size_t size,
    int (*progress)(uint64_t completed, uint64_t total));
int lunaPsxPrepareTitleCardsAutomatic(struct Target *target, const struct LunaPsxVmcSettings *selection,
    int createCards, char *error, size_t size,
    int (*progress)(uint64_t completed, uint64_t total),
    int (*choose)(const char *const *labels, unsigned count));
#ifdef LUNA_PSXCORE_DEVELOPMENT
// Local HostFS file: bootstrap path, one CLI argument per line, optional final
// "auto" line. Without auto, Select+Start invokes this entry from the library.
void lunaPsxDevelopment(int automatic);
#endif
#endif
