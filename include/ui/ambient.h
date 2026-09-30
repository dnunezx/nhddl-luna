#ifndef LUNA_UI_AMBIENT_H
#define LUNA_UI_AMBIENT_H

// Starts the packaged memory-card soundtrack after storage modules initialize.
// Missing audio must never prevent the library from booting.
void ambientStart(int enabled);
void ambientSetEnabled(int enabled);
void ambientStop(void);
// Call only after ambientStop, when resetting the IOP.
void ambientForgetDrivers(void);

#endif
