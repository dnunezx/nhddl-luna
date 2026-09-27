#include "ui/pad.h"
#include <kernel.h>
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned char padBuffer[2][256] ALIGNED(64);
static unsigned int prevInputs[2] = {0, 0};
static unsigned char scrollStickCentered[2] = {0, 0};

// Initializes gamepad input driver
void initPad() {
  padInit(0);
  padPortOpen(0, 0, padBuffer[0]);
  padPortOpen(1, 0, padBuffer[1]);

  prevInputs[0] = 0;
  prevInputs[1] = 0;
  scrollStickCentered[0] = 0;
  scrollStickCentered[1] = 0;
}

// Closes gamepad gamepad input driver
void closePad() {
  padPortClose(0, 0);
  padPortClose(1, 0);
  padEnd();
}

// Polls the gamepad and returns only changed inputs
int readPad(int port, int slot) {
  struct padButtonStatus buttons;
  uint32_t curInput, padData;

  curInput = 0;
  if (padRead(port, slot, &buttons) != 0) {
    padData = 0xffff ^ buttons.btns;

    curInput = padData & ~prevInputs[port];
    prevInputs[port] = padData;
  }

  return curInput;
}

// Polls the gamepad and returns currently pressed buttons
int pollPad(int port, int slot) {
  struct padButtonStatus buttons;
  if (padRead(port, slot, &buttons) != 0) {
    prevInputs[port] = 0xffff ^ buttons.btns;
    return prevInputs[port];
  }

  return 0;
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

int pollScrollInput(void) {
  int input = pollInput();
  for (int port = 0; port < 2; port++) {
    struct padButtonStatus buttons;
    if (padRead(port, 0, &buttons) == 0) {
      scrollStickCentered[port] = 0;
      continue;
    }
    // Digital pads leave the stick bytes at zero, which looks like a held
    // up-left stick unless the active pad mode is checked first.
    const int mode = padInfoMode(port, 0, PAD_MODECURID, 0);
    if (mode != PAD_TYPE_ANALOG && mode != PAD_TYPE_DUALSHOCK) {
      scrollStickCentered[port] = 0;
      continue;
    }
    const int horizontal = (int)buttons.ljoy_h - 128;
    const int vertical = (int)buttons.ljoy_v - 128;
    const int absHorizontal = horizontal < 0 ? -horizontal : horizontal;
    const int absVertical = vertical < 0 ? -vertical : vertical;
    if (absHorizontal < 24 && absVertical < 24)
      scrollStickCentered[port] = 1;
    if (!scrollStickCentered[port])
      continue;
    if (absVertical >= absHorizontal && absVertical >= 64)
      input |= vertical < 0 ? PAD_UP : PAD_DOWN;
    else if (absHorizontal >= 64)
      input |= horizontal < 0 ? PAD_LEFT : PAD_RIGHT;
  }
  return input;
}
