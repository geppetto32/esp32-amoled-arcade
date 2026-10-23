// wobble_game.cpp - Wobble: walk a glowing tightrope across a canyon. Pure inverted-pendulum balance, accelerometer only
// (no gyro needed).
//   TILT   = balance. The walker leans (lean angle, gravity makes it fall further); tilt the board toward the side it is
//            falling to and it steps its base under itself and catches it. Slight tilt = slight help; too much overshoots.
//   LEFT / RIGHT  = take a step with that foot (about 0.5 m). Stepping makes the body sway toward the stepping side, so
//            alternate left-right to walk straight; spamming one side builds a lean you must tilt away.
//   hold BOTH     = crouch: lower centre of mass (calmer, wind hits less) but you cannot step.
//   leave: triple-tap BOOT / touch-hold the top-left corner.
// Wind gusts start after ~15 m and get stronger and more frequent; random drift grows with distance. Score = metres
// walked (best saved per difficulty). A creaking tone rises with the lean. Without an IMU, touch the screen left / right
// of the middle as a stand-in for tilt.
#include "engine.h"
#include <math.h>

static const char *const WB_DIFF[] = {"EASY", "NORMAL", "HARD"};
static Option wbOpts[] = {{"LEVEL", WB_DIFF, 3, "wbDiff", 1}};

enum WbSt : uint8_t { WB_INTRO, WB_PLAY, WB_FALL, WB_OVER };
static constexpr int WB_ROPE = 250, WB_WX = 170;
static constexpr float WB_PX_M = 56.0f;                    // pixels per metre
static constexpr float WB_STEP = 26.0f;                    // px per step
static constexpr int WB_NSTREAK = 14, WB_NBIRD = 3;

struct WbStreak { float x, y, len, sp; };
struct WbBird { float x, y, vx, ph; };

static WbSt wbSt;
static float wbTh, wbThV, wbPhi, wbDist, wbCrouch, wbStepAnim, wbSwayDir, wbNoise, wbNoiseT, wbGust, wbGustT, wbGustDur, wbGustDir,
    wbGustAmp, wbNextGust, wbFx, wbFy, wbFvx, wbFvy, wbFrot, wbWind, wbTiltIn;
static int wbDiff, wbMeters, wbFlag;
static uint32_t wbBest, wbStAt, wbLastT, wbLastRender, wbCreakAt, wbNoiseAt, wbFlagAt;
static bool wbNewBest;
static WbStreak wbStreak[WB_NSTREAK];
static WbBird wbBird[WB_NBIRD];

static float wbRnd(float a, float b) { return a + (b - a) * (random(10001) / 10000.0f); }
static void wbKey(char *k) { snprintf(k, 16, "wbBest%d", wbDiff); }

static void wbStart() {
  wbDiff = wbOpts[0].value;
  char k[16]; wbKey(k);
  wbBest = loadBest(k);
  wbTh = 0.05f; wbThV = 0; wbPhi = 0; wbDist = 0; wbCrouch = 0; wbStepAnim = 0; wbSwayDir = 1; wbNoise = wbNoiseT = 0;
  wbGust = 0; wbGustT = 1e9f; wbGustDur = 1; wbGustAmp = 0; wbNextGust = 15.0f * WB_PX_M; wbWind = 0;
  wbMeters = 0; wbFlag = 0; wbNewBest = false; wbNoiseAt = 0; wbCreakAt = 0; wbFlagAt = 0;
  for (auto &s : wbStreak) s = {wbRnd(0, W), wbRnd(40, 330), wbRnd(14, 40), wbRnd(0.7f, 1.4f)};
  for (auto &b : wbBird) b = {wbRnd(0, W), wbRnd(50, 140), wbRnd(-40, -15), wbRnd(0, 6)};
  ptClear();
  imuZero();
  wbLastT = millis(); wbLastRender = 0;
  wbSt = WB_INTRO; wbStAt = wbLastT;
  sfxStart();
}

static void wbNewGust() {
  float m = wbDist / WB_PX_M;
  wbGustDir = random(2) ? 1.0f : -1.0f;
  wbGustAmp = 1.4f + fminf(m / 70.0f, 1.0f) * 2.6f;
  wbGustDur = wbRnd(1.4f, 2.6f); wbGustT = 0;
  float gap = fmaxf(3.0f, 12.0f - m / 12.0f);
  wbNextGust = wbDist + wbRnd(gap, gap * 1.8f) * WB_PX_M;
  play(180, 60);
}

