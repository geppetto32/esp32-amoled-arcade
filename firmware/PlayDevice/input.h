// input.h - debounced buttons with edge detection and key repeat.
#pragma once
#include <stdint.h>

enum Button : uint8_t { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_START, BTN_SELECT, BTN_COUNT };

void     inputInit();
void     inputUpdate();                 // call once per tick, before game update
bool     btnHeld(Button b);             // currently down
bool     btnPressed(Button b);          // went down this tick
bool     btnReleased(Button b);         // went up this tick
bool     btnRepeat(Button b);           // true on press, then auto-repeats (D-pad movement)
uint32_t btnHeldMs(Button b);           // how long it has been down (0 if up)
