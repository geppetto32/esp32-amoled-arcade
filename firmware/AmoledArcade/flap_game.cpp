// flap_game.cpp - Flappy-style game. Either button flaps. THEME: DAY (sky, hills, clouds) or NIGHT (AMOLED black, neon pipes).
#include "game.h"

static const char *const THEMES[] = {"DAY", "NIGHT"};
static Option flOpts[] = {{"THEME", THEMES, 2, "flTheme", 0}};

enum St : uint8_t { ST_COUNT, ST_PLAY, ST_OVER };
static constexpr int TOP = 32, GROUND = H - 30, PW = 54, NPIPE = 4, SPACING = 200, BX = 120, BIRD_R = 11;
static constexpr float GRAVITY = 1000.0f, FLAP_V = -310.0f, MAX_FALL = 520.0f;

struct Pipe { float x, gy, gh; bool scored; };
static Pipe pipes[NPIPE];
static St st;
static int theme, score;
static float by, bvy, scroll;
static uint32_t t0, lastT, lastRender, overAt, lastFlap, best;
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
static float curGap() { return 132.0f - 1.4f * (float)(score < 25 ? score : 25); }
static float curSpeed() { return 105.0f + 2.5f * (float)(score < 30 ? score : 30); }

static void newGap(Pipe &p, float prev) {
  p.gh = curGap();
  float lo = TOP + 26 + p.gh / 2, hi = GROUND - 26 - p.gh / 2;
  float gy = prev + (float)((int)(esp_random() % 281) - 140);
  p.gy = constrain(gy, lo, hi);
  p.scored = false;
}
static void resetPipes() {
  float prev = (TOP + GROUND) / 2.0f;
  for (int i = 0; i < NPIPE; i++) {
    pipes[i].x = W + 120 + i * SPACING;
    newGap(pipes[i], prev);
    prev = pipes[i].gy;
  }
}
static void recyclePipes() {
  for (int i = 0; i < NPIPE; i++) {
    if (pipes[i].x > -PW - 10) continue;
    int m = 0;
    for (int k = 1; k < NPIPE; k++) if (pipes[k].x > pipes[m].x) m = k;
    float px = pipes[m].x, pg = pipes[m].gy;
    pipes[i].x = px + SPACING;
    newGap(pipes[i], pg);
  }
}

// ---------------- background ----------------
static float hillH(float x, float seed, float amp) {
  return amp * (0.6f + 0.4f * sinf(x * 0.013f + seed)) + 0.35f * amp * sinf(x * 0.031f + seed * 2.0f);
}
static void buildBackground() {
  if (!bg) bg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  gfx->fillScreen(C_BG);
  for (int y = TOP; y < GROUND; y++) {
    float t = (float)(y - TOP) / (GROUND - TOP);
    uint16_t c = theme == 0 ? lerpColor(rgb(60, 150, 250), rgb(190, 230, 255), t)
                            : lerpColor(C_BG, rgb(34, 0, 52), t * t);
    gfx->drawFastHLine(0, y, W, c);
  }
  if (theme == 1) {                                        // moon
    for (int k = 5; k >= 1; k--) gfx->fillCircle(350, 96, 18 + k * 7, lerpColor(rgb(170, 190, 255), C_BG, 0.80f + k * 0.04f));
    gfx->fillCircle(350, 96, 20, rgb(228, 236, 255));
    gfx->fillCircle(344, 90, 15, rgb(196, 208, 246));
    gfx->fillCircle(354, 100, 4, rgb(176, 190, 236));
    for (int i = 0; i < 40; i++) {
      uint32_t h = hash2(i, 5);
      gfx->drawPixel(h % W, TOP + (h >> 9) % (GROUND - TOP - 60), rgb(90, 90, 120));
    }
  }
  memcpy(bg, frameBuffer(), FB_PIXELS * 2);
}

