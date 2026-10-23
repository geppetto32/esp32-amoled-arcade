// duopong_game.cpp - Duo Pong: two players, one board lying flat between them. First to 7.
//   Player 1 sits at the LEFT end and owns the LEFT button / left paddle; player 2 sits at the RIGHT end (RIGHT button).
//   STEER = HOLD (default): hold your button = paddle glides UP, release = it glides DOWN. The motion is smoothed, so
//        tapping rapidly makes it hover, and you can brake by letting go. Feels like "gas pedal against gravity".
//   STEER = REVERSE: the paddle always sweeps; each press reverses its direction (good for slow reflexes).
//   MODE: 2 PLAYER or VS CPU (the CPU takes the right paddle; SPEED also sets how good it is).
// Spin: the ball leaves at an angle from where it hit the paddle plus 30 % of the paddle's speed, and the rally speeds
// it up (pitch rises with the rally). Serve: the player who just lost the point receives the serve (random at the start)
// after a short READY pause. Scores are drawn huge as 7-segment digits twice (upright and upside down) in each half so
// both sides of the table can read them. Best = longest rally (saved).
#include "engine.h"
#include <math.h>

static const char *const DP_STEER[] = {"HOLD", "REVERSE"};
static const char *const DP_SPEED[] = {"SLOW", "NORMAL", "FAST"};
static const char *const DP_MODE[] = {"2 PLAYER", "VS CPU"};
static Option dpOpts[] = {{"STEER", DP_STEER, 2, "dpSteer", 0}, {"SPEED", DP_SPEED, 3, "dpSpeed", 1}, {"MODE", DP_MODE, 2, "dpMode", 0}};

enum DpSt : uint8_t { DP_INTRO, DP_SERVE, DP_PLAY, DP_OVER };
static constexpr int DP_PH = 76, DP_PT = 8, DP_BR = 7, DP_X0 = 18, DP_TOP = 8, DP_BOT = H - 8, DP_WIN = 7;
static constexpr float DP_V = 300.0f;                       // paddle top speed px/s
static const uint16_t DP_C[2] = {rgb(0, 224, 255), rgb(255, 60, 200)};
static constexpr int DP_TR = 12;

static DpSt dpSt;
static float dpBx, dpBy, dpVx, dpVy, dpSpd, dpBase, dpPy[2], dpPv[2], dpTrX[DP_TR], dpTrY[DP_TR];
static int dpScore[2], dpDir[2], dpServeTo, dpRally, dpSteer, dpSpeed, dpMode, dpWinner, dpLastRally;
static uint32_t dpBest, dpStAt, dpLastT, dpLastRender;
static bool dpNewBest;

static void dpPlace() { dpBx = W / 2; dpBy = H / 2; dpVx = dpVy = 0; for (int i = 0; i < DP_TR; i++) { dpTrX[i] = dpBx; dpTrY[i] = dpBy; } }
static void dpServe(uint32_t now) {
  dpSt = DP_SERVE; dpStAt = now; dpRally = 0; dpSpd = dpBase; dpPlace();
}
static void dpStart() {
  dpSteer = dpOpts[0].value; dpSpeed = dpOpts[1].value; dpMode = dpOpts[2].value;
  static const float base[3] = {210, 265, 330};
  dpBase = base[dpSpeed];
  dpBest = loadBest("dpRally");
  dpScore[0] = dpScore[1] = 0; dpPy[0] = dpPy[1] = H / 2; dpPv[0] = dpPv[1] = 0; dpDir[0] = dpDir[1] = 1;
  dpServeTo = random(2); dpLastRally = 0; dpNewBest = false;
  ptClear(); dpPlace();
  dpLastT = millis(); dpLastRender = 0;
  dpSt = DP_INTRO; dpStAt = dpLastT;
  sfxStart();
}

static void dpLaunch() {
  float ang = (random(-250, 251) / 1000.0f) * 1.2f;
  float dir = dpServeTo == 0 ? -1.0f : 1.0f;
  dpVx = dir * dpSpd * cosf(ang); dpVy = dpSpd * sinf(ang);
  play(440, 40);
}

