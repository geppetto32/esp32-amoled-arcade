// pinball_game.cpp - Pinball. One compressed table (320 x 336 px) with pop bumpers, slingshots, drop targets,
// rollover lanes, a habitrail ramp and a plunger lane.
//   LEFT / RIGHT = flippers (hold keeps them raised); touch left / right half = flippers too
//   hold RIGHT = pull the plunger, release = launch (LEFT while pulling, or a screen tap, also launches)
//   SHAKE / flick the device = nudge the ball. Too many nudges in a row = TILT (flippers die until the ball drains)
//   a flipper press also rotates the lit top lanes (lane change)
// Missions: light all 3 top lanes = multiplier +1 (max x5); light all 3 drop targets = MULTIBALL; ramp x3 = jackpot.
// Ball physics runs at a fixed 240 Hz sub-step (max 4.6 px per step at top speed, ball radius 6: no tunnelling).
// Flippers hit with their real angular velocity (surface velocity at the contact point).
// OPTIONS: BALLS 3 / 5, THEME NEON / FIRE.
#include "engine.h"
#include <math.h>

static const char *const PN_BALLSV[] = {"3", "5"};
static const char *const PN_THEMES[] = {"NEON", "FIRE"};
static Option pnOpts[] = {{"BALLS", PN_BALLSV, 2, "pnBalls", 0}, {"THEME", PN_THEMES, 2, "pnTheme", 0}};

enum PnSt : uint8_t { PN_PLAY, PN_OVER };

static constexpr float PN_R = 6.0f, PN_G = 520.0f, PN_H = 1.0f / 240.0f, PN_VMAX = 1100.0f;
static constexpr int PN_MAXB = 3, PN_LANEX = 371, PN_FLOOR = 348;
static constexpr int PN_TL = 64, PN_TR = 384;                 // table left / right (lane outer wall)

struct PnSeg { int16_t x1, y1, x2, y2; uint8_t kind; };       // kind 0 wall, 1 slingshot (kicks), 2 one-way gate
static PnSeg pnSeg[48];
static int pnNSeg = 0;

struct PnBumper { int16_t x, y, r; };
static const PnBumper PN_BUMP[3] = {{150, 125, 16}, {212, 98, 16}, {252, 152, 16}};
static const int16_t PN_TGT[3][2] = {{92, 150}, {92, 178}, {92, 206}};   // drop-target pegs, r 7
static const int16_t PN_LANE[3][2] = {{171, 62}, {211, 62}, {251, 62}};  // rollover lights
static constexpr int PN_RAMP_X0 = 286, PN_RAMP_X1 = 312, PN_RAMP_Y0 = 150, PN_RAMP_Y1 = 214;

struct PnBall { float x, y, vx, vy; bool on; float slow; float tx[7], ty[7]; };
struct PnFlip { float px, py, s, a, adot; bool up; };

static PnBall pnB[PN_MAXB];
static PnFlip pnF[2];
static PnSt pnSt;
static int pnTheme, pnBalls, pnBallNo, pnMult;
static uint32_t pnScore, pnBest, pnLast, pnLastRender, pnOverAt;
static float pnAcc, pnCharge, pnSaveT, pnNudge, pnBx, pnBy, pnMsgT, pnTrailT;
static bool pnWait, pnTilt, pnNewBest, pnWasL, pnWasR;
static bool pnLaneLit[3], pnTgtLit[3];
static float pnBumpFlash[3], pnSlingFlash[2];
static uint32_t pnNudgeAt;
static int pnRampHits;
static char pnMsg[32];
static uint16_t *pnBg = nullptr;
static int pnBgTheme = -1;

// theme colours
static uint16_t pnColWall() { return pnTheme ? rgb(255, 150, 40) : rgb(60, 220, 255); }
static uint16_t pnColBump() { return pnTheme ? rgb(255, 60, 40) : rgb(255, 60, 200); }
static uint16_t pnColTgt() { return pnTheme ? rgb(255, 230, 80) : rgb(255, 230, 60); }
static uint16_t pnColLane() { return pnTheme ? rgb(255, 200, 60) : rgb(80, 255, 140); }
static uint16_t pnColRamp() { return pnTheme ? rgb(255, 90, 160) : rgb(150, 120, 255); }

