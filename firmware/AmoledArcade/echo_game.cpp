// echo_game.cpp - Echo: tilt Simon. Four glowing pads light up in a growing sequence; repeat it.
//   WATCH the pads flash, then TILT toward a pad (it gets a bright outline) and press LEFT or RIGHT to play it.
//   TAP a pad on the screen works too.  Home: triple-tap BOOT / touch-hold the top-left corner.
// MODE: CLASSIC (repeat) or REVERSE (repeat the sequence backwards).  SPEED: SLOW / NORMAL / FAST.
// Each pad has its own pitch. The flashes get faster as the sequence grows. Score = longest sequence repeated;
// the best is saved per mode.
#include "engine.h"
#include <math.h>

static const char *const EC_MODES[] = {"CLASSIC", "REVERSE"};
static const char *const EC_SPEEDS[] = {"SLOW", "NORMAL", "FAST"};
static Option ecOpts[] = {{"MODE", EC_MODES, 2, "ecMode", 0}, {"SPEED", EC_SPEEDS, 3, "ecSpeed", 1}};

enum EcSt : uint8_t { EC_INTRO, EC_GAP, EC_SHOW, EC_INPUT, EC_WIN, EC_FAIL, EC_OVER };
static constexpr int EC_MAX = 96;
struct EcRect { int16_t x, y, w, h; };
// pads: 0 = UP, 1 = RIGHT, 2 = DOWN, 3 = LEFT (same as the tilt direction)
static const EcRect EC_R[4] = {{154, 40, 140, 86}, {304, 138, 136, 124}, {154, 274, 140, 86}, {8, 138, 136, 124}};
static const uint16_t EC_COL[4] = {rgb(40, 230, 90), rgb(255, 60, 80), rgb(255, 210, 40), rgb(60, 140, 255)};
static const uint16_t EC_HZ[4] = {523, 392, 330, 262};
static const int EC_CX = 224, EC_CY = 200;

static uint8_t ecSeq[EC_MAX];
static EcSt ecSt;
static int ecLen, ecIdx, ecScore, ecLit, ecSel, ecMode, ecSpeed, ecFailPad;
static uint32_t ecBest, ecT, ecStAt, ecLitEnd, ecLastT, ecLastRender;
static float ecGlow[4];
static bool ecNewBest;
static SnapDir ecSnap;

static int ecOnMs() {
  static const int base[3] = {640, 500, 380}, mn[3] = {300, 220, 150};
  int v = base[ecSpeed] - 16 * (ecLen - 1);
  return v < mn[ecSpeed] ? mn[ecSpeed] : v;
}
static void ecKey(char *k) { snprintf(k, 16, "ecBest%d", ecMode); }

static void ecLight(int p, uint32_t now, int ms) {
  ecLit = p; ecLitEnd = now + ms; ecGlow[p] = 1.0f;
  play(EC_HZ[p], ms > 40 ? ms - 20 : ms);
}
static void ecAdd(uint32_t now) {
  if (ecLen < EC_MAX) ecSeq[ecLen++] = (uint8_t)random(4);
  ecIdx = 0; ecSt = EC_GAP; ecT = now + 750; ecLit = -1;
}
static void ecStart() {
  ecMode = ecOpts[0].value; ecSpeed = ecOpts[1].value;
  char k[16]; ecKey(k);
  ecBest = loadBest(k);
  ecLen = 0; ecScore = 0; ecIdx = 0; ecLit = -1; ecSel = -1; ecNewBest = false;
  for (auto &g : ecGlow) g = 0;
  ecSnap = SnapDir();
  imuZero();
  ecSt = EC_INTRO; ecStAt = millis(); ecLastT = ecStAt; ecLastRender = 0;
  sfxStart();
}

static int ecPadAt(int x, int y) {
  for (int i = 0; i < 4; i++)
    if (x >= EC_R[i].x && x < EC_R[i].x + EC_R[i].w && y >= EC_R[i].y && y < EC_R[i].y + EC_R[i].h) return i;
  int dx = x - EC_CX, dy = y - EC_CY;
  if (abs(dx) > abs(dy)) return dx > 0 ? 1 : 3;
  return dy > 0 ? 2 : 0;
}

static void ecDrawPad(int i) {
  const EcRect &r = EC_R[i];
  float g = ecGlow[i];
  uint16_t c = EC_COL[i];
  uint16_t fill = lerpColor(lerpColor(c, C_BG, 0.86f), lerpColor(c, C_TEXT, 0.35f), g);
  if (g > 0.15f)
    for (int k = 4; k >= 1; k--)
      gfx->drawRoundRect(r.x - k * 2, r.y - k * 2, r.w + k * 4, r.h + k * 4, 18 + k * 2,
                         lerpColor(c, C_BG, 1.0f - g * 0.5f / k));
  gfx->fillRoundRect(r.x, r.y, r.w, r.h, 18, fill);
  gfx->drawRoundRect(r.x, r.y, r.w, r.h, 18, lerpColor(c, C_BG, g > 0.5f ? 0.0f : 0.5f));
  if (ecSt == EC_INPUT && ecSel == i) {                    // tilt selection
    gfx->drawRoundRect(r.x - 3, r.y - 3, r.w + 6, r.h + 6, 20, C_TEXT);
    gfx->drawRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 21, lerpColor(c, C_TEXT, 0.5f));
    gfx->fillCircle(r.x + r.w / 2, r.y + r.h / 2, 7, lerpColor(c, C_TEXT, 0.7f));
  }
}