static void dpPoint(int scorer, uint32_t now) {
  dpScore[scorer]++;
  if ((uint32_t)dpRally > dpBest) { dpBest = dpRally; saveBest("dpRally", dpBest); dpNewBest = true; }
  dpLastRally = dpRally;
  ptBurst(scorer ? 20 : W - 20, dpBy, 28, 220, DP_C[scorer], C_BG, 0.8f, 0, 2);
  if (dpScore[scorer] >= DP_WIN) {
    dpWinner = scorer; dpSt = DP_OVER; dpStAt = now; dpBx = -100; dpVx = dpVy = 0;
    tunePlay(TUNE_WIN);
  } else {
    play(196, 120); play(147, 160);
    dpServeTo = 1 - scorer;                                // the one who conceded receives
    dpServe(now);
  }
}

static void dpPaddles(float dt) {
  for (int p = 0; p < 2; p++) {
    float target;
    if (p == 1 && dpMode == 1) {                           // CPU
      float want = (dpVx > 0 && dpSt == DP_PLAY) ? dpBy + sinf(dpBx * 0.05f) * 18.0f : H / 2.0f;
      float lim = DP_V * (0.55f + 0.2f * dpSpeed);
      target = constrain((want - dpPy[1]) * 6.0f, -lim, lim);
      dpPv[1] = target;
    } else {
      // STICK: an analog stick could set the target here: target = in.ay * DP_V (instead of the one-button logic)
      bool held = p == 0 ? in.leftDown : in.rightDown;
      bool edge = p == 0 ? in.left : in.right;
      if (dpSteer == 0) target = held ? -DP_V : DP_V * 0.8f;
      else { if (edge) dpDir[p] = -dpDir[p]; target = dpDir[p] * DP_V * 0.85f; }
      dpPv[p] += (target - dpPv[p]) * fminf(1.0f, dt * 14.0f);
    }
    dpPy[p] += dpPv[p] * dt;
    float lo = DP_TOP + DP_PH / 2.0f, hi = DP_BOT - DP_PH / 2.0f;
    if (dpPy[p] < lo) { dpPy[p] = lo; dpPv[p] = 0; }
    if (dpPy[p] > hi) { dpPy[p] = hi; dpPv[p] = 0; }
  }
}

static void dpHit(int p) {
  float rel = constrain((dpBy - dpPy[p]) / (DP_PH / 2.0f + DP_BR), -1.0f, 1.0f);
  dpSpd = fminf(dpSpd * 1.045f, dpBase * 2.1f);
  float vy = dpSpd * sinf(rel * 0.85f) + dpPv[p] * 0.30f;
  vy = constrain(vy, -dpSpd * 0.88f, dpSpd * 0.88f);
  float vx = sqrtf(fmaxf(dpSpd * dpSpd - vy * vy, 1.0f));
  dpVx = p == 0 ? vx : -vx; dpVy = vy;
  dpRally++;
  int hz = 300 + dpRally * 22; if (hz > 1500) hz = 1500;
  play(hz, 28);
  ptBurst(dpBx, dpBy, 8, 110, DP_C[p], C_BG, 0.35f, 0, 2);
}

static void dpBall(float dt, uint32_t now) {
  float dist = sqrtf(dpVx * dpVx + dpVy * dpVy) * dt;
  int n = 1 + (int)(dist / 5.0f); if (n > 10) n = 10;
  float h = dt / n;
  for (int s = 0; s < n; s++) {
    dpBx += dpVx * h; dpBy += dpVy * h;
    if (dpBy < DP_TOP + DP_BR) { dpBy = DP_TOP + DP_BR; dpVy = fabsf(dpVy); play(180, 12); }
    if (dpBy > DP_BOT - DP_BR) { dpBy = DP_BOT - DP_BR; dpVy = -fabsf(dpVy); play(180, 12); }
    float fl = DP_X0 + DP_PT / 2.0f + DP_BR, fr = W - DP_X0 - DP_PT / 2.0f - DP_BR;
    if (dpVx < 0 && dpBx <= fl && dpBx > fl - 14 && fabsf(dpBy - dpPy[0]) <= DP_PH / 2.0f + DP_BR) { dpBx = fl; dpHit(0); }
    else if (dpVx > 0 && dpBx >= fr && dpBx < fr + 14 && fabsf(dpBy - dpPy[1]) <= DP_PH / 2.0f + DP_BR) { dpBx = fr; dpHit(1); }
    if (dpBx < -DP_BR) { dpPoint(1, now); return; }
    if (dpBx > W + DP_BR) { dpPoint(0, now); return; }
  }
}

