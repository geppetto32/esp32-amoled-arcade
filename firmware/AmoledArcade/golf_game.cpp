// golf_game.cpp - Tilt Golf, nine top-down mini-golf holes with walls, sand, water and slopes.
//   TILT (or the analog stick) = aim: the aim arrow points the way you tilt (level keeps the last aim)
//   HOLD LEFT = charge the power meter (it swings up and down), RELEASE = shoot
//   RIGHT = mulligan (take back the last shot, 2 per round)
//   TOUCH: put a finger down = aim from the ball toward the finger, distance = power, lift = shoot
// Sand drags the ball, water costs a stroke and puts you back, slope zones (animated arrows) push it along.
// A hole sinks when the ball is slow enough at the cup. 9 strokes max per hole. Best round (fewest strokes) is saved.
// COURSE = NORMAL (1..9) or REVERSE (9..1). GUIDE = LONG / SHORT / OFF aim line.
#include "engine.h"
#include <math.h>

static const char *const GF_COURSE[] = {"NORMAL", "REVERSE"};
static const char *const GF_GUIDE[] = {"LONG", "SHORT", "OFF"};
static Option gfOpts[] = {{"COURSE", GF_COURSE, 2, "gfCourse", 0}, {"GUIDE", GF_GUIDE, 3, "gfGuide", 0}};

enum GfSt : uint8_t { GF_INTRO, GF_AIM, GF_ROLL, GF_SINK, GF_CARD };

struct GfRect { int16_t x, y, w, h; };
struct GfSlope { int16_t x, y, w, h; int16_t ax, ay; };
struct GfHole {
  int16_t teeX, teeY, cupX, cupY;
  uint8_t par, nWall, nSand, nWater, nSlope;
  GfRect walls[6], sand[3], water[3];
  GfSlope slope[2];
};

static const GfHole GF_HOLES[9] = {
  // 1 straight
  {50, 200, 390, 200, 2, 0, 1, 0, 0, {}, {{250, 165, 44, 70}}, {}, {}},
  // 2 wall in the middle
  {60, 300, 380, 90, 3, 1, 1, 0, 0, {{200, 44, 24, 190}}, {{300, 230, 80, 56}}, {}, {}},
  // 3 water with a gap
  {50, 200, 390, 200, 3, 0, 0, 2, 0, {}, {}, {{180, 44, 80, 130}, {180, 226, 80, 130}}, {}},
  // 4 slope pushes down
  {50, 90, 390, 300, 3, 1, 0, 0, 1, {{330, 230, 60, 16}}, {}, {}, {{150, 140, 150, 150, 0, 170}}},
  // 5 zig-zag
  {50, 300, 410, 300, 4, 3, 0, 0, 0, {{130, 44, 22, 200}, {260, 156, 22, 200}, {370, 44, 22, 160}}, {}, {}, {}},
  // 6 blocks and sand
  {50, 200, 400, 200, 3, 3, 1, 0, 0, {{200, 120, 48, 24}, {200, 224, 48, 24}, {290, 176, 24, 48}}, {{340, 160, 40, 80}}, {}, {}},
  // 7 island green
  {50, 200, 385, 200, 4, 0, 1, 3, 1, {}, {{200, 150, 40, 100}}, {{330, 150, 104, 20}, {330, 230, 104, 20}, {414, 170, 20, 60}}, {{340, 172, 72, 56, 0, -110}}},
  // 8 opposing slopes
  {60, 320, 400, 80, 3, 1, 0, 0, 2, {{250, 44, 24, 150}}, {}, {}, {{120, 240, 160, 100, 0, -180}, {300, 120, 100, 130, -170, 0}}},
  // 9 long S
  {50, 320, 400, 320, 5, 3, 2, 0, 0, {{100, 44, 24, 220}, {220, 136, 24, 220}, {340, 44, 24, 220}}, {{150, 290, 50, 56}, {280, 60, 50, 60}}, {}, {}},
};

static constexpr int GF_PX0 = 14, GF_PY0 = 44, GF_PX1 = 434, GF_PY1 = 356;
static constexpr float GF_BR = 5, GF_CUPR = 9, GF_FR = 140, GF_SAND = 520, GF_MAXV = 470;
static constexpr int GF_TRAIL = 10;