static void drawHills(float sc) {
  for (int layer = 0; layer < 2; layer++) {
    float k = layer == 0 ? 0.18f : 0.42f, seed = layer == 0 ? 1.3f : 4.1f, amp = layer == 0 ? 70.0f : 46.0f;
    uint16_t body = theme == 0 ? (layer == 0 ? rgb(130, 200, 170) : rgb(80, 170, 100))
                               : (layer == 0 ? rgb(22, 6, 46) : rgb(44, 0, 70));
    uint16_t rim = layer == 0 ? rgb(0, 150, 200) : rgb(230, 30, 170);
    int prevY = 0;
    for (int x = 0; x <= W; x += 2) {
      int h = (int)hillH(x + sc * k, seed, amp);
      gfx->drawFastVLine(x, GROUND - h, h, body);
      gfx->drawFastVLine(x + 1, GROUND - h, h, body);
      if (theme == 1) {
        if (x > 0) gfx->drawLine(x - 2, prevY, x, GROUND - h, rim);
        prevY = GROUND - h;
      }
    }
  }
}
static void drawSky(uint32_t now) {
  if (theme == 0) {                                        // clouds, two speeds
    for (int i = 0; i < 6; i++) {
      float k = i < 3 ? 0.12f : 0.26f;
      float cx = wrapf(i * 150.0f - scroll * k, W + 140.0f) - 70.0f;
      int cy = 60 + (int)(hash2(i, 3) % 90);
      int r = i < 3 ? 12 : 17;
      uint16_t c = i < 3 ? rgb(235, 245, 255) : rgb(255, 255, 255);
      gfx->fillCircle((int)cx, cy, r, c);
      gfx->fillCircle((int)cx + r, cy + 3, r - 3, c);
      gfx->fillCircle((int)cx - r, cy + 4, r - 4, c);
      gfx->fillRect((int)cx - r, cy + 4, 2 * r, r - 4, c);
    }
  } else {                                                 // parallax stars
    for (int layer = 0; layer < 2; layer++) {
      float k = layer == 0 ? 0.06f : 0.14f;
      for (int i = 0; i < 34; i++) {
        uint32_t h = hash2(i, 40 + layer);
        float x = wrapf((float)(h % W) - scroll * k, (float)W);
        int y = TOP + 4 + (int)((h >> 10) % (GROUND - TOP - 70));
        uint16_t c = layer == 0 ? rgb(120, 120, 160) : lerpColor(rgb(150, 170, 255), C_TEXT, 0.5f + 0.5f * sinf(now * 0.004f + i));
        if (layer == 0) gfx->drawPixel((int)x, y, c);
        else gfx->fillRect((int)x, y, 2, 2, c);
      }
    }
  }
}
static void drawGround() {
  if (theme == 0) {
    gfx->fillRect(0, GROUND, W, H - GROUND, rgb(224, 202, 132));
    gfx->fillRect(0, GROUND, W, 7, rgb(96, 190, 60));
    gfx->fillRect(0, GROUND, W, 2, rgb(160, 235, 100));
    int off = (int)wrapf(scroll, 24.0f);
    for (int x = -off; x < W; x += 24) gfx->fillTriangle(x, GROUND + 7, x + 12, GROUND + 7, x + 4, GROUND + 13, rgb(200, 176, 108));
  } else {
    gfx->fillRect(0, GROUND, W, H - GROUND, C_BG);
    for (int k = 3; k >= 0; k--) gfx->drawFastHLine(0, GROUND + k, W, lerpColor(rgb(255, 40, 200), C_BG, k * 0.28f));
    int off = (int)wrapf(scroll, 32.0f);
    for (int x = -off; x < W + 32; x += 32) gfx->drawLine(x, GROUND + 3, x - 28, H - 1, rgb(80, 0, 90));
    gfx->drawFastHLine(0, GROUND + 14, W, rgb(50, 0, 60));
  }
}