static void pnAddSeg(int x1, int y1, int x2, int y2, uint8_t kind = 0) {
  if (pnNSeg < (int)(sizeof pnSeg / sizeof pnSeg[0])) pnSeg[pnNSeg++] = {(int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2, kind};
}
static void pnBuildSegs() {
  pnNSeg = 0;
  static const int16_t top[][2] = {{64, 112}, {66, 84}, {76, 62}, {94, 48}, {118, 39}, {150, 35}, {272, 35}, {310, 37},
                                   {344, 44}, {366, 58}, {380, 78}, {384, 100}, {384, 348}};
  for (size_t i = 0; i + 1 < sizeof top / sizeof top[0]; i++) pnAddSeg(top[i][0], top[i][1], top[i + 1][0], top[i + 1][1]);
  pnAddSeg(384, PN_FLOOR, 358, PN_FLOOR);                      // plunger lane floor
  pnAddSeg(358, 368, 358, 130);                                // lane inner wall
  pnAddSeg(64, 112, 64, 368);                                  // left wall
  pnAddSeg(359, 126, 383, 110, 2);                             // one-way gate at the top of the lane
  pnAddSeg(80, 262, 144, 320);  pnAddSeg(342, 262, 278, 320);  // inlane guides (outlanes drain)
  pnAddSeg(102, 222, 126, 262, 1); pnAddSeg(102, 222, 102, 262); pnAddSeg(102, 262, 126, 262);   // left slingshot
  pnAddSeg(320, 222, 296, 262, 1); pnAddSeg(320, 222, 320, 262); pnAddSeg(320, 262, 296, 262);   // right slingshot
  pnAddSeg(PN_RAMP_X0, PN_RAMP_Y1, PN_RAMP_X0, PN_RAMP_Y0);    // ramp rails
  pnAddSeg(PN_RAMP_X1, PN_RAMP_Y1, PN_RAMP_X1, PN_RAMP_Y0);
}

static void pnGlow(int x1, int y1, int x2, int y2, uint16_t c) {
  uint16_t d = lerpColor(c, C_BG, 0.78f);
  gfx->drawLine(x1 - 1, y1, x2 - 1, y2, d); gfx->drawLine(x1 + 1, y1, x2 + 1, y2, d);
  gfx->drawLine(x1, y1 - 1, x2, y2 - 1, d); gfx->drawLine(x1, y1 + 1, x2, y2 + 1, d);
  gfx->drawLine(x1, y1, x2, y2, c);
}

static void pnBuildBg() {
  if (!pnBg) pnBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!pnBg) return;
  gfx->fillScreen(C_BG);
  uint16_t wc = pnColWall();
  gfx->fillRect(PN_TL, 34, PN_TR - PN_TL, H - 34, pnTheme ? rgb(14, 5, 3) : rgb(3, 4, 14));       // playfield tint
  for (int i = 0; i < 40; i++) {                                                                   // faint dots
    uint32_t h = (uint32_t)i * 2654435761u;
    gfx->drawPixel(PN_TL + 6 + (h >> 8) % 300, 44 + (h >> 20) % 300, lerpColor(wc, C_BG, 0.85f));
  }
  for (int i = 0; i < pnNSeg; i++) {
    const PnSeg &s = pnSeg[i];
    uint16_t c = s.kind == 1 ? pnColBump() : (s.kind == 2 ? lerpColor(wc, C_BG, 0.5f) : wc);
    if (s.kind == 0 && s.x1 >= PN_RAMP_X0 && s.x1 <= PN_RAMP_X1 && s.y2 == PN_RAMP_Y0) c = pnColRamp();
    pnGlow(s.x1, s.y1, s.x2, s.y2, c);
  }
  for (int i = 0; i < 2; i++) {                                                                    // sling fill
    int d = i ? -1 : 1, bx = i ? 422 : 0;
    gfx->fillTriangle(bx + d * 102 + 0, 222, bx + d * 126, 262, bx + d * 102, 262, lerpColor(pnColBump(), C_BG, 0.82f));
  }
  for (auto &b : PN_BUMP) {
    gfx->drawCircle(b.x, b.y, b.r + 3, lerpColor(pnColBump(), C_BG, 0.7f));
    gfx->drawCircle(b.x, b.y, b.r + 5, lerpColor(pnColBump(), C_BG, 0.88f));
  }
  memcpy(pnBg, frameBuffer(), FB_PIXELS * 2);
  pnBgTheme = pnTheme;
}

