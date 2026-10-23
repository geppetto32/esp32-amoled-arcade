// stacker_game.cpp - one-button timing game. A block slides left/right; press LEFT or RIGHT to drop it on the tower.
// The overhang is cut off, the tower scrolls down as it grows, perfect drops give a bonus (and win width back).
#include "game.h"

enum St : uint8_t { ST_COUNT, ST_PLAY, ST_OVER };
static constexpr int LH = 22, BASE_W = 170, GY = H - 34, MAXL = 600, NDEB = 6;
static constexpr float PERFECT_PX = 4.0f;

struct Debris { float x, w, yo, vy; int layer; uint16_t c; bool on; };

static float lx[MAXL], lw[MAXL];
static uint16_t lc[MAXL];
static int n, total, combo;               // n = layers placed (index of the moving block), total = absolute count (for colour)
static float mx, mw, mdir, camY;
static Debris deb[NDEB];
static St st;
static uint32_t score, best, t0, lastT, lastRender, overAt, playAt, flashAt, popAt;
static int flashLayer;
static char popText[32];
static bool newBest;
static uint16_t *bg = nullptr;

static inline uint32_t hash2(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float wrapf(float v, float m) {
  v = fmodf(v, m);
  return v < 0 ? v + m : v;
}
static uint16_t hueCol(float h) {          // hue 0..360, bright and saturated
  h = wrapf(h, 360.0f);
  float c = 0.78f, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = 1.0f - c;
  float r = 0, g = 0, b = 0;
  int s = (int)(h / 60.0f);
  switch (s) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  return rgb((uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255));
}
static float layerY(float layer) { return GY - (layer + 1.0f - camY) * LH; }   // screen y of a layer's top edge

static void buildBackground() {
  if (!bg) bg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  gfx->fillScreen(C_BG);
  for (int y = 32; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(C_BG, rgb(16, 4, 36), (float)(y - 32) / (H - 32)));
  memcpy(bg, frameBuffer(), FB_PIXELS * 2);
}

static void drawBlock(float x, float y, float w, uint16_t c, bool shine) {
  int xi = (int)x, wi = (int)w, yi = (int)y, h = LH - 1;
  gfx->fillRect(xi - 3, yi - 2, wi + 6, h + 4, lerpColor(c, C_BG, 0.88f));
  gfx->fillRect(xi - 1, yi - 1, wi + 2, h + 2, lerpColor(c, C_BG, 0.7f));
  gfx->fillRect(xi, yi, wi, h / 2, shine ? lerpColor(c, C_TEXT, 0.25f) : c);
  gfx->fillRect(xi, yi + h / 2, wi, h - h / 2, lerpColor(c, C_BG, 0.3f));
  gfx->drawFastHLine(xi, yi, wi, lerpColor(c, C_TEXT, 0.7f));
  gfx->drawFastVLine(xi, yi, h, lerpColor(c, C_TEXT, 0.35f));
}

static void render(uint32_t now) {
  memcpy(frameBuffer(), bg, FB_PIXELS * 2);
  // parallax stars follow the camera
  for (int layer = 0; layer < 2; layer++) {
    float k = layer == 0 ? 0.12f : 0.3f;
    for (int i = 0; i < 40; i++) {
      uint32_t h = hash2(i, 90 + layer);
      int x = h % W, y = (int)wrapf((float)((h >> 9) % H) + camY * LH * k, (float)H);
      uint16_t c = layer == 0 ? rgb(80, 80, 110) : lerpColor(rgb(160, 170, 230), C_TEXT, 0.5f + 0.5f * sinf(now * 0.004f + i));
      gfx->fillRect(x, y, layer + 1, layer + 1, c);
    }
  }
  // floor under the first layer
  float fy = layerY(-1.0f) + LH;
  if (fy < H) {
    gfx->fillRect(0, (int)fy, W, H - (int)fy, rgb(14, 8, 28));
    gfx->drawFastHLine(0, (int)fy, W, rgb(120, 60, 200));
    gfx->drawFastHLine(0, (int)fy + 1, W, rgb(50, 20, 90));
  }
  // tower
  int i0 = (int)camY - 1;
  if (i0 < 0) i0 = 0;
  for (int i = i0; i < n; i++) {
    float y = layerY((float)i);
    if (y > H || y < -LH) continue;
    drawBlock(lx[i], y, lw[i], lc[i], false);
  }
  // flash ring on a perfect drop
  if (now - flashAt < 350 && flashLayer < n) {
    float f = (now - flashAt) / 350.0f;
    int e = (int)(f * 12), li = flashLayer;
    uint16_t c = lerpColor(C_TEXT, C_BG, f);
    gfx->drawRect((int)lx[li] - e, (int)layerY((float)li) - e, (int)lw[li] + 2 * e, LH - 1 + 2 * e, c);
  }
  // moving block (not in game over)
  if (st != ST_OVER) drawBlock(mx, layerY((float)n), mw, lc[n], true);
  // cut-off pieces
  for (int i = 0; i < NDEB; i++) {
    if (!deb[i].on) continue;
    drawBlock(deb[i].x, layerY((float)deb[i].layer) + deb[i].yo, deb[i].w, deb[i].c, false);
  }
  hudBar(score, best, C_BG, hueCol(total * 8.0f));
  gfx->drawFastHLine(0, 31, W, lerpColor(hueCol(total * 8.0f), C_BG, 0.5f));
  if (combo >= 2) {
    char b[24];
    snprintf(b, sizeof b, "COMBO x%d", combo);
    text(b, 250, 9, 2, C_ACC);
  }
  if (now - popAt < 800) {
    float f = (now - popAt) / 800.0f;
    centerText(popText, 70 - (int)(f * 24), 3, lerpColor(C_TEXT, C_BG, f * f));
  }
  uint16_t acc = hueCol(total * 8.0f);
  if (st == ST_COUNT) {
    int c3 = 3 - (int)((now - t0) / 700);
    char c[2] = {(char)('0' + constrain(c3, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, 60, 88, 88, 18, C_PANEL);
    centerText(c, 80, 6, acc);
    centerText("LEFT / RIGHT drop the block", 160, 2, C_DIM);
  } else if (st == ST_OVER) {
    char b[32];
    snprintf(b, sizeof b, newBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)score);
    panelBox("TOWER FALLS", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void nextBlock() {                  // the moving block for layer n
  mw = lw[n - 1];
  lc[n] = hueCol(total * 8.0f);
  mdir = -mdir;
  mx = mdir > 0 ? 0.0f : W - mw;
}

static void stackerStart() {
  if (!bg) buildBackground();
  memset(deb, 0, sizeof deb);
  lx[0] = (W - BASE_W) / 2.0f; lw[0] = BASE_W; lc[0] = hueCol(0);
  n = 1; total = 1; combo = 0; score = 0; camY = 0;
  mdir = 1;                                // nextBlock() flips it, so the first block comes from the left
  nextBlock();
  newBest = false; flashAt = popAt = 0; flashLayer = 0;
  best = loadBest("stBest");
  st = ST_COUNT;
  t0 = lastT = millis();
  lastRender = 0;
  flashAt = popAt = t0 - 10000;
  play(660, 90);
  render(t0);
}

static void addDebris(float x, float w, int layer, uint16_t c) {
  for (int i = 0; i < NDEB; i++)
    if (!deb[i].on) { deb[i] = {x, w, 0.0f, 0.0f, layer, c, true}; return; }
}

static void drop(uint32_t now) {
  float tl = lx[n - 1], tr = lx[n - 1] + lw[n - 1];
  float l = fmaxf(mx, tl), r = fminf(mx + mw, tr), ov = r - l;
  if (ov < 3.0f) {                         // missed entirely: the whole block falls, game over
    addDebris(mx, mw, n, lc[n]);
    st = ST_OVER;
    overAt = now;
    combo = 0;
    newBest = saveBest("stBest", score);
    if (newBest) best = score;
    newBest ? sfxBest() : sfxBad();
    return;
  }
  if (fabsf(mx - tl) <= PERFECT_PX) {      // perfect: snap and keep the full width
    combo++;
    lx[n] = tl; lw[n] = lw[n - 1];
    if (combo >= 3 && lw[n] < BASE_W) {    // streak wins width back
      float g = fminf(6.0f, BASE_W - lw[n]);
      lx[n] -= g / 2; lw[n] += g;
      lx[n] = constrain(lx[n], 0.0f, W - lw[n]);
    }
    int bonus = 1 + (combo < 5 ? combo : 5);
    score += 1 + bonus;
    flashAt = now; flashLayer = n;
    snprintf(popText, sizeof popText, combo > 1 ? "PERFECT x%d" : "PERFECT", combo);
    popAt = now;
    play(700 + 90 * (combo < 8 ? combo : 8), 60);
    play(1000 + 90 * (combo < 8 ? combo : 8), 80);
  } else {
    combo = 0;
    lx[n] = l; lw[n] = ov;
    if (mx < tl) addDebris(mx, tl - mx, n, lc[n]);
    else addDebris(tr, mx + mw - tr, n, lc[n]);
    score += 1;
    play(420, 40);
  }
  n++; total++;
  if (n >= MAXL - 1) {                     // keep the arrays bounded: drop the oldest 300 layers
    memmove(lx, lx + 300, (n - 300) * sizeof(float));
    memmove(lw, lw + 300, (n - 300) * sizeof(float));
    memmove(lc, lc + 300, (n - 300) * sizeof(uint16_t));
    n -= 300; camY -= 300; flashLayer -= 300;
    for (int i = 0; i < NDEB; i++) deb[i].layer -= 300;
  }
  nextBlock();
}

static bool stackerFrame(uint32_t now) {
  float dt = constrain((now - lastT) / 1000.0f, 0.0f, 0.05f);
  lastT = now;
  switch (st) {
    case ST_COUNT: {
      static int lastN = 3;
      int c3 = 3 - (int)((now - t0) / 700);
      if (c3 != lastN && c3 >= 1) play(660, 90);
      lastN = c3;
      if (now - t0 >= 2100) { st = ST_PLAY; playAt = now; lastN = 3; sfxStart(); }
      break;
    }
    case ST_PLAY: {
      float spd = fminf(140.0f + 6.0f * (n - 1), 400.0f);
      mx += mdir * spd * dt;
      if (mx < 0) { mx = 0; mdir = 1; }
      if (mx > W - mw) { mx = W - mw; mdir = -1; }
      if ((in.left || in.right) && now - playAt > 150) drop(now);
      break;
    }
    case ST_OVER:
      if (now - overAt > 700) {
        if (in.right) { stackerStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  // camera eases so the moving block stays about 8 layers above the floor
  float targetCam = n > 7 ? (float)(n - 7) : 0.0f;
  camY += (targetCam - camY) * fminf(1.0f, dt * 5.0f);
  for (int i = 0; i < NDEB; i++) {
    if (!deb[i].on) continue;
    deb[i].vy += 1100.0f * dt;
    deb[i].yo += deb[i].vy * dt;
    if (layerY((float)deb[i].layer) + deb[i].yo > H + 30) deb[i].on = false;
  }
  if (now - lastRender >= 16) { lastRender = now; render(now); }
  return true;
}

Game GAME_STACKER = {"STACKER", "drop blocks, build a tower", 0xB81F, nullptr, 0, stackerStart, stackerFrame};
