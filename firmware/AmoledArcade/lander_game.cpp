// lander_game.cpp - Lander: land a glowing craft on a pad of a procedural mountain range.
//   TILT (roll) rotates the ship
//   hold LEFT or RIGHT = main thrust (one button 60 %, both buttons = full burn); touching the screen = 60 % thrust
// Fuel is limited. A soft touchdown on a pad (slow, upright) scores by speed, angle, fuel left and the pad multiplier;
// the next level has smaller pads, rougher ground and a bit less spare fuel. Three ships per run.
// PLANET: MOON (gentle), MARS (stronger gravity), IO (strongest gravity + wind gusts).
#include "engine.h"
#include <math.h>

static const char *const LN_PLANETS[] = {"MOON", "MARS", "IO"};
static Option lnOpts[] = {{"PLANET", LN_PLANETS, 3, "lnPlanet", 0}};

enum LnSt : uint8_t { LN_COUNT, LN_FLY, LN_LANDED, LN_CRASH, LN_OVER };

static constexpr int LN_SEG = 14, LN_NPT = W / LN_SEG + 1;       // terrain points every 14 px (33)
static constexpr float LN_VY_MAX = 42.0f, LN_VX_MAX = 24.0f, LN_ANG_MAX = 0.21f, LN_THRUST = 205.0f;

struct LnPlanet { const char *name; float g, fuel, wind; uint16_t sky0, sky1, line, fill0, fill1, star; };
static const LnPlanet LN_P[3] = {
  {"MOON", 36.0f, 100.0f, 0.0f, rgb(0, 0, 6), rgb(14, 14, 34), rgb(210, 220, 245), rgb(52, 56, 78), rgb(8, 8, 14), rgb(200, 200, 230)},
  {"MARS", 62.0f, 115.0f, 0.0f, rgb(10, 2, 2), rgb(86, 30, 12), rgb(255, 130, 80), rgb(96, 34, 18), rgb(14, 4, 2), rgb(255, 200, 170)},
  {"IO", 84.0f, 130.0f, 22.0f, rgb(14, 10, 0), rgb(92, 70, 6), rgb(255, 236, 70), rgb(112, 86, 8), rgb(18, 14, 0), rgb(255, 240, 160)},
};
struct LnPad { int i0, i1, mult; float y; };

static float lnTY[LN_NPT];
static LnPad lnPad[2];
static LnSt lnSt;
static int lnPlanet, lnLevel, lnShips, lnPts, lnMult;
static float lnX, lnY, lnVx, lnVy, lnAng, lnFuel, lnFuelStart, lnThr, lnWindT;
static uint32_t lnScore, lnBest, lnStAt, lnLastT, lnLastRender, lnLastRumble, lnSeed;
static bool lnNewBest, lnPadHit;
static uint16_t *lnBg = nullptr;
static char lnMsg[32];

static const char *lnBestKey() { static const char *k[3] = {"lnBestM", "lnBestR", "lnBestI"}; return k[lnPlanet]; }
static float lnRand() { return (esp_random() & 0xFFFF) / 65535.0f; }
static inline uint32_t lnHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static float lnTerrainY(float x) {
  if (x <= 0) return lnTY[0];
  if (x >= W) return lnTY[LN_NPT - 1];
  int i = (int)(x / LN_SEG);
  float f = x / LN_SEG - i;
  return lnTY[i] + (lnTY[i + 1] - lnTY[i]) * f;
}