// ---------------- physics ----------------
static bool pnColSeg(PnBall &b, const PnSeg &s, float &impact) {
  float dx = s.x2 - s.x1, dy = s.y2 - s.y1, l2 = dx * dx + dy * dy;
  float t = ((b.x - s.x1) * dx + (b.y - s.y1) * dy) / l2;
  t = constrain(t, 0.0f, 1.0f);
  float cx = s.x1 + t * dx, cy = s.y1 + t * dy, nx = b.x - cx, ny = b.y - cy, d2 = nx * nx + ny * ny;
  if (s.kind == 2) {                                           // one way: solid only from the upper side
    float len = sqrtf(l2), gx = dy / len, gy = -dx / len;
    if (gy > 0) { gx = -gx; gy = -gy; }
    if ((b.x - s.x1) * gx + (b.y - s.y1) * gy < 0) return false;
  }
  if (d2 >= PN_R * PN_R) return false;
  float d = sqrtf(d2);
  if (d < 1e-4f) { float len = sqrtf(l2); nx = dy / len; ny = -dx / len; d = 0; }
  else { nx /= d; ny /= d; }
  b.x += nx * (PN_R - d); b.y += ny * (PN_R - d);
  float vn = b.vx * nx + b.vy * ny;
  if (vn < 0) {
    float e = s.kind == 1 ? 0.55f : 0.45f;
    b.vx -= (1 + e) * vn * nx; b.vy -= (1 + e) * vn * ny;
    impact = -vn;
  }
  if (s.kind == 1 && -vn > 60) { b.vx += nx * 330; b.vy += ny * 330; impact = 1000; }
  return true;
}

static void pnAdd(uint32_t pts) { pnScore += pts * (uint32_t)pnMult; if (pnScore > pnBest) pnBest = pnScore; }
static void pnSay(const char *m, float t = 1.6f) { snprintf(pnMsg, sizeof pnMsg, "%s", m); pnMsgT = t; }

static void pnMultiball() {
  int added = 0;
  for (auto &b : pnB) {
    if (b.on || added >= 2) continue;
    b = {};
    b.x = 205.0f + 12 * added; b.y = 78; b.vx = added ? 90.0f : -90.0f; b.vy = 40; b.on = true;
    for (int k = 0; k < 7; k++) { b.tx[k] = b.x; b.ty[k] = b.y; }
    added++;
  }
  pnSay("MULTIBALL", 2.2f);
  tunePlay(TUNE_WIN);
}

static void pnTgtHit(int i) {
  if (!pnTgtLit[i]) { pnTgtLit[i] = true; pnAdd(500); play(1319, 20); play(1760, 30); }
  else pnAdd(50);
  if (pnTgtLit[0] && pnTgtLit[1] && pnTgtLit[2]) {
    pnAdd(5000);
    for (auto &t : pnTgtLit) t = false;
    pnMultiball();
  }
}

