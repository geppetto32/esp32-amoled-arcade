// app_counter.cpp - COUNTER: up to 4 named tally counters, saved.
//   RIGHT = +1, LEFT = -1, hold LEFT 2 s = reset the active counter.
//   Both buttons together (or tap a card, or gamepad stick up/down) = next counter. Tap the big name = rename (cycle).
//   Counting means quick repeated taps, so the triple-tap HOME gesture is off here (homeLock). Leave with: both buttons held
//   1.5 s, a touch held on the top-left corner 1.5 s, or gamepad B.
#include "app_common.h"
#include "engine.h"

static const char *const CT_NAMES[] = {"COUNT", "CUTS", "BOARDS", "PARTS", "SCREWS", "LAPS", "ITEMS", "SETS"};
static constexpr int CT_NNAMES = sizeof(CT_NAMES) / sizeof(CT_NAMES[0]);
static constexpr int32_t CT_MAX = 99999;

static int32_t ctVal[4];
static uint8_t ctName[4], ctAct;
static bool ctNeed, ctSavePending, ctHeld, ctFired;
static uint32_t ctSaveAt, ctLastDraw, ctHeldT, ctPendT;
static int ctPend;                                 // 0 none, 1 LEFT waiting, 2 RIGHT waiting (chord window)
static constexpr uint32_t CT_CHORD_MS = 150;
static Repeat ctRepU, ctRepD;

static void ctSaveNow() {
  char k[8];
  for (int i = 0; i < 4; i++) {
    snprintf(k, sizeof k, "ct_v%d", i); prefs.putUInt(k, (uint32_t)ctVal[i]);
    snprintf(k, sizeof k, "ct_n%d", i); prefs.putUChar(k, ctName[i]);
  }
  prefs.putUChar("ct_act", ctAct);
  ctSavePending = false;
}
static void ctChanged(uint32_t now) { ctSavePending = true; ctSaveAt = now + 250; ctNeed = true; }

static void ctStart() {
  char k[8];
  for (int i = 0; i < 4; i++) {
    snprintf(k, sizeof k, "ct_v%d", i); ctVal[i] = constrain((int32_t)prefs.getUInt(k, 0), 0, CT_MAX);
    snprintf(k, sizeof k, "ct_n%d", i); ctName[i] = prefs.getUChar(k, (uint8_t)i);
    if (ctName[i] >= CT_NNAMES) ctName[i] = (uint8_t)i;
  }
  ctAct = prefs.getUChar("ct_act", 0);
  if (ctAct > 3) ctAct = 0;
  ctNeed = true; ctSavePending = false; ctHeld = false; ctFired = false; ctPend = 0; ctLastDraw = 0;
}

static void ctApply(int side, uint32_t now) {
  int32_t &v = ctVal[ctAct];
  if (side == 2) { if (v < CT_MAX) v++; play(1200, 25); }
  else { if (v > 0) v--; play(700, 25); }
  ctChanged(now);
}
static void ctNext(uint32_t now) {
  ctAct = (uint8_t)((ctAct + 1) & 3);
  play(900, 30);
  ctChanged(now);
}

