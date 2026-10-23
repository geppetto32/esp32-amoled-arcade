// murmur_game.cpp - Murmuration. A zen flock of up to 300 starlings (boids) over a sunset.
//   TILT moves a glowing light; the flock drifts after it
//   hold LEFT = gather (the flock tightens and rushes to the light)
//   hold RIGHT = scatter (birds near the light burst away)
//   TOUCH = a predator: birds flee from the finger
// MODE ZEN: no goal. MODE HERD: 60 s, hold >= 55 % of the flock inside the ring for 2.5 s to score; each ring adds time.
// Frame cost: neighbour search uses a uniform grid (cell = perception radius) built by counting sort every frame, so
// the cost is O(N * ~20). The number of active birds also adapts: the update+render time is smoothed and the flock
// size is trimmed (down to 120) when it exceeds ~14 ms, and grown back (up to 300) when there is headroom.
// The sky is pre-rendered once into a PSRAM buffer and blitted each frame; trails are 5-point polylines per bird.
// THEME: SUNSET (dark birds on a sunset sky) or NIGHT (glowing birds on black).
#include "engine.h"
#include <math.h>

static const char *const MM_THEMES[] = {"SUNSET", "NIGHT"};
static const char *const MM_MODES[] = {"ZEN", "HERD"};
static Option mmOpts[] = {{"MODE", MM_MODES, 2, "mmMode", 0}, {"THEME", MM_THEMES, 2, "mmTheme", 0}};

static constexpr int MM_N = 300, MM_TRAIL = 5;
static constexpr float MM_R = 34.0f, MM_S = 13.0f;                  // perception / separation radius
static constexpr int MM_GX = 14, MM_GY = 11;                        // 448/34, 368/34 rounded up

struct MmBird { float x, y, vx, vy; int16_t hx[MM_TRAIL], hy[MM_TRAIL]; };
static MmBird mmB[MM_N];
static uint16_t mmCell[MM_N], mmCellStart[MM_GX * MM_GY + 1], mmList[MM_N];
static uint16_t mmBgRow[H];
static uint16_t *mmBg = nullptr;
static int mmBgTheme = -1;
static int mmTheme, mmMode, mmActive;
static float mmLx, mmLy, mmTrailT, mmEma = 8000.0f, mmAdaptT, mmTime, mmProg, mmSpeedAvg;
static uint32_t mmLast, mmLastRender, mmScore, mmBest, mmHintUntil, mmOverAt;
static bool mmOver, mmNewBest;
static float mmRx, mmRy;                                            // herd ring
static constexpr float MM_RING = 48.0f;
static TiltCursor mmCur;

static float mmRand() { return (esp_random() & 0xFFFF) / 65535.0f; }

static void mmBuildBg() {
  if (!mmBg) mmBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!mmBg) return;
  gfx->fillScreen(C_BG);
  for (int y = 0; y < H; y++) {
    uint16_t c;
    if (mmTheme == 1) c = lerpColor(rgb(0, 0, 4), rgb(6, 4, 20), y / (float)H);
    else {
      float t = y / (float)H;
      c = t < 0.55f ? lerpColor(rgb(58, 30, 120), rgb(212, 74, 120), t / 0.55f)
                    : lerpColor(rgb(212, 74, 120), rgb(255, 170, 70), (t - 0.55f) / 0.45f);
    }
    mmBgRow[y] = c;
    gfx->drawFastHLine(0, y, W, c);
  }
  if (mmTheme == 1) {
    for (int i = 0; i < 80; i++) {
      uint32_t h = (uint32_t)i * 2654435761u;
      int b = 40 + (h >> 24) % 120;
      gfx->drawPixel(h % W, (h >> 10) % H, rgb(b, b, b + 30));
    }
  } else {
    for (int x = 0; x < W; x++) {                                    // distant hills
      int h1 = H - 26 - (int)(16 * sinf(x * 0.013f + 0.7f) + 9 * sinf(x * 0.041f + 2.1f));
      gfx->drawFastVLine(x, h1, H - h1, rgb(34, 14, 44));
    }
  }
  memcpy(mmBg, frameBuffer(), FB_PIXELS * 2);
  mmBgTheme = mmTheme;
}

static void mmNewRing() {
  mmRx = 90 + mmRand() * (W - 180);
  mmRy = 90 + mmRand() * (H - 190);
  mmProg = 0;
}

