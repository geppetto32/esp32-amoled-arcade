// breakout_game.cpp - Breakout with glowing gradient bricks, multi-hit bricks, levels, lives and particle bursts.
// CONTROL: BUTTONS (hold LEFT/RIGHT, a press also nudges) or TILT (tiltX moves the paddle; LEFT = recentre, RIGHT = launch / pause).
#include "game.h"

static const char *const CONTROLS[] = {"BUTTONS", "TILT"};
static Option boOpts[] = {{"CONTROL", CONTROLS, 2, "boCtrl", 0}};

enum St : uint8_t { ST_COUNT, ST_PLAY, ST_PAUSE, ST_OVER };
static constexpr int COLS = 10, ROWS = 8, BRW = 42, BRH = 16, PITCH_X = 44, PITCH_Y = 18, BX0 = 4, BY0 = 48;
static constexpr int PADDLE_Y = H - 30, PADDLE_H = 10, BALL_R = 5;
static constexpr float PADDLE_W = 72.0f;

static St st;
static int ctrl, lives, level, bricksLeft;
static uint32_t score, best, t0, lastT, lastRender, overAt, bannerUntil;
static bool newBest, stuck;
static float padX, padV, bx, by, ux, uy, spd;
static uint8_t hp[ROWS][COLS], hpMax[ROWS][COLS];
static uint16_t *bg = nullptr;

struct Particle { float x, y, vx, vy; uint32_t born; uint16_t c; };
static Particle parts[56];
static int partIdx = 0;

static const uint16_t ROWCOL[ROWS] = {rgb(255, 60, 80), rgb(255, 140, 40), rgb(255, 220, 50), rgb(120, 255, 70),
                                      rgb(40, 230, 255), rgb(70, 120, 255), rgb(190, 80, 255), rgb(255, 80, 190)};

static void buildBackground() {
  if (!bg) bg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  gfx->fillScreen(C_BG);
  for (int y = 32; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(C_BG, rgb(10, 6, 34), (float)(y - 32) / (H - 32)));
  for (int x = 0; x < W; x += 28) gfx->drawFastVLine(x, 32, H - 32, rgb(10, 10, 26));
  for (int y = 32; y < H; y += 28) gfx->drawFastHLine(0, y, W, rgb(10, 10, 26));
  memcpy(bg, frameBuffer(), FB_PIXELS * 2);
}

static void buildLevel(int lvl) {
  int rows = 4 + lvl;
  if (rows > ROWS) rows = ROWS;
  int tough = lvl - 1;
  if (tough > 3) tough = 3;
  bricksLeft = 0;
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      uint8_t h = 0;
      if (r < rows && !(lvl % 3 == 0 && (c + r) % 5 == 0) && !(lvl % 4 == 0 && (c == 4 || c == 5) && r > 1)) {
        h = r < tough ? 2 : 1;
        if (lvl >= 5 && r == 0) h = 3;
      }
      hp[r][c] = hpMax[r][c] = h;
      if (h) bricksLeft++;
    }
}

static void burst(float x, float y, uint16_t col, uint32_t now, int n) {
  for (int i = 0; i < n; i++) {
    float a = (float)(esp_random() % 628) * 0.01f, s = 40.0f + (float)(esp_random() % 120);
    parts[partIdx] = {x, y, cosf(a) * s, sinf(a) * s - 30.0f, now, i & 1 ? col : lerpColor(col, C_TEXT, 0.6f)};
    partIdx = (partIdx + 1) % 56;
  }
}
static void drawParticles(uint32_t now) {
  for (int i = 0; i < 56; i++) {
    uint32_t age = now - parts[i].born;
    if (age > 520) continue;
    float t = age / 1000.0f;
    int x = (int)(parts[i].x + parts[i].vx * t), y = (int)(parts[i].y + parts[i].vy * t + 260.0f * t * t);
    gfx->fillRect(x - 1, y - 1, 3, 3, lerpColor(parts[i].c, C_BG, age / 520.0f));
  }
}

static void drawBrick(int r, int c) {
  uint8_t h = hp[r][c];
  if (!h) return;
  int x = BX0 + c * PITCH_X, y = BY0 + r * PITCH_Y;
  uint16_t col = ROWCOL[r];
  gfx->fillRect(x - 1, y - 1, BRW + 2, BRH + 2, lerpColor(col, C_BG, 0.82f));
  gfx->fillRect(x, y, BRW, BRH / 2, lerpColor(col, C_TEXT, 0.3f));
  gfx->fillRect(x, y + BRH / 2, BRW, BRH / 2, lerpColor(col, C_BG, 0.4f));
  gfx->drawFastHLine(x, y, BRW, lerpColor(col, C_TEXT, 0.75f));
  if (h >= 2) gfx->drawRect(x + 2, y + 2, BRW - 4, BRH - 4, lerpColor(C_TEXT, col, 0.25f));
  if (h >= 3) gfx->drawRect(x + 4, y + 4, BRW - 8, BRH - 8, C_TEXT);
}