static void wbFall(uint32_t now) {
  wbSt = WB_FALL; wbStAt = now;
  float L = 48.0f - 14.0f * wbCrouch;
  wbFx = WB_WX + L * sinf(wbTh); wbFy = WB_ROPE - L * cosf(wbTh);
  wbFvx = wbThV * 22.0f + (wbTh > 0 ? 30 : -30); wbFvy = -60; wbFrot = wbTh;
  char k[16]; wbKey(k);
  wbNewBest = saveBest(k, (uint32_t)wbMeters);
  if (wbNewBest) wbBest = wbMeters;
  tunePlay(TUNE_LOSE);
}

static void wbUpdate(float dt, uint32_t now) {
  float m = wbDist / WB_PX_M;
  bool crouch = in.leftDown && in.rightDown;
  wbCrouch += ((crouch ? 1.0f : 0.0f) - wbCrouch) * fminf(1.0f, dt * 9.0f);
  // ---- input: tilt (accelerometer). STICK: axisX() already returns the analog stick (when deflected) instead of tilt.
  float tilt;
  if (imuOk) tilt = axisX();
  else tilt = in.touching ? constrain((in.touchX - W / 2) / (W / 2.0f), -1.0f, 1.0f) * 0.4f : 0.0f;
  if (tilt > 0.03f) tilt -= 0.03f; else if (tilt < -0.03f) tilt += 0.03f; else tilt = 0;
  tilt = constrain(tilt, -0.7f, 0.7f);
  wbTiltIn = tilt;
  wbPhi += (constrain(tilt * 1.4f, -0.7f, 0.7f) - wbPhi) * fminf(1.0f, dt * 10.0f);   // pole follows the tilt
  // ---- steps ----
  if (!crouch && wbCrouch < 0.3f) {
    if (in.left) { wbDist += WB_STEP; wbThV -= 0.55f; wbStepAnim = 1; wbSwayDir = -1; play(300, 10); }
    if (in.right) { wbDist += WB_STEP; wbThV += 0.55f; wbStepAnim = 1; wbSwayDir = 1; play(340, 10); }
  }
  wbStepAnim = fmaxf(0.0f, wbStepAnim - dt * 5.0f);
  // ---- wind ----
  if (wbDist >= wbNextGust && wbGustT > wbGustDur) wbNewGust();
  wbWind = 0;
  if (wbGustT <= wbGustDur) { wbGustT += dt; wbWind = wbGustDir * wbGustAmp * sinf(3.14159f * fminf(wbGustT / wbGustDur, 1.0f)); }
  // ---- drift noise ----
  static const float dm[3] = {0.6f, 1.0f, 1.5f}, am[3] = {2.6f, 3.4f, 4.4f};
  if (now >= wbNoiseAt) { wbNoiseAt = now + 350; wbNoiseT = wbRnd(-1.0f, 1.0f) * (0.6f + fminf(m / 80.0f, 1.0f)) * dm[wbDiff]; }
  wbNoise += (wbNoiseT - wbNoise) * fminf(1.0f, dt * 3.0f);
  // ---- inverted pendulum ----
  float a = am[wbDiff] * (1.0f - 0.45f * wbCrouch);
  float exposure = 1.0f - 0.4f * wbCrouch;
  float acc = a * sinf(wbTh) - 0.9f * wbThV - 16.0f * tilt + (wbWind + wbNoise) * exposure * cosf(wbTh);
  wbThV += acc * dt;
  wbTh += wbThV * dt;
  // ---- creak ----
  float lean = fabsf(wbTh);
  if (lean > 0.38f && now >= wbCreakAt) { play((uint16_t)(70 + lean * 240), 45); wbCreakAt = now + (uint32_t)(420 - lean * 260); }
  // ---- score ----
  int mm = (int)(wbDist / WB_PX_M);
  if (mm != wbMeters) {
    wbMeters = mm;
    if (mm / 25 > wbFlag) { wbFlag = mm / 25; wbFlagAt = now; static const TuneNote fl[] = {{660, 70}, {880, 70}, {1175, 110}}; tunePlay(fl); }
    if ((uint32_t)wbMeters > wbBest) wbBest = wbMeters;
  }
  if (lean > 1.05f) wbFall(now);
}