// ---------------- terrain ----------------
static void lnGenTerrain() {
  float p1 = lnRand() * 6.28f, p2 = lnRand() * 6.28f, p3 = lnRand() * 6.28f;
  float amp = fminf(1.0f + 0.12f * lnLevel, 1.7f);
  for (int i = 0; i < LN_NPT; i++) {
    float x = (float)(i * LN_SEG);
    float y = 282.0f + amp * (30.0f * sinf(x * 0.012f + p1) + 18.0f * sinf(x * 0.031f + p2) + 8.0f * sinf(x * 0.07f + p3));
    lnTY[i] = constrain(y, 224.0f, 346.0f);
  }
  int nA = max(3, 7 - lnLevel), nB = max(2, 4 - lnLevel / 2);
  int sA = 2 + (int)(esp_random() % (uint32_t)(LN_NPT - nA - 4));
  int sB;
  for (int tries = 0; tries < 30; tries++) {
    sB = 2 + (int)(esp_random() % (uint32_t)(LN_NPT - nB - 4));
    if (sB + nB + 2 < sA || sB > sA + nA + 2) break;
  }
  if (!(sB + nB + 2 < sA || sB > sA + nA + 2)) sB = sA >= 12 ? 2 : LN_NPT - nB - 3;
  lnPad[0] = {sA, sA + nA, 1, 0};
  lnPad[1] = {sB, sB + nB, nB >= 3 ? 2 : 3, 0};
  for (auto &p : lnPad) {
    float s = 0; for (int i = p.i0; i <= p.i1; i++) s += lnTY[i];
    p.y = s / (p.i1 - p.i0 + 1);
    for (int i = p.i0; i <= p.i1; i++) lnTY[i] = p.y;
  }
}

static void lnBuildBg() {
  if (!lnBg) lnBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!lnBg) return;
  const LnPlanet &P = LN_P[lnPlanet];
  for (int y = 0; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(P.sky0, P.sky1, constrain((y - 30) / 320.0f, 0.0f, 1.0f)));
  for (int i = 0; i < 80; i++) {
    uint32_t h = lnHash(i, lnSeed);
    int x = h % W, y = 34 + (h >> 9) % 220, b = (h >> 20) & 3;
    gfx->drawPixel(x, y, lerpColor(P.star, P.sky0, 0.35f + 0.15f * b));
  }
  if (lnPlanet == 1) {                                               // Mars: two faint moons
    gfx->fillCircle(360, 80, 9, rgb(120, 70, 50)); gfx->fillCircle(90, 130, 5, rgb(110, 64, 46));
  } else if (lnPlanet == 0) {                                        // Earth
    gfx->fillCircle(370, 76, 20, rgb(14, 40, 120)); gfx->fillCircle(364, 70, 8, rgb(30, 130, 70));
    gfx->drawCircle(370, 76, 21, rgb(70, 110, 200));
  } else {                                                           // Io: Jupiter band
    for (int r = 54; r > 20; r -= 6) gfx->fillCircle(380, 70, r, lerpColor(rgb(30, 20, 4), rgb(120, 70, 30), (54 - r) / 34.0f));
  }
  for (int x = 0; x < W; x++) {                                      // ground fill + glow line
    int ty = (int)lnTerrainY((float)x);
    for (int y = ty; y < H; y += 1) {
      float t = (y - ty) / (float)(H - ty + 1);
      uint16_t c = lerpColor(P.fill0, P.fill1, constrain(t * 1.4f, 0.0f, 1.0f));
      if (((x * 7 + y * 13) & 31) == 0) c = lerpColor(c, P.line, 0.25f);   // speckle
      gfx->drawPixel(x, y, c);
    }
  }
  for (int x = 0; x < W - 1; x++) {
    float y0 = lnTerrainY((float)x), y1 = lnTerrainY((float)x + 1);
    gfx->drawLine(x, (int)y0 - 2, x + 1, (int)y1 - 2, lerpColor(P.line, P.sky1, 0.78f));
    gfx->drawLine(x, (int)y0 - 1, x + 1, (int)y1 - 1, lerpColor(P.line, P.sky1, 0.45f));
    gfx->drawLine(x, (int)y0, x + 1, (int)y1, P.line);
  }
  for (auto &p : lnPad) {                                            // pads
    int x0 = p.i0 * LN_SEG, x1 = p.i1 * LN_SEG, y = (int)p.y;
    gfx->fillRect(x0, y - 1, x1 - x0, 5, rgb(255, 255, 255));
    gfx->fillRect(x0, y + 4, x1 - x0, 3, lerpColor(P.line, C_BG, 0.5f));
    gfx->fillRect(x0 + 2, y + 8, 3, H - y, lerpColor(P.fill0, C_BG, 0.4f));
    char b[8];
    snprintf(b, sizeof b, "x%d", p.mult);
    text(b, (x0 + x1) / 2 - 12, y + 12, 2, p.mult > 1 ? C_ACC : C_DIM);
  }
  memcpy(lnBg, frameBuffer(), FB_PIXELS * 2);
}

