// ringbreaker_game.cpp - Ring Breaker, a circular Arkanoid. Concentric rings of bricks sit in the middle of the screen;
// your paddle is an arc that orbits the OUTSIDE of the arena, and the ball bounces around inside.
//   TILT (or the analog stick): the paddle goes to the side you tilt toward (tilt toward you = bottom, right = right).
//                               Level = it stays where it is.
//   LEFT / RIGHT held = rotate the paddle counter-clockwise / clockwise (precision)
//   LEFT + RIGHT together (or a tap) = launch the stuck ball (it also auto-launches), or fire the laser
//   TOUCH: put a finger down and the paddle points at it
// A gravity well in the centre curves the ball (WELL: OFF / WEAK / STRONG). Each brick in a row (combo) lifts the pitch
// and the score multiplier; bricks sometimes drop power-ups: MULTI ball, WIDE paddle, LASER, SLOW. Rings spin from level 2.
#include "engine.h"
#include <math.h>

static const char *const RB_WELL[] = {"WEAK", "OFF", "STRONG"};
static const char *const RB_PAD[] = {"NORMAL", "WIDE", "NARROW"};
static Option rbOpts[] = {{"WELL", RB_WELL, 3, "rbWell", 0}, {"PADDLE", RB_PAD, 3, "rbPad", 0}};

enum RbSt : uint8_t { RB_INTRO, RB_PLAY, RB_LOST, RB_LEVEL, RB_OVER };
enum : uint8_t { RBP_MULTI, RBP_WIDE, RBP_LASER, RBP_SLOW, RBP_N };

static constexpr float RB_CX = 224, RB_CY = 200, RB_RP = 150, RB_BR = 4, RB_HALF = 6;
static constexpr float RB_PI2 = 6.2831853f;
static constexpr int RB_MAXR = 5, RB_MAXS = 26, RB_MAXBALL = 3, RB_TRAIL = 8, RB_MAXPK = 4;
static const float RB_RAD[RB_MAXR] = {112, 97, 82, 67, 52};
static const int RB_NS[RB_MAXR] = {26, 22, 18, 15, 12};
static const uint16_t RB_COL[RB_MAXR] = {rgb(255, 70, 90), rgb(255, 160, 50), rgb(250, 230, 60), rgb(80, 240, 120), rgb(70, 200, 255)};
static const uint16_t RB_PCOL[RBP_N] = {rgb(255, 80, 240), rgb(80, 160, 255), rgb(255, 80, 60), rgb(120, 255, 160)};
static const char RB_PCH[RBP_N] = {'M', 'W', 'L', 'S'};

struct RbBall { float x, y, vx, vy; bool on, stuck; float tx[RB_TRAIL], ty[RB_TRAIL]; };
struct RbPick { float th, r; uint8_t type; bool on; };

static uint8_t rbHp[RB_MAXR][RB_MAXS];
static float rbRot[RB_MAXR], rbRotSp[RB_MAXR];
static RbBall rbBall[RB_MAXBALL];
static RbPick rbPk[RB_MAXPK];
static RbSt rbSt;
static int rbRings, rbLevel, rbLives, rbCombo, rbBricks, rbLaser, rbWell, rbPadOpt;
static float rbTh, rbHw, rbBaseSpeed, rbWideT, rbSlowT, rbStall, rbLaserCd, rbLaserTh, rbLaserFx, rbServeT, rbGT, rbPulse;
static uint32_t rbScore, rbBest, rbStAt, rbLastT, rbLastRender;
static bool rbNewBest;

static float rbRand() { return (esp_random() & 0xFFFF) / 65535.0f; }
static float rbWrap(float a) { a = fmodf(a, RB_PI2); return a < 0 ? a + RB_PI2 : a; }
static float rbDiff(float a, float b) { float d = rbWrap(a - b); return d > (float)M_PI ? d - RB_PI2 : d; }   // a - b in -PI..PI

static float rbHalfW() {
  float base = rbPadOpt == 1 ? 0.40f : rbPadOpt == 2 ? 0.22f : 0.30f;
  return rbWideT > 0 ? base * 1.55f : base;
}

