// snake_logic.h - pure Snake rules (no Arduino / display includes), host-testable.
// Rules ported from games/html_prototypes/snake.html: start length 3 heading right,
// +10 per food, step interval 140 ms reduced by 4 ms per food down to 50 ms,
// walls and self (including the tail cell) are deadly, direction change cannot reverse.
#pragma once
#include <stdint.h>
#include "logic_rng.h"

#define SN_COLS 32
#define SN_ROWS 16
#define SN_MAX  (SN_COLS * SN_ROWS)

enum SnakeResult : uint8_t { SN_MOVED, SN_ATE, SN_DEAD, SN_WON };

struct SnakeState {
  uint8_t  sx[SN_MAX], sy[SN_MAX];   // index 0 = head
  int      len;
  int8_t   dx, dy;                   // direction used by the last step
  int8_t   ndx, ndy;                 // queued direction
  uint8_t  fx, fy;                   // food
  uint32_t score;
  int      stepMs;
  bool     alive;
  uint32_t rng;
};

inline bool snakeOccupies(const SnakeState& s, int x, int y) {
  for (int i = 0; i < s.len; i++)
    if (s.sx[i] == x && s.sy[i] == y) return true;
  return false;
}

// Picks a uniformly random FREE cell. Returns false if the board is full.
inline bool snakePlaceFood(SnakeState& s) {
  int freeCount = SN_COLS * SN_ROWS - s.len;
  if (freeCount <= 0) return false;
  int k = (int)(rngNext(s.rng) % (uint32_t)freeCount);
  for (int y = 0; y < SN_ROWS; y++)
    for (int x = 0; x < SN_COLS; x++) {
      if (snakeOccupies(s, x, y)) continue;
      if (k-- == 0) { s.fx = (uint8_t)x; s.fy = (uint8_t)y; return true; }
    }
  return false;   // unreachable
}

inline void snakeReset(SnakeState& s, uint32_t seed) {
  s.len = 3;
  for (int i = 0; i < 3; i++) { s.sx[i] = (uint8_t)(SN_COLS / 2 - i); s.sy[i] = SN_ROWS / 2; }
  s.dx = s.ndx = 1;
  s.dy = s.ndy = 0;
  s.score = 0;
  s.stepMs = 140;
  s.alive = true;
  s.rng = seed ? seed : 1;
  snakePlaceFood(s);
}

// Queue a direction; ignored if it reverses the currently queued one.
inline void snakeSetDir(SnakeState& s, int dx, int dy) {
  if (dx == -s.ndx && dy == -s.ndy) return;
  s.ndx = (int8_t)dx;
  s.ndy = (int8_t)dy;
}

inline SnakeResult snakeStep(SnakeState& s) {
  s.dx = s.ndx;
  s.dy = s.ndy;
  int hx = s.sx[0] + s.dx;
  int hy = s.sy[0] + s.dy;
  if (hx < 0 || hx >= SN_COLS || hy < 0 || hy >= SN_ROWS || snakeOccupies(s, hx, hy)) {
    s.alive = false;
    return SN_DEAD;
  }
  bool ate = (hx == s.fx && hy == s.fy);
  int newLen = s.len + (ate ? 1 : 0);
  for (int i = newLen - 1; i > 0; i--) { s.sx[i] = s.sx[i - 1]; s.sy[i] = s.sy[i - 1]; }
  s.sx[0] = (uint8_t)hx;
  s.sy[0] = (uint8_t)hy;
  s.len = newLen;
  if (!ate) return SN_MOVED;
  s.score += 10;
  s.stepMs = s.stepMs - 4 < 50 ? 50 : s.stepMs - 4;
  if (!snakePlaceFood(s)) { s.alive = false; return SN_WON; }
  return SN_ATE;
}
