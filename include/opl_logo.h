#ifndef LUNA_OPL_LOGO_H
#define LUNA_OPL_LOGO_H

// Check the mounted disc and BIOS before handing off to rom0:PS2LOGO.
// An unavailable or incompatible logo simply falls back to the game ELF.
int oplPrepareLogo(int discFd);

#endif
