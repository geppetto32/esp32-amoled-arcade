// app_dice.cpp - DICE: 1-6 dice, d4/d6/d8/d10/d12/d20, tumbling roll animation, sum.
//   Roll: shake the board (acceleration spike), press RIGHT, tap the screen, or gamepad A.
//   LEFT = next die type.  Gamepad stick up/down = more / fewer dice.  Options DICE and TYPE are remembered.
#include "app_common.h"
#include <math.h>
#include <esp_random.h>

static const char *const DC_N[] = {"1", "2", "3", "4", "5", "6"};
static const char *const DC_T[] = {"D4", "D6", "D8", "D10", "D12", "D20"};
static const uint8_t DC_SIDES[6] = {4, 6, 8, 10, 12, 20};
static Option dcOpts[] = {{"DICE", DC_N, 6, "dc_n", 1}, {"TYPE", DC_T, 6, "dc_t", 1}};

// Shake threshold: |acceleration| deviates from 1 g by more than this (UNVERIFIED on the real board; the IMU
// vector is low-pass filtered, so a firm shake is needed).
static constexpr float DC_SHAKE_G = 0.8f;
static constexpr uint32_t DC_ROLL_MS = 900, DC_STAGGER_MS = 140;

static int dcVal[6], dcFinal[6];
static uint32_t dcStop[6], dcTickAt[6], dcRollT0, dcLastDraw, dcLastRoll;
static bool dcRolling, dcNeed, dcEverRolled;
static Repeat dcRepU, dcRepD;

static int dcCount() { return dcOpts[0].value + 1; }
static int dcSides() { return DC_SIDES[dcOpts[1].value]; }

static void dcSave() {
  prefs.putUChar(dcOpts[0].key, dcOpts[0].value);
  prefs.putUChar(dcOpts[1].key, dcOpts[1].value);
}

static int dcRand(int sides) { return 1 + (int)(esp_random() % (uint32_t)sides); }

static void dcRoll(uint32_t now) {
  int n = dcCount(), s = dcSides();
  dcRolling = true;
  dcEverRolled = true;
  dcRollT0 = dcLastRoll = now;
  for (int i = 0; i < n; i++) {
    dcFinal[i] = dcRand(s);
    dcVal[i] = dcRand(s);
    dcStop[i] = now + DC_ROLL_MS + i * DC_STAGGER_MS;
    dcTickAt[i] = now;
  }
  dcNeed = true;
}

static void dcStart() {
  dcRolling = false;
  dcEverRolled = false;
  dcNeed = true;
  dcLastDraw = 0;
  dcLastRoll = millis();
  for (int i = 0; i < 6; i++) dcVal[i] = dcFinal[i] = 1;
  imuZero();
}

// ---- drawing ----
static void dcPoly(int cx, int cy, float R, int k, float a0, uint16_t fill, uint16_t edge) {
  int vx[6], vy[6];
  for (int i = 0; i < k; i++) {
    float a = a0 + i * 6.2831853f / k;
    vx[i] = cx + (int)lroundf(cosf(a) * R);
    vy[i] = cy + (int)lroundf(sinf(a) * R);
  }
  for (int i = 1; i + 1 < k; i++) gfx->fillTriangle(vx[0], vy[0], vx[i], vy[i], vx[i + 1], vy[i + 1], fill);
  for (int i = 0; i < k; i++) {
    int j = (i + 1) % k;
    gfx->drawLine(vx[i], vy[i], vx[j], vy[j], edge);
    gfx->drawLine(vx[i] + 1, vy[i], vx[j] + 1, vy[j], edge);
  }
}

// one die centred at (cx, cy), radius R, rotated by ang (radians), showing v
static void dcDie(int cx, int cy, float R, float ang, int v, int sides, bool landed) {
  uint16_t fill = landed ? rgb(246, 244, 236) : rgb(226, 224, 216), edge = rgb(120, 118, 110), ink = rgb(24, 24, 36);
  int k = 4;
  float a0 = ang + 0.7853982f;                      // d6: axis-aligned square
  switch (sides) {
    case 4: k = 3; a0 = ang - 1.5707963f; break;
    case 8: k = 4; a0 = ang; break;
    case 10: k = 5; a0 = ang - 1.5707963f; break;
    case 12: k = 5; a0 = ang + 1.5707963f; break;
    case 20: k = 6; a0 = ang; break;
    default: break;
  }
  float Rr = sides == 6 ? R * 1.18f : R;
  dcPoly(cx, cy, Rr, k, a0, fill, edge);
  if (sides == 6) {
    static const int8_t PX[7][6] = {{0}, {0}, {-1, 1}, {-1, 0, 1}, {-1, 1, -1, 1}, {-1, 1, 0, -1, 1}, {-1, 1, -1, 1, -1, 1}};
    static const int8_t PY[7][6] = {{0}, {0}, {-1, 1}, {-1, 0, 1}, {-1, -1, 1, 1}, {-1, -1, 0, 1, 1}, {-1, -1, 0, 0, 1, 1}};
    static const uint8_t PN[7] = {0, 1, 2, 3, 4, 5, 6};
    float q = R * 0.62f, pr = R * 0.17f, ca = cosf(ang), sa = sinf(ang);
    auto pip = [&](float ox, float oy) {
      gfx->fillCircle(cx + (int)lroundf(ox * ca - oy * sa), cy + (int)lroundf(ox * sa + oy * ca), (int)pr + 1, ink);
    };
    if (v == 1) pip(0, 0);
    else if (v == 6) {                               // two columns of three
      for (int i = 0; i < 3; i++) { pip(-q * 0.8f, (i - 1) * q); pip(q * 0.8f, (i - 1) * q); }
    } else {
      for (int i = 0; i < PN[v]; i++) pip(PX[v][i] * q, PY[v][i] * q);
    }
  } else {
    char b[4];
    snprintf(b, sizeof b, "%d", v);
    int size = R >= 66 ? 6 : R >= 52 ? 5 : R >= 40 ? 4 : 3;
    int len = (int)strlen(b);
    text(b, cx - len * 3 * size, cy - 4 * size, size, ink);
  }
}

