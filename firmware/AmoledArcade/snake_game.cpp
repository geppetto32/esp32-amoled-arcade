// snake_game.cpp - Snake with three looks:
//   CLASSIC   - tiles on a dark grid (the first version)
//   REALISTIC - shaded scaly snake with eyes and tongue on dark ground, smooth movement
//   NEON      - glowing synthwave snake on a neon grid, particle bursts
// Control: BUTTONS (LEFT/RIGHT turn relative to heading) or TILT (tilt toward where to go, RIGHT = pause).
#include "game.h"
#define SN_COLS 28
#define SN_ROWS 21
#include "snake_logic.h"

static const char *const THEMES[] = {"CLASSIC", "REALISTIC", "NEON"};
static const char *const CONTROLS[] = {"BUTTONS", "TILT"};
static Option snakeOpts[] = {{"THEME", THEMES, 3, "snTheme", 1}, {"CONTROL", CONTROLS, 2, "snCtrl", 0}};
enum { TH_CLASSIC, TH_REAL, TH_NEON };

static constexpr int CELL = 16, OY = 32;            // board starts under the 32 px status bar
static constexpr int START_MS = 230, MIN_MS = 90;
static inline float cxf(int c) { return c * CELL + 8.0f; }
static inline float cyf(int r) { return OY + r * CELL + 8.0f; }

enum St : uint8_t { ST_COUNT, ST_PLAY, ST_PAUSE, ST_OVER };
static SnakeState sn;
static St st;
static int theme, ctrl;
static uint32_t t0, lastStep, lastRender, best;
static int8_t turnQ[2];
static int turnN;
static int prevTailX, prevTailY;      // cell the tail left at the last step
static bool newBest;
static uint16_t *bg = nullptr;        // pre-rendered background (raw canvas layout)
static int bgTheme = -1;

struct Particle { float x, y, vx, vy; uint32_t born; uint16_t c; };
static Particle parts[24];
static int nparts = 0;