static GfSt gfSt;
static int gfIdx, gfStrokes, gfTotal, gfPar, gfMull, gfHoleStrokes[9], gfGuide, gfCourse;
static float gfBx, gfBy, gfVx, gfVy, gfLx, gfLy, gfPx, gfPy, gfAim, gfPow, gfPowDir, gfSinkT, gfGT;
static float gfTx[GF_TRAIL], gfTy[GF_TRAIL];
static bool gfCharging, gfWasTouch, gfNewBest, gfMullOk;
static uint32_t gfStAt, gfLastT, gfLastRender;
static char gfBestKey[16];
static uint32_t gfBest;
static uint32_t gfSplashAt;

static const GfHole &gfH() { return GF_HOLES[gfCourse ? 8 - gfIdx : gfIdx]; }

static void gfPlaceTee() {
  const GfHole &h = gfH();
  gfBx = gfLx = gfPx = h.teeX; gfBy = gfLy = gfPy = h.teeY; gfVx = gfVy = 0;
  for (int i = 0; i < GF_TRAIL; i++) { gfTx[i] = gfBx; gfTy[i] = gfBy; }
  gfStrokes = 0; gfCharging = false; gfPow = 0; gfPowDir = 1; gfMullOk = false;
  gfPar = h.par;
  float dx = h.cupX - h.teeX, dy = h.cupY - h.teeY;
  gfAim = atan2f(dy, dx);
}

static void gfStart() {
  gfGuide = gfOpts[1].value; gfCourse = gfOpts[0].value;
  snprintf(gfBestKey, sizeof gfBestKey, gfCourse ? "gfBestR" : "gfBestN");
  gfBest = loadBest(gfBestKey);
  gfIdx = 0; gfTotal = 0; gfMull = 2; gfNewBest = false; gfWasTouch = false;
  memset(gfHoleStrokes, 0, sizeof gfHoleStrokes);
  gfPlaceTee();
  ptClear();
  uint32_t now = millis();
  gfSt = GF_INTRO; gfStAt = now; gfLastT = now; gfLastRender = 0; gfGT = 0;
  sfxStart();
}

static void gfShoot(float power) {
  gfLx = gfBx; gfLy = gfBy;
  gfPx = gfBx; gfPy = gfBy;
  float v = 70 + power * (GF_MAXV - 70);
  gfVx = cosf(gfAim) * v; gfVy = sinf(gfAim) * v;
  gfStrokes++; gfMullOk = true;
  gfSt = GF_ROLL; gfCharging = false; gfPow = 0;
  play(180, 14); play(900, 10);
}