static void resetBall() {
  stuck = true;
  bx = padX;
  by = PADDLE_Y - BALL_R - 1;
  ux = 0; uy = -1;
}

static void render(uint32_t now) {
  memcpy(frameBuffer(), bg, FB_PIXELS * 2);
  for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) drawBrick(r, c);
  drawParticles(now);
  // paddle with glow
  int px = (int)(padX - PADDLE_W / 2);
  gfx->fillRoundRect(px - 4, PADDLE_Y - 4, (int)PADDLE_W + 8, PADDLE_H + 8, 8, rgb(0, 24, 40));
  gfx->fillRoundRect(px - 2, PADDLE_Y - 2, (int)PADDLE_W + 4, PADDLE_H + 4, 7, rgb(0, 60, 90));
  gfx->fillRoundRect(px, PADDLE_Y, (int)PADDLE_W, PADDLE_H, 5, rgb(40, 200, 255));
  gfx->fillRoundRect(px + 4, PADDLE_Y + 1, (int)PADDLE_W - 8, 3, 1, rgb(210, 250, 255));
  // ball with glow
  int x = (int)bx, y = (int)by;
  gfx->fillCircle(x, y, BALL_R + 6, rgb(18, 18, 36));
  gfx->fillCircle(x, y, BALL_R + 3, rgb(60, 60, 110));
  gfx->fillCircle(x, y, BALL_R, rgb(235, 240, 255));
  gfx->fillCircle(x - 1, y - 1, 2, C_TEXT);
  hudBar(score, best, C_BG, rgb(80, 240, 255));
  gfx->drawFastHLine(0, 31, W, rgb(60, 80, 160));
  char b[16];
  snprintf(b, sizeof b, "LV %d", level);
  text(b, 240, 9, 2, C_ACC);
  for (int i = 0; i < lives; i++) gfx->fillCircle(330 + i * 16, 16, 5, C_BAD);
  uint16_t acc = rgb(80, 240, 255);
  if (st == ST_COUNT) {
    int n = 3 - (int)((now - t0) / 700);
    char c[2] = {(char)('0' + constrain(n, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, H / 2 + 20, 88, 88, 18, C_PANEL);
    centerText(c, H / 2 + 40, 6, acc);
    centerText(ctrl ? "tilt to move   RIGHT launch" : "LEFT / RIGHT move   RIGHT launch", H / 2 + 118, 2, C_DIM);
  } else if (st == ST_PLAY) {
    if (stuck) centerText(ctrl ? "RIGHT launch" : "RIGHT launches the ball", H - 66, 2, C_ACC);
    if (now < bannerUntil) {
      snprintf(b, sizeof b, "LEVEL %d", level);
      centerText(b, H / 2 + 40, 4, C_ACC);
    }
  } else if (st == ST_PAUSE) {
    panelBox("PAUSED", nullptr, "RIGHT resume   LEFT menu", acc);
  } else {
    char s[32];
    snprintf(s, sizeof s, newBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)score);
    panelBox("GAME OVER", s, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void breakoutStart() {
  ctrl = boOpts[0].value;
  if (ctrl == 1 && !imuOk) ctrl = 0;
  if (!bg) buildBackground();
  score = 0; lives = 3; level = 1; padX = W / 2.0f; padV = 0; spd = 230.0f;
  newBest = false; bannerUntil = 0;
  buildLevel(level);
  resetBall();
  best = loadBest(ctrl ? "boBestT" : "boBestB");
  st = ST_COUNT;
  t0 = lastT = millis();
  lastRender = 0;
  for (int i = 0; i < 56; i++) parts[i].born = t0 - 10000;   // all particles expired
  play(660, 90);
  render(t0);
}

static void gameOver(uint32_t now) {
  st = ST_OVER;
  overAt = now;
  newBest = saveBest(ctrl ? "boBestT" : "boBestB", score);
  if (newBest) best = score;
  newBest ? sfxBest() : sfxBad();
}

// brick under a point? returns true and cell if a live brick is there
static bool brickAt(float x, float y, int &r, int &c) {
  float fx = x - BX0, fy = y - BY0;
  if (fx < 0 || fy < 0) return false;
  c = (int)(fx / PITCH_X);
  r = (int)(fy / PITCH_Y);
  if (c >= COLS || r >= ROWS) return false;
  if (fx - c * PITCH_X >= BRW || fy - r * PITCH_Y >= BRH) return false;
  return hp[r][c] > 0;
}

static void hitBrick(int r, int c, uint32_t now) {
  hp[r][c]--;
  float cx = BX0 + c * PITCH_X + BRW / 2.0f, cy = BY0 + r * PITCH_Y + BRH / 2.0f;
  if (hp[r][c] == 0) {
    bricksLeft--;
    score += 10 + (ROWS - r) * 2;
    burst(cx, cy, ROWCOL[r], now, 9);
    play(440 + r * 70, 35);
    if (spd < 420.0f) spd += 1.5f;
  } else {
    burst(cx, cy, C_TEXT, now, 3);
    play(260, 30);
  }
}

static void stepBall(float ds, uint32_t now) {
  int r, c;
  float nx = bx + ux * ds;
  if (nx < BALL_R) { nx = BALL_R; ux = -ux; play(200, 15); }
  else if (nx > W - BALL_R) { nx = W - BALL_R; ux = -ux; play(200, 15); }
  if (brickAt(nx + (ux > 0 ? BALL_R : -BALL_R), by, r, c)) { hitBrick(r, c, now); ux = -ux; }
  else bx = nx;
  float ny = by + uy * ds;
  if (ny < 32 + BALL_R) { ny = 32 + BALL_R; uy = -uy; play(200, 15); }
  if (uy > 0 && ny + BALL_R >= PADDLE_Y && by + BALL_R <= PADDLE_Y + 4 && fabsf(bx - padX) <= PADDLE_W / 2 + BALL_R) {
    ny = PADDLE_Y - BALL_R;
    float a = constrain((bx - padX) / (PADDLE_W / 2 + BALL_R), -1.0f, 1.0f) * 1.05f;
    ux = sinf(a); uy = -cosf(a);
    play(330, 20);
  } else if (brickAt(bx, ny + (uy > 0 ? BALL_R : -BALL_R), r, c)) { hitBrick(r, c, now); uy = -uy; }
  else by = ny;
}

static uint32_t boPauseAt;                         // a stray LEFT right after pausing must not exit
static bool breakoutFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = constrain((now - lastT) / 1000.0f, 0.0f, 0.05f);
  lastT = now;
  switch (st) {
    case ST_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - t0) / 700);
      if (n != lastN && n >= 1) play(660, 90);
      lastN = n;
      if (now - t0 >= 2100) { st = ST_PLAY; imuZero(); lastN = 3; sfxStart(); }
      break;
    }
    case ST_PLAY: {
      // paddle
      if (ctrl == 0) {
        int dir = (in.leftDown ? -1 : 0) + (in.rightDown ? 1 : 0);
        if (in.left) padV = -380.0f;
        if (in.right) padV = 380.0f;
        if (dir) padV += (dir * 360.0f - padV) * fminf(1.0f, dt * 14.0f);
        else padV *= expf(-dt * 6.0f);
        padX += padV * dt;
      } else {
        float target = W / 2.0f + axisX() * 560.0f;
        padX += (target - padX) * fminf(1.0f, dt * 16.0f);
        if (in.left && !stuck) { imuZero(); sfxClick(); }
      }
      float lim = PADDLE_W / 2;
      if (padX < lim) { padX = lim; padV = 0; }
      if (padX > W - lim) { padX = W - lim; padV = 0; }
      if (stuck) {
        bx = padX;
        by = PADDLE_Y - BALL_R - 1;
        if (in.right || (ctrl && in.left)) {
          stuck = false;
          float s = (esp_random() & 1) ? 0.35f : -0.35f;
          ux = s; uy = -sqrtf(1.0f - s * s);
          play(520, 40);
        }
      } else {
        if (ctrl && in.right) { st = ST_PAUSE; boPauseAt = now; sfxClick(); break; }
        float move = spd * dt;
        int n = (int)(move / 3.0f) + 1;
        float ds = move / n;
        for (int i = 0; i < n && !stuck; i++) {
          stepBall(ds, now);
          if (by > H + BALL_R) {
            lives--;
            sfxBad();
            if (lives <= 0) { gameOver(now); break; }
            resetBall();
          }
          if (bricksLeft == 0) {
            level++;
            if (spd < 420.0f) spd = fminf(spd + 20.0f, 420.0f);
            score += 50;
            buildLevel(level);
            resetBall();
            bannerUntil = now + 1500;
            sfxGood();
          }
        }
      }
      break;
    }
    case ST_PAUSE:
      if (in.right) { st = ST_PLAY; imuZero(); sfxClick(); lastT = now; }
      if (in.left && now - boPauseAt > 500) return false;
      break;
    case ST_OVER:
      if (now - overAt > 500) {
        if (in.right) { breakoutStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - lastRender >= 16) { lastRender = now; render(now); }
  return true;
}

Game GAME_BREAKOUT = {"BREAKOUT", "bounce the ball, clear the bricks", 0xFB40, boOpts, 1, breakoutStart, breakoutFrame};