// ---- 7-segment digit, optionally rotated 180 degrees ----
static void dpDigit(int x, int y, int h, int d, uint16_t col, bool rot) {
  static const uint8_t mask[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
  int w = h / 2, t = h / 9, half = h / 2;
  const int r[7][4] = {{t, 0, w - 2 * t, t}, {w - t, t, t, half - t - t / 2}, {w - t, half + t / 2, t, half - t - t / 2},
                       {t, h - t, w - 2 * t, t}, {0, half + t / 2, t, half - t - t / 2}, {0, t, t, half - t - t / 2},
                       {t, half - t / 2, w - 2 * t, t}};
  for (int i = 0; i < 7; i++) {
    if (!(mask[d] & (1 << i))) continue;
    int rx = r[i][0], ry = r[i][1], rw = r[i][2], rh = r[i][3];
    if (rot) { rx = w - rx - rw; ry = h - ry - rh; }
    gfx->fillRect(x + rx, y + ry, rw, rh, col);
  }
}

static void dpRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  // scores, big, twice (upright in the top half, upside down in the bottom half), dim so the ball stays visible
  for (int p = 0; p < 2; p++) {
    int cx = p == 0 ? W / 4 : 3 * W / 4;
    uint16_t c = lerpColor(DP_C[p], C_BG, 0.62f);
    int d = dpScore[p] > 9 ? 9 : dpScore[p];
    dpDigit(cx - 21, 40, 84, d, c, false);
    dpDigit(cx - 21, H - 40 - 84, 84, d, c, true);
  }
  // walls + dashed centre line
  gfx->drawFastHLine(0, DP_TOP - 3, W, rgb(60, 70, 120));
  gfx->drawFastHLine(0, DP_BOT + 2, W, rgb(60, 70, 120));
  for (int y = DP_TOP + 4; y < DP_BOT - 8; y += 20) gfx->fillRect(W / 2 - 1, y, 3, 10, rgb(40, 44, 80));
  // paddles with glow
  for (int p = 0; p < 2; p++) {
    int x = p == 0 ? DP_X0 : W - DP_X0, y = (int)dpPy[p];
    for (int k = 3; k >= 1; k--)
      gfx->drawRoundRect(x - DP_PT / 2 - k * 2, y - DP_PH / 2 - k * 2, DP_PT + k * 4, DP_PH + k * 4, 5 + k * 2,
                         lerpColor(DP_C[p], C_BG, 0.45f + 0.18f * k));
    gfx->fillRoundRect(x - DP_PT / 2, y - DP_PH / 2, DP_PT, DP_PH, 4, DP_C[p]);
    gfx->fillRoundRect(x - 1, y - DP_PH / 2 + 4, 2, DP_PH - 8, 1, C_TEXT);
  }
  // ball with trail
  for (int i = DP_TR - 1; i >= 0; i--) {
    float f = 1.0f - i / (float)DP_TR;
    gfx->fillCircle((int)dpTrX[i], (int)dpTrY[i], (int)(DP_BR * (0.35f + 0.55f * f)), lerpColor(C_ACC, C_BG, 1.0f - f * 0.5f));
  }
  if (dpSt != DP_OVER) {
    int br = DP_BR + (dpSt == DP_SERVE ? (int)(3.0f * (0.5f + 0.5f * sinf(now * 0.014f))) : 0);
    gfx->drawCircle((int)dpBx, (int)dpBy, br + 3, lerpColor(C_ACC, C_BG, 0.55f));
    gfx->fillCircle((int)dpBx, (int)dpBy, br, C_TEXT);
  }
  ptDraw();
  char b[40];
  snprintf(b, sizeof b, "RALLY %d   BEST %lu", dpRally, (unsigned long)dpBest);
  centerText(b, 12, 1, C_DIM);
  if (dpSt == DP_SERVE) {
    int dir = dpServeTo == 0 ? -1 : 1;                     // arrows show who receives
    for (int k = 0; k < 3; k++) {
      int ax = W / 2 + dir * (26 + k * 18);
      gfx->fillTriangle(ax + dir * 8, H / 2, ax - dir * 4, H / 2 - 9, ax - dir * 4, H / 2 + 9, DP_C[dpServeTo]);
    }
    centerText("READY", H / 2 + 24, 2, C_TEXT);
  } else if (dpSt == DP_INTRO) {
    gfx->fillRoundRect(46, 120, W - 92, 150, 16, C_PANEL);
    gfx->drawRoundRect(46, 120, W - 92, 150, 16, C_ACC);
    centerText("DUO PONG", 132, 3, C_ACC);
    centerText("first to 7", 170, 1, C_TEXT);
    if (dpSteer == 0) { centerText("HOLD your button = paddle up", 188, 1, C_TEXT); centerText("release = paddle down", 202, 1, C_TEXT); }
    else { centerText("each press REVERSES your paddle", 188, 1, C_TEXT); centerText("it never stops sweeping", 202, 1, C_TEXT); }
    centerText(dpMode ? "you: LEFT button vs the CPU" : "LEFT button = left player", 220, 1, C_DIM);
    if (!dpMode) centerText("RIGHT button = right player", 234, 1, C_DIM);
    centerText("press any button", 252, 1, C_DIM);
  } else if (dpSt == DP_OVER) {
    char t[24], l2[32];
    snprintf(t, sizeof t, dpMode && dpWinner ? "CPU WINS" : (dpWinner ? "PLAYER 2 WINS" : "PLAYER 1 WINS"));
    snprintf(l2, sizeof l2, "%d : %d", dpScore[0], dpScore[1]);
    gfx->fillRoundRect(54, 104, W - 108, 160, 16, C_PANEL);
    gfx->drawRoundRect(54, 104, W - 108, 160, 16, DP_C[dpWinner]);
    centerText(t, 122, 2, DP_C[dpWinner]);
    centerText(l2, 156, 4, C_TEXT);
    snprintf(b, sizeof b, dpNewBest ? "NEW BEST RALLY %lu" : "best rally %lu", (unsigned long)dpBest);
    centerText(b, 204, 1, C_ACC);
    if (now - dpStAt > 1500) centerText("RIGHT rematch   LEFT menu", 232, 1, C_DIM);
  }
  dirty = true;
}

static bool dpFrame(uint32_t now) {
  homeLock = true;                                         // quick BOOT presses are the game
  float dt = fminf((now - dpLastT) / 1000.0f, 0.05f);
  dpLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  if (dpSt != DP_OVER) dpPaddles(dt);
  switch (dpSt) {
    case DP_INTRO:
      if ((in.left || in.right) && now - dpStAt > 400) dpServe(now);
      break;
    case DP_SERVE:
      if (now - dpStAt > 1300) { dpLaunch(); dpSt = DP_PLAY; }
      break;
    case DP_PLAY:
      for (int i = DP_TR - 1; i > 0; i--) { dpTrX[i] = dpTrX[i - 1]; dpTrY[i] = dpTrY[i - 1]; }
      dpTrX[0] = dpBx; dpTrY[0] = dpBy;
      dpBall(dt, now);
      break;
    case DP_OVER:
      if (now - dpStAt > 1500) {
        if (in.right) { dpStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - dpLastRender >= 16) { dpLastRender = now; dpRender(now); }
  return true;
}

Game GAME_DUOPONG = {"DUO PONG", "two players, one board, first to 7", 0xF81F, dpOpts, 3, dpStart, dpFrame};