static bool gfInRect(float x, float y, const GfRect &r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

static void gfBounceRect(const GfRect &r) {
  float cx = constrain(gfBx, (float)r.x, (float)(r.x + r.w)), cy = constrain(gfBy, (float)r.y, (float)(r.y + r.h));
  float dx = gfBx - cx, dy = gfBy - cy, d2 = dx * dx + dy * dy;
  if (d2 >= GF_BR * GF_BR) return;
  float nx, ny;
  if (d2 < 0.0001f) {                           // centre inside: push out along the shallowest axis
    float l = gfBx - r.x, rr = r.x + r.w - gfBx, t = gfBy - r.y, b = r.y + r.h - gfBy, m = fminf(fminf(l, rr), fminf(t, b));
    nx = ny = 0;
    if (m == l) nx = -1; else if (m == rr) nx = 1; else if (m == t) ny = -1; else ny = 1;
    gfBx += nx * (m + GF_BR); gfBy += ny * (m + GF_BR);
  } else {
    float d = sqrtf(d2); nx = dx / d; ny = dy / d;
    gfBx = cx + nx * GF_BR; gfBy = cy + ny * GF_BR;
  }
  float vn = gfVx * nx + gfVy * ny;
  if (vn < 0) { gfVx -= 1.8f * vn * nx; gfVy -= 1.8f * vn * ny; gfVx *= 0.92f; gfVy *= 0.92f; if (-vn > 40) play(260, 8); }
}

static void gfEndHole(uint32_t now) {
  gfSt = GF_SINK; gfStAt = now; gfSinkT = 0;
  gfHoleStrokes[gfIdx] = gfStrokes; gfTotal += gfStrokes;
  const GfHole &h = gfH();
  ptBurst((float)h.cupX, (float)h.cupY, 22, 130, rgb(255, 240, 120), rgb(60, 220, 120), 0.8f);
  static const TuneNote GF_CHIME[4] = {{880, 80}, {1175, 80}, {1568, 110}, {2093, 220}};
  tunePlay(GF_CHIME);
}

static void gfPhysics(float dt, uint32_t now) {
  const GfHole &h = gfH();
  float sp = sqrtf(gfVx * gfVx + gfVy * gfVy);
  int n = (int)ceilf(sp * dt / 3.0f);
  if (n < 1) n = 1;
  float s = dt / n;
  for (int i = 0; i < n; i++) {
    gfBx += gfVx * s; gfBy += gfVy * s;
    if (gfBx < GF_PX0 + GF_BR) { gfBx = GF_PX0 + GF_BR; if (gfVx < 0) gfVx = -gfVx * 0.8f; }
    if (gfBx > GF_PX1 - GF_BR) { gfBx = GF_PX1 - GF_BR; if (gfVx > 0) gfVx = -gfVx * 0.8f; }
    if (gfBy < GF_PY0 + GF_BR) { gfBy = GF_PY0 + GF_BR; if (gfVy < 0) gfVy = -gfVy * 0.8f; }
    if (gfBy > GF_PY1 - GF_BR) { gfBy = GF_PY1 - GF_BR; if (gfVy > 0) gfVy = -gfVy * 0.8f; }
    for (int k = 0; k < h.nWall; k++) gfBounceRect(h.walls[k]);
  }
  // surface effects
  float fr = GF_FR, sax = 0, say = 0;
  for (int k = 0; k < h.nSand; k++) if (gfInRect(gfBx, gfBy, h.sand[k])) fr = GF_SAND;
  for (int k = 0; k < h.nSlope; k++) {
    const GfSlope &sl = h.slope[k];
    if (gfBx >= sl.x && gfBx < sl.x + sl.w && gfBy >= sl.y && gfBy < sl.y + sl.h) { sax += sl.ax; say += sl.ay; }
  }
  sp = sqrtf(gfVx * gfVx + gfVy * gfVy);
  if (sp > 0) {
    float ns = fmaxf(0.0f, sp - fr * dt);
    gfVx *= ns / sp; gfVy *= ns / sp;
  }
  gfVx += sax * dt; gfVy += say * dt;
  sp = sqrtf(gfVx * gfVx + gfVy * gfVy);
  float sa = sqrtf(sax * sax + say * say);
  // water
  for (int k = 0; k < h.nWater; k++) if (gfInRect(gfBx, gfBy, h.water[k])) {
    ptBurst(gfBx, gfBy, 18, 100, rgb(150, 210, 255), rgb(0, 30, 80), 0.6f, 160, 2);
    play(300, 40); play(200, 60); play(130, 80);
    gfStrokes++;                                            // penalty
    gfBx = gfLx; gfBy = gfLy; gfVx = gfVy = 0;
    gfSplashAt = now;
    if (gfStrokes >= 9) { gfEndHole(now); return; }
    gfSt = GF_AIM;
    return;
  }
  // cup
  float cdx = h.cupX - gfBx, cdy = h.cupY - gfBy, cd = sqrtf(cdx * cdx + cdy * cdy);
  if (cd < GF_CUPR && sp < 300) { gfBx = (float)h.cupX; gfBy = (float)h.cupY; gfVx = gfVy = 0; gfEndHole(now); return; }
  if (cd < GF_CUPR + 6 && sp < 420) { gfVx += cdx / fmaxf(cd, 1.0f) * 220 * dt; gfVy += cdy / fmaxf(cd, 1.0f) * 220 * dt; }
  // stop
  if (sp < 7 && sa < 120) {
    gfVx = gfVy = 0;
    if (gfStrokes >= 9) { gfEndHole(now); return; }
    gfSt = GF_AIM; gfPx = gfBx; gfPy = gfBy;
    play(330, 12);
  }
}

// ---------------- drawing ----------------
static uint32_t gfHash(uint32_t x, uint32_t y) {
  uint32_t v = x * 374761393u + y * 668265263u;
  v = (v ^ (v >> 13)) * 1274126177u;
  return v ^ (v >> 16);
}

static void gfDrawSlope(const GfSlope &sl) {
  gfx->fillRect(sl.x, sl.y, sl.w, sl.h, rgb(10, 52, 30));
  gfx->drawRect(sl.x, sl.y, sl.w, sl.h, rgb(40, 150, 80));
  float mag = sqrtf((float)(sl.ax * sl.ax + sl.ay * sl.ay));
  float ux = sl.ax / mag, uy = sl.ay / mag;
  float ph = fmodf(gfGT * 36, 26.0f);
  uint16_t c = mag > 150 ? rgb(255, 200, 80) : rgb(110, 255, 150);
  for (int gx = sl.x - 26; gx < sl.x + sl.w + 26; gx += 26)
    for (int gy = sl.y - 26; gy < sl.y + sl.h + 26; gy += 26) {
      float cx = gx + 13 + ux * ph, cy = gy + 13 + uy * ph;
      if (cx < sl.x + 6 || cx > sl.x + sl.w - 6 || cy < sl.y + 6 || cy > sl.y + sl.h - 6) continue;
      int x = (int)cx, y = (int)cy;
      int px = (int)(-uy * 5), py = (int)(ux * 5);
      gfx->drawLine(x - (int)(ux * 5) + px, y - (int)(uy * 5) + py, x + (int)(ux * 5), y + (int)(uy * 5), c);
      gfx->drawLine(x - (int)(ux * 5) - px, y - (int)(uy * 5) - py, x + (int)(ux * 5), y + (int)(uy * 5), c);
    }
}

static void gfRender(uint32_t now) {
  const GfHole &h = gfH();
  gfx->fillScreen(C_BG);
  // course
  gfx->fillRect(GF_PX0, GF_PY0, GF_PX1 - GF_PX0, GF_PY1 - GF_PY0, rgb(6, 36, 18));
  for (int x = GF_PX0; x < GF_PX1; x += 28)
    for (int y = GF_PY0 + (((x - GF_PX0) / 28) & 1) * 28; y < GF_PY1; y += 56) gfx->fillRect(x, y, 28, 28, rgb(8, 42, 22));
  for (int k = 0; k < h.nSand; k++) {
    const GfRect &r = h.sand[k];
    gfx->fillRect(r.x, r.y, r.w, r.h, rgb(70, 56, 24));
    for (int d = 0; d < r.w * r.h / 40; d++) {
      uint32_t hv = gfHash(r.x + d, r.y);
      gfx->drawPixel(r.x + hv % r.w, r.y + (hv >> 8) % r.h, rgb(150, 125, 60));
    }
    gfx->drawRect(r.x, r.y, r.w, r.h, rgb(120, 98, 44));
  }
  for (int k = 0; k < h.nSlope; k++) gfDrawSlope(h.slope[k]);
  for (int k = 0; k < h.nWater; k++) {
    const GfRect &r = h.water[k];
    gfx->fillRect(r.x, r.y, r.w, r.h, rgb(4, 20, 70));
    for (int y = r.y + 6; y < r.y + r.h - 2; y += 12) {
      int off = (int)(gfGT * 20 + y * 3) % 24;
      for (int x = r.x + off - 24; x < r.x + r.w; x += 24) {
        int x0 = max(x, (int)r.x), x1 = min(x + 10, (int)(r.x + r.w));
        if (x1 > x0) gfx->drawFastHLine(x0, y, x1 - x0, rgb(40, 100, 200));
      }
    }
    gfx->drawRect(r.x, r.y, r.w, r.h, rgb(40, 110, 230));
  }
  // boundary and walls
  gfx->drawRect(GF_PX0 - 2, GF_PY0 - 2, GF_PX1 - GF_PX0 + 4, GF_PY1 - GF_PY0 + 4, rgb(0, 90, 120));
  gfx->drawRect(GF_PX0 - 1, GF_PY0 - 1, GF_PX1 - GF_PX0 + 2, GF_PY1 - GF_PY0 + 2, C_CYAN);
  for (int k = 0; k < h.nWall; k++) {
    const GfRect &r = h.walls[k];
    gfx->fillRect(r.x - 2, r.y - 2, r.w + 4, r.h + 4, rgb(0, 40, 56));
    gfx->fillRect(r.x, r.y, r.w, r.h, rgb(0, 80, 110));
    gfx->drawRect(r.x, r.y, r.w, r.h, C_CYAN);
  }
  // tee and cup
  gfx->drawCircle(h.teeX, h.teeY, 9, rgb(60, 130, 90));
  float pulse = 0.5f + 0.5f * sinf(now * 0.006f);
  gfx->fillCircle(h.cupX, h.cupY, 13, lerpColor(rgb(255, 230, 120), C_BG, 0.9f - 0.1f * pulse));
  gfx->fillCircle(h.cupX, h.cupY, 9, C_BG);
  gfx->drawCircle(h.cupX, h.cupY, 9, rgb(255, 230, 120));
  gfx->drawLine(h.cupX, h.cupY, h.cupX, h.cupY - 26, C_TEXT);
  gfx->fillTriangle(h.cupX, h.cupY - 26, h.cupX + 14, h.cupY - 21, h.cupX, h.cupY - 16, C_BAD);
  // ball and trail
  float bs = gfSt == GF_SINK ? fmaxf(0.0f, 1.0f - gfSinkT / 0.35f) : 1.0f;
  if (gfSt == GF_ROLL) {
    for (int i = GF_TRAIL - 1; i >= 1; i--) {
      float f = 1.0f - (float)i / GF_TRAIL;
      gfx->fillCircle((int)gfTx[i], (int)gfTy[i], 1 + (int)(3 * f), lerpColor(rgb(255, 255, 255), C_BG, 0.95f - 0.35f * f));
    }
  }
  if (bs > 0.05f) {
    gfx->fillCircle((int)gfBx, (int)gfBy, (int)(GF_BR * bs) + 3, lerpColor(C_TEXT, C_BG, 0.85f));
    gfx->fillCircle((int)gfBx, (int)gfBy, (int)(GF_BR * bs) + 1, C_TEXT);
  }
  // aim
  if (gfSt == GF_AIM) {
    float pw = gfCharging || in.touching ? gfPow : 0.35f;
    float len = gfGuide == 2 ? 0.0f : (gfGuide == 0 ? 40 + pw * 170 : 22 + pw * 40);
    if (gfCharging || in.touching) len = fmaxf(len, 24.0f);
    float ux = cosf(gfAim), uy = sinf(gfAim);
    uint16_t c = lerpColor(C_GOOD, C_BAD, gfCharging || in.touching ? gfPow : 0.0f);
    for (float d = 14; d < len; d += 10) gfx->fillCircle((int)(gfBx + ux * d), (int)(gfBy + uy * d), d > len - 12 ? 2 : 1, c);
    gfx->fillTriangle((int)(gfBx + ux * (len + 8)), (int)(gfBy + uy * (len + 8)), (int)(gfBx + ux * len - uy * 5), (int)(gfBy + uy * len + ux * 5),
                      (int)(gfBx + ux * len + uy * 5), (int)(gfBy + uy * len - ux * 5), c);
  }
  ptDraw();
  // HUD
  gfx->fillRect(0, 0, W, 32, rgb(6, 22, 14));
  char b[40];
  snprintf(b, sizeof b, "HOLE %d/9", gfIdx + 1);
  text(b, 8, 9, 2, C_GOOD);
  snprintf(b, sizeof b, "PAR %d", gfPar);
  text(b, 132, 9, 2, C_TEXT);
  snprintf(b, sizeof b, "STR %d", gfStrokes);
  text(b, 232, 9, 2, gfStrokes > gfPar ? C_BAD : C_ACC);
  snprintf(b, sizeof b, "TOT %d", gfTotal);
  text(b, 330, 9, 2, C_DIM);
  // power meter
  if (gfSt == GF_AIM || gfSt == GF_ROLL) {
    gfx->drawRect(8, H - 10, 130, 7, rgb(60, 70, 90));
    float p = gfCharging || in.touching ? gfPow : 0;
    gfx->fillRect(9, H - 9, (int)(128 * p), 5, lerpColor(C_GOOD, C_BAD, p));
    text("LEFT hold = power", 146, H - 10, 1, C_DIM);
    snprintf(b, sizeof b, "RIGHT mulligan %d", gfMull);
    text(b, 300, H - 10, 1, gfMull ? C_DIM : rgb(60, 60, 70));
  }
  if (now - gfSplashAt < 900 && gfSt != GF_SINK) centerText("SPLASH +1", 10 + 150, 3, rgb(120, 200, 255));
  if (gfSt == GF_INTRO) {
    gfx->fillRoundRect(60, 120, W - 120, 124, 16, C_PANEL);
    gfx->drawRoundRect(60, 120, W - 120, 124, 16, C_GOOD);
    centerText("TILT GOLF", 130, 3, C_GOOD);
    centerText("tilt = aim   hold LEFT = power", 168, 1, C_TEXT);
    centerText("release = shoot   touch = point & lift", 184, 1, C_DIM);
    int left = 3 - (int)((now - gfStAt) / 700);
    if (left < 1) left = 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 204, 3, C_ACC);
  } else if (gfSt == GF_SINK && now - gfStAt > 350) {
    int d = gfStrokes - gfPar;
    const char *nm = gfStrokes == 1 ? "HOLE IN ONE" : d <= -2 ? "EAGLE" : d == -1 ? "BIRDIE" : d == 0 ? "PAR" : d == 1 ? "BOGEY" : "DOUBLE+";
    char l2[24];
    snprintf(l2, sizeof l2, "%d strokes", gfStrokes);
    panelBox(nm, l2, "LEFT / RIGHT next", d <= 0 ? C_GOOD : C_ACC);
  } else if (gfSt == GF_CARD) {
    int par = 0;
    for (int i = 0; i < 9; i++) par += GF_HOLES[gfCourse ? 8 - i : i].par;
    gfx->fillRoundRect(40, 60, W - 80, 250, 16, C_PANEL);
    gfx->drawRoundRect(40, 60, W - 80, 250, 16, C_GOOD);
    centerText("SCORECARD", 70, 3, C_GOOD);
    for (int i = 0; i < 9; i++) {
      int x = 62 + i * 40;
      snprintf(b, sizeof b, "%d", i + 1);
      text(b, x + 10, 108, 1, C_DIM);
      snprintf(b, sizeof b, "%d", GF_HOLES[gfCourse ? 8 - i : i].par);
      text(b, x + 10, 124, 1, C_TEXT);
      snprintf(b, sizeof b, "%d", gfHoleStrokes[i]);
      text(b, x + 10, 142, 2, gfHoleStrokes[i] <= GF_HOLES[gfCourse ? 8 - i : i].par ? C_GOOD : C_ACC);
    }
    snprintf(b, sizeof b, "TOTAL %d  PAR %d", gfTotal, par);
    centerText(b, 180, 2, C_TEXT);
    snprintf(b, sizeof b, "%s%d", gfTotal - par >= 0 ? "+" : "", gfTotal - par);
    centerText(b, 206, 3, gfTotal <= par ? C_GOOD : C_ACC);
    if (gfNewBest) centerText("NEW BEST ROUND", 244, 2, C_ACC);
    else if (gfBest) { snprintf(b, sizeof b, "best %d", 1000 - (int)gfBest); centerText(b, 246, 2, C_DIM); }
    centerText("RIGHT again  LEFT menu", 280, 1, C_DIM);
  }
  dirty = true;
}

