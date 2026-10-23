// cut_game.cpp - Cut to Length: a workshop timing game.
// A saw blade sits at the screen centre; the board with its ruler scrolls under it. A target length is shown
// (a fraction like 7-3/8 in, or millimetres). Slide the board until the blade is over the target, then cut.
//   HOLD LEFT / RIGHT = slide the saw (starts slowly for fine positioning, then speeds up)
//   BOTH buttons together (or TAP the screen) = CUT
// Three misses end the run. Tolerance shrinks with every 3 cuts (inch: 1/8 in down to 1/64 in, fractions down to 1/32;
// mm: 1 mm down to 0.1 mm), the saw gets faster and the time per cut shorter.
// UNITS: INCH or MM.  MARK: FADE = pencil mark shown for the first 3 cuts, ALWAYS, or NEVER (read the ruler only).
#include "engine.h"
#include <math.h>

static const char *const CT_UNITS[] = {"INCH", "MM"};
static const char *const CT_MARKS[] = {"FADE", "ALWAYS", "NEVER"};
static Option ctOpts[] = {{"UNITS", CT_UNITS, 2, "ctUnits", 0}, {"MARK", CT_MARKS, 3, "ctMark", 0}};

enum CtSt : uint8_t { CT_COUNT, CT_PLAY, CT_RESULT, CT_OVER };

static constexpr int CT_CX = W / 2;                   // blade screen x
static constexpr int CT_RY0 = 110, CT_RY1 = 152;      // ruler strip
static constexpr int CT_BY0 = 152, CT_BH = 110;       // board
static constexpr int CT_TEXW = 240;
static constexpr int CT_LEVELS = 7;
static constexpr int CT_MAXKERF = 8;

static const float CT_TOL_IN[CT_LEVELS] = {0.125f, 0.094f, 0.0625f, 0.047f, 0.031f, 0.0234f, 0.0156f};
static const uint8_t CT_DEN_IN[CT_LEVELS] = {2, 4, 8, 8, 16, 16, 32};
static const float CT_TOL_MM[CT_LEVELS] = {1.0f, 0.8f, 0.6f, 0.4f, 0.3f, 0.2f, 0.1f};
static const float CT_STEP_MM[CT_LEVELS] = {10, 5, 5, 2, 1, 0.5f, 0.5f};

struct CtKerf { float u; bool ok; };

static CtSt ctSt;
static int ctUnits, ctMarkMode, ctLevel, ctCuts, ctLives, ctStreak, ctBestStreak, ctHits;
static float ctSaw, ctVel, ctTarget, ctTol, ctPPU, ctLen, ctErrSum, ctLastErr;
static bool ctLastOk, ctPerfect, ctBothPrev, ctLock, ctNewBest;
static uint32_t ctScore, ctBest, ctStAt, ctLastT, ctLastRender, ctLimitMs, ctCutAt, ctPressAt, ctLastZip;
static int ctDirPrev;
static CtKerf ctKerf[CT_MAXKERF];
static int ctNKerf;
static uint16_t *ctBg = nullptr;
static uint16_t *ctTex = nullptr;
static int ctCountLen = 2100;
static int ctPoints;

static const char *ctBestKey() { return ctUnits ? "ctBestMm" : "ctBestIn"; }
static const char *ctStreakKey() { return ctUnits ? "ctStkMm" : "ctStkIn"; }