static void dcDraw(uint32_t now) {
  int n = dcCount(), s = dcSides();
  gfx->fillScreen(C_BG);
  char b[40];
  snprintf(b, sizeof b, "DICE  %d x D%d", n, s);
  appHeader(b, dcRolling ? "ROLLING" : "", C_ACC);

  static const int PX1[1] = {224}, PX2[2] = {130, 318}, PX3[3] = {84, 224, 364}, PX4[4] = {62, 170, 278, 386};
  static const int PX5a[3] = {84, 224, 364}, PX5b[2] = {154, 294};
  float R;
  int cx[6], cy[6], sumY, hintY = H - 10;
  switch (n) {
    case 1: R = 84; cx[0] = PX1[0]; cy[0] = 160; sumY = 292; break;
    case 2: R = 74; for (int i = 0; i < 2; i++) { cx[i] = PX2[i]; cy[i] = 160; } sumY = 292; break;
    case 3: R = 62; for (int i = 0; i < 3; i++) { cx[i] = PX3[i]; cy[i] = 160; } sumY = 292; break;
    case 4: R = 46; for (int i = 0; i < 4; i++) { cx[i] = PX4[i]; cy[i] = 160; } sumY = 292; break;
    case 5:
      R = 52; sumY = 318;
      for (int i = 0; i < 3; i++) { cx[i] = PX5a[i]; cy[i] = 118; }
      for (int i = 0; i < 2; i++) { cx[3 + i] = PX5b[i]; cy[3 + i] = 232; }
      break;
    default:
      R = 52; sumY = 318;
      for (int i = 0; i < 3; i++) { cx[i] = PX5a[i]; cy[i] = 118; cx[3 + i] = PX5a[i]; cy[3 + i] = 232; }
      break;
  }
  int sum = 0;
  bool allLanded = true;
  for (int i = 0; i < n; i++) {
    bool landed = !dcRolling || now >= dcStop[i];
    float r = 0;
    if (!landed) {
      allLanded = false;
      r = (float)(dcStop[i] - now) / (float)(dcStop[i] - dcRollT0);
      r = constrain(r, 0.0f, 1.0f);
    }
    float ang = r * r * (i & 1 ? 7.0f : -7.0f) + sinf(now * 0.03f + i) * 0.18f * r;
    int ox = (int)(sinf(now * 0.021f + i * 1.7f) * 10.0f * r), oy = (int)(cosf(now * 0.027f + i) * 10.0f * r);
    dcDie(cx[i] + ox, cy[i] + oy, R, ang, dcVal[i], s, landed);
    sum += dcVal[i];
  }
  if (!dcEverRolled) centerText("SHAKE or press RIGHT", sumY, 2, C_ACC);
  else if (allLanded) {
    snprintf(b, sizeof b, n > 1 ? "SUM %d" : "%d", sum);
    centerText(b, sumY, n > 1 ? 4 : 4, C_GOOD);
  }
  centerText("RIGHT / tap / shake = roll   LEFT = die type", hintY - 2, 1, C_DIM);
}

static bool dcFrame(uint32_t now) {
  if (in.b) return false;
  int n = dcCount();
  keepAwake = dcRolling;

  bool roll = in.right || in.tap || in.a;
  float ax, ay, az;
  if (!dcRolling && now - dcLastRoll > 1200 && imuRaw(ax, ay, az)) {
    float m = sqrtf(ax * ax + ay * ay + az * az);
    if (fabsf(m - 1.0f) > DC_SHAKE_G) roll = true;
  }
  if (in.left) {
    dcOpts[1].value = (dcOpts[1].value + 1) % 6;
    dcSave();
    sfxClick();
    dcEverRolled = false;
    dcRolling = false;
    dcNeed = true;
  }
  int su = repeatStep(dcRepU, in.up, in.upDown, now, false), sd = repeatStep(dcRepD, in.down, in.downDown, now, false);
  if (su && n < 6) { dcOpts[0].value++; dcSave(); sfxClick(); dcEverRolled = false; dcNeed = true; }
  if (sd && n > 1) { dcOpts[0].value--; dcSave(); sfxClick(); dcEverRolled = false; dcNeed = true; }
  if (roll && !dcRolling) dcRoll(now);

  if (dcRolling) {
    int s = dcSides();
    bool any = false;
    for (int i = 0; i < n; i++) {
      if (now >= dcStop[i]) { dcVal[i] = dcFinal[i]; continue; }
      any = true;
      float r = (float)(dcStop[i] - now) / (float)(dcStop[i] - dcRollT0);
      if ((int32_t)(now - dcTickAt[i]) >= 0) {
        int nv = dcRand(s);
        if (nv == dcVal[i]) nv = nv % s + 1;
        dcVal[i] = nv;
        dcTickAt[i] = now + 45 + (uint32_t)((1.0f - r) * 190.0f);
        if (i == 0) play((uint16_t)(420 + esp_random() % 380), 12);
      }
    }
    dcNeed = true;
    if (!any) {
      dcRolling = false;
      dcLastRoll = now;
      sfxGood();
    }
  }
  if (dcNeed && now - dcLastDraw >= 33) {
    dcLastDraw = now;
    dcNeed = dcRolling;
    dcDraw(now);
  }
  return true;
}

Game APP_DICE = {"DICE", "d4-d20, shake to roll, 1-6 dice", 0x87F0, dcOpts, 2, dcStart, dcFrame};
