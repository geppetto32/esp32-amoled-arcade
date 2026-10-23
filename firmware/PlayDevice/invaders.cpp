// Invaders - marching grid, LEFT/RIGHT to move, A to shoot, shields, waves.
#include "game.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"

namespace {
const int ICOLS = 8, IROWS_MAX = 5;
const int PITCH_X = 22, PITCH_Y = 14;
const int SPR_W = 11, SPR_H = 8;
const int SHIP_W = 13, SHIP_H = 7, SHIP_Y = SCREEN_H - 12;
const int NSHIELD = 4, SH_BX = 5, SH_BY = 3, SH_CELL = 4, SH_Y = SHIP_Y - 28;
const int MAX_EBULLETS = 3;

// 11x8 sprites, 2 animation frames each. Bit 10 = leftmost pixel.
const uint16_t kSprites[3][2][8] = {
  {{0b00000100000, 0b00001110000, 0b00011111000, 0b00110101100, 0b00111111100, 0b00010101000, 0b00100000100, 0b00010001000},
   {0b00000100000, 0b00001110000, 0b00011111000, 0b00110101100, 0b00111111100, 0b00001010000, 0b00010101000, 0b00101010100}},
  {{0b00100000100, 0b00010001000, 0b00111111100, 0b01101110110, 0b11111111111, 0b10111111101, 0b10100000101, 0b00011011000},
   {0b00100000100, 0b10010001001, 0b10111111101, 0b11101110111, 0b11111111111, 0b01111111110, 0b00100000100, 0b01000000010}},
  {{0b00001111000, 0b01111111110, 0b11111111111, 0b11100110111, 0b11111111111, 0b00011011000, 0b00110110110, 0b11000000011},
   {0b00001111000, 0b01111111110, 0b11111111111, 0b11100110111, 0b11111111111, 0b00111001100, 0b01100110110, 0b00110000110}},
};
const uint16_t kShip[SHIP_H] = {0b0000001000000, 0b0000011100000, 0b0000011100000, 0b0111111111110,
                                0b1111111111111, 0b1111111111111, 0b1111111111111};
const uint16_t kRowColor[IROWS_MAX] = {C_PINK, C_CYAN, C_CYAN, C_GREEN, C_GREEN};
const uint8_t kRowType[IROWS_MAX] = {0, 1, 1, 2, 2};
const uint8_t kRowPoints[IROWS_MAX] = {30, 20, 20, 10, 10};

struct Bullet { float x, y; bool on; };

bool alive[IROWS_MAX][ICOLS];
int rows, aliveCount;
float fx, fy;
int dirX;                 // +1 / -1
int marchTimer, anim;
float shipX;
Bullet pb;                // player bullet
Bullet eb[MAX_EBULLETS];
int fireTimer;
bool shield[NSHIELD][SH_BY][SH_BX];
int lives, wave, invuln;
uint32_t score, best;
bool over, newRecord;

void drawSprite(int x, int y, const uint16_t* rowsBits, int w, int h, uint16_t col) {
  for (int r = 0; r < h; r++) {
    int c = 0;
    while (c < w) {
      if (!(rowsBits[r] & (1 << (w - 1 - c)))) { c++; continue; }
      int start = c;
      while (c < w && (rowsBits[r] & (1 << (w - 1 - c)))) c++;
      canvas->drawFastHLine(x + start, y + r, c - start, col);
    }
  }
}

void buildWave() {
  rows = 3 + (wave > 1 ? 2 : wave);   // 3,4,5,5,...
  if (rows > IROWS_MAX) rows = IROWS_MAX;
  for (int r = 0; r < IROWS_MAX; r++)
    for (int c = 0; c < ICOLS; c++) alive[r][c] = (r < rows);
  aliveCount = rows * ICOLS;
  fx = 24;
  fy = 18 + (wave > 4 ? 4 : wave) * 6;
  dirX = 1;
  marchTimer = 0;
  anim = 0;
  fireTimer = 60;
  pb.on = false;
  for (auto& b : eb) b.on = false;
  for (int s = 0; s < NSHIELD; s++)
    for (int r = 0; r < SH_BY; r++)
      for (int c = 0; c < SH_BX; c++) shield[s][r][c] = true;
}

int shieldX(int s) { return 30 + s * 72; }

void gInit() {
  wave = 0;
  lives = 3;
  score = 0;
  over = newRecord = false;
  invuln = 0;
  shipX = (SCREEN_W - SHIP_W) / 2;
  best = hiscoreGet("inv");
  buildWave();
}

void endGame() {
  over = true;
  newRecord = hiscoreSubmit("inv", score);
  if (newRecord) best = score;
  soundPlay(SFX_GAMEOVER);
}

// Kills the shield block under (x,y) if any. Returns true if one was hit.
bool hitShield(float x, float y) {
  for (int s = 0; s < NSHIELD; s++) {
    int c = ((int)x - shieldX(s)) / SH_CELL;
    int r = ((int)y - SH_Y) / SH_CELL;
    if (x < shieldX(s) || y < SH_Y || c >= SH_BX || r >= SH_BY) continue;
    if (shield[s][r][c]) { shield[s][r][c] = false; return true; }
  }
  return false;
}

void marchStep() {
  int minC = ICOLS, maxC = -1, maxR = -1;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < ICOLS; c++)
      if (alive[r][c]) {
        if (c < minC) minC = c;
        if (c > maxC) maxC = c;
        if (r > maxR) maxR = r;
      }
  if (maxC < 0) return;
  float left = fx + minC * PITCH_X + dirX * 4;
  float right = fx + maxC * PITCH_X + SPR_W + dirX * 4;
  if (left < 2 || right > SCREEN_W - 2) {
    dirX = -dirX;
    fy += 8;
  } else {
    fx += dirX * 4;
  }
  anim ^= 1;
  if (fy + maxR * PITCH_Y + SPR_H >= SHIP_Y - 2) endGame();
}