static bool gfFrame(uint32_t now) {
  homeLock = true;                                     // charging holds LEFT; a quick double-tap is also normal here
  float dt = fminf((now - gfLastT) / 1000.0f, 0.04f);
  gfLastT = now;
  gfGT += dt;
  tuneUpdate(now);
  ptUpdate(dt);
  switch (gfSt) {
    case GF_INTRO:
      if (now - gfStAt >= 2100) { gfSt = GF_AIM; imuZero(); }
      break;
    case GF_AIM: {
      float ax = axisX(), ay = axisY();
      if (in.touching) {
        float dx = in.touchX - gfBx, dy = in.touchY - gfBy, d = sqrtf(dx * dx + dy * dy);
        if (d > 8) { gfAim = atan2f(dy, dx); gfPow = constrain(d / 150.0f, 0.0f, 1.0f); }
        gfWasTouch = true;
      } else {
        if (gfWasTouch) {                                   // lifted: shoot when it was a real pull
          gfWasTouch = false;
          if (gfPow > 0.1f) { gfShoot(gfPow); gfPow = 0; break; }
          gfPow = 0;
        }
        if (sqrtf(ax * ax + ay * ay) > 0.13f) {
          float target = atan2f(ay, ax), d = target - gfAim;
          while (d > (float)M_PI) d -= 6.2831853f;
          while (d < -(float)M_PI) d += 6.2831853f;
          gfAim += constrain(d, -6.0f * dt, 6.0f * dt);
        }
        if (in.leftDown) {
          if (!gfCharging) { gfCharging = true; gfPow = 0; gfPowDir = 1; }
          gfPow += gfPowDir * dt / 1.3f;
          if (gfPow >= 1) { gfPow = 1; gfPowDir = -1; }
          if (gfPow <= 0) { gfPow = 0; gfPowDir = 1; }
        } else if (gfCharging) {
          float p = gfPow;
          gfCharging = false; gfPow = 0;
          if (p > 0.05f) { gfShoot(p); break; }
        }
      }
      if (in.right && gfMull > 0 && gfStrokes > 0 && gfMullOk) {   // mulligan: take the last shot back
        gfMull--; gfStrokes--; gfBx = gfPx; gfBy = gfPy; gfLx = gfPx; gfLy = gfPy; gfVx = gfVy = 0; gfMullOk = false;
        play(500, 30); play(700, 40);
      }
      break;
    }
    case GF_ROLL: {
      for (int i = GF_TRAIL - 1; i > 0; i--) { gfTx[i] = gfTx[i - 1]; gfTy[i] = gfTy[i - 1]; }
      gfTx[0] = gfBx; gfTy[0] = gfBy;
      gfPhysics(dt, now);
      if (in.right && gfMull > 0 && gfStrokes > 0 && gfSt == GF_ROLL) {      // mulligan while rolling
        gfMull--; gfStrokes--; gfBx = gfPx; gfBy = gfPy; gfLx = gfPx; gfLy = gfPy; gfVx = gfVy = 0; gfSt = GF_AIM; gfMullOk = false;
        play(500, 30); play(700, 40);
      }
      break;
    }
    case GF_SINK:
      gfSinkT += dt;
      if (now - gfStAt > 900 && (in.left || in.right || now - gfStAt > 3500)) {
        if (gfIdx >= 8) {
          gfSt = GF_CARD; gfStAt = now;
          int par = 0;
          for (int i = 0; i < 9; i++) par += GF_HOLES[gfCourse ? 8 - i : i].par;
          (void)par;
          gfNewBest = saveBest(gfBestKey, (uint32_t)(1000 - gfTotal));
          if (gfNewBest) { gfBest = (uint32_t)(1000 - gfTotal); tunePlay(TUNE_WIN); } else tunePlay(TUNE_LEVEL);
        } else { gfIdx++; gfPlaceTee(); gfSt = GF_AIM; }
      }
      break;
    case GF_CARD:
      if (now - gfStAt > 900) {
        if (in.right) { gfStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - gfLastRender >= 16) { gfLastRender = now; gfRender(now); }
  return true;
}

Game GAME_GOLF = {"Tilt Golf", "9 holes, tilt to aim, hold to hit", 0x87F0, gfOpts, 2, gfStart, gfFrame};