static inline uint32_t ctHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// ---------------- art ----------------
static void ctBuildArt() {
  if (!ctBg) ctBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!ctTex) ctTex = (uint16_t *)ps_malloc(CT_TEXW * CT_BH * 2);
  if (ctBg) {
    for (int y = 0; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(rgb(4, 6, 14), rgb(18, 12, 10), y / (float)H));
    for (int y = 40; y < H - 20; y += 24)                       // pegboard dots
      for (int x = 12; x < W; x += 24) gfx->fillRect(x, y, 2, 2, rgb(26, 28, 44));
    memcpy(ctBg, frameBuffer(), FB_PIXELS * 2);
  }
  if (ctTex) {
    static const uint16_t dark = rgb(104, 62, 28), light = rgb(196, 134, 74);
    for (int x = 0; x < CT_TEXW; x++) {
      float u = x * 6.2831853f / CT_TEXW;
      for (int y = 0; y < CT_BH; y++) {
        float g = sinf(y * 0.31f + 2.2f * sinf(u * 2 + y * 0.045f) + 1.3f * sinf(u * 5 + 1.0f));
        float g2 = sinf(y * 0.9f + 3.0f * sinf(u * 3));
        float n = (ctHash(x, y) & 255) / 255.0f;
        float t = (g * 0.5f + 0.5f) * 0.68f + (g2 * 0.5f + 0.5f) * 0.14f + n * 0.12f;
        float edge = fminf(y, CT_BH - 1 - y) / 8.0f;            // darker toward the edges
        if (edge < 1.0f) t *= 0.55f + 0.45f * edge;
        ctTex[x * CT_BH + (CT_BH - 1 - y)] = lerpColor(dark, light, constrain(t, 0.0f, 1.0f));
      }
    }
  }
}

// ---------------- target ----------------
static int ctGcd(int a, int b) { return b ? ctGcd(b, a % b) : a; }
static void ctTargetText(char *b, size_t n) {
  if (ctUnits == 0) {
    int den = CT_DEN_IN[ctLevel];
    int k = (int)lroundf(ctTarget * den);
    int whole = k / den, num = k % den;
    if (num == 0) { snprintf(b, n, "%d in", whole); return; }
    int g = ctGcd(num, den);
    num /= g; int d = den / g;
    if (whole > 0) snprintf(b, n, "%d-%d/%d in", whole, num, d);
    else snprintf(b, n, "%d/%d in", num, d);
  } else {
    if (fabsf(ctTarget - roundf(ctTarget)) < 0.01f) snprintf(b, n, "%d mm", (int)lroundf(ctTarget));
    else snprintf(b, n, "%.1f mm", ctTarget);
  }
}
static void ctPickTarget() {
  ctLevel = min(ctCuts / 3, CT_LEVELS - 1);
  ctTol = ctUnits ? CT_TOL_MM[ctLevel] : CT_TOL_IN[ctLevel];
  float lo = ctUnits ? 15.0f : 1.0f, hi = ctUnits ? ctLen - 15.0f : ctLen - 1.0f;
  float minGap = ctUnits ? 30.0f : 1.5f, step = ctUnits ? CT_STEP_MM[ctLevel] : 1.0f / CT_DEN_IN[ctLevel];
  for (int tries = 0; tries < 20; tries++) {
    int nSteps = (int)((hi - lo) / step);
    float t = lo + step * (int)(esp_random() % (uint32_t)(nSteps + 1));
    if (ctUnits) t = roundf(t / step) * step;
    ctTarget = t;
    if (fabsf(t - ctSaw) >= minGap) break;
  }
  ctLimitMs = (uint32_t)(max(8.0f, 16.0f - 1.3f * ctLevel) * 1000.0f);
  ctStAt = millis();
}

// ---------------- drawing ----------------
static void ctDrawRuler(float sawPx) {
  gfx->fillRect(0, CT_RY0, W, CT_RY1 - CT_RY0, rgb(10, 12, 24));
  gfx->drawFastHLine(0, CT_RY0, W, rgb(40, 46, 80));
  float x0 = sawPx - CT_CX, x1 = sawPx + (W - CT_CX);
  const uint16_t base = rgb(120, 134, 176), hot = rgb(255, 235, 170);
  int sub = ctUnits ? 2 : 32;                                  // ticks per unit
  int i0 = (int)floorf(x0 / ctPPU * sub) - 1, i1 = (int)ceilf(x1 / ctPPU * sub) + 1;
  for (int i = i0; i <= i1; i++) {
    if (i < 0 || i > (int)(ctLen * sub)) continue;
    int sx = CT_CX + (int)lroundf(i * ctPPU / sub - sawPx);
    if (sx < 0 || sx >= W) continue;
    int len;
    if (ctUnits) len = (i % 20 == 0) ? 22 : (i % 10 == 0) ? 16 : (i % 2 == 0) ? 11 : 6;
    else len = (i % 32 == 0) ? 22 : (i % 16 == 0) ? 18 : (i % 8 == 0) ? 14 : (i % 4 == 0) ? 10 : (i % 2 == 0) ? 7 : 4;
    int d = abs(sx - CT_CX);
    uint16_t c = d < 56 ? lerpColor(hot, base, d / 56.0f) : base;
    gfx->drawFastVLine(sx, CT_RY1 - 1 - len, len, c);
    if (d < 20 && len >= 10) gfx->drawFastVLine(sx + 1, CT_RY1 - 1 - len, len, lerpColor(c, rgb(10, 12, 24), 0.55f));
    bool label = ctUnits ? (i % 20 == 0) : (i % 32 == 0);
    if (label) {
      char b[12];
      snprintf(b, sizeof b, "%d", ctUnits ? i / 2 : i / 32);
      text(b, sx - (int)strlen(b) * 6 + 1, CT_RY0 + 3, 2, d < 70 ? hot : C_DIM);
    }
  }
}