// ---------------- drawing ----------------
static void wbDrawBg(uint32_t now) {
  uint16_t top = rgb(10, 8, 42), mid = rgb(150, 50, 92), low = rgb(26, 8, 40);
  for (int y = 0; y < H; y++)
    gfx->drawFastHLine(0, y, W, y < 210 ? lerpColor(top, mid, powf(y / 210.0f, 1.6f)) : lerpColor(mid, low, (y - 210) / 158.0f));
  for (int i = 0; i < 36; i++) {                           // stars
    int x = (i * 83 + 17) % W, y = 36 + (i * 47) % 130;
    int tw = (int)(40 + 40 * sinf(now * 0.002f + i));
    gfx->drawPixel(x, y, rgb(tw + 110, tw + 110, tw + 140));
  }
  gfx->fillCircle(360, 78, 36, lerpColor(rgb(255, 230, 190), rgb(150, 50, 92), 0.88f));       // moon glow
  gfx->fillCircle(360, 78, 24, lerpColor(rgb(255, 230, 190), rgb(150, 50, 92), 0.7f));
  gfx->fillCircle(360, 78, 17, rgb(255, 240, 215));
  // far / mid ridges with parallax
  for (int layer = 0; layer < 2; layer++) {
    float par = layer ? 0.32f : 0.12f;
    uint16_t c = layer ? rgb(38, 14, 52) : rgb(66, 26, 78);
    float off = wbDist * par;
    for (int x = 0; x < W; x += 4) {
      float u = (x + off) * (layer ? 0.016f : 0.011f);
      float h = (layer ? 280 : 215) - 26.0f * sinf(u) - 16.0f * sinf(u * 2.7f + 1.0f) - 8.0f * sinf(u * 6.1f);
      gfx->fillRect(x, (int)h, 4, H - (int)h, c);
    }
  }
  // fog at the bottom of the canyon
  for (int y = 318; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(rgb(60, 24, 80), low, 1.0f - (y - 318) / 50.0f * 0.6f));
}

static void wbDrawBirds(float dt) {
  for (auto &b : wbBird) {
    b.x += b.vx * dt; b.ph += dt * 7;
    if (b.x < -20) { b.x = W + 20; b.y = wbRnd(50, 150); }
    int x = (int)b.x, y = (int)b.y, f = (int)(5 * sinf(b.ph));
    uint16_t c = rgb(20, 8, 28);
    gfx->drawLine(x - 8, y - f, x, y, c); gfx->drawLine(x, y, x + 8, y - f, c);
  }
}

static void wbDrawWalker(uint32_t now) {
  uint16_t body = rgb(90, 245, 255), glowc = rgb(20, 70, 90), poleC = rgb(255, 190, 70);
  float L = 48.0f - 14.0f * wbCrouch;
  float sn = sinf(wbTh), cs = cosf(wbTh);
  float fx = WB_WX, fy = WB_ROPE;
  float tx = fx + L * sn, ty = fy - L * cs;                 // top of body
  float hx = fx + L * 0.42f * sn, hy = fy - L * 0.42f * cs; // hip
  float sx = fx + L * 0.84f * sn, sy = fy - L * 0.84f * cs; // shoulders (pole)
  float step = wbStepAnim * 7.0f;
  // legs
  for (int pass = 0; pass < 2; pass++) {
    uint16_t c = pass ? body : glowc;
    int w = pass ? 0 : 1;
    gfx->drawLine((int)hx, (int)hy, (int)(fx - 4 - (wbSwayDir < 0 ? step : 0)), (int)(fy - (wbSwayDir < 0 ? step : 0)), c);
    gfx->drawLine((int)hx, (int)hy, (int)(fx + 4 + (wbSwayDir > 0 ? step : 0)), (int)(fy - (wbSwayDir > 0 ? step : 0)), c);
    gfx->drawLine((int)hx + w, (int)hy, (int)(hx + (sx - hx)) + w, (int)sy, c);
    gfx->drawLine((int)hx - w, (int)hy, (int)(hx + (sx - hx)) - w, (int)sy, c);
  }
  gfx->drawLine((int)sx, (int)sy, (int)tx, (int)ty, body);
  gfx->fillCircle((int)(tx + 6 * sn), (int)(ty - 6 * cs), 7, glowc);
  gfx->fillCircle((int)(tx + 6 * sn), (int)(ty - 6 * cs), 5, body);
  // pole
  float P = 50.0f, pc = cosf(wbPhi), ps = sinf(wbPhi);
  int x0 = (int)(sx - pc * P), y0 = (int)(sy - ps * P), x1 = (int)(sx + pc * P), y1 = (int)(sy + ps * P);
  gfx->drawLine(x0, y0 - 1, x1, y1 - 1, lerpColor(poleC, C_BG, 0.6f));
  gfx->drawLine(x0, y0 + 1, x1, y1 + 1, lerpColor(poleC, C_BG, 0.6f));
  gfx->drawLine(x0, y0, x1, y1, poleC);
  gfx->fillCircle(x0, y0, 3, rgb(255, 120, 40)); gfx->fillCircle(x1, y1, 3, rgb(255, 120, 40));
  gfx->drawCircle(x0, y0, 5, lerpColor(rgb(255, 120, 40), C_BG, 0.6f)); gfx->drawCircle(x1, y1, 5, lerpColor(rgb(255, 120, 40), C_BG, 0.6f));
  (void)now;
}