static void rbBuildLevel() {
  rbRings = min(RB_MAXR, 2 + rbLevel);
  rbBricks = 0;
  memset(rbHp, 0, sizeof rbHp);
  for (int k = 0; k < rbRings; k++) {
    for (int s = 0; s < RB_NS[k]; s++) {
      bool alive = true;
      if (rbLevel % 3 == 2 && s % 6 == 5) alive = false;            // gates
      if (rbLevel % 3 == 0 && ((s + k) % 5 == 4)) alive = false;
      if (!alive) continue;
      rbHp[k][s] = (rbLevel >= 4 && (s + k) % 5 == 0 && k < 2) ? 2 : 1;
      rbBricks++;
    }
    rbRot[k] = 0;
    float sp = fminf(0.1f * (rbLevel - 1), 0.38f);
    rbRotSp[k] = (k & 1 ? 1 : -1) * sp * (0.7f + 0.15f * k);
  }
  rbBaseSpeed = fminf(200 + 14 * rbLevel, 330);
  for (auto &p : rbPk) p.on = false;
}

static void rbServe() {
  for (auto &b : rbBall) b.on = false;
  RbBall &b = rbBall[0];
  b.on = true; b.stuck = true; b.vx = b.vy = 0;
  b.x = RB_CX + (RB_RP - 14) * cosf(rbTh); b.y = RB_CY + (RB_RP - 14) * sinf(rbTh);
  for (int i = 0; i < RB_TRAIL; i++) { b.tx[i] = b.x; b.ty[i] = b.y; }
  rbCombo = 0; rbServeT = 1.6f; rbStall = 0;
}

static void rbStart() {
  rbWell = rbOpts[0].value; rbPadOpt = rbOpts[1].value;
  rbBest = loadBest("rbBest");
  rbScore = 0; rbLives = 3; rbLevel = 1; rbNewBest = false; rbWideT = rbSlowT = 0; rbLaser = 0; rbTh = (float)M_PI / 2;
  rbHw = rbHalfW(); rbGT = 0;
  rbBuildLevel();
  ptClear();
  rbServe();
  uint32_t now = millis();
  rbSt = RB_INTRO; rbStAt = now; rbLastT = now; rbLastRender = 0;
  sfxStart();
}

static void rbAddScore(uint32_t s) { rbScore += s; if (rbScore > rbBest) rbBest = rbScore; }

static void rbPoint(float r, float th, float &x, float &y) { x = RB_CX + r * cosf(th); y = RB_CY + r * sinf(th); }

static void rbDestroy(int k, int s) {
  float th = rbWrap((s + 0.5f) * RB_PI2 / RB_NS[k] + rbRot[k]), x, y;
  rbPoint(RB_RAD[k], th, x, y);
  rbCombo++;
  int mult = 1 + min(rbCombo / 3, 4);
  rbAddScore((uint32_t)((10 + (4 - k) * 5) * mult));
  rbBricks--;
  ptBurst(x, y, 10, 110, lerpColor(RB_COL[k], C_TEXT, 0.5f), RB_COL[k], 0.5f, 0, 2);
  play((uint16_t)(380 + min(rbCombo, 20) * 38), 26);
  if (rbRand() < 0.13f) {
    for (auto &p : rbPk) if (!p.on) { p.on = true; p.th = th; p.r = RB_RAD[k]; p.type = (uint8_t)(esp_random() % RBP_N); break; }
  }
}

static void rbHitBrick(int k, int s) {
  if (rbHp[k][s] > 1) { rbHp[k][s]--; play(520, 20); rbCombo++; return; }
  rbHp[k][s] = 0;
  rbDestroy(k, s);
}

static void rbLoseBall(RbBall &b, uint32_t now) {
  b.on = false;
  int n = 0;
  for (auto &o : rbBall) if (o.on) n++;
  if (n == 0) {
    rbSt = RB_LOST; rbStAt = now;
    float x, y; rbPoint(RB_RP, rbTh, x, y);
    ptBurst(x, y, 24, 140, rgb(255, 200, 100), rgb(255, 40, 40), 0.7f, 0, 3);
    play(220, 90); play(150, 120); play(100, 200);
    rbWideT = 0; rbLaser = 0;
  }
}

static void rbLaunch() {
  for (auto &b : rbBall) if (b.on && b.stuck) {
    float ang = rbTh + (float)M_PI + (rbRand() - 0.5f) * 0.5f;
    b.stuck = false; b.vx = cosf(ang) * rbBaseSpeed; b.vy = sinf(ang) * rbBaseSpeed;
    play(700, 30);
  }
}