static void mmStart() {
  mmTheme = mmOpts[1].value; mmMode = mmOpts[0].value;
  if (mmBgTheme != mmTheme || !mmBg) mmBuildBg();
  mmBest = loadBest("mmBest");
  mmScore = 0; mmOver = false; mmNewBest = false; mmTime = 60.0f; mmProg = 0;
  mmActive = 260;
  for (auto &b : mmB) {
    b.x = W / 2 + (mmRand() - 0.5f) * 160; b.y = H / 2 + (mmRand() - 0.5f) * 120;
    float a = mmRand() * 6.2832f, s = 80 + mmRand() * 40;
    b.vx = cosf(a) * s; b.vy = sinf(a) * s;
    for (int k = 0; k < MM_TRAIL; k++) { b.hx[k] = (int16_t)b.x; b.hy[k] = (int16_t)b.y; }
  }
  mmCur.set(W / 2, H / 2);
  mmLx = W / 2; mmLy = H / 2;
  mmNewRing();
  imuZero();
  mmLast = millis(); mmLastRender = 0; mmTrailT = 0; mmEma = 8000.0f; mmAdaptT = 0;
  mmHintUntil = mmLast + 6000;
  sfxStart();
}

// ---------------- simulation ----------------
static void mmUpdate(float dt, bool gather, bool scatter, bool pred, float px, float py) {
  // spatial grid by counting sort
  memset(mmCellStart, 0, sizeof mmCellStart);
  for (int i = 0; i < mmActive; i++) {
    int cx = constrain((int)(mmB[i].x / MM_R), 0, MM_GX - 1), cy = constrain((int)(mmB[i].y / MM_R), 0, MM_GY - 1);
    mmCell[i] = (uint16_t)(cy * MM_GX + cx);
    mmCellStart[mmCell[i] + 1]++;
  }
  for (int c = 0; c < MM_GX * MM_GY; c++) mmCellStart[c + 1] += mmCellStart[c];
  static uint16_t fill[MM_GX * MM_GY];
  memcpy(fill, mmCellStart, sizeof fill);
  for (int i = 0; i < mmActive; i++) mmList[fill[mmCell[i]]++] = (uint16_t)i;

  float spSum = 0;
  const float R2 = MM_R * MM_R, S2 = MM_S * MM_S;
  for (int i = 0; i < mmActive; i++) {
    MmBird &b = mmB[i];
    int cx = mmCell[i] % MM_GX, cy = mmCell[i] / MM_GX;
    float sx = 0, sy = 0, avx = 0, avy = 0, cmx = 0, cmy = 0;
    int n = 0;
    for (int gy = cy - 1; gy <= cy + 1 && n < 26; gy++) {
      if (gy < 0 || gy >= MM_GY) continue;
      for (int gx = cx - 1; gx <= cx + 1 && n < 26; gx++) {
        if (gx < 0 || gx >= MM_GX) continue;
        int c = gy * MM_GX + gx;
        for (int k = mmCellStart[c]; k < mmCellStart[c + 1]; k++) {
          int j = mmList[k];
          if (j == i) continue;
          float dx = mmB[j].x - b.x, dy = mmB[j].y - b.y, d2 = dx * dx + dy * dy;
          if (d2 >= R2 || d2 < 1e-3f) continue;
          n++;
          avx += mmB[j].vx; avy += mmB[j].vy; cmx += dx; cmy += dy;
          if (d2 < S2) { float d = sqrtf(d2), w = (1.0f - d / MM_S) / d; sx -= dx * w; sy -= dy * w; }
        }
      }
    }
    float ax = sx * 340.0f, ay = sy * 340.0f;
    if (n) {
      float inv = 1.0f / n;
      float cohK = gather ? 2.4f : 1.1f;
      ax += (avx * inv - b.vx) * 2.4f + cmx * inv * cohK;
      ay += (avy * inv - b.vy) * 2.4f + cmy * inv * cohK;
    }
    // the light
    float lx = mmLx - b.x, ly = mmLy - b.y, ld2 = lx * lx + ly * ly, ld = sqrtf(ld2) + 1e-3f;
    float pull = gather ? 300.0f : 62.0f;
    if (ld > 24.0f) { ax += lx / ld * pull; ay += ly / ld * pull; }
    if (scatter && ld < 160.0f) { float f = 900.0f * (1.0f - ld / 160.0f); ax -= lx / ld * f; ay -= ly / ld * f; }
    if (pred) {
      float dx = b.x - px, dy = b.y - py, d2 = dx * dx + dy * dy;
      if (d2 < 120.0f * 120.0f && d2 > 1) { float d = sqrtf(d2), f = 1100.0f * (1.0f - d / 120.0f); ax += dx / d * f; ay += dy / d * f; }
    }
    // soft walls
    const float M = 40.0f, top = 44.0f;
    if (b.x < M) ax += (M - b.x) * 9.0f; else if (b.x > W - M) ax -= (b.x - (W - M)) * 9.0f;
    if (b.y < top) ay += (top - b.y) * 9.0f; else if (b.y > H - M) ay -= (b.y - (H - M)) * 9.0f;
    ax += (mmRand() - 0.5f) * 60.0f; ay += (mmRand() - 0.5f) * 60.0f;     // a little wobble
    b.vx += ax * dt; b.vy += ay * dt;
    float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
    float lo = 62.0f, hi = scatter || pred ? 230.0f : 165.0f;
    if (sp > hi) { b.vx *= hi / sp; b.vy *= hi / sp; sp = hi; }
    else if (sp < lo) { if (sp < 1) { b.vx = lo; b.vy = 0; } else { b.vx *= lo / sp; b.vy *= lo / sp; } sp = lo; }
    b.x += b.vx * dt; b.y += b.vy * dt;
    spSum += sp;
  }
  mmSpeedAvg = spSum / (mmActive ? mmActive : 1);
}