// ---------------- pipes and bird ----------------
static void pipeSeg(int x, int y0, int y1, bool capAtBottom, int idx) {
  if (y1 <= y0) return;
  int h = y1 - y0;
  int capH = 16, capY = capAtBottom ? y1 - capH : y0;
  if (theme == 0) {
    gfx->fillRect(x, y0, PW, h, rgb(40, 110, 30));
    gfx->fillRect(x + 3, y0, PW - 10, h, rgb(96, 190, 60));
    gfx->fillRect(x + 8, y0, 9, h, rgb(170, 240, 110));
    gfx->fillRect(x - 4, capY, PW + 8, capH, rgb(40, 110, 30));
    gfx->fillRect(x - 1, capY + 2, PW + 2, capH - 4, rgb(96, 190, 60));
    gfx->fillRect(x + 5, capY + 2, 9, capH - 4, rgb(170, 240, 110));
  } else {
    uint16_t core = idx & 1 ? rgb(255, 50, 210) : rgb(40, 240, 255);
    gfx->fillRect(x, y0, PW, h, rgb(8, 0, 16));
    gfx->fillRect(x - 4, capY, PW + 8, capH, rgb(12, 0, 22));
    for (int k = 3; k >= 1; k--) {
      uint16_t g = lerpColor(core, C_BG, 0.55f + 0.15f * k);
      gfx->drawRect(x - k, y0 - (capAtBottom ? 0 : k), PW + 2 * k, h + k, g);
      gfx->drawRect(x - 4 - k, capY - k, PW + 8 + 2 * k, capH + 2 * k, g);
    }
    gfx->drawRect(x, y0, PW, h, core);
    gfx->drawRect(x - 4, capY, PW + 8, capH, core);
    gfx->fillRect(x + 6, y0 + (capAtBottom ? 0 : capH), 2, h - capH, lerpColor(core, C_BG, 0.6f));
  }
}
static void drawPipes() {
  for (int i = 0; i < NPIPE; i++) {
    int x = (int)pipes[i].x;
    if (x > W + 8 || x < -PW - 8) continue;
    int top = (int)(pipes[i].gy - pipes[i].gh / 2), bot = (int)(pipes[i].gy + pipes[i].gh / 2);
    pipeSeg(x, TOP, top, true, i);
    pipeSeg(x, bot, GROUND, false, i);
  }
}
static void drawBird(uint32_t now) {
  int x = BX, y = (int)by;
  bool up = (now - lastFlap) < 150;
  if (theme == 0) {
    gfx->fillEllipse(x + 1, y + 2, BIRD_R + 1, BIRD_R, rgb(200, 130, 0));
    gfx->fillCircle(x, y, BIRD_R, rgb(255, 214, 50));
    gfx->fillEllipse(x + 1, y + 5, 8, 5, rgb(255, 240, 170));
    gfx->fillEllipse(x - 5, y + (up ? -1 : 4), 6, up ? 3 : 4, rgb(255, 160, 30));
    gfx->fillCircle(x + 5, y - 4, 4, C_TEXT);
    gfx->fillCircle(x + 6, y - 4, 2, C_BG);
    gfx->fillTriangle(x + 9, y - 1, x + 18, y + 2, x + 9, y + 6, rgb(255, 90, 30));
  } else {
    gfx->fillCircle(x, y, BIRD_R + 10, rgb(0, 20, 30));
    gfx->fillCircle(x, y, BIRD_R + 6, rgb(0, 54, 70));
    gfx->fillCircle(x, y, BIRD_R + 3, rgb(0, 110, 140));
    gfx->fillCircle(x, y, BIRD_R, rgb(60, 240, 255));
    gfx->fillCircle(x - 1, y - 1, BIRD_R - 5, rgb(200, 255, 255));
    gfx->fillTriangle(x - 4, y + (up ? -2 : 3), x - 14, y + (up ? -9 : 7), x - 2, y + (up ? 3 : 8), rgb(255, 50, 210));
    gfx->fillCircle(x + 5, y - 3, 3, C_BG);
    gfx->fillTriangle(x + 10, y - 1, x + 16, y + 2, x + 10, y + 5, rgb(255, 220, 60));
  }
}