void fireEnemy() {
  int free = -1;
  for (int i = 0; i < MAX_EBULLETS; i++) if (!eb[i].on) { free = i; break; }
  if (free < 0) return;
  int col = (int)random(ICOLS);
  for (int k = 0; k < ICOLS; k++) {
    int c = (col + k) % ICOLS;
    for (int r = rows - 1; r >= 0; r--)
      if (alive[r][c]) {
        eb[free] = {fx + c * PITCH_X + SPR_W / 2.0f, fy + r * PITCH_Y + SPR_H, true};
        return;
      }
  }
}

void loseLife() {
  soundPlay(SFX_HIT);
  for (auto& b : eb) b.on = false;
  if (--lives <= 0) { endGame(); return; }
  invuln = 90;
}

void gUpdate() {
  if (over) {
    if (btnPressed(BTN_A)) gInit();
    return;
  }
  if (btnHeld(BTN_LEFT))  shipX -= 2.5f;
  if (btnHeld(BTN_RIGHT)) shipX += 2.5f;
  if (shipX < 2) shipX = 2;
  if (shipX > SCREEN_W - SHIP_W - 2) shipX = SCREEN_W - SHIP_W - 2;
  if (invuln > 0) invuln--;

  if (!pb.on && btnHeld(BTN_A)) {
    pb = {shipX + SHIP_W / 2.0f, (float)SHIP_Y - 4, true};
    soundPlay(SFX_SHOOT);
  }

  // March: faster with fewer invaders and later waves.
  int interval = (aliveCount * 2) / 3 + 2 - wave;
  if (interval < 2) interval = 2;
  if (++marchTimer >= interval) { marchTimer = 0; marchStep(); if (over) return; }

  // Player bullet
  if (pb.on) {
    pb.y -= 5;
    if (pb.y < 12) pb.on = false;
    else if (hitShield(pb.x, pb.y)) pb.on = false;
    else {
      for (int r = 0; r < rows && pb.on; r++)
        for (int c = 0; c < ICOLS && pb.on; c++) {
          if (!alive[r][c]) continue;
          float ix = fx + c * PITCH_X, iy = fy + r * PITCH_Y;
          if (pb.x >= ix && pb.x < ix + SPR_W && pb.y >= iy && pb.y < iy + SPR_H) {
            alive[r][c] = false;
            aliveCount--;
            pb.on = false;
            score += kRowPoints[r];
            soundPlay(SFX_EAT);
          }
        }
    }
  }

  // Enemy fire
  if (--fireTimer <= 0) {
    fireEnemy();
    int base = 45 - wave * 5;
    fireTimer = (base < 14 ? 14 : base) + (int)random(20);
  }
  float espeed = 2.0f + wave * 0.2f;
  if (espeed > 3.5f) espeed = 3.5f;
  for (auto& b : eb) {
    if (!b.on) continue;
    b.y += espeed;
    if (b.y > SCREEN_H) { b.on = false; continue; }
    if (hitShield(b.x, b.y + 4)) { b.on = false; continue; }
    if (invuln == 0 && b.y + 4 >= SHIP_Y && b.y <= SHIP_Y + SHIP_H && b.x >= shipX && b.x < shipX + SHIP_W) {
      b.on = false;
      loseLife();
      if (over) return;
    }
  }

  if (aliveCount == 0) {
    wave++;
    score += 100;
    buildWave();
    soundPlay(SFX_LINE);
  }
}

void gDraw() {
  canvas->fillScreen(C_BLACK);
  char buf[24];
  snprintf(buf, sizeof buf, "Score %lu", (unsigned long)score);
  uiText(2, 2, buf, C_WHITE);
  snprintf(buf, sizeof buf, "Wave %d", wave + 1);
  uiTextC(2, buf, C_DIM);
  snprintf(buf, sizeof buf, "Best %lu", (unsigned long)best);
  uiTextR(SCREEN_W - 50, 2, buf, C_YELLOW);
  for (int i = 0; i < lives; i++) drawSprite(SCREEN_W - 46 + i * 15, 2, kShip, SHIP_W, SHIP_H, C_GREEN);
  canvas->drawFastHLine(0, 11, SCREEN_W, C_GRID);

  for (int r = 0; r < rows; r++)
    for (int c = 0; c < ICOLS; c++)
      if (alive[r][c])
        drawSprite((int)fx + c * PITCH_X, (int)fy + r * PITCH_Y, kSprites[kRowType[r]][anim], SPR_W, SPR_H, kRowColor[r]);

  for (int s = 0; s < NSHIELD; s++)
    for (int r = 0; r < SH_BY; r++)
      for (int c = 0; c < SH_BX; c++)
        if (shield[s][r][c])
          canvas->fillRect(shieldX(s) + c * SH_CELL, SH_Y + r * SH_CELL, SH_CELL, SH_CELL, C_GREEN);

  if (invuln == 0 || (invuln / 6) % 2 == 0)
    drawSprite((int)shipX, SHIP_Y, kShip, SHIP_W, SHIP_H, C_GREEN);
  if (pb.on) canvas->fillRect((int)pb.x, (int)pb.y, 1, 4, C_WHITE);
  for (auto& b : eb) if (b.on) canvas->fillRect((int)b.x, (int)b.y, 2, 4, C_RED);
  if (over) uiGameOver(score, best, newRecord);
}
}  // namespace

const Game gameInvaders = {"Invaders", "inv", gInit, gUpdate, gDraw};
