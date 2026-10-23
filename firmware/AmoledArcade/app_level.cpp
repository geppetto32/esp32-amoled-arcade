// app_level.cpp - LEVEL: woodworker's angle gauge from the accelerometer.
//   FLAT: board lying flat = bubble level (X/Y degrees).  EDGE: board standing on its long edge = digital protractor.
//   LEFT = hold / release the reading, RIGHT = zero here (REL), tap the screen = clear zero (back to absolute).
//   Beeps when the reading is within +-0.2 deg of level / zero (EDGE, absolute: also at 45 and 90).
#include "app_common.h"
#include <math.h>

static const char *const LV_MODES[] = {"FLAT", "EDGE"};
static Option lvOpts[] = {{"MODE", LV_MODES, 2, "lv_mode", 0}};

// Flip these if a reading has the wrong sign on the real board (UNVERIFIED on hardware).
static constexpr float LV_SIGN_X = 1.0f, LV_SIGN_Y = 1.0f;

static float lvX, lvY, lvZ;                 // filtered accelerometer vector (g)
static bool lvFilterInit, lvRel, lvHold, lvLevel;
static float lvZeroA, lvZeroB;              // zero offsets in degrees
static float lvFrozenA, lvFrozenB;
static float lvCurA, lvCurB;                // live (absolute) angles of the last frame
static uint32_t lvLastT, lvLastDraw, lvLastBeep;

static float rad2deg(float r) { return r * 57.29578f; }

static void lvStart() {
  lvFilterInit = false;
  lvRel = lvHold = lvLevel = false;
  lvZeroA = lvZeroB = 0;
  lvLastT = millis();
  lvLastDraw = 0;
  lvLastBeep = 0;
  gfx->fillScreen(C_BG);
  dirty = true;
}

static float lvShow(float v) {              // avoid "-0.0"
  return fabsf(v) < 0.05f ? 0.0f : v;
}

static void lvDrawFlat(float dA, float dB, bool level) {
  const int cx = 128, cy = 204, R = 112;
  const float pxPerDeg = 20.0f;
  gfx->drawCircle(cx, cy, R, 0x4A69);
  gfx->drawCircle(cx, cy, R - 1, 0x4A69);
  gfx->drawCircle(cx, cy, 60, 0x2104);
  gfx->drawCircle(cx, cy, 20, level ? C_GOOD : 0x4208);
  gfx->drawFastHLine(cx - R, cy, 2 * R, 0x2104);
  gfx->drawFastVLine(cx, cy - R, 2 * R, 0x2104);
  // bubble drifts to the HIGH side (opposite of the way the board leans)
  float bx = -dA * pxPerDeg, by = -dB * pxPerDeg;
  float d = sqrtf(bx * bx + by * by), lim = R - 18;
  if (d > lim) { bx *= lim / d; by *= lim / d; }
  uint16_t col = level ? C_GOOD : lerpColor(C_CYAN, C_ACC, constrain(d / 80.0f, 0.0f, 1.0f));
  gfx->fillCircle(cx + (int)bx, cy + (int)by, 18, lerpColor(C_BG, col, 0.35f));
  gfx->fillCircle(cx + (int)bx, cy + (int)by, 15, col);
  gfx->fillCircle(cx + (int)bx - 5, cy + (int)by - 5, 4, lerpColor(col, C_TEXT, 0.7f));

  char b[16];
  const char *lab[2] = {"X", "Y"};
  float v[2] = {lvShow(dA), lvShow(dB)};
  for (int i = 0; i < 2; i++) {
    int y = 52 + i * 92;
    text(lab[i], 268, y, 2, C_DIM);
    snprintf(b, sizeof b, "%+.1f", v[i]);
    uint16_t c = fabsf(v[i]) <= 0.2f ? C_GOOD : C_TEXT;
    text(b, 268, y + 22, 4, c);
    degreeMark(268 + textW(b, 4) + 2, y + 22, 4, c);
  }
  if (level) text("LEVEL", 268, 242, 4, C_GOOD);
  else text("tilt to level", 268, 250, 2, 0x6B4D);
}

static void lvDrawEdge(float a, bool valid, bool level) {
  char b[16];
  uint16_t col = !valid ? C_DIM : level ? C_GOOD : C_TEXT;
  if (valid) snprintf(b, sizeof b, "%.1f", lvShow(a));
  else snprintf(b, sizeof b, "--.-");
  int total = textW(b, 9) + 28;
  int x = (W - total) / 2;
  text(b, x, 48, 9, col);
  degreeMark(x + textW(b, 9) + 2, 48, 9, col);

  const int x0 = 24, x1 = 424, sy = 218;
  auto px = [&](float ang) { return x0 + (int)((ang + 90.0f) * (x1 - x0) / 180.0f); };
  gfx->drawFastHLine(x0, sy, x1 - x0 + 1, C_DIM);
  gfx->drawFastHLine(x0, sy + 1, x1 - x0 + 1, C_DIM);
  for (int ang = -90; ang <= 90; ang += 5) {
    int len = ang % 45 == 0 ? 26 : ang % 15 == 0 ? 15 : 8;
    uint16_t tc = ang % 45 == 0 ? C_ACC : C_DIM;
    gfx->drawFastVLine(px((float)ang), sy, len, tc);
    if (ang % 45 == 0) gfx->drawFastVLine(px((float)ang) + 1, sy, len, tc);
  }
  static const int labA[5] = {-90, -45, 0, 45, 90};
  for (int i = 0; i < 5; i++) {
    snprintf(b, sizeof b, "%d", labA[i]);
    text(b, px((float)labA[i]) - textW(b, 2) / 2, sy + 34, 2, labA[i] == 0 ? C_TEXT : C_DIM);
  }
  if (valid) {
    int m = px(constrain(a, -90.0f, 90.0f));
    uint16_t mc = level ? C_GOOD : C_CYAN;
    gfx->fillTriangle(m - 12, sy - 46, m + 12, sy - 46, m, sy - 8, mc);
    gfx->drawFastVLine(m, sy - 8, 10, mc);
  } else {
    centerText("STAND THE BOARD ON", 292, 2, C_ACC);
    centerText("ITS LONG EDGE", 314, 2, C_ACC);
  }
}