static void rbFireLaser(uint32_t now) {
  (void)now;
  if (rbLaser <= 0 || rbLaserCd > 0) return;
  rbLaser--; rbLaserCd = 0.28f; rbLaserTh = rbTh; rbLaserFx = 0.12f;
  for (int k = 0; k < rbRings; k++) {
    float a = rbWrap(rbTh - rbRot[k]);
    int s = (int)(a / (RB_PI2 / RB_NS[k])) % RB_NS[k];
    if (rbHp[k][s]) { rbHitBrick(k, s); break; }
  }
  play(1500, 15); play(900, 25);
}

static void rbStepBall(RbBall &b, float h, uint32_t now) {
  float px = b.x, py = b.y;
  b.x += b.vx * h; b.y += b.vy * h;
  float dx = b.x - RB_CX, dy = b.y - RB_CY, r = sqrtf(dx * dx + dy * dy);
  if (r < 0.001f) return;
  float pr = sqrtf((px - RB_CX) * (px - RB_CX) + (py - RB_CY) * (py - RB_CY));
  float hx = dx / r, hy = dy / r;
  float vr = b.vx * hx + b.vy * hy;
  // paddle / leaving the arena
  if (r + RB_BR >= RB_RP - 6 && vr > 0) {
    float th = atan2f(dy, dx), d = rbDiff(th, rbTh);
    if (fabsf(d) <= rbHw + 0.05f) {
      float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
      float ang = th + (float)M_PI + constrain(d / rbHw, -1.0f, 1.0f) * 0.85f;
      b.vx = cosf(ang) * sp; b.vy = sinf(ang) * sp;
      float rr = RB_RP - 6 - RB_BR - 0.5f;
      b.x = RB_CX + rr * hx; b.y = RB_CY + rr * hy;
      rbCombo = 0; rbStall = 0;
      play(300, 18);
      return;
    }
    if (r > RB_RP + 12) { rbLoseBall(b, now); return; }
  }
  // bricks
  float th = atan2f(dy, dx);
  for (int k = 0; k < rbRings; k++) {
    float rc = RB_RAD[k];
    if (r < rc - RB_HALF - RB_BR || r > rc + RB_HALF + RB_BR) continue;
    float a = rbWrap(th - rbRot[k]);
    int s = (int)(a / (RB_PI2 / RB_NS[k])) % RB_NS[k];
    if (!rbHp[k][s]) continue;
    rbHitBrick(k, s);
    rbStall = 0;
    float tx = -hy, ty = hx;
    float vt = b.vx * tx + b.vy * ty;
    bool radial = pr > rc + RB_HALF + RB_BR || pr < rc - RB_HALF - RB_BR;
    if (radial) {
      vr = -vr;
      float rr = pr > rc ? rc + RB_HALF + RB_BR + 0.6f : rc - RB_HALF - RB_BR - 0.6f;
      b.x = RB_CX + rr * hx; b.y = RB_CY + rr * hy;
    } else {
      vt = -vt;
      b.x = px; b.y = py;
    }
    b.vx = hx * vr + tx * vt; b.vy = hy * vr + ty * vt;
    return;
  }
}