// ---------------- ship ----------------
static void lnWorld(float lx, float ly, float &wx, float &wy) {
  float c = cosf(lnAng), s = sinf(lnAng);
  wx = lnX + lx * c - ly * s;
  wy = lnY + lx * s + ly * c;
}
static const int8_t LN_HULL[8][2] = {{0, -14}, {7, -8}, {8, 2}, {5, 6}, {-5, 6}, {-8, 2}, {-7, -8}, {0, -14}};

static void lnDrawShip(bool flame) {
  uint16_t c = rgb(120, 240, 255), g1 = rgb(20, 70, 110), g2 = rgb(40, 130, 180);
  float ax, ay, bx, by;
  if (flame && lnThr > 0) {                                          // flame triangle (two layers)
    float len = 10.0f + 22.0f * lnThr + (float)(esp_random() % 6);
    float lx, ly, rx, ry, tx, ty;
    lnWorld(-4, 6, lx, ly); lnWorld(4, 6, rx, ry); lnWorld(0, 6 + len, tx, ty);
    gfx->fillTriangle((int)lx, (int)ly, (int)rx, (int)ry, (int)tx, (int)ty, rgb(255, 110, 20));
    float t2x, t2y; lnWorld(0, 6 + len * 0.55f, t2x, t2y);
    lnWorld(-2.5f, 6, lx, ly); lnWorld(2.5f, 6, rx, ry);
    gfx->fillTriangle((int)lx, (int)ly, (int)rx, (int)ry, (int)t2x, (int)t2y, rgb(255, 240, 160));
  }
  for (int pass = 0; pass < 3; pass++) {
    uint16_t col = pass == 0 ? g1 : pass == 1 ? g2 : c;
    int ox = pass == 0 ? 1 : 0, oy = pass == 0 ? 1 : 0;
    for (int i = 0; i < 7; i++) {
      lnWorld(LN_HULL[i][0], LN_HULL[i][1], ax, ay); lnWorld(LN_HULL[i + 1][0], LN_HULL[i + 1][1], bx, by);
      gfx->drawLine((int)ax + ox, (int)ay + oy, (int)bx + ox, (int)by + oy, col);
      if (pass == 0) gfx->drawLine((int)ax - 1, (int)ay, (int)bx - 1, (int)by, col);
    }
    for (int s = -1; s <= 1; s += 2) {                               // legs and foot bars
      lnWorld(s * 6.0f, 6, ax, ay); lnWorld(s * 10.0f, 12, bx, by);
      gfx->drawLine((int)ax + ox, (int)ay + oy, (int)bx + ox, (int)by + oy, col);
      float cx2, cy2;
      lnWorld(s * 7.0f, 12, cx2, cy2); lnWorld(s * 13.0f, 12, ax, ay);
      gfx->drawLine((int)cx2 + ox, (int)cy2 + oy, (int)ax + ox, (int)ay + oy, col);
    }
  }
  lnWorld(0, -4, ax, ay);
  gfx->fillCircle((int)ax, (int)ay, 3, rgb(255, 230, 120));
}

