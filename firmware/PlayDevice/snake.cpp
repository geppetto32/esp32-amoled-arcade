// Snake - port of games/html_prototypes/snake.html onto a 32x16 grid of 10 px cells.
#include "game.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"
#include "snake_logic.h"

namespace {
const int CELL = 10;
const int TOP = SCREEN_H - SN_ROWS * CELL;   // 10 px HUD strip

SnakeState ss;
int acc;
uint32_t best;
bool newRecord;
bool dead;

void gInit() {
  snakeReset(ss, esp_random());
  acc = 0;
  dead = false;
  newRecord = false;
  best = hiscoreGet("snake");
}

void die() {
  dead = true;
  newRecord = hiscoreSubmit("snake", ss.score);
  if (newRecord) best = ss.score;
  soundPlay(SFX_GAMEOVER);
}

void gUpdate() {
  if (dead) {
    if (btnPressed(BTN_A)) gInit();
    return;
  }
  if (btnPressed(BTN_UP))    snakeSetDir(ss, 0, -1);
  if (btnPressed(BTN_DOWN))  snakeSetDir(ss, 0, 1);
  if (btnPressed(BTN_LEFT))  snakeSetDir(ss, -1, 0);
  if (btnPressed(BTN_RIGHT)) snakeSetDir(ss, 1, 0);

  acc += TICK_MS;
  while (acc >= ss.stepMs && !dead) {
    acc -= ss.stepMs;
    SnakeResult r = snakeStep(ss);
    if (r == SN_ATE) soundPlay(SFX_EAT);
    else if (r == SN_DEAD || r == SN_WON) die();
  }
}

void gDraw() {
  canvas->fillScreen(C_BG);
  canvas->fillRect(0, TOP, SCREEN_W, SN_ROWS * CELL, C_BLACK);
  canvas->drawRect(0, TOP, SCREEN_W, SN_ROWS * CELL, C_GRID);

  char buf[24];
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)ss.score);
  uiText(2, 1, buf, C_WHITE);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  uiTextR(SCREEN_W - 2, 1, buf, C_YELLOW);

  canvas->fillCircle(ss.fx * CELL + CELL / 2, TOP + ss.fy * CELL + CELL / 2, CELL / 2 - 1, C_RED);
  for (int i = ss.len - 1; i >= 0; i--)
    canvas->fillRect(ss.sx[i] * CELL + 1, TOP + ss.sy[i] * CELL + 1, CELL - 2, CELL - 2,
                     i == 0 ? C_CYAN : C_GREEN);
  if (dead) uiGameOver(ss.score, best, newRecord);
}
}  // namespace

const Game gameSnake = {"Snake", "snake", gInit, gUpdate, gDraw};