static void pnBallStep(PnBall &b, float h) {
  b.vy += PN_G * h;
  float sp2 = b.vx * b.vx + b.vy * b.vy;
  if (sp2 > PN_VMAX * PN_VMAX) { float k = PN_VMAX / sqrtf(sp2); b.vx *= k; b.vy *= k; }
  float prevY = b.y;
  b.x += b.vx * h; b.y += b.vy * h;
  float impact = 0;
  for (int i = 0; i < pnNSeg; i++) {
    impact = 0;
    if (pnColSeg(b, pnSeg[i], impact) && impact > 700 && pnSeg[i].kind == 1) {
      pnSlingFlash[pnSeg[i].x1 > 211 ? 1 : 0] = 0.12f;
      pnAdd(10); play(310, 12); ptBurst(b.x, b.y, 4, 90, C_TEXT, pnColBump(), 0.3f, 0, 2);
    }
  }
  for (int i = 0; i < 3; i++) {                                // pop bumpers
    float dx = b.x - PN_BUMP[i].x, dy = b.y - PN_BUMP[i].y, rr = PN_BUMP[i].r + PN_R, d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) continue;
    float d = sqrtf(d2); if (d < 1e-3f) { dx = 0; dy = -1; d = 1; }
    float nx = dx / d, ny = dy / d;
    b.x = PN_BUMP[i].x + nx * rr; b.y = PN_BUMP[i].y + ny * rr;
    b.vx = nx * 430; b.vy = ny * 430;
    pnBumpFlash[i] = 0.14f;
    pnAdd(100);
    play(880 + i * 120, 14); play(1320 + i * 120, 14);
    ptBurst(b.x, b.y, 7, 120, C_TEXT, pnColBump(), 0.35f, 0, 2);
  }
  for (int i = 0; i < 3; i++) {                                // drop-target pegs (r 7)
    float dx = b.x - PN_TGT[i][0], dy = b.y - PN_TGT[i][1], rr = 7 + PN_R, d2 = dx * dx + dy * dy;
    if (d2 >= rr * rr) continue;
    float d = sqrtf(d2); if (d < 1e-3f) { dx = 1; dy = 0; d = 1; }
    float nx = dx / d, ny = dy / d;
    b.x = PN_TGT[i][0] + nx * rr; b.y = PN_TGT[i][1] + ny * rr;
    float vn = b.vx * nx + b.vy * ny;
    if (vn < 0) { b.vx -= 1.5f * vn * nx; b.vy -= 1.5f * vn * ny; }
    pnTgtHit(i);
  }
  for (auto &f : pnF) {                                        // flippers (capsule, moving surface)
    float L = 56, ca = cosf(f.a), sa = sinf(f.a);
    float ex = f.s * ca * L, ey = sa * L, l2 = L * L;
    float t = ((b.x - f.px) * ex + (b.y - f.py) * ey) / l2;
    t = constrain(t, 0.0f, 1.0f);
    float cx = f.px + t * ex, cy = f.py + t * ey, rf = 7.0f - 3.0f * t, rr = PN_R + rf;
    float nx = b.x - cx, ny = b.y - cy, d2 = nx * nx + ny * ny;
    if (d2 >= rr * rr) continue;
    float d = sqrtf(d2); if (d < 1e-3f) { nx = 0; ny = -1; d = 1; } else { nx /= d; ny /= d; }
    b.x += nx * (rr - d); b.y += ny * (rr - d);
    float rad = t * L, svx = -f.s * sa * rad * f.adot, svy = ca * rad * f.adot;
    float vn = (b.vx - svx) * nx + (b.vy - svy) * ny;
    if (vn < 0) { b.vx -= (1.3f) * vn * nx; b.vy -= (1.3f) * vn * ny; }
  }
  // lane rollovers (no physics)
  for (int i = 0; i < 3; i++) {
    float dx = b.x - PN_LANE[i][0], dy = b.y - PN_LANE[i][1];
    if (dx * dx + dy * dy < 100 && !pnLaneLit[i]) { pnLaneLit[i] = true; pnAdd(250); play(988, 18); }
  }
  // ramp: count a pass going up through the tube
  if (prevY > 172 && b.y <= 172 && b.x > PN_RAMP_X0 + 2 && b.x < PN_RAMP_X1 - 2 && b.vy < 0) {
    pnRampHits++;
    pnAdd(1500);
    play(523, 30); play(659, 30); play(784, 30); play(1047, 40);
    ptBurst(b.x, 160, 14, 150, C_TEXT, pnColRamp(), 0.6f, 0, 2);
    if (pnRampHits % 3 == 0) { pnAdd(10000); pnSay("JACKPOT", 2.0f); tunePlay(TUNE_LEVEL); }
    else pnSay("RAMP", 0.9f);
  }
  // stuck rescue (a ball resting somewhere that is not the flippers / lane)
  float v2 = b.vx * b.vx + b.vy * b.vy;
  if (v2 < 40 && b.y < 290 && b.x < 356) b.slow += h; else b.slow = 0;
  if (b.slow > 2.5f) { b.vx = (esp_random() & 1 ? 1 : -1) * 130.0f; b.vy = -180; b.slow = 0; }
  if (b.x < PN_TL - 8 || b.x > PN_TR + 8 || b.y < 28) { b.x = 211; b.y = 80; b.vx = b.vy = 0; }   // safety net
  if (b.y > H - 2 && b.x < 356) b.on = false;                  // drained
  else if (b.y > H + 20) b.on = false;
}