static void lnRender(uint32_t now) {
  const LnPlanet &P = LN_P[lnPlanet];
  if (lnBg) memcpy(frameBuffer(), lnBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  bool blink = (now / 300) & 1;
  for (auto &p : lnPad) {                                            // pad lights
    int x0 = p.i0 * LN_SEG, x1 = p.i1 * LN_SEG, y = (int)p.y;
    for (int i = 0; i <= 3; i++) {
      int x = x0 + (x1 - x0) * i / 3;
      bool on = ((i & 1) != 0) == blink;
      gfx->fillCircle(x, y - 4, 2, on ? C_GOOD : rgb(20, 80, 30));
      if (on) gfx->drawCircle(x, y - 4, 4, rgb(30, 110, 50));
    }
  }
  if (P.wind > 0 && lnSt == LN_FLY) {                                // wind streaks
    float w = P.wind * sinf(lnWindT);
    for (int i = 0; i < 6; i++) {
      uint32_t h = lnHash(i, 99);
      float x = fmodf((float)(h % W) + now * 0.08f * w * (1.0f + (h & 3) * 0.3f) + W * 10, (float)W);
      int y = 60 + (int)(h >> 8) % 160;
      gfx->drawFastHLine((int)x, y, (int)(6 + fabsf(w) * 0.6f), lerpColor(P.line, P.sky1, 0.7f));
    }
  }
  bool alive = lnSt == LN_FLY || lnSt == LN_COUNT || lnSt == LN_LANDED;
  if (alive) lnDrawShip(lnSt == LN_FLY);
  ptDraw();
  hudBar(lnScore, lnBest, rgb(8, 8, 20), C_TEXT);
  char b[32];
  text(P.name, 262, 9, 2, P.line);
  // status line: fuel bar + speeds
  gfx->fillRect(0, 32, W, 22, rgb(6, 6, 16));
  text("FUEL", 8, 38, 1, C_DIM);
  gfx->drawRect(38, 36, 112, 10, 0x4208);
  float fr = constrain(lnFuel / (P.fuel + 40.0f), 0.0f, 1.0f);
  gfx->fillRect(40, 38, (int)(108 * fr), 6, lnFuel < 20 ? C_BAD : lerpColor(C_ACC, C_GOOD, fr));
  bool vyOk = lnVy <= LN_VY_MAX, vxOk = fabsf(lnVx) <= LN_VX_MAX, aOk = fabsf(lnAng) <= LN_ANG_MAX;
  snprintf(b, sizeof b, "VY %3d", (int)lnVy);
  text(b, 164, 38, 1, vyOk ? C_GOOD : C_BAD);
  snprintf(b, sizeof b, "VX %3d", (int)lnVx);
  text(b, 214, 38, 1, vxOk ? C_GOOD : C_BAD);
  snprintf(b, sizeof b, "ANG %3d", (int)(lnAng * 57.3f));
  text(b, 264, 38, 1, aOk ? C_GOOD : C_BAD);
  for (int i = 0; i < 3; i++) {                                      // ships left
    int x = 372 + i * 22;
    if (i < lnShips) gfx->fillTriangle(x, 36, x - 6, 50, x + 6, 50, C_ACC);
    else gfx->drawTriangle(x, 36, x - 6, 50, x + 6, 50, 0x4208);
  }
  if (P.wind > 0 && lnSt == LN_FLY) {
    float w = P.wind * sinf(lnWindT);
    int cx = W / 2, y = 62;
    text("WIND", cx - 40, y - 3, 1, C_DIM);
    gfx->drawFastHLine(cx, y, (int)w, P.line);
    if (fabsf(w) > 4) gfx->fillTriangle(cx + (int)w, y, cx + (int)w - (w > 0 ? 5 : -5), y - 3, cx + (int)w - (w > 0 ? 5 : -5), y + 3, P.line);
  }
  uint16_t acc = P.line;
  if (lnSt == LN_COUNT) {
    gfx->fillRoundRect(40, 110, W - 80, 118, 16, C_PANEL);
    gfx->drawRoundRect(40, 110, W - 80, 118, 16, acc);
    snprintf(b, sizeof b, "LEVEL %d  %s", lnLevel, P.name);
    centerText(b, 122, 3, acc);
    centerText("TILT = rotate   hold button = thrust", 160, 1, C_TEXT);
    centerText("land slowly and upright on a pad", 178, 1, C_DIM);
    centerText("home: tap BOOT 3x, or hold top-left corner", 196, 1, C_DIM);
  } else if (lnSt == LN_LANDED) {
    char l2[32];
    snprintf(l2, sizeof l2, "+%d", lnPts);
    panelBox("LANDED", l2, lnMult > 1 ? "pad bonus applied" : "", acc);
  } else if (lnSt == LN_CRASH) {
    if (now - lnStAt > 500) {
      char l3[40];
      snprintf(l3, sizeof l3, "%s", lnMsg);
      panelBox("CRASHED", lnShips > 0 ? "ship lost" : "no ships left", l3, C_BAD);
    }
  } else if (lnSt == LN_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, lnNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)lnScore);
    panelBox("GAME OVER", l2, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

// ---------------- game flow ----------------
static void lnResetShip(uint32_t now) {
  lnX = 50.0f + lnRand() * (W - 100.0f); lnY = 84.0f;
  lnVx = (lnRand() - 0.5f) * 50.0f; lnVy = 0; lnAng = 0; lnThr = 0;
  lnFuel = lnFuelStart;
  lnSt = LN_COUNT; lnStAt = now;
  ptClear();
}
static void lnNewLevel(uint32_t now) {
  lnSeed = esp_random();
  lnGenTerrain();
  lnBuildBg();
  lnResetShip(now);
}
static void lnStart() {
  lnPlanet = lnOpts[0].value;
  lnBest = loadBest(lnBestKey());
  lnScore = 0; lnLevel = 1; lnShips = 3; lnNewBest = false;
  lnFuelStart = LN_P[lnPlanet].fuel;
  lnWindT = lnRand() * 6.0f;
  lnLastT = millis(); lnLastRender = 0; lnLastRumble = 0;
  lnNewLevel(millis());
  sfxStart();
}

static void lnCrash(uint32_t now, const char *why) {
  lnSt = LN_CRASH; lnStAt = now;
  lnShips--;
  snprintf(lnMsg, sizeof lnMsg, "%s", why);
  ptBurst(lnX, lnY, 40, 180, rgb(255, 230, 140), rgb(160, 30, 10), 1.0f, 180.0f, 3);
  ptBurst(lnX, lnY, 24, 110, rgb(200, 220, 255), C_BG, 1.3f, 120.0f, 2);
  play(130, 80); play(90, 140); play(55, 260);
}

static void lnLandOk(uint32_t now) {
  const LnPlanet &P = LN_P[lnPlanet];
  (void)P;
  lnMult = lnPad[lnPadHit ? 1 : 0].mult;
  float sp = 1.0f - lnVy / LN_VY_MAX, an = 1.0f - fabsf(lnAng) / LN_ANG_MAX;
  lnPts = (100 + (int)(lnFuel * 2.0f) + (int)(80 * constrain(sp, 0.0f, 1.0f)) + (int)(40 * constrain(an, 0.0f, 1.0f)) + 50 * lnLevel) * lnMult;
  lnScore += lnPts;
  if (lnScore > lnBest) lnBest = lnScore;
  lnVx = lnVy = 0; lnThr = 0;
  lnSt = LN_LANDED; lnStAt = now;
  tunePlay(TUNE_LEVEL);
  ptBurst(lnX, lnY + 12, 18, 70, rgb(255, 255, 200), C_BG, 0.8f, 0, 2);
}

static void lnPhysics(float dt, uint32_t now) {
  const LnPlanet &P = LN_P[lnPlanet];
  // rotation from tilt
  float tgt = 0;
  if (imuOk) {
    float t = axisX();
    float sh = fabsf(t) < 0.04f ? 0.0f : (t > 0 ? t - 0.04f : t + 0.04f);
    tgt = constrain(sh * 2.3f, -1.2f, 1.2f);
  }
  lnAng += (tgt - lnAng) * (1.0f - expf(-dt * 12.0f));
  // thrust
  float thr = 0;
  if (lnFuel > 0) {
    if (in.leftDown && in.rightDown) thr = 1.0f;
    else if (in.leftDown || in.rightDown || in.touching) thr = 0.6f;
  }
  lnThr = thr;
  lnFuel = fmaxf(0.0f, lnFuel - thr * 13.0f * dt);
  lnWindT += dt * 0.8f;
  float wind = P.wind * sinf(lnWindT) * (1.0f + 0.5f * sinf(lnWindT * 2.7f));
  const int SUB = 2;
  float h = dt / SUB;
  for (int s = 0; s < SUB; s++) {
    float ax = sinf(lnAng) * LN_THRUST * thr + wind, ay = P.g - cosf(lnAng) * LN_THRUST * thr;
    lnVx += ax * h; lnVy += ay * h;
    lnX += lnVx * h; lnY += lnVy * h;
    if (lnX < 12) { lnX = 12; lnVx = fmaxf(lnVx, 0); }
    if (lnX > W - 12) { lnX = W - 12; lnVx = fminf(lnVx, 0); }
    if (lnY < 62) { lnY = 62; lnVy = fmaxf(lnVy, 0); }
    // collision
    float fxl, fyl, fxr, fyr;
    lnWorld(-10, 12, fxl, fyl); lnWorld(10, 12, fxr, fyr);
    bool footL = fyl >= lnTerrainY(fxl), footR = fyr >= lnTerrainY(fxr);
    static const int8_t pts[5][2] = {{0, -14}, {-8, -6}, {8, -6}, {-9, 3}, {9, 3}};
    bool body = false;
    for (auto &p : pts) { float bx, by; lnWorld(p[0], p[1], bx, by); if (by >= lnTerrainY(bx)) body = true; }
    if (footL || footR || body) {
      int pad = -1;
      for (int i = 0; i < 2; i++)
        if (fxl >= lnPad[i].i0 * LN_SEG && fxr <= lnPad[i].i1 * LN_SEG && fabsf(fyl - lnPad[i].y) < 14 && fabsf(fyr - lnPad[i].y) < 14) pad = i;
      bool slow = lnVy <= LN_VY_MAX && fabsf(lnVx) <= LN_VX_MAX, up = fabsf(lnAng) <= LN_ANG_MAX;
      if (!body && pad >= 0 && slow && up) { lnPadHit = pad == 1; lnY = lnPad[pad].y - 12.0f; lnLandOk(now); }
      else {
        const char *why = body ? "hit the rock" : pad < 0 ? "missed the pad" : !slow ? (lnVy > LN_VY_MAX ? "too fast down" : "too fast sideways") : "not upright";
        lnCrash(now, why);
      }
      return;
    }
  }
  // exhaust
  if (thr > 0) {
    float nx, ny;
    lnWorld(0, 8, nx, ny);
    float dir = atan2f(cosf(lnAng), -sinf(lnAng));
    ptSpray(nx, ny, dir, 0.25f, 1 + (int)(thr * 3.0f), 190.0f + 90.0f * thr, rgb(255, 240, 170), rgb(160, 30, 0), 0.4f, 0, 2);
    if (now - lnLastRumble > 55) { lnLastRumble = now; play((uint16_t)(80 + thr * 60), 50); }
  }
}

static bool lnFrame(uint32_t now) {
  float dt = fminf((now - lnLastT) / 1000.0f, 0.04f);
  lnLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  switch (lnSt) {
    case LN_COUNT:
      if (now - lnStAt >= 2400) { lnSt = LN_FLY; imuZero(); sfxStart(); }
      break;
    case LN_FLY:
      lnPhysics(dt, now);
      break;
    case LN_LANDED:
      if (now - lnStAt > 2400) {
        lnLevel++;
        lnFuelStart = fminf(lnFuel + 45.0f, LN_P[lnPlanet].fuel + 30.0f);
        lnNewLevel(now);
      }
      break;
    case LN_CRASH:
      if (now - lnStAt > 1900) {
        if (lnShips > 0) lnResetShip(now);
        else {
          lnSt = LN_OVER; lnStAt = now;
          lnNewBest = saveBest(lnBestKey(), lnScore);
          if (lnNewBest) { lnBest = lnScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
        }
      }
      break;
    case LN_OVER:
      if (now - lnStAt > 800) {
        if (in.right) { lnStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - lnLastRender >= 16) { lnLastRender = now; lnRender(now); }
  return true;
}

Game GAME_LANDER = {"LANDER", "tilt to turn, thrust to land", 0x07FF, lnOpts, 1, lnStart, lnFrame};