static void ctDraw(uint32_t now) {
  gfx->fillScreen(C_BG);
  appHeader("COUNTER", "", C_DIM);
  char b[24];
  for (int i = 0; i < 4; i++) {
    int y = 38 + i * 72;
    bool on = i == ctAct;
    gfx->fillRoundRect(8, y, 156, 66, 10, on ? C_PANEL : C_BG);
    gfx->drawRoundRect(8, y, 156, 66, 10, on ? C_ACC : 0x2104);
    text(CT_NAMES[ctName[i]], 18, y + 8, 2, on ? C_TEXT : C_DIM);
    snprintf(b, sizeof b, "%ld", (long)ctVal[i]);
    text(b, 18, y + 32, 3, on ? C_ACC : 0x6B4D);
  }
  { const char *nm = CT_NAMES[ctName[ctAct]]; text(nm, 172 + (W - 172 - textW(nm, 3)) / 2, 52, 3, C_CYAN); }
  // big value in the right panel
  snprintf(b, sizeof b, "%ld", (long)ctVal[ctAct]);
  int len = (int)strlen(b), size = min(10, 250 / (6 * len));
  int bx = 172 + (W - 172 - len * 6 * size) / 2;
  text(b, bx, 130, size, C_TEXT);
  if (ctHeld && now - ctHeldT > 400 && !ctFired) {
    float f = constrain((now - ctHeldT - 400) / 1600.0f, 0.0f, 1.0f);
    gfx->drawRoundRect(180, 262, 250, 18, 6, C_BAD);
    gfx->fillRoundRect(182, 264, (int)(246 * f), 14, 4, C_BAD);
    text("RESET", 180, 286, 2, C_BAD);
  } else {
    text("RIGHT +1   LEFT -1", 180, 262, 2, C_DIM);
    text("hold LEFT 2 s = reset", 180, 284, 2, C_DIM);
  }
  centerText("both = next counter   hold both 1.5 s = exit", H - 12, 1, C_DIM);
}

static bool ctFrame(uint32_t now) {
  homeLock = true;                                 // counting = quick BOOT taps: no triple-tap home in this app
  static uint32_t bothSince = 0;
  if (in.leftDown && in.rightDown) { if (!bothSince) bothSince = now; } else bothSince = 0;
  if (in.b || (bothSince && now - bothSince >= 1500)) { bothSince = 0; if (ctSavePending) ctSaveNow(); return false; }
  bool L = in.left, R = in.right;
  if (L && R) { ctPend = 0; ctNext(now); }
  else if (L || R) {
    int side = L ? 1 : 2;
    if (ctPend && ctPend != side && now - ctPendT <= CT_CHORD_MS) { ctPend = 0; ctNext(now); }
    else { if (ctPend) ctApply(ctPend, now); ctPend = side; ctPendT = now; }
  }
  if (ctPend && now - ctPendT >= CT_CHORD_MS) { ctApply(ctPend, now); ctPend = 0; }
  if (in.a && !ctPend) ctApply(2, now);

  if (repeatStep(ctRepD, in.down, in.downDown, now, false)) ctNext(now);
  if (repeatStep(ctRepU, in.up, in.upDown, now, false)) { ctAct = (uint8_t)((ctAct + 3) & 3); ctChanged(now); }

  if (in.tap) {
    if (in.tapX < 170 && in.tapY >= 38) {
      int i = constrain((in.tapY - 38) / 72, 0, 3);
      if (i != ctAct) { ctAct = (uint8_t)i; play(900, 30); ctChanged(now); }
    } else if (in.tapY >= 40 && in.tapY < 110) {      // rename: next name in the list
      ctName[ctAct] = (uint8_t)((ctName[ctAct] + 1) % CT_NNAMES);
      play(1000, 25);
      ctChanged(now);
    }
  }

  if (in.leftDown) {
    if (!ctHeld) { ctHeld = true; ctFired = false; ctHeldT = now; }
    else if (!ctFired && now - ctHeldT >= 2000) {
      ctFired = true; ctPend = 0;
      ctVal[ctAct] = 0;
      sfxBad();
      ctChanged(now);
    }
    if (!ctFired) ctNeed = true;                   // animate the reset bar
  } else if (ctHeld) { ctHeld = false; ctNeed = true; }

  if (ctSavePending && now >= ctSaveAt) ctSaveNow();
  if (ctNeed && now - ctLastDraw >= 33) { ctLastDraw = now; ctNeed = false; ctDraw(now); }
  return true;
}

Game APP_COUNTER = {"COUNTER", "4 tally counters, saved", 0xFFE0, nullptr, 0, ctStart, ctFrame};