static void pnLanesCheck() {
  if (pnLaneLit[0] && pnLaneLit[1] && pnLaneLit[2]) {
    for (auto &l : pnLaneLit) l = false;
    if (pnMult < 5) pnMult++;
    pnAdd(2500);
    char m[32]; snprintf(m, sizeof m, "MULTIPLIER x%d", pnMult); pnSay(m, 1.8f);
    play(784, 40); play(988, 40); play(1319, 80);
  }
}

static void pnSpawnLane() {
  for (auto &b : pnB) b.on = false;
  PnBall &b = pnB[0];
  b = {};
  b.x = PN_LANEX; b.y = PN_FLOOR - 7; b.on = true;
  for (int k = 0; k < 7; k++) { b.tx[k] = b.x; b.ty[k] = b.y; }
  pnWait = true; pnCharge = 0; pnTilt = false; pnNudge = 0; pnSaveT = 0;
  pnWasR = false;
}

static void pnStart() {
  pnTheme = pnOpts[1].value;
  pnBalls = pnOpts[0].value ? 5 : 3;
  if (pnBgTheme != pnTheme || !pnBg) { pnBuildSegs(); pnBuildBg(); }
  pnBest = loadBest("pnBest");
  pnScore = 0; pnMult = 1; pnBallNo = 1; pnNewBest = false; pnRampHits = 0; pnMsgT = 0; pnTrailT = 0;
  for (auto &l : pnLaneLit) l = false;
  for (auto &t : pnTgtLit) t = false;
  for (auto &x : pnBumpFlash) x = 0;
  pnSlingFlash[0] = pnSlingFlash[1] = 0;
  pnF[0] = {150, 322, 1, 0.52f, 0, false};
  pnF[1] = {272, 322, -1, 0.52f, 0, false};
  ptClear();
  pnSpawnLane();
  imuZero();
  pnBx = tiltX(); pnBy = tiltY();
  pnSt = PN_PLAY; pnAcc = 0; pnNudgeAt = 0;
  pnLast = millis(); pnLastRender = 0;
  sfxStart();
}

static void pnLaunch(float power) {
  PnBall &b = pnB[0];
  b.vx = 0; b.vy = -(640.0f + 440.0f * constrain(power, 0.0f, 1.0f));
  pnWait = false; pnSaveT = 7.0f;
  play(220, 20); play(440, 30); play(660, 40);
}

// ---------------- render ----------------
static void pnDrawFlip(const PnFlip &f, bool lit) {
  float ca = cosf(f.a), sa = sinf(f.a), L = 56;
  float tx = f.px + f.s * ca * L, ty = f.py + sa * L;
  uint16_t c = lit ? lerpColor(pnColTgt(), C_TEXT, 0.4f) : pnColWall();
  float nx = -sa * f.s, ny = ca;                                // perpendicular
  gfx->fillTriangle((int)(f.px + nx * 7), (int)(f.py + ny * 7), (int)(f.px - nx * 7), (int)(f.py - ny * 7),
                    (int)(tx + nx * 3), (int)(ty + ny * 3), lerpColor(c, C_BG, 0.55f));
  gfx->fillTriangle((int)(f.px - nx * 7), (int)(f.py - ny * 7), (int)(tx + nx * 3), (int)(ty + ny * 3),
                    (int)(tx - nx * 3), (int)(ty - ny * 3), lerpColor(c, C_BG, 0.55f));
  gfx->fillCircle((int)f.px, (int)f.py, 7, lerpColor(c, C_BG, 0.55f));
  gfx->fillCircle((int)tx, (int)ty, 3, lerpColor(c, C_BG, 0.55f));
  gfx->drawLine((int)(f.px + nx * 7), (int)(f.py + ny * 7), (int)(tx + nx * 3), (int)(ty + ny * 3), c);
  gfx->drawLine((int)(f.px - nx * 7), (int)(f.py - ny * 7), (int)(tx - nx * 3), (int)(ty - ny * 3), c);
  gfx->drawCircle((int)f.px, (int)f.py, 7, c);
  gfx->drawCircle((int)tx, (int)ty, 3, c);
  gfx->fillCircle((int)f.px, (int)f.py, 2, C_TEXT);
}

