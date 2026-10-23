#include "input.h"
#include "config.h"

namespace {
const uint8_t kPins[BTN_COUNT] = {PIN_BTN_UP, PIN_BTN_DOWN, PIN_BTN_LEFT, PIN_BTN_RIGHT,
                                  PIN_BTN_A,  PIN_BTN_B,    PIN_BTN_START, PIN_BTN_SELECT};
// Optional second pin that acts as the same button (onboard buttons).
const int8_t kAlias[BTN_COUNT] = {-1, -1, -1, -1, -1, -1, PIN_USER_BTN, PIN_BOOT_BTN};

const uint32_t DEBOUNCE_MS = 12;
const uint32_t REPEAT_DELAY_MS = 170;
const uint32_t REPEAT_RATE_MS = 55;

struct BtnState {
  bool raw, stable, pressed, released, repeat;
  uint32_t rawSince, downSince, nextRepeat;
};
BtnState st[BTN_COUNT];

bool readPhysical(uint8_t i) {
  bool down = digitalRead(kPins[i]) == LOW;
  if (kAlias[i] >= 0 && digitalRead(kAlias[i]) == LOW) down = true;
  return down;
}
}  // namespace

void inputInit() {
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    pinMode(kPins[i], INPUT_PULLUP);
    if (kAlias[i] >= 0) pinMode(kAlias[i], INPUT_PULLUP);
    st[i] = BtnState{};
  }
}

void inputUpdate() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < BTN_COUNT; i++) {
    BtnState& s = st[i];
    bool r = readPhysical(i);
    if (r != s.raw) { s.raw = r; s.rawSince = now; }
    s.pressed = s.released = s.repeat = false;
    if (s.raw != s.stable && (now - s.rawSince) >= DEBOUNCE_MS) {
      s.stable = s.raw;
      if (s.stable) { s.pressed = true; s.downSince = now; s.nextRepeat = now + REPEAT_DELAY_MS; }
      else s.released = true;
    }
    if (s.pressed) s.repeat = true;
    else if (s.stable && now >= s.nextRepeat) { s.repeat = true; s.nextRepeat = now + REPEAT_RATE_MS; }
  }
}

bool btnHeld(Button b)     { return st[b].stable; }
bool btnPressed(Button b)  { return st[b].pressed; }
bool btnReleased(Button b) { return st[b].released; }
bool btnRepeat(Button b)   { return st[b].repeat; }
uint32_t btnHeldMs(Button b) { return st[b].stable ? millis() - st[b].downSince : 0; }