static void wbRender(uint32_t now, float dt) {
  wbDrawBg(now);
  wbDrawBirds(dt);
  // start cliff (world x < 0)
  int x0 = (int)(WB_WX - wbDist);
  if (x0 > 0) {
    gfx->fillRect(0, WB_ROPE + 2, x0, H - WB_ROPE, rgb(22, 8, 30));
    gfx->drawFastHLine(0, WB_ROPE + 2, x0, rgb(140, 70, 120));
    gfx->fillRect(x0 - 6, WB_ROPE - 34, 6, 36, rgb(70, 40, 60));                        // anchor post
  }
  // rope with glow and pegs
  gfx->drawFastHLine(0, WB_ROPE - 2, W, rgb(60, 30, 20));
  gfx->drawFastHLine(0, WB_ROPE + 2, W, rgb(60, 30, 20));
  gfx->drawFastHLine(0, WB_ROPE - 1, W, rgb(150, 100, 50));
  gfx->drawFastHLine(0, WB_ROPE + 1, W, rgb(150, 100, 50));
  gfx->drawFastHLine(0, WB_ROPE, W, rgb(255, 215, 130));
  float ph = fmodf(wbDist, WB_PX_M);
  int m0 = (int)(wbDist / WB_PX_M);
  for (int k = -3; k < 9; k++) {
    int x = (int)(WB_WX + k * WB_PX_M - ph);
    if (x < 0 || x >= W) continue;
    int mk = m0 + k + 1;
    bool five = mk % 5 == 0;
    gfx->drawFastVLine(x, WB_ROPE + 3, five ? 9 : 5, lerpColor(rgb(255, 215, 130), C_BG, five ? 0.2f : 0.55f));
    if (five && mk > 0) { char b[8]; snprintf(b, sizeof b, "%d", mk); text(b, x - (int)strlen(b) * 3, WB_ROPE + 14, 1, rgb(190, 130, 150)); }
  }
  // wind streaks
  float wv = wbWind * 90.0f;
  for (auto &s : wbStreak) {
    s.x += (wv * s.sp - 20.0f) * dt;
    if (s.x < -50) s.x += W + 100;
    if (s.x > W + 50) s.x -= W + 100;
    if (fabsf(wbWind) > 0.25f) gfx->drawFastHLine((int)s.x, (int)s.y, (int)(s.len * fabsf(wbWind) / 2), lerpColor(rgb(190, 220, 255), C_BG, 0.5f));
  }
  if (wbSt == WB_FALL || wbSt == WB_OVER) {
    // falling walker: a tumbling stick figure
    float c = cosf(wbFrot), s = sinf(wbFrot);
    int x = (int)wbFx, y = (int)wbFy;
    gfx->drawLine(x, y, x - (int)(s * 34), y + (int)(c * 34), rgb(90, 245, 255));
    gfx->fillCircle(x, y, 6, rgb(90, 245, 255));
    gfx->drawLine(x - 40, y + (int)(wbFrot * 6), x + 40, y - (int)(wbFrot * 6), rgb(255, 190, 70));
  } else wbDrawWalker(now);
  ptDraw();
  hudBar(wbMeters, wbBest, rgb(8, 8, 22), C_TEXT);
  text("m", 10 + 12 * (wbMeters >= 100 ? 3 : wbMeters >= 10 ? 2 : 1) + 4, 9, 2, C_DIM);
  if (wbSt == WB_PLAY || wbSt == WB_INTRO) {
    // balance gauge
    int gx = W / 2 - 100, gy = 346;
    gfx->drawRoundRect(gx, gy, 200, 8, 4, rgb(60, 50, 90));
    gfx->drawFastVLine(W / 2, gy - 3, 14, rgb(90, 80, 130));
    float f = constrain(wbTh / 1.05f, -1.0f, 1.0f);
    uint16_t c = lerpColor(C_GOOD, C_BAD, fminf(1.0f, fabsf(f) * 1.3f));
    gfx->fillCircle(W / 2 + (int)(f * 96), gy + 4, 6, c);
    // tilt input tick
    gfx->drawFastVLine(W / 2 + (int)(constrain(wbTiltIn / 0.7f, -1.0f, 1.0f) * 96), gy + 10, 5, C_CYAN);
    if (fabsf(wbWind) > 0.3f) centerText(wbWind > 0 ? "WIND  >>>" : "<<<  WIND", 40, 2, C_CYAN);
    if (wbFlag > 0 && now - wbFlagAt < 1800) { char b[16]; snprintf(b, sizeof b, "%d m!", wbFlag * 25); centerText(b, 76, 3, C_ACC); }
  }
  if (wbSt == WB_INTRO) {
    gfx->fillRoundRect(40, 90, W - 80, 160, 16, C_PANEL);
    gfx->drawRoundRect(40, 90, W - 80, 160, 16, C_ACC);
    centerText("WOBBLE", 102, 4, C_ACC);
    centerText("TILT toward the side it falls to", 148, 1, C_TEXT);
    centerText("LEFT / RIGHT = step with that foot", 164, 1, C_TEXT);
    centerText("alternate feet - one side makes it lean", 180, 1, C_TEXT);
    centerText("hold BOTH = crouch (calmer, no steps)", 196, 1, C_TEXT);
    centerText("press a button to start", 222, 1, C_DIM);
  } else if (wbSt == WB_OVER) {
    char l2[32], l3[48];
    snprintf(l2, sizeof l2, wbNewBest ? "NEW BEST %d m" : "%d m", wbMeters);
    snprintf(l3, sizeof l3, "RIGHT again   LEFT menu");
    panelBox("YOU FELL", l2, nullptr, C_BAD);
    centerText(l3, 236, 1, C_DIM);
  }
  dirty = true;
}

