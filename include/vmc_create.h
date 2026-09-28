#ifndef LUNA_VMC_CREATE_H
#define LUNA_VMC_CREATE_H

// Creates a formatted, raw 8 MiB PS2 card image. Never replaces an existing file.
// Returns 0 on success, -1 on failure. A partial new image is removed on failure.
int lunaCreateVMC8(const char *path, void (*progress)(int percent, void *context),
                   void *context);

#endif