static void ctDrawBoard(float sawPx) {
  int off = (int)floorf(sawPx) - CT_CX;
  int boardPx = (int)(ctLen * ctPPU);
  uint16_t *fb = frameBuffer();
  if (ctTex) {
    for (int sx = 0; sx < W; sx++) {
      int wx = off + sx;
      if (wx < 0 || wx >= boardPx) continue;
      memcpy(fb + fbIndex(sx, CT_BY0 + CT_BH - 1), ctTex + (wx % CT_TEXW) * CT_BH, CT_BH * 2);
    }
  } else {
    gfx->fillRect(max(0, -off), CT_BY0, min(W, boardPx - off) - max(0, -off), CT_BH, rgb(150, 100, 56));
  }
  int sx0 = -off, sx1 = boardPx - off;                         // board ends
  if (sx0 >= 0 && sx0 < W) { gfx->drawFastVLine(sx0, CT_BY0, CT_BH, rgb(255, 210, 150)); gfx->drawFastVLine(sx0 - 1, CT_BY0, CT_BH, rgb(20, 12, 8)); }
  if (sx1 > 0 && sx1 <= W) { gfx->drawFastVLine(sx1 - 1, CT_BY0, CT_BH, rgb(255, 210, 150)); gfx->drawFastVLine(sx1, CT_BY0, CT_BH, rgb(20, 12, 8)); }
  gfx->drawFastHLine(max(0, sx0), CT_BY0, min(W, sx1) - max(0, sx0), rgb(255, 214, 160));   // top edge highlight
  gfx->drawFastHLine(max(0, sx0), CT_BY0 + CT_BH - 1, min(W, sx1) - max(0, sx0), rgb(40, 22, 10));
  for (int k = 0; k < ctNKerf; k++) {                          // earlier cuts: dark kerf + result tick
    int x = CT_CX + (int)lroundf(ctKerf[k].u * ctPPU - sawPx);
    if (x < -3 || x > W + 3) continue;
    gfx->fillRect(x - 1, CT_BY0, 3, CT_BH, rgb(8, 4, 2));
    gfx->fillRect(x - 3, CT_BY0 + CT_BH - 6, 7, 5, ctKerf[k].ok ? C_GOOD : C_BAD);
  }
}

static void ctDrawMark(float sawPx, bool show) {
  int x = CT_CX + (int)lroundf(ctTarget * ctPPU - sawPx);
  if (show) {
    if (x >= -2 && x <= W + 2) {
      gfx->fillRect(x - 1, CT_BY0 + 4, 3, CT_BH - 8, rgb(36, 28, 24));
      gfx->drawFastVLine(x, CT_BY0 + 4, CT_BH - 8, rgb(255, 246, 220));
      gfx->drawLine(x - 7, CT_BY0 + CT_BH / 2 - 7, x + 7, CT_BY0 + CT_BH / 2 + 7, rgb(255, 246, 220));
      gfx->drawLine(x - 7, CT_BY0 + CT_BH / 2 + 7, x + 7, CT_BY0 + CT_BH / 2 - 7, rgb(255, 246, 220));
    }
  }
  if (ctSt == CT_PLAY && show) {                               // off-screen pointer
    int y = CT_BY0 + CT_BH / 2;
    if (x < 8) gfx->fillTriangle(6, y, 22, y - 12, 22, y + 12, C_ACC);
    else if (x > W - 8) gfx->fillTriangle(W - 6, y, W - 22, y - 12, W - 22, y + 12, C_ACC);
  }
}

