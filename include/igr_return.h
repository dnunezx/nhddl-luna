#ifndef LUNA_IGR_RETURN_H
#define LUNA_IGR_RETURN_H

// Neutrino's return buffer is 128 bytes including its terminator.
#define LUNA_IGR_PATH_SIZE 128

void lunaIgrConfigure(const char *directory, int commandLineOverride);
int lunaIgrDeviceSupported(const char *path);
const char *lunaIgrCustomPath(void);
const char *lunaIgrReturnPath(void);
// An empty path restores the configured destination. Returns a translation key on error.
const char *lunaIgrSavePath(const char *path);

#endif
