// Tetris - 10x20 playfield, 8 px cells. Logic lives in tetris_logic.h.
#include "game.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"
#include "tetris_logic.h"

namespace {
const int CELL = 8;
const int BX = 120, BY = 5;   // playfield top-left on screen

TetState ts;
int fall;
uint32_t best;
bool newRecord;

const uint16_t kColors[TET_TYPES] = {C_CYAN, C_YELLOW, C_PURPLE, C_GREEN, C_RED, C_BLUE, C_ORANGE};

void gInit() {
  tetReset(ts, esp_random());
  fall = 0;
  newRecord = false;
  best = hiscoreGet("tetris");
}

void endGame() {
  newRecord = hiscoreSubmit("tetris", ts.score);
  if (newRecord) best = ts.score;
  soundPlay(SFX_GAMEOVER);
}

void afterLock(int cleared) {
  fall = 0;
  if (ts.over) { endGame(); return; }
  soundPlay(cleared ? SFX_LINE : SFX_HIT);
}

void gUpdate() {
  if (ts.over) {
    if (btnPressed(BTN_A)) gInit();
    return;
  }
  if (btnRepeat(BTN_LEFT))  tetTryMove(ts, -1, 0);
  if (btnRepeat(BTN_RIGHT)) tetTryMove(ts, 1, 0);
  if (btnPressed(BTN_A)) { if (tetTryRotate(ts, 1))  soundPlay(SFX_CLICK); }
  if (btnPressed(BTN_B)) { if (tetTryRotate(ts, -1)) soundPlay(SFX_CLICK); }

  if (btnPressed(BTN_UP)) {   // hard drop
    int d = tetDropDistance(ts);
    ts.py = (int8_t)(ts.py + d);
    ts.score += 2 * d;
    afterLock(tetLock(ts));
    return;
  }

  int g = tetGravityFrames(ts.level);
  if (btnHeld(BTN_DOWN) && g > 2) g = 2;
  if (++fall >= g) {
    fall = 0;
    if (tetTryMove(ts, 0, 1)) {
      if (btnHeld(BTN_DOWN)) ts.score += 1;
    } else {
      afterLock(tetLock(ts));
    }
  }
}

void drawCell(int x, int y, uint16_t col) {
  canvas->fillRect(x, y, CELL - 1, CELL - 1, col);
}

void stat(int y, const char* label, uint32_t v, uint16_t col) {
  uiText(6, y, label, C_DIM);
  uiNum(6, y + 10, v, col, 2);
}

void gDraw() {
  canvas->fillScreen(C_BG);
  canvas->fillRect(BX - 1, BY - 1, TET_W * CELL + 2, TET_H * CELL + 2, C_GRID);
  canvas->fillRect(BX, BY, TET_W * CELL, TET_H * CELL, C_BLACK);

  for (int y = 0; y < TET_H; y++)
    for (int x = 0; x < TET_W; x++)
      if (ts.board[y][x]) drawCell(BX + x * CELL, BY + y * CELL, kColors[ts.board[y][x] - 1]);

  if (!ts.over) {
    int n = kTetShapes[(int)ts.type].n;
    int gy = tetDropDistance(ts);
    for (int r = 0; r < n; r++)
      for (int c = 0; c < n; c++) {
        if (!tetCell(ts.type, ts.rot, r, c)) continue;
        int x = ts.px + c, y = ts.py + r;
        if (y + gy >= 0)   // ghost outline
          canvas->drawRect(BX + x * CELL, BY + (y + gy) * CELL, CELL - 1, CELL - 1, C_DIM);
        if (y >= 0) drawCell(BX + x * CELL, BY + y * CELL, kColors[(int)ts.type]);
      }
  }

  stat(6, "SCORE", ts.score, C_WHITE);
  stat(40, "LINES", ts.lines, C_WHITE);
  stat(74, "LEVEL", ts.level + 1, C_WHITE);
  stat(108, "BEST", best, C_YELLOW);

  uiText(228, 6, "NEXT", C_DIM);
  int n = kTetShapes[(int)ts.next].n;
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++)
      if (tetCell(ts.next, 0, r, c))
        canvas->fillRect(228 + c * 10, 24 + r * 10, 9, 9, kColors[(int)ts.next]);

  if (ts.over) uiGameOver(ts.score, best, newRecord);
}
}  // namespace

const Game gameTetris = {"Tetris", "tetris", gInit, gUpdate, gDraw};