static void ecRender() {
  gfx->fillScreen(C_BG);
  for (int i = 0; i < 4; i++) ecDrawPad(i);
  gfx->fillCircle(EC_CX, EC_CY, 58, C_PANEL);
  gfx->drawCircle(EC_CX, EC_CY, 58, lerpColor(C_ACC, C_BG, 0.5f));
  char b[24];
  if (ecSt != EC_INTRO) {
    snprintf(b, sizeof b, "%d", ecLen);
    centerText(b, EC_CY - 22, 5, C_TEXT);
    const char *lab = ecSt == EC_INPUT ? (ecMode ? "BACKWARDS" : "REPEAT") : ecSt == EC_FAIL || ecSt == EC_OVER ? "MISS" :
                      ecSt == EC_WIN ? "GOOD" : "WATCH";
    uint16_t lc = ecSt == EC_INPUT ? C_GOOD : (ecSt == EC_FAIL || ecSt == EC_OVER) ? C_BAD : C_CYAN;
    centerText(lab, EC_CY + 24, 1, lc);
    if (ecSt == EC_INPUT) {
      snprintf(b, sizeof b, "%d / %d", ecIdx, ecLen);
      centerText(b, EC_CY + 36, 1, C_DIM);
      centerText("TILT + PRESS / TAP", 132, 1, C_DIM);
    }
  }
  hudBar(ecScore, ecBest, rgb(8, 8, 22), C_TEXT);
  if (ecSt == EC_INTRO) {
    gfx->fillRoundRect(40, 120, W - 80, 128, 16, C_PANEL);
    gfx->drawRoundRect(40, 120, W - 80, 128, 16, C_ACC);
    centerText("ECHO", 132, 4, C_ACC);
    centerText("watch, then repeat the pads", 176, 1, C_TEXT);
    centerText("TILT toward a pad, press LEFT / RIGHT", 194, 1, C_TEXT);
    centerText("or tap the pad", 210, 1, C_TEXT);
    centerText(ecMode ? "REVERSE: play it backwards!" : "faster as it grows", 228, 1, C_DIM);
  } else if (ecSt == EC_OVER) {
    char l2[32], l3[48];
    snprintf(l2, sizeof l2, ecNewBest ? "NEW BEST %d" : "LENGTH %d", ecScore);
    snprintf(l3, sizeof l3, "RIGHT again   LEFT menu");
    panelBox("MISSED", l2, nullptr, C_BAD);
    centerText(l3, 236, 1, C_DIM);
  }
  dirty = true;
}

static bool ecFrame(uint32_t now) {
  homeLock = true;                                         // quick BOOT presses are the game
  float dt = fminf((now - ecLastT) / 1000.0f, 0.05f);
  ecLastT = now;
  tuneUpdate(now);
  // glow: held while a pad is lit, fades after
  if (ecLit >= 0 && now >= ecLitEnd) ecLit = -1;
  for (int i = 0; i < 4; i++) {
    if (i == ecLit) ecGlow[i] = 1.0f;
    else if (ecGlow[i] > 0) ecGlow[i] = fmaxf(0.0f, ecGlow[i] - dt * 4.5f);
  }
  // tilt -> pad. STICK: axisX()/axisY() already return the analog stick (when deflected) instead of the tilt.
  uint8_t d = ecSnap.update(axisX(), axisY());
  ecSel = d == DIR_U ? 0 : d == DIR_R ? 1 : d == DIR_D ? 2 : d == DIR_L ? 3 : -1;
  bool press = in.left || in.right;

  switch (ecSt) {
    case EC_INTRO:
      if (now - ecStAt > 3000 || press || in.tap) { ecAdd(now); }
      break;
    case EC_GAP:
      if (now >= ecT) { ecLight(ecSeq[ecIdx], now, ecOnMs()); ecT = now + ecOnMs(); ecSt = EC_SHOW; }
      break;
    case EC_SHOW:
      if (now >= ecT) {
        ecIdx++;
        if (ecIdx >= ecLen) { ecIdx = 0; ecSt = EC_INPUT; imuZero(); ecSnap = SnapDir(); }
        else { ecSt = EC_GAP; ecT = now + ecOnMs() * 2 / 5; }
      }
      break;
    case EC_INPUT: {
      int p = -1;
      if (press) p = ecSel;
      if (in.tap) p = ecPadAt(in.tapX, in.tapY);
      if (p >= 0) {
        int want = ecSeq[ecMode ? ecLen - 1 - ecIdx : ecIdx];
        if (p == want) {
          ecLight(p, now, 220);
          if (++ecIdx >= ecLen) {
            ecScore = ecLen;
            if ((uint32_t)ecScore > ecBest) ecBest = ecScore;
            ecSt = EC_WIN; ecT = now + 420;
          }
        } else {
          ecFailPad = want;
          ecLit = -1; ecGlow[p] = 0.6f;
          play(110, 320); play(90, 260);
          ecSt = EC_FAIL; ecStAt = now;
        }
      }
      break;
    }
    case EC_WIN:
      if (now >= ecT) {
        static const TuneNote up[] = {{660, 70}, {880, 110}};
        tunePlay(up);
        ecAdd(now);
      }
      break;
    case EC_FAIL:
      if (now - ecStAt > 350) ecGlow[ecFailPad] = fmaxf(ecGlow[ecFailPad], 0.9f);   // show what it should have been
      if (now - ecStAt > 1300) {
        char k[16]; ecKey(k);
        ecNewBest = saveBest(k, ecScore);
        if (ecNewBest) tunePlay(TUNE_WIN); else tunePlay(TUNE_LOSE);
        ecSt = EC_OVER; ecStAt = now;
      }
      break;
    case EC_OVER:
      if (now - ecStAt > 700) {
        if (in.right) { ecStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - ecLastRender >= 16) { ecLastRender = now; ecRender(); }
  return true;
}

Game GAME_ECHO = {"ECHO", "tilt Simon: repeat the lights", 0x07E0, ecOpts, 2, ecStart, ecFrame};