static void rbUpdate(float dt, uint32_t now) {
  rbGT += dt; rbPulse += dt;
  if (rbLaserCd > 0) rbLaserCd -= dt;
  if (rbLaserFx > 0) rbLaserFx -= dt;
  for (int k = 0; k < rbRings; k++) rbRot[k] = rbWrap(rbRot[k] + rbRotSp[k] * dt);
  if (rbSt != RB_PLAY && rbSt != RB_INTRO) return;
  if (rbWideT > 0) rbWideT -= dt;
  if (rbSlowT > 0) rbSlowT -= dt;
  float hwT = rbHalfW();
  rbHw += (hwT - rbHw) * fminf(1.0f, dt * 8);
  if (rbSt != RB_PLAY) return;
  // balls
  float mulSpeed = rbSlowT > 0 ? 0.65f : 1.0f, target = rbBaseSpeed * mulSpeed;
  float g = rbWell == 0 ? 70.0f : rbWell == 2 ? 170.0f : 0.0f;
  rbStall += dt;
  for (auto &b : rbBall) {
    if (!b.on) continue;
    for (int i = RB_TRAIL - 1; i > 0; i--) { b.tx[i] = b.tx[i - 1]; b.ty[i] = b.ty[i - 1]; }
    b.tx[0] = b.x; b.ty[0] = b.y;
    if (b.stuck) {
      b.x = RB_CX + (RB_RP - 14) * cosf(rbTh); b.y = RB_CY + (RB_RP - 14) * sinf(rbTh);
      rbServeT -= dt;
      if (rbServeT <= 0) rbLaunch();
      continue;
    }
    float dx = b.x - RB_CX, dy = b.y - RB_CY, r = fmaxf(1.0f, sqrtf(dx * dx + dy * dy));
    float gg = rbStall > 9.0f ? -90.0f : g;                         // anti-orbit: push outward if nothing was hit for long
    b.vx -= dx / r * gg * dt; b.vy -= dy / r * gg * dt;
    float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
    float lo = target * 0.8f, hi = target * 1.3f, ns = sp;
    if (sp < lo) ns = lo; else if (sp > hi) ns = hi;
    if (sp > 0.01f && ns != sp) { b.vx *= ns / sp; b.vy *= ns / sp; }
    float dist = ns * dt;
    int n = (int)ceilf(dist / 3.0f);
    if (n < 1) n = 1;
    float h = dt / n;
    for (int i = 0; i < n && b.on; i++) rbStepBall(b, h, now);
    if (rbSt != RB_PLAY) return;
  }
  // pickups
  for (auto &p : rbPk) {
    if (!p.on) continue;
    p.r += 75 * dt;
    if (p.r >= RB_RP - 8) {
      if (fabsf(rbDiff(p.th, rbTh)) < rbHw + 0.14f) {
        switch (p.type) {
          case RBP_MULTI: {
            RbBall *src = nullptr;
            for (auto &b : rbBall) if (b.on && !b.stuck) { src = &b; break; }
            if (src) {
              float vx = src->vx, vy = src->vy, sx = src->x, sy = src->y;
              for (int s = -1; s <= 1; s += 2)
                for (auto &b : rbBall) if (!b.on) {
                  float c = cosf(0.5f * s), sn = sinf(0.5f * s);
                  b.on = true; b.stuck = false; b.x = sx; b.y = sy; b.vx = vx * c - vy * sn; b.vy = vx * sn + vy * c;
                  for (int i = 0; i < RB_TRAIL; i++) { b.tx[i] = sx; b.ty[i] = sy; }
                  break;
                }
            }
            break;
          }
          case RBP_WIDE: rbWideT = 14; break;
          case RBP_LASER: rbLaser = 8; break;
          default: rbSlowT = 9; break;
        }
        rbAddScore(100);
        play(900, 40); play(1200, 60);
      }
      p.on = false;
    }
  }
  if (rbBricks <= 0) {
    rbSt = RB_LEVEL; rbStAt = now; tunePlay(TUNE_WIN);
    rbAddScore(500 + 100 * (uint32_t)rbLives);
  }
}

// ---------------- drawing ----------------
static void rbDrawBrick(int k, int s) {
  float seg = RB_PI2 / RB_NS[k], g = 1.5f / RB_RAD[k];
  float a0 = s * seg + rbRot[k] + g, a1 = (s + 1) * seg + rbRot[k] - g;
  float ri = RB_RAD[k] - RB_HALF, ro = RB_RAD[k] + RB_HALF;
  float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
  int x0 = (int)(RB_CX + ri * c0), y0 = (int)(RB_CY + ri * s0), x1 = (int)(RB_CX + ro * c0), y1 = (int)(RB_CY + ro * s0);
  int x2 = (int)(RB_CX + ro * c1), y2 = (int)(RB_CY + ro * s1), x3 = (int)(RB_CX + ri * c1), y3 = (int)(RB_CY + ri * s1);
  uint16_t c = RB_COL[k], f = lerpColor(c, C_BG, rbHp[k][s] > 1 ? 0.3f : 0.62f);
  gfx->fillTriangle(x0, y0, x1, y1, x2, y2, f);
  gfx->fillTriangle(x0, y0, x2, y2, x3, y3, f);
  uint16_t e = rbHp[k][s] > 1 ? lerpColor(c, C_TEXT, 0.6f) : c;
  gfx->drawLine(x0, y0, x1, y1, e); gfx->drawLine(x1, y1, x2, y2, e);
  gfx->drawLine(x2, y2, x3, y3, e); gfx->drawLine(x3, y3, x0, y0, e);
}

