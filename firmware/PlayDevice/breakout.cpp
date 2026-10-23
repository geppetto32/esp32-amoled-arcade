// Breakout - paddle LEFT/RIGHT, A launches, ball angle depends on where it hits the paddle.
#include <math.h>
#include "game.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"

namespace {
const int COLS = 16, ROWS = 6;
const int BW = 20, BH = 8, BPITCH = 9, BTOP = 20;   // brick width/height/row pitch/top
const int PW = 40, PH = 4, PY = SCREEN_H - 10;      // paddle
const int BALL = 4;
const float PADDLE_SPEED = 4.0f;

bool bricks[ROWS][COLS];
int bricksLeft;
float px;                       // paddle left edge
float bx, by, vx, vy;
bool stuck;                     // ball resting on paddle
int lives, level;
uint32_t score, best;
int brokenThisLevel;
bool over, newRecord;

const uint16_t kRowColors[ROWS] = {C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_CYAN, C_PURPLE};

float ballSpeed() {
  float s = 2.4f + 0.25f * level + 0.015f * brokenThisLevel;
  return s > 4.2f ? 4.2f : s;
}

void setVelocity(float angleRad) {   // angle from vertical, negative = left, always upward
  float s = ballSpeed();
  vx = s * sinf(angleRad);
  vy = -s * cosf(angleRad);
}

void rebuildBricks() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) bricks[r][c] = true;
  bricksLeft = ROWS * COLS;
  brokenThisLevel = 0;
}

void resetBall() {
  stuck = true;
  vx = vy = 0;
  bx = px + PW / 2 - BALL / 2;
  by = PY - BALL;
}

void gInit() {
  px = (SCREEN_W - PW) / 2;
  lives = 3;
  level = 0;
  score = 0;
  over = false;
  newRecord = false;
  best = hiscoreGet("breakout");
  rebuildBricks();
  resetBall();
}

void endGame() {
  over = true;
  newRecord = hiscoreSubmit("breakout", score);
  if (newRecord) best = score;
  soundPlay(SFX_GAMEOVER);
}

// Returns true (and removes the brick) if the ball rect at (x,y) overlaps a brick.
bool hitBrick(float x, float y) {
  const float cx[2] = {x, x + BALL - 0.01f};
  const float cy[2] = {y, y + BALL - 0.01f};
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      int c = (int)floorf(cx[i] / BW);
      int r = (int)floorf((cy[j] - BTOP) / BPITCH);
      if (r < 0 || r >= ROWS || c < 0 || c >= COLS || !bricks[r][c]) continue;
      if (cy[j] - BTOP - r * BPITCH >= BH) continue;   // in the gap below the brick
      bricks[r][c] = false;
      bricksLeft--;
      brokenThisLevel++;
      score += 10 * (ROWS - r);
      soundPlay(SFX_HIT);
      return true;
    }
  return false;
}

void gUpdate() {
  if (over) {
    if (btnPressed(BTN_A)) gInit();
    return;
  }
  if (btnHeld(BTN_LEFT))  px -= PADDLE_SPEED;
  if (btnHeld(BTN_RIGHT)) px += PADDLE_SPEED;
  if (px < 0) px = 0;
  if (px > SCREEN_W - PW) px = SCREEN_W - PW;

  if (stuck) {
    bx = px + PW / 2 - BALL / 2;
    by = PY - BALL;
    if (btnPressed(BTN_A)) {
      stuck = false;
      setVelocity((random(2) ? 1 : -1) * 0.35f);
      soundPlay(SFX_SHOOT);
    }
    return;
  }

  // X move
  float nx = bx + vx;
  if (nx < 0) { nx = 0; vx = -vx; }
  if (nx > SCREEN_W - BALL) { nx = SCREEN_W - BALL; vx = -vx; }
  if (hitBrick(nx, by)) vx = -vx; else bx = nx;
  // Y move
  float ny = by + vy;
  if (ny < 12) { ny = 12; vy = -vy; }
  if (hitBrick(bx, ny)) vy = -vy; else by = ny;

  // Paddle
  if (vy > 0 && by + BALL >= PY && by < PY + PH && bx + BALL > px && bx < px + PW) {
    float off = ((bx + BALL / 2) - (px + PW / 2)) / (PW / 2.0f);
    if (off > 1) off = 1;
    if (off < -1) off = -1;
    setVelocity(off * 1.05f);   // up to ~60 degrees
    by = PY - BALL;
    soundPlay(SFX_CLICK);
  }

  // Keep speed in step with progress (direction preserved).
  float cur = sqrtf(vx * vx + vy * vy), want = ballSpeed();
  if (cur > 0.01f) { vx *= want / cur; vy *= want / cur; }

  if (bricksLeft == 0) {
    level++;
    score += 100;
    rebuildBricks();
    resetBall();
    soundPlay(SFX_LINE);
    return;
  }
  if (by > SCREEN_H) {
    if (--lives <= 0) endGame();
    else { resetBall(); soundPlay(SFX_GAMEOVER); }
  }
}

void gDraw() {
  canvas->fillScreen(C_BLACK);
  char buf[24];
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  uiText(2, 2, buf, C_WHITE);
  snprintf(buf, sizeof buf, "Lv %d", level + 1);
  uiTextC(2, buf, C_DIM);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  uiTextR(SCREEN_W - 40, 2, buf, C_YELLOW);
  for (int i = 0; i < lives; i++) canvas->fillRect(SCREEN_W - 34 + i * 11, 2, 8, 8, C_RED);
  canvas->drawFastHLine(0, 11, SCREEN_W, C_GRID);

  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      if (bricks[r][c]) canvas->fillRect(c * BW, BTOP + r * BPITCH, BW - 1, BH, kRowColors[r]);

  canvas->fillRect((int)px, PY, PW, PH, C_WHITE);
  canvas->fillRect((int)bx, (int)by, BALL, BALL, C_YELLOW);
  if (stuck && !over) uiTextC(120, "A = launch", C_DIM);
  if (over) uiGameOver(score, best, newRecord);
}
}  // namespace

const Game gameBreakout = {"Breakout", "brk", gInit, gUpdate, gDraw};