static void ctDrawBlade(uint32_t now) {
  int plunge = 0;
  if (ctSt == CT_RESULT) {
    float t = constrain((now - ctCutAt) / 220.0f, 0.0f, 1.0f);
    plunge = (int)(8 * sinf(t * 3.1416f));
  }
  int y0 = 96 + plunge, y1 = CT_BY0 + CT_BH + 12;
  uint16_t core = ctSt == CT_RESULT ? (ctLastOk ? C_GOOD : C_BAD) : rgb(255, 70, 40);
  gfx->fillRect(CT_CX - 3, y0 + 10, 7, y1 - y0 - 10, lerpColor(core, C_BG, 0.82f));      // glow layers
  gfx->fillRect(CT_CX - 2, y0 + 10, 5, y1 - y0 - 10, lerpColor(core, C_BG, 0.6f));
  gfx->fillRect(CT_CX - 1, y0 + 10, 3, y1 - y0 - 10, lerpColor(core, C_TEXT, 0.25f));
  gfx->drawFastVLine(CT_CX, y0 + 10, y1 - y0 - 10, C_TEXT);
  gfx->fillRoundRect(CT_CX - 12, y0, 24, 12, 4, rgb(210, 214, 230));                       // saw handle
  gfx->fillRect(CT_CX - 8, y0 + 3, 16, 3, rgb(40, 44, 70));
  gfx->fillTriangle(CT_CX - 7, y1 - 6, CT_CX + 7, y1 - 6, CT_CX, y1 + 4, core);
}

static void ctDrawHud(uint32_t now) {
  hudBar(ctScore, ctBest, rgb(30, 18, 10), C_TEXT);
  for (int i = 0; i < 3; i++) {
    int x = 266 + i * 22;
    if (i < ctLives) gfx->fillCircle(x, 16, 7, C_ACC);
    else gfx->drawCircle(x, 16, 7, 0x4208);
  }
  char b[28];
  text("CUT AT", 12, 38, 2, C_DIM);
  snprintf(b, sizeof b, "STREAK %d", ctStreak);
  text(b, W - 12 - (int)strlen(b) * 12, 38, 2, ctStreak >= 3 ? C_ACC : C_DIM);
  ctTargetText(b, sizeof b);
  centerText(b, 54, 5, rgb(255, 214, 120));
  if (ctSt == CT_PLAY) {                                        // time bar
    float t = 1.0f - (float)(now - ctStAt) / ctLimitMs;
    t = constrain(t, 0.0f, 1.0f);
    gfx->fillRect(0, 93, W, 3, rgb(20, 20, 30));
    gfx->fillRect(0, 93, (int)(W * t), 3, lerpColor(C_BAD, C_GOOD, t));
  }
}

static void ctFmtVal(char *b, size_t n, float v, bool sign) {
  if (ctUnits) snprintf(b, n, sign ? "%+.2f mm" : "%.2f mm", v);
  else snprintf(b, n, sign ? "%+.3f in" : "%.3f in", v);
}