// raw pixel write into the canvas (rotation 1: logical x -> row, logical y -> mirrored column)
static inline void rawPix(uint16_t *fb, int x, int y, uint16_t c) { fb[x * 368 + (367 - y)] = c; }
static inline uint32_t hash2(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// ---------------- CLASSIC tiles ----------------
enum Tile { T_EMPTY, T_HEAD, T_BODY, T_BODY2, T_FOOD, T_COUNT };
static uint16_t tiles[T_COUNT][CELL * CELL];
static bool tilesBuilt = false;
static void buildTiles() {
  const uint16_t fill[T_COUNT] = {C_BG, C_GOOD, 0x05E0, 0x0480, C_BAD};
  for (int t = 0; t < T_COUNT; t++)
    for (int y = 0; y < CELL; y++)
      for (int x = 0; x < CELL; x++) {
        uint16_t c = C_BG;
        if (t == T_EMPTY) {
          if ((x == 7 || x == 8) && (y == 7 || y == 8)) c = 0x0841;
        } else if (t == T_FOOD) {
          float dx = x - 7.5f, dy = y - 8.5f;
          if (dx * dx + dy * dy <= 36.f) c = C_BAD;
          if ((x == 8 || x == 9) && y >= 1 && y <= 3) c = 0x05E0;
        } else {
          int ix = x < 8 ? 6 - x : x - 9, iy = y < 8 ? 6 - y : y - 9;
          bool inside = x >= 2 && x <= 13 && y >= 2 && y <= 13;
          if (inside && !(ix >= 2 && iy >= 2 && ix + iy > 5)) c = fill[t];
        }
        tiles[t][y * CELL + x] = c;
      }
  tilesBuilt = true;
}

// ---------------- backgrounds ----------------
static void buildBackground() {
  if (!bg) bg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  uint16_t *fb = frameBuffer();
  gfx->fillScreen(C_BG);
  if (theme == TH_CLASSIC) {
    for (int r = 0; r < SN_ROWS; r++)
      for (int c = 0; c < SN_COLS; c++) gfx->fillRect(c * CELL + 7, OY + r * CELL + 7, 2, 2, 0x0841);
  } else if (theme == TH_REAL) {
    // dark earth with grain, darker towards the edges
    for (int y = OY; y < H; y++)
      for (int x = 0; x < W; x++) {
        uint32_t h = hash2(x, y);
        float ex = (x - W / 2.0f) / (W / 2.0f), ey = (y - (OY + H) / 2.0f) / ((H - OY) / 2.0f);
        float v = 1.0f - 0.45f * (ex * ex + ey * ey) * 0.5f;
        int n = (int)(h & 15) - 8;
        int r = (int)((26 + n) * v), g = (int)((30 + n) * v), b = (int)((14 + n / 2) * v);
        rawPix(fb, x, y, rgb(max(r, 0) * 1, max(g, 0), max(b, 0)));
      }
    // grass tufts and pebbles
    for (int i = 0; i < 900; i++) {
      uint32_t h = hash2(i, 7);
      int x = h % W, y = OY + (h >> 9) % (H - OY), len = 2 + (h >> 20) % 5;
      uint16_t c = (h >> 24) & 1 ? rgb(40, 70, 26) : rgb(52, 86, 30);
      gfx->drawLine(x, y, x + (int)((h >> 26) % 3) - 1, y - len, c);
    }
    for (int i = 0; i < 70; i++) {
      uint32_t h = hash2(i, 99);
      int x = h % W, y = OY + (h >> 9) % (H - OY), r = 1 + (h >> 20) % 2;
      gfx->fillCircle(x, y, r, rgb(58, 56, 50));
      gfx->drawPixel(x - 1, y - 1, rgb(90, 88, 80));
    }
    gfx->drawRect(0, OY, W, H - OY, rgb(60, 44, 24));
  } else {
    // neon grid
    for (int x = 0; x <= W; x += CELL) gfx->drawFastVLine(min(x, W - 1), OY, H - OY, (x / CELL) % 4 ? rgb(28, 0, 46) : rgb(70, 0, 110));
    for (int y = OY; y <= H; y += CELL) gfx->drawFastHLine(0, min(y, H - 1), W, ((y - OY) / CELL) % 4 ? rgb(28, 0, 46) : rgb(70, 0, 110));
    for (int k = 0; k < 3; k++) gfx->drawRect(k, OY + k, W - 2 * k, H - OY - 2 * k, lerpColor(rgb(255, 40, 200), C_BG, k * 0.35f));
  }
  memcpy(bg, fb, FB_PIXELS * 2);
  bgTheme = theme;
}

// ---------------- snake path (smooth) ----------------
static float spx[4096], spy[4096], sfr[4096];   // samples along the body: position + 0..1 head->tail
static int ns = 0;
static float headX, headY, dirX, dirY;

static void buildPath(float t) {
  ns = 0;
  static float px[SN_MAX + 2], py[SN_MAX + 2];   // static: too big for the stack
  int np = 0;
  if (sn.len > 1) {
    headX = cxf(sn.sx[1]) + (cxf(sn.sx[0]) - cxf(sn.sx[1])) * t;
    headY = cyf(sn.sy[1]) + (cyf(sn.sy[0]) - cyf(sn.sy[1])) * t;
    dirX = sn.sx[0] - sn.sx[1];
    dirY = sn.sy[0] - sn.sy[1];
  } else {
    headX = cxf(sn.sx[0]); headY = cyf(sn.sy[0]); dirX = sn.dx; dirY = sn.dy;
  }
  px[np] = headX; py[np++] = headY;
  for (int i = 1; i < sn.len; i++) { px[np] = cxf(sn.sx[i]); py[np++] = cyf(sn.sy[i]); }
  float lx = cxf(sn.sx[sn.len - 1]), ly = cyf(sn.sy[sn.len - 1]);
  px[np] = cxf(prevTailX) + (lx - cxf(prevTailX)) * t;
  py[np++] = cyf(prevTailY) + (ly - cyf(prevTailY)) * t;
  // total length for the 0..1 fraction
  float total = 0;
  for (int i = 1; i < np; i++) total += fabsf(px[i] - px[i - 1]) + fabsf(py[i] - py[i - 1]);
  if (total < 1) total = 1;
  float run = 0;
  for (int i = 1; i < np && ns < 4090; i++) {
    float dx = px[i] - px[i - 1], dy = py[i] - py[i - 1];
    float seg = fabsf(dx) + fabsf(dy);
    int steps = max(1, (int)(seg / 3));
    for (int k = 0; k < steps && ns < 4090; k++) {
      float f = (float)k / steps;
      spx[ns] = px[i - 1] + dx * f;
      spy[ns] = py[i - 1] + dy * f;
      sfr[ns++] = (run + seg * f) / total;
    }
    run += seg;
  }
  spx[ns] = px[np - 1]; spy[ns] = py[np - 1]; sfr[ns++] = 1.0f;
}

static float taper(float f) { return f < 0.75f ? 1.0f : 1.0f - 0.55f * (f - 0.75f) / 0.25f; }

// ---------------- REALISTIC ----------------
static void drawApple(float fx, float fy) {
  int cx = (int)fx, cy = (int)fy;
  gfx->fillEllipse(cx + 1, cy + 7, 6, 2, rgb(10, 12, 6));              // shadow
  for (int y = -7; y <= 7; y++)
    for (int x = -7; x <= 7; x++) {
      int d2 = x * x + y * y;
      if (d2 > 49) continue;
      float t = sqrtf((x + 2.5f) * (x + 2.5f) + (y + 2.5f) * (y + 2.5f)) / 11.0f;
      uint16_t c = lerpColor(rgb(255, 96, 80), rgb(110, 0, 12), t);
      if ((x + 3) * (x + 3) + (y + 3) * (y + 3) <= 2) c = rgb(255, 230, 220);   // shine
      gfx->writePixel(cx + x, cy + y, c);
    }
  gfx->drawLine(cx, cy - 6, cx + 1, cy - 10, rgb(90, 60, 30));
  gfx->fillEllipse(cx + 4, cy - 9, 3, 1, rgb(70, 150, 40));
}

static void drawRealSnake(uint32_t now) {
  // shadow, outline, body, dorsal pattern, gloss
  for (int i = ns - 1; i >= 0; i--) gfx->fillCircle(spx[i] + 2, spy[i] + 3, (int)(7 * taper(sfr[i])), rgb(8, 10, 5));
  for (int i = ns - 1; i >= 0; i--) gfx->fillCircle(spx[i], spy[i], (int)(7 * taper(sfr[i]) + 0.5f), rgb(16, 36, 10));
  for (int i = ns - 1; i >= 0; i--)
    gfx->fillCircle(spx[i], spy[i], (int)(6 * taper(sfr[i])), lerpColor(rgb(118, 176, 62), rgb(66, 112, 38), sfr[i]));
  for (int i = ns - 1; i >= 4; i -= 5) {
    float f = sfr[i];
    if (f > 0.92f) continue;
    gfx->fillCircle(spx[i], spy[i], (int)(3 * taper(f)), lerpColor(rgb(52, 90, 28), rgb(34, 62, 20), f));
    gfx->drawPixel(spx[i], spy[i], rgb(170, 160, 70));               // tiny yellow scale fleck
  }
  for (int i = ns - 1; i >= 0; i--)
    if (sfr[i] < 0.9f) gfx->fillCircle(spx[i] - 2, spy[i] - 2, 1, lerpColor(rgb(200, 236, 140), rgb(120, 160, 80), sfr[i]));
  // head
  int hx = (int)headX, hy = (int)headY;
  float px = -dirY, py = dirX;
  gfx->fillCircle(hx + 2, hy + 3, 8, rgb(8, 10, 5));
  gfx->fillCircle(hx, hy, 8, rgb(16, 36, 10));
  gfx->fillCircle(hx, hy, 7, rgb(126, 184, 66));
  gfx->fillCircle(hx - 2, hy - 2, 2, rgb(210, 240, 150));
  for (int s = -1; s <= 1; s += 2) {
    int ex = hx + (int)(dirX * 3 + px * 4 * s), ey = hy + (int)(dirY * 3 + py * 4 * s);
    gfx->fillCircle(ex, ey, 2, rgb(235, 225, 150));
    gfx->fillRect(ex + (int)dirX - (dirX == 0 ? 0 : 0), ey + (int)dirY, 1 + (dirX == 0), 1 + (dirY == 0), C_BG);  // slit pupil
  }
  if (st == ST_PLAY && (now / 110) % 16 < 2) {                       // tongue flick
    int tx = hx + (int)(dirX * 8), ty = hy + (int)(dirY * 8);
    int ux = hx + (int)(dirX * 13), uy = hy + (int)(dirY * 13);
    gfx->drawLine(tx, ty, ux, uy, rgb(220, 30, 50));
    gfx->drawLine(ux, uy, ux + (int)(dirX * 2 + px * 2), uy + (int)(dirY * 2 + py * 2), rgb(220, 30, 50));
    gfx->drawLine(ux, uy, ux + (int)(dirX * 2 - px * 2), uy + (int)(dirY * 2 - py * 2), rgb(220, 30, 50));
  }
}

// ---------------- NEON ----------------
static void spawnBurst(float x, float y, uint32_t now) {
  nparts = 0;
  for (int i = 0; i < 24; i++) {
    float a = i * 0.2618f + (hash2(i, now) % 100) * 0.003f, s = 0.06f + (hash2(now, i) % 100) * 0.0012f;
    parts[nparts++] = {x, y, cosf(a) * s, sinf(a) * s, now, i & 1 ? rgb(255, 60, 200) : rgb(80, 240, 255)};
  }
}
static void drawParticles(uint32_t now) {
  for (int i = 0; i < nparts; i++) {
    uint32_t age = now - parts[i].born;
    if (age > 650) continue;
    float x = parts[i].x + parts[i].vx * age, y = parts[i].y + parts[i].vy * age;
    gfx->fillRect((int)x - 1, (int)y - 1, 3, 3, lerpColor(parts[i].c, C_BG, age / 650.0f));
  }
}
static void drawNeonSnake(uint32_t now) {
  for (int i = ns - 1; i >= 0; i--)
    gfx->fillCircle(spx[i], spy[i], (int)(8 * taper(sfr[i])), lerpColor(rgb(0, 60, 80), rgb(46, 0, 84), sfr[i]));
  for (int i = ns - 1; i >= 0; i--)
    gfx->fillCircle(spx[i], spy[i], (int)(5 * taper(sfr[i])), lerpColor(rgb(0, 220, 255), rgb(170, 60, 255), sfr[i]));
  for (int i = ns - 1; i >= 0; i--)
    gfx->fillCircle(spx[i], spy[i], (int)(2 * taper(sfr[i])), lerpColor(rgb(210, 255, 255), rgb(240, 200, 255), sfr[i]));
  int hx = (int)headX, hy = (int)headY;
  float pulse = 0.5f + 0.5f * sinf(now * 0.012f);
  gfx->fillCircle(hx, hy, 10, lerpColor(rgb(0, 50, 70), rgb(0, 100, 130), pulse));
  gfx->fillCircle(hx, hy, 7, rgb(120, 255, 255));
  gfx->fillCircle(hx, hy, 3, C_TEXT);
  float px = -dirY, py = dirX;
  for (int s = -1; s <= 1; s += 2)
    gfx->fillCircle(hx + (int)(dirX * 3 + px * 4 * s), hy + (int)(dirY * 3 + py * 4 * s), 2, rgb(255, 40, 200));
}
static void drawNeonFood(float fx, float fy, uint32_t now) {
  float s = sinf(now * 0.008f);
  int cx = (int)fx, cy = (int)fy;
  gfx->drawCircle(cx, cy, 10 + (int)(3 * s), rgb(90, 0, 70));
  gfx->drawCircle(cx, cy, 8 + (int)(2 * s), rgb(160, 0, 120));
  for (int k = 6; k >= 1; k--) {                                       // diamond, glowing
    uint16_t c = lerpColor(rgb(255, 255, 255), rgb(255, 30, 170), k / 6.0f);
    gfx->fillTriangle(cx - k, cy, cx + k, cy, cx, cy - k - 2, c);
    gfx->fillTriangle(cx - k, cy, cx + k, cy, cx, cy + k + 2, c);
  }
}

// ---------------- frame ----------------
static void render(uint32_t now) {
  if (bgTheme != theme) buildBackground();
  memcpy(frameBuffer(), bg, FB_PIXELS * 2);
  float t = st == ST_PLAY ? constrain((now - lastStep) / (float)sn.stepMs, 0.0f, 1.0f) : 1.0f;
  if (theme == TH_CLASSIC) {
    for (int i = sn.len - 1; i >= 0; i--)
      gfx->draw16bitRGBBitmap(sn.sx[i] * CELL, OY + sn.sy[i] * CELL, tiles[i == 0 ? T_HEAD : (i & 1 ? T_BODY : T_BODY2)], CELL, CELL);
    gfx->draw16bitRGBBitmap(sn.fx * CELL, OY + sn.fy * CELL, tiles[T_FOOD], CELL, CELL);
    hudBar(sn.score, best, C_PANEL, C_TEXT);
  } else if (theme == TH_REAL) {
    drawApple(cxf(sn.fx), cyf(sn.fy));
    buildPath(t);
    drawRealSnake(now);
    hudBar(sn.score, best, rgb(30, 24, 14), rgb(232, 214, 160));
  } else {
    drawNeonFood(cxf(sn.fx), cyf(sn.fy), now);
    buildPath(t);
    drawNeonSnake(now);
    drawParticles(now);
    hudBar(sn.score, best, C_BG, rgb(80, 240, 255));
    gfx->drawFastHLine(0, 31, W, rgb(255, 40, 200));
  }
  uint16_t acc = theme == TH_NEON ? rgb(255, 60, 200) : theme == TH_REAL ? rgb(232, 200, 120) : C_ACC;
  if (st == ST_COUNT) {
    int n = 3 - (int)((now - t0) / 700);
    char c[2] = {(char)('0' + constrain(n, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, H / 2 - 34, 88, 88, 18, C_PANEL);
    centerText(c, H / 2 - 14, 6, acc);
    centerText(ctrl ? "hold the board how you play" : "LEFT / RIGHT turn the snake", H / 2 + 66, 2, C_DIM);
  } else if (st == ST_PAUSE) {
    panelBox("PAUSED", nullptr, "RIGHT resume   LEFT menu", acc);
  } else if (st == ST_OVER) {
    char b[32];
    snprintf(b, sizeof b, newBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)sn.score);
    panelBox(sn.alive ? "YOU WIN!" : "GAME OVER", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void snakeStart() {
  theme = snakeOpts[0].value;
  ctrl = snakeOpts[1].value;
  if (ctrl == 1 && !imuOk) ctrl = 0;
  if (!tilesBuilt) buildTiles();
  snakeReset(sn, esp_random());
  sn.stepMs = START_MS;
  prevTailX = sn.sx[sn.len - 1] - 1;
  prevTailY = sn.sy[sn.len - 1];
  turnN = 0;
  nparts = 0;
  newBest = false;
  best = loadBest(ctrl ? "snBestT" : "snBestB");
  st = ST_COUNT;
  t0 = millis();
  play(660, 90);
  render(t0);
}

static uint32_t snPauseAt, snOverAt;                // input guards: a stray press right after pause / death must not exit
static void doStep(uint32_t now) {
  if (ctrl == 0 && turnN > 0) {
    int t = turnQ[0];
    turnQ[0] = turnQ[1];
    turnN--;
    if (t < 0) snakeSetDir(sn, sn.dy, -sn.dx);     // left
    else snakeSetDir(sn, -sn.dy, sn.dx);           // right
  }
  int tx = sn.sx[sn.len - 1], ty = sn.sy[sn.len - 1];
  SnakeResult r = snakeStep(sn);
  if (r == SN_DEAD || r == SN_WON) {
    st = ST_OVER; snOverAt = now;
    newBest = saveBest(ctrl ? "snBestT" : "snBestB", sn.score);
    if (newBest) best = sn.score;
    newBest ? sfxBest() : sfxBad();
    return;
  }
  if (r == SN_MOVED) { prevTailX = tx; prevTailY = ty; }
  else { prevTailX = sn.sx[sn.len - 1]; prevTailY = sn.sy[sn.len - 1]; }
  if (r == SN_ATE) {
    if (sn.stepMs < MIN_MS) sn.stepMs = MIN_MS;
    sfxGood();
    if (theme == TH_NEON) spawnBurst(cxf(sn.sx[0]), cyf(sn.sy[0]), now);
  }
}

static bool snakeFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  switch (st) {
    case ST_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - t0) / 700);
      if (n != lastN && n >= 1) { play(660, 90); }
      lastN = n;
      if (now - t0 >= 2100) { st = ST_PLAY; imuZero(); lastStep = now; sfxStart(); lastN = 3; }
      break;
    }
    case ST_PLAY:
      if (ctrl == 0) {
        if (in.left && turnN < 2) { turnQ[turnN++] = -1; play(1500, 12); }
        if (in.right && turnN < 2) { turnQ[turnN++] = +1; play(1500, 12); }
      } else {
        int dx, dy;
        if (tiltDir(dx, dy) && !(dx == sn.dx && dy == sn.dy) && !(dx == -sn.dx && dy == -sn.dy) &&
            (dx != sn.ndx || dy != sn.ndy)) { snakeSetDir(sn, dx, dy); play(1500, 12); }
        if (in.right) { st = ST_PAUSE; snPauseAt = now; sfxClick(); break; }
      }
      if ((int32_t)(now - lastStep) >= sn.stepMs) {
        lastStep = (now - lastStep > 2u * sn.stepMs) ? now : lastStep + sn.stepMs;   // steady rhythm
        doStep(now);
      }
      break;
    case ST_PAUSE:
      if (in.right) { st = ST_PLAY; imuZero(); lastStep = now; sfxClick(); }
      if (in.left && now - snPauseAt > 500) return false;
      break;
    case ST_OVER:
      if (now - snOverAt > 700) {
        if (in.right) { snakeStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - lastRender >= 16) { lastRender = now; render(now); }       // up to ~60 fps
  return true;
}

Game GAME_SNAKE = {"SNAKE", "classic, realistic or neon", 0x87F0, snakeOpts, 2, snakeStart, snakeFrame};