// ---------------- render ----------------
static void mmRender(uint32_t now) {
  if (mmBgTheme != mmTheme || !mmBg) mmBuildBg();
  if (mmBg) memcpy(frameBuffer(), mmBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  bool night = mmTheme == 1;
  if (mmMode == 1 && !mmOver) {                                       // ring + progress
    uint16_t rc = night ? rgb(120, 255, 200) : rgb(255, 240, 160);
    float pulse = 0.5f + 0.5f * sinf(now * 0.006f);
    int ri = (int)MM_RING;
    gfx->fillCircle((int)mmRx, (int)mmRy, (int)(MM_RING * mmProg), lerpColor(rc, mmBgRow[constrain((int)mmRy, 0, H - 1)], 0.8f));
    gfx->drawCircle((int)mmRx, (int)mmRy, ri, rc);
    gfx->drawCircle((int)mmRx, (int)mmRy, ri + 1, lerpColor(rc, C_BG, 0.35f + 0.3f * pulse));
    gfx->drawCircle((int)mmRx, (int)mmRy, ri + 3, lerpColor(rc, C_BG, 0.75f));
  }
  for (int i = 0; i < mmActive; i++) {
    const MmBird &b = mmB[i];
    float sp = sqrtf(b.vx * b.vx + b.vy * b.vy);
    float s01 = constrain((sp - 62.0f) / 150.0f, 0.0f, 1.0f);
    uint16_t base = night ? lerpColor(rgb(90, 170, 255), rgb(255, 150, 230), s01) : lerpColor(rgb(14, 6, 24), rgb(90, 24, 60), s01);
    uint16_t bgc = mmBgRow[constrain((int)b.y, 0, H - 1)];
    int px = (int)b.x, py = (int)b.y;
    for (int k = MM_TRAIL - 1; k >= 0; k--) {
      float f = (k + 1) / (float)(MM_TRAIL + 1);
      uint16_t c = lerpColor(base, bgc, f * 0.9f);
      gfx->drawLine(px, py, b.hx[k], b.hy[k], c);
      px = b.hx[k]; py = b.hy[k];
    }
    gfx->fillRect((int)b.x - 1, (int)b.y - 1, 3, 3, night ? lerpColor(base, C_TEXT, 0.5f) : base);
  }
  // the light
  {
    uint16_t lc = night ? rgb(200, 230, 255) : rgb(255, 230, 150);
    int lx = (int)mmLx, ly = (int)mmLy;
    float pulse = 0.5f + 0.5f * sinf(now * 0.005f);
    gfx->fillCircle(lx, ly, 22, lerpColor(lc, mmBgRow[constrain(ly, 0, H - 1)], 0.86f));
    gfx->fillCircle(lx, ly, 15, lerpColor(lc, mmBgRow[constrain(ly, 0, H - 1)], 0.62f - 0.1f * pulse));
    gfx->fillCircle(lx, ly, 9, lerpColor(lc, C_TEXT, 0.4f));
    gfx->fillCircle(lx, ly, 4, C_TEXT);
    if (in.rightDown) gfx->drawCircle(lx, ly, 70 + (int)(30 * pulse), lerpColor(lc, C_BG, 0.4f));
    if (in.leftDown) gfx->drawCircle(lx, ly, 46 - (int)(24 * pulse), lerpColor(lc, C_BG, 0.3f));
  }
  if (mmMode == 1) {
    hudBar(mmScore, mmBest, rgb(8, 8, 22), C_TEXT);
    char b[16];
    snprintf(b, sizeof b, "%d s", (int)ceilf(fmaxf(mmTime, 0)));
    text(b, 262, 9, 2, mmTime < 10 ? C_BAD : C_ACC);
  }
  if (now < mmHintUntil) {
    uint16_t hc = lerpColor(C_TEXT, C_BG, constrain(1.0f - (mmHintUntil - now) / 1200.0f, 0.0f, 1.0f));
    centerText(mmMode == 1 ? "herd the birds into the ring" : "tilt = light   LEFT gather   RIGHT scatter   touch = predator", 346, 1, hc);
  }
  if (mmOver) {
    char l2[32];
    snprintf(l2, sizeof l2, mmNewBest ? "NEW BEST %lu" : "RINGS %lu", (unsigned long)mmScore);
    panelBox("TIME UP", l2, nullptr, C_ACC);
    centerText("RIGHT again   LEFT menu", 226, 1, C_DIM);
  }
  dirty = true;
}

// ---------------- frame ----------------
static bool mmFrame(uint32_t now) {
  homeLock = true;                                                    // LEFT / RIGHT are held often: no triple-tap home
  uint32_t t0 = micros();
  float dt = fminf((now - mmLast) / 1000.0f, 0.04f);
  mmLast = now;
  tuneUpdate(now);
  if (mmOver) {
    if (now - mmOverAt > 900) {
      if (in.right) { mmStart(); return true; }
      if (in.left) return false;
    }
    if (now - mmLastRender >= 33) { mmLastRender = now; mmRender(now); }
    return true;
  }
  // light follows the tilt (STICK: axisX()/axisY() already merge the analog stick)
  if (imuOk) mmCur.update(dt, W / 2, (44 + H - 30) / 2.0f, 900.0f, 760.0f, 0.04f, 7.0f, 20, W - 20, 50, H - 30);
  else { mmCur.x = W / 2 + 150 * sinf(now * 0.0006f); mmCur.y = H / 2 + 90 * sinf(now * 0.0009f); }
  mmLx = mmCur.x; mmLy = mmCur.y;
  bool pred = in.touching;
  mmUpdate(dt, in.leftDown && !in.rightDown, in.rightDown && !in.leftDown, pred, (float)in.touchX, (float)in.touchY);
  mmTrailT += dt;
  if (mmTrailT >= 0.03f) {
    mmTrailT = 0;
    for (int i = 0; i < mmActive; i++) {
      MmBird &b = mmB[i];
      for (int k = MM_TRAIL - 1; k > 0; k--) { b.hx[k] = b.hx[k - 1]; b.hy[k] = b.hy[k - 1]; }
      b.hx[0] = (int16_t)b.x; b.hy[0] = (int16_t)b.y;
    }
  }
  if (in.left && !in.rightDown) play(330, 14);
  if (in.right && !in.leftDown) play(660, 14);

  if (mmMode == 1) {
    mmTime -= dt;
    int inside = 0;
    float r2 = MM_RING * MM_RING;
    for (int i = 0; i < mmActive; i++) {
      float dx = mmB[i].x - mmRx, dy = mmB[i].y - mmRy;
      if (dx * dx + dy * dy < r2) inside++;
    }
    if (inside >= (int)(mmActive * 0.55f)) mmProg += dt / 2.5f; else mmProg = fmaxf(0.0f, mmProg - dt / 4.0f);
    if (mmProg >= 1.0f) {
      mmScore++; mmTime += 6.0f; if (mmScore > mmBest) mmBest = mmScore;
      ptBurst(mmRx, mmRy, 24, 160, C_TEXT, C_ACC, 0.8f, 0, 2);
      tunePlay(TUNE_LEVEL);
      mmNewRing();
    }
    if (mmTime <= 0) {
      mmOver = true; mmOverAt = now;
      mmNewBest = saveBest("mmBest", mmScore);
      if (mmNewBest) { mmBest = mmScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
    }
  }
  ptUpdate(dt);
  if (now - mmLastRender >= 16) {
    mmLastRender = now;
    mmRender(now);
    ptDraw();
  }
  // adaptive flock size from the smoothed update+render cost
  mmEma += ((float)(micros() - t0) - mmEma) * 0.05f;
  mmAdaptT += dt;
  if (mmAdaptT > 0.5f) {
    mmAdaptT = 0;
    if (mmEma > 14000.0f && mmActive > 120) mmActive -= 12;
    else if (mmEma < 9000.0f && mmActive < MM_N) mmActive = min(MM_N, mmActive + 6);
  }
  return true;
}

Game GAME_MURMUR = {"MURMUR", "tilt a light, steer the flock", 0xC81F, mmOpts, 2, mmStart, mmFrame};