static void ctRender(uint32_t now) {
  float sawPx = ctSaw * ctPPU;
  if (ctBg) memcpy(frameBuffer(), ctBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  bool show = ctMarkMode == 1 || (ctMarkMode == 0 && ctCuts < 3) || ctSt == CT_RESULT;
  ctDrawRuler(sawPx);
  ctDrawBoard(sawPx);
  ctDrawMark(sawPx, show);
  ctDrawBlade(now);
  ptDraw();
  ctDrawHud(now);
  char b[48];
  uint16_t acc = rgb(255, 190, 80);
  if (ctSt == CT_PLAY || ctSt == CT_COUNT) {
    char t[24];
    ctFmtVal(t, sizeof t, ctTol, false);
    snprintf(b, sizeof b, "TOLERANCE +/- %s", t);
    centerText(b, 280, 2, C_DIM);
    centerText("HOLD = slide    BOTH = CUT", 316, 2, lerpColor(C_DIM, C_BG, 0.3f));
  }
  if (ctSt == CT_RESULT) {
    char v[24];
    ctFmtVal(v, sizeof v, ctLastErr, true);
    if (ctLastOk) {
      snprintf(b, sizeof b, ctPerfect ? "PERFECT  %s" : "GOOD CUT  %s", v);
      centerText(b, 278, 3, C_GOOD);
      snprintf(b, sizeof b, "+%d", ctPoints);
      centerText(b, 314, 3, acc);
    } else {
      snprintf(b, sizeof b, "OFF  %s", v);
      centerText(b, 278, 3, C_BAD);
      centerText(ctLastErr > 0 ? "cut too far right" : "cut too far left", 314, 2, C_DIM);
    }
  }
  if (ctSt == CT_COUNT) {
    int cnt = 3 - (int)((now - ctStAt) * 3 / ctCountLen);
    gfx->fillRoundRect(40, 164, W - 80, 88, 14, C_PANEL);
    gfx->drawRoundRect(40, 164, W - 80, 88, 14, acc);
    centerText("HOLD LEFT / RIGHT = slide the saw", 176, 2, C_TEXT);
    centerText("BOTH buttons (or TAP) = CUT", 202, 2, acc);
    char d[2] = {(char)('0' + constrain(cnt, 1, 3)), 0};
    centerText(d, 224, 3, C_TEXT);
  } else if (ctSt == CT_OVER) {
    char l2[32], l3[64];
    snprintf(l2, sizeof l2, ctNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)ctScore);
    float avg = ctCuts ? ctErrSum / ctCuts : 0;
    char av[20];
    ctFmtVal(av, sizeof av, avg, false);
    snprintf(l3, sizeof l3, "best streak %d  avg err %s", ctBestStreak, av);
    gfx->fillRoundRect(34, 150, W - 68, 170, 16, C_PANEL);
    gfx->drawRoundRect(34, 150, W - 68, 170, 16, acc);
    centerText("BOARD RUINED", 164, 3, C_TEXT);
    centerText(l2, 206, 3, acc);
    centerText(l3, 248, 1, C_DIM);
    centerText("RIGHT again   LEFT menu", 280, 2, C_DIM);
  }
  dirty = true;
}

// ---------------- game ----------------
static void ctStart() {
  ctUnits = ctOpts[0].value;
  ctMarkMode = ctOpts[1].value;
  ctPPU = ctUnits ? 16.0f : 160.0f;
  ctLen = ctUnits ? 200.0f : 12.0f;
  ctBest = loadBest(ctBestKey());
  ctBestStreak = 0;
  ctScore = 0; ctCuts = 0; ctLives = 3; ctStreak = 0; ctHits = 0; ctErrSum = 0; ctNewBest = false;
  ctSaw = ctUnits ? 20.0f : 1.0f;
  ctVel = 0; ctNKerf = 0; ctBothPrev = false; ctLock = false; ctDirPrev = 0; ctPressAt = 0; ctLastZip = 0;
  ptClear();
  if (!ctBg || !ctTex) ctBuildArt();
  ctPickTarget();
  ctSt = CT_COUNT; ctStAt = millis();
  ctLastT = ctStAt; ctLastRender = 0;
  sfxStart();
  ctRender(ctStAt);
}

static void ctCut(uint32_t now) {
  float err = ctSaw - ctTarget;
  ctLastErr = err;
  ctErrSum += fabsf(err);
  ctCuts++;
  ctLastOk = fabsf(err) <= ctTol;
  ctPerfect = fabsf(err) <= ctTol * 0.25f;
  ctCutAt = now;
  if (ctNKerf == CT_MAXKERF) { for (int i = 1; i < CT_MAXKERF; i++) ctKerf[i - 1] = ctKerf[i]; ctNKerf--; }
  ctKerf[ctNKerf++] = {ctSaw, ctLastOk};
  float px = CT_CX, py = CT_BY0 + CT_BH * 0.5f;
  ptBurst(px, py, 30, 150, rgb(255, 224, 160), rgb(120, 70, 30), 0.7f, 380.0f, 2);
  ptSpray(px, CT_BY0 + 6, -1.5708f, 0.9f, 14, 220, rgb(255, 240, 200), rgb(100, 60, 30), 0.6f, 500.0f, 2);
  play(2400, 12); play(150, 55);                                // snap
  if (ctLastOk) {
    ctStreak++; ctHits++;
    if (ctStreak > ctBestStreak) ctBestStreak = ctStreak;
    ctPoints = 100 + (int)(100.0f * (1.0f - fabsf(err) / ctTol)) + min(ctStreak, 10) * 20 + (ctPerfect ? 100 : 0);
    ctScore += ctPoints;
    if (ctPerfect) { play(1175, 50); play(1568, 50); play(2093, 120); }
    else { play(880, 45); play(1320, 70); }
  } else {
    ctStreak = 0; ctLives--; ctPoints = 0;
    sfxBad();
  }
  ctSt = CT_RESULT; ctStAt = now;
}

