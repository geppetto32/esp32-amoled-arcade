// Pong - UP/DOWN moves the left paddle against a simple CPU. First to 7 wins.
// High score: 100 + 10 per point of margin over the CPU on a win (0 on a loss).
#include <math.h>
#include "game.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"

namespace {
const int WIN_POINTS = 7;
const int PAD_W = 4, PAD_H = 30;
const int PLAYER_X = 8, CPU_X = SCREEN_W - 8 - PAD_W;
const int BALL = 5;
const float PLAYER_SPEED = 3.6f;
const float CPU_SPEED = 2.5f;

float py, cy;            // paddle tops
float bx, by, vx, vy;
float speed;
int pScore, cScore;
int serveDelay;          // ticks until ball is released
int serveDir;            // +1 toward CPU, -1 toward player
float cpuAimError;
bool over, newRecord;
uint32_t best, finalScore;

void serve() {
  bx = SCREEN_W / 2 - BALL / 2;
  by = SCREEN_H / 2 - BALL / 2;
  speed = 3.0f;
  float a = ((int)random(-30, 31)) * 0.01f;   // slight random angle
  vx = serveDir * speed * cosf(a);
  vy = speed * sinf(a);
  serveDelay = 45;
  cpuAimError = (float)random(-8, 9);
}

void gInit() {
  py = cy = (SCREEN_H - PAD_H) / 2;
  pScore = cScore = 0;
  over = newRecord = false;
  finalScore = 0;
  best = hiscoreGet("pong");
  serveDir = random(2) ? 1 : -1;
  serve();
}

void finish() {
  over = true;
  finalScore = (pScore > cScore) ? 100 + 10 * (pScore - cScore) : 0;
  newRecord = hiscoreSubmit("pong", finalScore);
  if (newRecord) best = finalScore;
  soundPlay(pScore > cScore ? SFX_START : SFX_GAMEOVER);
}

void bounceOffPaddle(float padTop, int dir) {   // dir = direction ball leaves in (+1 right)
  float off = ((by + BALL / 2) - (padTop + PAD_H / 2)) / (PAD_H / 2.0f);
  if (off > 1) off = 1;
  if (off < -1) off = -1;
  if (speed < 6.5f) speed += 0.3f;
  float a = off * 1.0f;   // radians from horizontal, up to ~57 degrees
  vx = dir * speed * cosf(a);
  vy = speed * sinf(a);
  soundPlay(SFX_CLICK);
}

void gUpdate() {
  if (over) {
    if (btnPressed(BTN_A)) gInit();
    return;
  }
  if (btnHeld(BTN_UP))   py -= PLAYER_SPEED;
  if (btnHeld(BTN_DOWN)) py += PLAYER_SPEED;
  if (py < 0) py = 0;
  if (py > SCREEN_H - PAD_H) py = SCREEN_H - PAD_H;

  // CPU: track the ball when it approaches, otherwise drift to centre.
  float target = (vx > 0 && serveDelay == 0) ? by + BALL / 2 + cpuAimError : SCREEN_H / 2;
  float mid = cy + PAD_H / 2;
  if (target > mid + 5) cy += CPU_SPEED;
  else if (target < mid - 5) cy -= CPU_SPEED;
  if (cy < 0) cy = 0;
  if (cy > SCREEN_H - PAD_H) cy = SCREEN_H - PAD_H;

  if (serveDelay > 0) { serveDelay--; return; }

  bx += vx;
  by += vy;
  if (by < 0) { by = 0; vy = -vy; soundPlay(SFX_HIT); }
  if (by > SCREEN_H - BALL) { by = SCREEN_H - BALL; vy = -vy; soundPlay(SFX_HIT); }

  if (vx < 0 && bx <= PLAYER_X + PAD_W && bx + BALL >= PLAYER_X && by + BALL >= py && by <= py + PAD_H) {
    bx = PLAYER_X + PAD_W;
    bounceOffPaddle(py, 1);
  } else if (vx > 0 && bx + BALL >= CPU_X && bx <= CPU_X + PAD_W && by + BALL >= cy && by <= cy + PAD_H) {
    bx = CPU_X - BALL;
    bounceOffPaddle(cy, -1);
    cpuAimError = (float)random(-10, 11);
  }

  if (bx + BALL < 0) {
    cScore++;
    serveDir = -1;   // serve toward the player who just lost the point
    soundPlay(SFX_GAMEOVER);
    if (cScore >= WIN_POINTS) finish(); else serve();
  } else if (bx > SCREEN_W) {
    pScore++;
    serveDir = 1;
    soundPlay(SFX_EAT);
    if (pScore >= WIN_POINTS) finish(); else serve();
  }
}

void gDraw() {
  canvas->fillScreen(C_BLACK);
  for (int y = 0; y < SCREEN_H; y += 10) canvas->fillRect(SCREEN_W / 2 - 1, y, 2, 5, C_GRID);
  uiNum(SCREEN_W / 2 - 40, 6, pScore, C_WHITE, 3);
  uiNum(SCREEN_W / 2 + 22, 6, cScore, C_WHITE, 3);
  canvas->fillRect(PLAYER_X, (int)py, PAD_W, PAD_H, C_CYAN);
  canvas->fillRect(CPU_X, (int)cy, PAD_W, PAD_H, C_ORANGE);
  canvas->fillRect((int)bx, (int)by, BALL, BALL, C_WHITE);
  if (over) {
    char a[40];
    snprintf(a, sizeof a, "%d - %d   Score %lu   Best %lu", pScore, cScore,
             (unsigned long)finalScore, (unsigned long)best);
    uiOverlay(pScore > cScore ? "YOU WIN" : "CPU WINS", a,
              newRecord ? "NEW RECORD!   A = rematch" : "A = rematch");
  }
}
}  // namespace

const Game gamePong = {"Pong", "pong", gInit, gUpdate, gDraw};