static void rbDrawPaddle(float th, float hw, uint16_t c, int rr) {
  int n = max(4, (int)(hw / 0.05f));
  for (int j = 0; j < n; j++) {
    float a0 = th - hw + 2 * hw * j / n, a1 = th - hw + 2 * hw * (j + 1) / n;
    for (int o = -3; o <= 3; o++) {
      float r = RB_RP + o + rr;
      uint16_t col = (o == -3 || o == 3) ? lerpColor(c, C_BG, rr ? 0.7f : 0.2f) : (rr ? lerpColor(c, C_BG, 0.85f) : c);
      if (rr == 0 && (o == -3 || o == 3)) col = c;
      else if (rr == 0) col = lerpColor(c, C_BG, 0.45f);
      gfx->drawLine((int)(RB_CX + r * cosf(a0)), (int)(RB_CY + r * sinf(a0)), (int)(RB_CX + r * cosf(a1)), (int)(RB_CY + r * sinf(a1)), col);
    }
  }
}

static void rbRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  gfx->drawCircle((int)RB_CX, (int)RB_CY, (int)RB_RP, rgb(18, 22, 44));
  gfx->drawCircle((int)RB_CX, (int)RB_CY, (int)RB_RP + 8, rgb(10, 12, 26));
  // gravity well
  if (rbWell != 1) {
    float p = 0.5f + 0.5f * sinf(rbPulse * 3);
    uint16_t wc = rbWell == 2 ? rgb(170, 70, 255) : rgb(60, 120, 255);
    gfx->fillCircle((int)RB_CX, (int)RB_CY, 22 + (int)(p * 3), lerpColor(wc, C_BG, 0.93f));
    gfx->fillCircle((int)RB_CX, (int)RB_CY, 12, lerpColor(wc, C_BG, 0.82f));
    gfx->fillCircle((int)RB_CX, (int)RB_CY, 4, lerpColor(wc, C_TEXT, 0.5f));
  }
  for (int k = 0; k < rbRings; k++)
    for (int s = 0; s < RB_NS[k]; s++) if (rbHp[k][s]) rbDrawBrick(k, s);
  if (rbLaserFx > 0) {
    float x0, y0, x1, y1;
    rbPoint(RB_RP - 6, rbLaserTh, x0, y0); rbPoint(30, rbLaserTh, x1, y1);
    gfx->drawLine((int)x0, (int)y0, (int)x1, (int)y1, rgb(255, 90, 70));
    gfx->drawLine((int)x0 + 1, (int)y0, (int)x1 + 1, (int)y1, rgb(255, 220, 200));
  }
  // pickups
  for (auto &p : rbPk) if (p.on) {
    float x, y; rbPoint(p.r, p.th, x, y);
    uint16_t c = RB_PCOL[p.type];
    gfx->fillRoundRect((int)x - 9, (int)y - 8, 18, 16, 5, lerpColor(c, C_BG, 0.7f));
    gfx->drawRoundRect((int)x - 9, (int)y - 8, 18, 16, 5, c);
    char s[2] = {RB_PCH[p.type], 0};
    text(s, (int)x - 3, (int)y - 4, 1, C_TEXT);
  }
  // paddle
  uint16_t pc = rbWideT > 0 ? rgb(120, 190, 255) : rbLaser > 0 ? rgb(255, 130, 100) : C_CYAN;
  if (rbSt != RB_LOST) { rbDrawPaddle(rbTh, rbHw + 0.06f, pc, 2); rbDrawPaddle(rbTh, rbHw, pc, 0); }
  // balls with comet trails
  for (auto &b : rbBall) {
    if (!b.on) continue;
    for (int i = RB_TRAIL - 1; i >= 1; i--) {
      float f = 1.0f - (float)i / RB_TRAIL;
      gfx->fillCircle((int)b.tx[i], (int)b.ty[i], (int)(1 + 3 * f), lerpColor(rgb(255, 160, 40), C_BG, 1.0f - f * 0.6f));
    }
    gfx->fillCircle((int)b.x, (int)b.y, 6, lerpColor(rgb(255, 230, 160), C_BG, 0.82f));
    gfx->fillCircle((int)b.x, (int)b.y, 4, rbSlowT > 0 ? rgb(150, 255, 190) : C_TEXT);
  }
  ptDraw();
  hudBar(rbScore, rbBest, rgb(8, 12, 30), C_TEXT);
  char b[24];
  for (int i = 0; i < rbLives; i++) gfx->fillCircle(236 + i * 14, 16, 5, C_CYAN);
  snprintf(b, sizeof b, "L%d", rbLevel);
  text(b, 292, 9, 2, C_ACC);
  if (rbCombo >= 3) { snprintf(b, sizeof b, "x%d", 1 + min(rbCombo / 3, 4)); text(b, 336, 9, 2, C_GOOD); }
  if (rbLaser > 0) { snprintf(b, sizeof b, "LASER %d", rbLaser); text(b, 8, 346, 1, rgb(255, 130, 100)); }
  if (rbSt == RB_INTRO) {
    gfx->fillRoundRect(110, 138, W - 220, 98, 14, C_PANEL);
    gfx->drawRoundRect(110, 138, W - 220, 98, 14, C_CYAN);
    centerText("RING BREAKER", 146, 2, C_CYAN);
    centerText("tilt: paddle goes your way", 176, 1, C_TEXT);
    centerText("LEFT/RIGHT rotate  both = launch", 192, 1, C_DIM);
    int left = 3 - (int)((now - rbStAt) / 700);
    if (left < 1) left = 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 208, 2, C_ACC);
  } else if (rbSt == RB_LEVEL) {
    char l2[24];
    snprintf(l2, sizeof l2, "LEVEL %d", rbLevel);
    panelBox("RING CLEAR", l2, nullptr, C_CYAN);
  } else if (rbSt == RB_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, rbNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)rbScore);
    panelBox("GAME OVER", l2, "RIGHT again  LEFT menu", C_CYAN);
  }
  dirty = true;
}