static void ctGameOver(uint32_t now) {
  ctSt = CT_OVER; ctStAt = now;
  ctNewBest = saveBest(ctBestKey(), ctScore);
  uint32_t oldStk = prefs.getUInt(ctStreakKey(), 0);
  if ((uint32_t)ctBestStreak > oldStk) prefs.putUInt(ctStreakKey(), (uint32_t)ctBestStreak);
  if (ctNewBest) { ctBest = ctScore; tunePlay(TUNE_WIN); }
  else tunePlay(TUNE_LOSE);
}

static bool ctFrame(uint32_t now) {
  float dt = fminf((now - ctLastT) / 1000.0f, 0.05f);
  ctLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  switch (ctSt) {
    case CT_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - ctStAt) * 3 / ctCountLen);
      if (n != lastN && n >= 1) play(660, 70);
      lastN = n;
      if (now - ctStAt >= (uint32_t)ctCountLen) { ctSt = CT_PLAY; ctStAt = now; lastN = 3; ctBothPrev = true; ctLock = true; }
      break;
    }
    case CT_PLAY: {
      bool both = in.leftDown && in.rightDown;
      bool cutNow = (both && !ctBothPrev) || in.tap;
      ctBothPrev = both;
      if (both) ctLock = true;
      else if (!in.leftDown && !in.rightDown) ctLock = false;
      int dir = (!ctLock && (in.leftDown != in.rightDown)) ? (in.leftDown ? -1 : 1) : 0;
      if (dir != ctDirPrev) { ctPressAt = now; ctDirPrev = dir; }
      if (dir) {
        uint32_t held = now - ctPressAt;
        if (held > 45) {
          float vmax = 1000.0f * (1.0f + 0.07f * ctLevel);
          float r = constrain((held - 350) / 750.0f, 0.0f, 1.0f);
          float target = 24.0f + (vmax - 24.0f) * r * r;
          ctVel += (target - ctVel) * (1.0f - expf(-dt * 14.0f));
          ctSaw += dir * ctVel * dt / ctPPU;
          if (now - ctLastZip > 85 && ctVel > 90) { ctLastZip = now; play((uint16_t)(160 + ctVel * 0.22f), 14); }
        }
      } else {
        ctVel *= expf(-dt * 30.0f);
      }
      ctSaw = constrain(ctSaw, 0.0f, ctLen);
      if (cutNow) { ctCut(now); break; }
      if (now - ctStAt > ctLimitMs) {                           // out of time: counts as a miss
        ctLastErr = ctSaw - ctTarget; ctErrSum += fabsf(ctLastErr); ctCuts++;
        ctLastOk = false; ctPerfect = false; ctStreak = 0; ctLives--; ctPoints = 0; ctCutAt = now;
        ctSt = CT_RESULT; ctStAt = now;
        sfxBad();
      }
      break;
    }
    case CT_RESULT:
      ctVel *= expf(-dt * 30.0f);
      if (now - ctStAt > 1250) {
        if (ctLives <= 0) ctGameOver(now);
        else { ctPickTarget(); ctSt = CT_PLAY; ctBothPrev = true; ctLock = true; }
      }
      break;
    case CT_OVER:
      if (now - ctStAt > 700) {
        if (in.right) { ctStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - ctLastRender >= 16) { ctLastRender = now; ctRender(now); }
  return true;
}

Game GAME_CUT = {"CUT LENGTH", "slide the saw, cut at the mark", 0xFD20, ctOpts, 2, ctStart, ctFrame};