// ---------------- frame ----------------
static void render(uint32_t now) {
  memcpy(frameBuffer(), bg, FB_PIXELS * 2);
  uint16_t acc = theme == 0 ? rgb(255, 214, 50) : rgb(255, 60, 200);
  drawSky(now);
  drawHills(scroll);
  drawPipes();
  drawGround();
  drawBird(now);
  uint16_t fg = theme == 0 ? C_TEXT : rgb(80, 240, 255);
  hudBar(score, best, theme == 0 ? rgb(24, 70, 130) : C_BG, fg);
  if (theme == 1) gfx->drawFastHLine(0, 31, W, rgb(255, 40, 200));
  if (st == ST_PLAY) {
    char b[12];
    snprintf(b, sizeof b, "%d", score);
    centerText(b, 47, 5, C_BG);
    centerText(b, 44, 5, C_TEXT);
  }
  if (st == ST_COUNT) {
    int n = 3 - (int)((now - t0) / 700);
    char c[2] = {(char)('0' + constrain(n, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, 70, 88, 88, 18, C_PANEL);
    centerText(c, 90, 6, acc);
    centerText("LEFT / RIGHT flap", H / 2 + 56, 2, theme == 0 ? C_BG : C_DIM);
    centerText("LEFT / RIGHT flap", H / 2 + 55, 2, C_TEXT);
  } else if (st == ST_OVER) {
    char b[32];
    snprintf(b, sizeof b, newBest ? "NEW BEST %d" : "SCORE %d", score);
    panelBox("GAME OVER", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void flapStart() {
  theme = flOpts[0].value;
  buildBackground();
  score = 0;
  by = (TOP + GROUND) / 2.0f;
  bvy = 0;
  scroll = 0;
  newBest = false;
  lastFlap = 0;
  best = loadBest("flBest");
  resetPipes();
  st = ST_COUNT;
  t0 = lastT = millis();
  lastRender = 0;
  play(660, 90);
  render(t0);
}

static void die(uint32_t now) {
  st = ST_OVER;
  overAt = now;
  newBest = saveBest("flBest", score);
  if (newBest) best = score;
  newBest ? sfxBest() : sfxBad();
}

static bool circleHitsRect(float cx, float cy, float r, float rx, float ry, float rw, float rh) {
  float nx = constrain(cx, rx, rx + rw), ny = constrain(cy, ry, ry + rh);
  float dx = cx - nx, dy = cy - ny;
  return dx * dx + dy * dy < r * r;
}

static bool flapFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = constrain((now - lastT) / 1000.0f, 0.0f, 0.05f);
  lastT = now;
  switch (st) {
    case ST_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - t0) / 700);
      if (n != lastN && n >= 1) play(660, 90);
      lastN = n;
      by = (TOP + GROUND) / 2.0f + sinf(now * 0.006f) * 8.0f;
      scroll += 50.0f * dt;
      if (now - t0 >= 2100) {
        st = ST_PLAY; bvy = FLAP_V; lastFlap = now; lastN = 3; sfxStart();
      }
      break;
    }
    case ST_PLAY: {
      if (in.left || in.right) { bvy = FLAP_V; lastFlap = now; play(900, 25); }
      bvy = fminf(bvy + GRAVITY * dt, MAX_FALL);
      by += bvy * dt;
      float sp = curSpeed();
      scroll += sp * dt;
      for (int i = 0; i < NPIPE; i++) pipes[i].x -= sp * dt;
      recyclePipes();
      if (by - BIRD_R < TOP) { by = TOP + BIRD_R; if (bvy < 0) bvy = 0; }
      for (int i = 0; i < NPIPE; i++) {
        Pipe &p = pipes[i];
        if (!p.scored && p.x + PW < BX - BIRD_R) { p.scored = true; score++; play(1200, 40); }
        if (BX + BIRD_R > p.x - 4 && BX - BIRD_R < p.x + PW + 4) {
          float top = p.gy - p.gh / 2, bot = p.gy + p.gh / 2;
          if (circleHitsRect(BX, by, BIRD_R - 1, p.x - 4, TOP, PW + 8, top - TOP) ||
              circleHitsRect(BX, by, BIRD_R - 1, p.x - 4, bot, PW + 8, GROUND - bot)) { die(now); break; }
        }
      }
      if (st == ST_PLAY && by + BIRD_R >= GROUND) { by = GROUND - BIRD_R; die(now); }
      break;
    }
    case ST_OVER:
      if (by + BIRD_R < GROUND) {                              // bird tumbles to the ground
        bvy = fminf(bvy + GRAVITY * dt, MAX_FALL);
        by = fminf(by + bvy * dt, (float)(GROUND - BIRD_R));
      }
      if (now - overAt > 600) {
        if (in.right) { flapStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - lastRender >= 16) { lastRender = now; render(now); }
  return true;
}

Game GAME_FLAP = {"FLAP", "tap through the pipe gaps", 0xFE60, flOpts, 1, flapStart, flapFrame};