static bool rbFrame(uint32_t now) {
  homeLock = true;
  float dt = fminf((now - rbLastT) / 1000.0f, 0.04f);
  rbLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  // paddle control (not on the game-over screen)
  if (rbSt == RB_PLAY || rbSt == RB_INTRO) {
    float ax = axisX(), ay = axisY();
    float target = rbTh, maxStep = 11.0f * dt;
    bool have = false;
    if (in.touching) { target = atan2f(in.touchY - RB_CY, in.touchX - RB_CX); have = true; }
    else if (sqrtf(ax * ax + ay * ay) > 0.14f) { target = atan2f(ay, ax); have = true; }
    if (have) { float d = rbDiff(target, rbTh); rbTh = rbWrap(rbTh + constrain(d, -maxStep, maxStep)); }
    if (in.leftDown) rbTh = rbWrap(rbTh - 3.6f * dt);
    if (in.rightDown) rbTh = rbWrap(rbTh + 3.6f * dt);
  }
  switch (rbSt) {
    case RB_INTRO:
      rbUpdate(dt, now);
      if (now - rbStAt >= 2100) { rbSt = RB_PLAY; imuZero(); rbServeT = 1.4f; }
      break;
    case RB_PLAY: {
      bool both = (in.left && in.rightDown) || (in.right && in.leftDown) || in.tap;
      if (both) {
        bool stuck = false;
        for (auto &b : rbBall) if (b.on && b.stuck) stuck = true;
        if (stuck) rbLaunch(); else rbFireLaser(now);
      }
      rbUpdate(dt, now);
      break;
    }
    case RB_LOST:
      rbUpdate(dt, now);
      if (now - rbStAt > 1200) {
        if (--rbLives <= 0) {
          rbSt = RB_OVER; rbStAt = now;
          rbNewBest = saveBest("rbBest", rbScore);
          if (rbNewBest) { rbBest = rbScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
        } else { rbServe(); rbSt = RB_PLAY; }
      }
      break;
    case RB_LEVEL:
      rbUpdate(dt, now);
      if (now - rbStAt > 2000) { rbLevel++; rbBuildLevel(); rbServe(); rbSt = RB_PLAY; }
      break;
    case RB_OVER:
      rbUpdate(dt, now);
      if (now - rbStAt > 900) {
        if (in.right) { rbStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - rbLastRender >= 16) { rbLastRender = now; rbRender(now); }
  return true;
}

Game GAME_RINGBREAKER = {"Ring Breaker", "orbit the paddle, break rings", 0xF81F, rbOpts, 2, rbStart, rbFrame};