static void pnRender(uint32_t now) {
  if (pnBgTheme != pnTheme || !pnBg) pnBuildBg();
  if (pnBg) memcpy(frameBuffer(), pnBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  uint16_t bc = pnColBump(), tc = pnColTgt(), lc = pnColLane();
  for (int i = 0; i < 3; i++) {                                // bumpers
    float fl = pnBumpFlash[i] > 0 ? pnBumpFlash[i] / 0.14f : 0;
    uint16_t c = lerpColor(bc, C_TEXT, fl);
    gfx->fillCircle(PN_BUMP[i].x, PN_BUMP[i].y, PN_BUMP[i].r, lerpColor(c, C_BG, 0.65f - 0.4f * fl));
    gfx->drawCircle(PN_BUMP[i].x, PN_BUMP[i].y, PN_BUMP[i].r, c);
    gfx->drawCircle(PN_BUMP[i].x, PN_BUMP[i].y, PN_BUMP[i].r - 4, lerpColor(c, C_BG, 0.4f));
    gfx->fillCircle(PN_BUMP[i].x, PN_BUMP[i].y, 3, C_TEXT);
  }
  for (int i = 0; i < 2; i++)                                  // sling flash
    if (pnSlingFlash[i] > 0) {
      int d = i ? -1 : 1, bx = i ? 422 : 0;
      gfx->drawLine(bx + d * 102, 222, bx + d * 126, 262, C_TEXT);
      gfx->drawLine(bx + d * 103, 222, bx + d * 127, 262, C_TEXT);
    }
  for (int i = 0; i < 3; i++) {                                // targets + lanes
    uint16_t c = pnTgtLit[i] ? tc : lerpColor(tc, C_BG, 0.7f);
    gfx->fillCircle(PN_TGT[i][0], PN_TGT[i][1], 7, lerpColor(c, C_BG, pnTgtLit[i] ? 0.2f : 0.7f));
    gfx->drawCircle(PN_TGT[i][0], PN_TGT[i][1], 7, c);
    gfx->drawCircle(PN_TGT[i][0], PN_TGT[i][1], 10, lerpColor(c, C_BG, 0.6f));
    uint16_t l = pnLaneLit[i] ? lc : lerpColor(lc, C_BG, 0.78f);
    gfx->fillCircle(PN_LANE[i][0], PN_LANE[i][1], pnLaneLit[i] ? 6 : 4, l);
    if (pnLaneLit[i]) gfx->drawCircle(PN_LANE[i][0], PN_LANE[i][1], 9, lerpColor(lc, C_BG, 0.55f));
  }
  float rp = 0.5f + 0.5f * sinf(now * 0.008f);                 // ramp entry arrow
  gfx->fillTriangle((PN_RAMP_X0 + PN_RAMP_X1) / 2, PN_RAMP_Y0 + 10, (PN_RAMP_X0 + PN_RAMP_X1) / 2 - 6, PN_RAMP_Y0 + 22,
                    (PN_RAMP_X0 + PN_RAMP_X1) / 2 + 6, PN_RAMP_Y0 + 22, lerpColor(pnColRamp(), C_BG, 0.7f - 0.5f * rp));
  gfx->fillRect(PN_LANEX - 11, PN_FLOOR - 1, 22, 3, lerpColor(pnColWall(), C_TEXT, 0.4f));   // plunger pad
  if (pnWait) {                                                 // plunger spring + power bar
    int pull = (int)(pnCharge * 16);
    gfx->fillRect(PN_LANEX - 9, PN_FLOOR + 3, 18, 4 + pull, lerpColor(C_DIM, C_BG, 0.4f));
    gfx->drawRect(PN_TR + 12, 120, 12, 190, lerpColor(pnColWall(), C_BG, 0.5f));
    int fh = (int)(188 * pnCharge);
    gfx->fillRect(PN_TR + 13, 309 - fh, 10, fh, lerpColor(C_GOOD, C_BAD, pnCharge));
  }
  pnDrawFlip(pnF[0], pnF[0].up);
  pnDrawFlip(pnF[1], pnF[1].up);
  for (auto &b : pnB) {                                         // balls with a trail
    if (!b.on) continue;
    for (int k = 0; k < 7; k++) {
      int tr = 7 - k;
      gfx->fillCircle((int)b.tx[k], (int)b.ty[k], 4 - tr / 3 > 1 ? 4 - tr / 3 : 1, lerpColor(pnColWall(), C_BG, 0.35f + tr * 0.09f));
    }
    gfx->fillCircle((int)b.x, (int)b.y, 9, lerpColor(pnColWall(), C_BG, 0.78f));
    gfx->fillCircle((int)b.x, (int)b.y, (int)PN_R, rgb(225, 235, 245));
    gfx->fillCircle((int)b.x - 2, (int)b.y - 2, 2, C_TEXT);
  }
  ptDraw();
  hudBar(pnScore, pnBest, rgb(8, 8, 20), C_TEXT);
  char s[24];
  gfx->fillRect(0, 34, PN_TL, H - 34, C_BG);                    // side panels
  gfx->fillRect(PN_TR + 1, 34, W - PN_TR - 1, H - 34, C_BG);
  if (pnWait) {                                                 // redraw power bar (panel clear above removed it)
    gfx->drawRect(PN_TR + 12, 120, 12, 190, lerpColor(pnColWall(), C_BG, 0.5f));
    int fh = (int)(188 * pnCharge);
    gfx->fillRect(PN_TR + 13, 309 - fh, 10, fh, lerpColor(C_GOOD, C_BAD, pnCharge));
    text("PULL", PN_TR + 8, 100, 1, C_DIM);
  }
  text("BALL", 14, 50, 1, C_DIM);
  snprintf(s, sizeof s, "%d/%d", pnBallNo > pnBalls ? pnBalls : pnBallNo, pnBalls);
  text(s, 12, 64, 2, C_TEXT);
  text("MULT", 14, 100, 1, C_DIM);
  snprintf(s, sizeof s, "x%d", pnMult);
  text(s, 14, 114, 3, pnColTgt());
  text("RAMP", 14, 160, 1, C_DIM);
  snprintf(s, sizeof s, "%d", pnRampHits);
  text(s, 14, 174, 2, pnColRamp());
  if (pnSaveT > 0) text("SAVE", 14, 210, 1, C_GOOD);
  if (pnNudge > 2.4f && !pnTilt) text("DANGER", 8, 230, 1, C_BAD);
  if (pnTilt) text("TILT", 14, 230, 2, C_BAD);
  if (pnWait && pnBallNo == 1 && pnScore == 0) {
    centerText("LEFT / RIGHT flippers", 252, 1, C_DIM);
    centerText("hold RIGHT = plunger, release = launch", 266, 1, C_DIM);
    centerText("shake = nudge (careful: TILT)", 280, 1, C_DIM);
  }
  if (pnMsgT > 0) centerText(pnMsg, 220, 2, pnColTgt());
  if (pnSt == PN_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, pnNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)pnScore);
    panelBox("GAME OVER", l2, nullptr, pnColWall());
    centerText("RIGHT again   LEFT menu", 226, 1, C_DIM);
  }
  dirty = true;
}

