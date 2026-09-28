#include "ui/pad.h"
#include <kernel.h>
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned char padBuffer[2][256] ALIGNED(64);
static unsigned int prevInputs[2] = {0, 0};
static unsigned char stickCentered[2] = {0, 0};
static unsigned char analogModeChecked[2] = {0, 0};

static int samplePad(int port, int slot, unsigned int *sampled) {
  struct padButtonStatus buttons;
  int state = padGetState(port, slot);
  if (state == PAD_STATE_DISCONN) {
    prevInputs[port] = 0;
    stickCentered[port] = 0;
    analogModeChecked[port] = 0;
  }
  if (padRead(port, slot, &buttons) == 0)
    return 0;

  unsigned int input = 0xffff ^ buttons.btns;
  int mode = padInfoMode(port, slot, PAD_MODECURID, 0);
  if (!analogModeChecked[port] &&
      (state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1) && mode > 0) {
    // Configure DualShock pads once per connection without blocking startup.
    if (mode == PAD_TYPE_DIGITAL) {
      int modes = padInfoMode(port, slot, PAD_MODETABLE, -1);
      for (int i = 0; i < modes; i++) {
        if (padInfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK) {
          padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_UNLOCK);
          break;
        }
      }
    }
    analogModeChecked[port] = 1;
  }

  // Digital pads leave the stick bytes at zero, which would look like up-left.
  if (mode != PAD_TYPE_ANALOG && mode != PAD_TYPE_DUALSHOCK) {
    stickCentered[port] = 0;
    *sampled = input;
    return 1;
  }

  int horizontal = (int)buttons.ljoy_h - 128;
  int vertical = (int)buttons.ljoy_v - 128;
  int absHorizontal = horizontal < 0 ? -horizontal : horizontal;
  int absVertical = vertical < 0 ? -vertical : vertical;
  if (absHorizontal < 24 && absVertical < 24)
    stickCentered[port] = 1;
  if (stickCentered[port] && !(input & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))) {
    if (absVertical >= absHorizontal && absVertical >= 64)
      input |= vertical < 0 ? PAD_UP : PAD_DOWN;
    else if (absHorizontal >= 64)
      input |= horizontal < 0 ? PAD_LEFT : PAD_RIGHT;
  }
  *sampled = input;
  return 1;
}

// Initializes gamepad input driver
void initPad() {
  padInit(0);
  padPortOpen(0, 0, padBuffer[0]);
  padPortOpen(1, 0, padBuffer[1]);

  prevInputs[0] = 0;
  prevInputs[1] = 0;
  stickCentered[0] = 0;
  stickCentered[1] = 0;
  analogModeChecked[0] = 0;
  analogModeChecked[1] = 0;
}

// Closes gamepad gamepad input driver
void closePad() {
  padPortClose(0, 0);
  padPortClose(1, 0);
  padEnd();
}

// Polls the gamepad and returns only changed inputs
int readPad(int port, int slot) {
  unsigned int input;
  if (!samplePad(port, slot, &input))
    return 0;
  unsigned int pressed = input & ~prevInputs[port];
  prevInputs[port] = input;
  return pressed;
}

// Polls the gamepad and returns currently pressed buttons
int pollPad(int port, int slot) {
  unsigned int input;
  if (!samplePad(port, slot, &input))
    return 0;
  prevInputs[port] = input;
  return input;
}

// Blocks until input changes on any of the two gamepads.
// To capture press of any button, pass -1.
int waitForInput(int button) {
  int curInputs;
  while (1) {
    curInputs = (readPad(0, 0) | readPad(1, 0));
    if (curInputs & button)
      return curInputs;
    usleep(1000);
  }
}

int readInput(void) { return readPad(0, 0) | readPad(1, 0); }

// Returns inputs on both gamepads
int pollInput() { return (pollPad(0, 0) | pollPad(1, 0)); }