static bool wbFrame(uint32_t now) {
  homeLock = true;                                         // quick BOOT presses are the game
  float dt = fminf((now - wbLastT) / 1000.0f, 0.05f);
  wbLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  switch (wbSt) {
    case WB_INTRO:
      wbTh = 0.0f; wbThV = 0;
      wbPhi += (constrain(axisX() * 1.4f, -0.7f, 0.7f) - wbPhi) * fminf(1.0f, dt * 10.0f);
      if ((in.left || in.right || in.tap) && now - wbStAt > 400) { wbSt = WB_PLAY; imuZero(); wbTh = 0.05f * (random(2) ? 1 : -1); }
      else if (now - wbStAt > 15000) { wbSt = WB_PLAY; imuZero(); }
      break;
    case WB_PLAY:
      wbUpdate(dt, now);
      break;
    case WB_FALL:
      wbFvy += 700.0f * dt; wbFx += wbFvx * dt; wbFy += wbFvy * dt; wbFrot += (wbTh > 0 ? 4.0f : -4.0f) * dt;
      if (now - wbStAt > 1400) { wbSt = WB_OVER; wbStAt = now; }
      break;
    case WB_OVER:
      if (now - wbStAt > 700) {
        if (in.right) { wbStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - wbLastRender >= 16) {
    float rdt = fminf((now - wbLastRender) / 1000.0f, 0.05f);
    wbLastRender = now;
    wbRender(now, rdt);
  }
  return true;
}

Game GAME_WOBBLE = {"WOBBLE", "tilt to balance on a tightrope", 0xFC60, wbOpts, 1, wbStart, wbFrame};