// ---------------- frame ----------------
static void pnUpdateFlippers(float h, bool wantL, bool wantR) {
  bool want[2] = {wantL, wantR};
  for (int i = 0; i < 2; i++) {
    PnFlip &f = pnF[i];
    float target = want[i] ? -0.5f : 0.52f, old = f.a, rate = want[i] ? 12.0f : 8.0f;
    if (f.a < target) f.a = fminf(target, f.a + rate * h); else f.a = fmaxf(target, f.a - rate * h);
    f.adot = (f.a - old) / h;
    f.up = want[i];
  }
}

static bool pnFrame(uint32_t now) {
  homeLock = true;                                   // fast BOOT taps are the flippers: no triple-tap home
  float dt = fminf((now - pnLast) / 1000.0f, 0.05f);
  pnLast = now;
  tuneUpdate(now);
  ptUpdate(dt);
  if (pnMsgT > 0) pnMsgT -= dt;
  for (auto &x : pnBumpFlash) if (x > 0) x -= dt;
  for (auto &x : pnSlingFlash) if (x > 0) x -= dt;

  if (pnSt == PN_OVER) {
    if (now - pnOverAt > 900) {
      if (in.right) { pnStart(); return true; }
      if (in.left) return false;
    }
    if (now - pnLastRender >= 16) { pnLastRender = now; pnRender(now); }
    return true;
  }

  bool wantL = !pnTilt && (in.leftDown || (in.touching && in.touchX < W / 2));
  bool wantR = !pnTilt && (in.rightDown || (in.touching && in.touchX >= W / 2));
  if (wantL && !pnWasL) {                            // lane change + click
    bool t = pnLaneLit[0]; pnLaneLit[0] = pnLaneLit[1]; pnLaneLit[1] = pnLaneLit[2]; pnLaneLit[2] = t;
    play(190, 6);
  }
  if (wantR && !pnWasR) {
    bool t = pnLaneLit[2]; pnLaneLit[2] = pnLaneLit[1]; pnLaneLit[1] = pnLaneLit[0]; pnLaneLit[0] = t;
    play(190, 6);
  }
  pnWasL = wantL; pnWasR = wantR;

  // plunger
  if (pnWait) {
    if (in.rightDown) pnCharge = fminf(1.0f, pnCharge + dt / 0.9f);
    bool fire = false;
    if (pnCharge > 0.02f && (!in.rightDown || in.leftDown)) fire = true;      // release, or both buttons
    if (in.tap && in.tapX >= PN_TL) { pnCharge = fmaxf(pnCharge, 0.85f); fire = true; }
    if (fire) pnLaunch(pnCharge);
  } else {
    // nudge detector: high-pass the tilt, fire on a quick change
    if (imuOk) {
      float x = tiltX(), y = tiltY(), k = 1.0f - expf(-dt / 0.5f);
      pnBx += (x - pnBx) * k; pnBy += (y - pnBy) * k;
      float dx = x - pnBx, dy = y - pnBy;
      if (now - pnNudgeAt > 380 && !pnTilt && (fabsf(dx) > 0.32f || dy < -0.32f)) {
        pnNudgeAt = now;
        for (auto &b : pnB) if (b.on && b.x < 356) {
          if (fabsf(dx) > -dy) b.vx += (dx > 0 ? 1 : -1) * 140.0f;
          else b.vy -= 110.0f;
        }
        pnNudge += 1.0f;
        play(140, 20);
        if (pnNudge >= 3.7f) { pnTilt = true; pnSay("TILT", 2.5f); play(110, 120); play(90, 200); }
      }
    }
    pnNudge = fmaxf(0.0f, pnNudge - dt * 0.45f);
    if (pnSaveT > 0) pnSaveT -= dt;
  }

  // fixed-step physics
  pnAcc += dt;
  int steps = 0;
  while (pnAcc >= PN_H && steps < 12) {
    pnAcc -= PN_H; steps++;
    pnUpdateFlippers(PN_H, wantL, wantR);
    for (auto &b : pnB) if (b.on) pnBallStep(b, PN_H);
  }
  if (pnAcc > PN_H * 12) pnAcc = 0;
  pnLanesCheck();

  // ball trail (sampled at 50 Hz)
  pnTrailT += dt;
  if (pnTrailT > 0.02f) {
    pnTrailT = 0;
    for (auto &b : pnB) {
      if (!b.on) continue;
      for (int k = 0; k < 6; k++) { b.tx[k] = b.tx[k + 1]; b.ty[k] = b.ty[k + 1]; }
      b.tx[6] = b.x; b.ty[6] = b.y;
    }
  }

  // draining
  int alive = 0;
  for (auto &b : pnB) alive += b.on;
  if (alive == 0 && !pnWait) {
    if (pnSaveT > 0 && !pnTilt) {
      pnSay("BALL SAVED", 1.6f); pnSpawnLane(); play(660, 40); play(990, 60);
    } else {
      pnBallNo++;
      if (pnBallNo > pnBalls) {
        pnSt = PN_OVER; pnOverAt = now;
        pnNewBest = saveBest("pnBest", pnScore);
        if (pnNewBest) { pnBest = pnScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
      } else {
        pnSpawnLane(); tunePlay(TUNE_LOSE);
        pnMult = pnMult > 1 ? pnMult - 1 : 1;
        char m[16]; snprintf(m, sizeof m, "BALL %d", pnBallNo); pnSay(m, 1.4f);
      }
    }
  }
  if (now - pnLastRender >= 16) { pnLastRender = now; pnRender(now); }
  return true;
}

Game GAME_PINBALL = {"PINBALL", "flippers, bumpers, multiball", 0xF81F, pnOpts, 2, pnStart, pnFrame};
