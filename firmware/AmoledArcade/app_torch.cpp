// app_torch.cpp - TORCH: full-screen white (or red / warm night light) at the brightness you choose, screen never dims.
//   LEFT / RIGHT = dimmer / brighter (10..100 %, hold = repeat), tap = next colour (option COLOR sets the start colour).
//   Gamepad: stick up/down = brightness, A = next colour. Tap BOOT 3 times = back (the launcher restores your brightness).
#include "app_common.h"

static const char *const TC_COL[] = {"WHITE", "RED", "WARM"};
static Option tcOpts[] = {{"COLOR", TC_COL, 3, "tc_color", 0}};

static int tcPct = 100;
static uint32_t tcShowUntil, tcLastDraw;
static bool tcNeed;
static Repeat tcRepL, tcRepR, tcRepU;

static uint16_t tcColor() {
  switch (tcOpts[0].value) {
    case 1: return rgb(255, 0, 0);
    case 2: return rgb(255, 196, 120);
    default: return 0xFFFF;
  }
}

static void tcSetPct(int v, uint32_t now) {
  v = constrain(v, 10, 100);
  if (v != tcPct) { tcPct = v; prefs.putUChar("tc_br", (uint8_t)tcPct); applyBrightness(tcPct); }
  tcShowUntil = now + 1500;
  tcNeed = true;
}

static void tcStart() {
  tcPct = constrain((int)prefs.getUChar("tc_br", 100), 10, 100);
  applyBrightness(tcPct);
  tcShowUntil = millis() + 2500;
  tcNeed = true;
  tcLastDraw = 0;
}

static bool tcFrame(uint32_t now) {
  keepAwake = true;
  if (in.b) return false;
  int sl = repeatStep(tcRepL, in.left, in.leftDown, now, false);
  int sr = repeatStep(tcRepR, in.right, in.rightDown, now, false);
  int su = repeatStep(tcRepU, in.up, in.upDown, now, false);
  if (sr) tcSetPct(tcPct + 10, now);
  if (sl) tcSetPct(tcPct - 10, now);
  if (su && !sr) tcSetPct(tcPct + 10, now);
  if (in.down) tcSetPct(tcPct - 10, now);
  if (in.tap || in.a) {
    tcOpts[0].value = (tcOpts[0].value + 1) % 3;
    prefs.putUChar(tcOpts[0].key, tcOpts[0].value);
    tcShowUntil = now + 1200;
    tcNeed = true;
  }
  bool overlay = now < tcShowUntil;
  static bool wasOverlay = false;
  if (wasOverlay && !overlay) tcNeed = true;
  wasOverlay = overlay;
  if (tcNeed && now - tcLastDraw >= 30) {
    tcLastDraw = now;
    tcNeed = false;
    uint16_t c = tcColor();
    gfx->fillScreen(c);
    if (overlay) {
      uint16_t fg = tcOpts[0].value == 1 ? 0x0000 : rgb(40, 40, 40);
      char b[24];
      snprintf(b, sizeof b, "%d%%", tcPct);
      centerText(b, 130, 8, fg);
      centerText(TC_COL[tcOpts[0].value], 210, 3, fg);
      centerText("LEFT dim  RIGHT bright  tap colour", H - 30, 2, fg);
    }
    dirty = true;
  }
  return true;
}

Game APP_TORCH = {"TORCH", "white / red light, max bright", 0xFFFF, tcOpts, 1, tcStart, tcFrame};