static bool lvFrame(uint32_t now) {
  keepAwake = true;
  float rx, ry, rz;
  bool haveImu = imuOk && imuRaw(rx, ry, rz);
  if (!haveImu) {
    if (now - lvLastDraw >= 200) {
      lvLastDraw = now;
      gfx->fillScreen(C_BG);
      appHeader("LEVEL", "", C_DIM);
      centerText("IMU NOT FOUND", 170, 3, C_BAD);
      dirty = true;
    }
    return true;
  }
  rx *= LV_SIGN_X; ry *= LV_SIGN_Y;
  float dt = constrain((now - lvLastT) / 1000.0f, 0.0f, 0.2f);
  lvLastT = now;
  if (!lvFilterInit) { lvX = rx; lvY = ry; lvZ = rz; lvFilterInit = true; }
  float k = 1.0f - expf(-dt / 0.18f);          // extra low-pass on top of the 125 Hz driver filter
  lvX += k * (rx - lvX); lvY += k * (ry - lvY); lvZ += k * (rz - lvZ);

  uint8_t mode = lvOpts[0].value;
  bool valid = true;
  float angA, angB = 0;
  if (mode == 0) {
    angA = rad2deg(atan2f(lvX, sqrtf(lvY * lvY + lvZ * lvZ)));
    angB = rad2deg(atan2f(lvY, sqrtf(lvX * lvX + lvZ * lvZ)));
  } else {
    valid = hypotf(lvX, lvY) > 0.5f;           // gravity must lie in the screen plane
    angA = rad2deg(atan2f(lvX, fabsf(lvY)));
  }
  lvCurA = angA; lvCurB = angB;

  if (in.left) {                                // hold / release
    lvHold = !lvHold;
    if (lvHold) { lvFrozenA = angA - (lvRel ? lvZeroA : 0); lvFrozenB = angB - (lvRel ? lvZeroB : 0); }
    sfxClick();
  }
  if (in.right) {                               // zero here
    lvZeroA = angA; lvZeroB = angB; lvRel = true; lvHold = false;
    sfxClick();
  }
  if (in.tap && lvRel) { lvRel = false; lvZeroA = lvZeroB = 0; lvHold = false; sfxClick(); }

  float dA = lvHold ? lvFrozenA : angA - (lvRel ? lvZeroA : 0);
  float dB = lvHold ? lvFrozenB : angB - (lvRel ? lvZeroB : 0);

  // "level" test with a little hysteresis; EDGE (absolute) also targets 45 and 90
  float th = lvLevel ? 0.4f : 0.2f;
  bool nowLevel;
  if (mode == 0) nowLevel = fabsf(dA) <= th && fabsf(dB) <= th;
  else {
    float aa = fabsf(dA), tgt = 0;
    if (!lvRel) tgt = aa > 67.5f ? 90.0f : aa > 22.5f ? 45.0f : 0.0f;
    nowLevel = valid && fabsf(aa - tgt) <= th;
  }
  if (nowLevel && !lvLevel && !lvHold && now - lvLastBeep > 400) { play(1760, 70); lvLastBeep = now; }
  lvLevel = nowLevel;

  if (now - lvLastDraw < 33) return true;
  lvLastDraw = now;
  gfx->fillScreen(C_BG);
  char badge[16];
  snprintf(badge, sizeof badge, "%s%s%s", lvHold ? "HOLD" : "", (lvHold && lvRel) ? " " : "", lvRel ? "REL" : "");
  char title[24];
  snprintf(title, sizeof title, "LEVEL  %s", LV_MODES[mode]);
  appHeader(title, badge, lvHold ? C_CYAN : C_ACC);
  if (mode == 0) lvDrawFlat(dA, dB, lvLevel);
  else lvDrawEdge(dA, valid, lvLevel);
  centerText(lvRel ? "L hold   R zero   tap = absolute" : "L hold    R zero here", H - 24, 2, C_DIM);
  dirty = true;
  return true;
}

Game APP_LEVEL = {"LEVEL", "bubble level + angle gauge", 0x07E0, lvOpts, 1, lvStart, lvFrame};
