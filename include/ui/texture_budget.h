#ifndef LUNA_TEXTURE_BUDGET_H
#define LUNA_TEXTURE_BUDGET_H
#include <gsKit.h>
#include <stddef.h>

size_t texturePoolCapacity(const GSGLOBAL *gs);
// Fresh, privately owned RGBA pixels only. Returns 0 unchanged, 1 resized,
// or -1 on failure; failed fits leave the caller's pixels untouched.
int fitTextureToVram(const GSGLOBAL *gs, GSTEXTURE *texture);
// Refuse impossible allocations before entering gsKit's eviction loop.
// Returns 1 on a safe bind, 0 if the texture must be omitted.
int bindTextureSafe(GSGLOBAL *gs, GSTEXTURE *texture);
#endif
