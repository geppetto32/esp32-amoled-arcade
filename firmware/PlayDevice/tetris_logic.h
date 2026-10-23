// tetris_logic.h - pure Tetris rules (no Arduino / display includes), host-testable.
#pragma once
#include <stdint.h>
#include <string.h>
#include "logic_rng.h"

#define TET_W 10
#define TET_H 20

enum TetType : int8_t { TET_I, TET_O, TET_T, TET_S, TET_Z, TET_J, TET_L, TET_TYPES };

struct TetShape { int8_t n; const char* rows[4]; };   // n x n bounding box at rotation 0
static const TetShape kTetShapes[TET_TYPES] = {
  {4, {"....", "####", "....", "...."}},
  {2, {"##", "##", "", ""}},
  {3, {".#.", "###", "...", ""}},
  {3, {".##", "##.", "...", ""}},
  {3, {"##.", ".##", "...", ""}},
  {3, {"#..", "###", "...", ""}},
  {3, {"..#", "###", "...", ""}},
};

struct TetState {
  uint8_t  board[TET_H][TET_W];   // 0 = empty, else piece type + 1
  int8_t   type, rot, px, py;     // current piece; (px,py) = top-left of its box
  int8_t   next;
  uint8_t  bag[7];
  int      bagPos;
  uint32_t score;
  int      lines, level;
  bool     over;
  uint32_t rng;
};

// Is cell (r,c) of the piece (rotated CW `rot` times) filled?
inline bool tetCell(int type, int rot, int r, int c) {
  int n = kTetShapes[type].n;
  for (int i = 0; i < (rot & 3); i++) { int nr = n - 1 - c; c = r; r = nr; }
  return kTetShapes[type].rows[r][c] == '#';
}

inline bool tetCollides(const uint8_t board[TET_H][TET_W], int type, int rot, int px, int py) {
  int n = kTetShapes[type].n;
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++) {
      if (!tetCell(type, rot, r, c)) continue;
      int x = px + c, y = py + r;
      if (x < 0 || x >= TET_W || y >= TET_H) return true;
      if (y >= 0 && board[y][x]) return true;
    }
  return false;
}

// Removes full rows, drops the rows above. Returns the number cleared.
inline int tetClearLines(uint8_t board[TET_H][TET_W]) {
  int cleared = 0;
  for (int y = TET_H - 1; y >= 0; y--) {
    bool full = true;
    for (int x = 0; x < TET_W; x++) if (!board[y][x]) { full = false; break; }
    if (!full) continue;
    for (int yy = y; yy > 0; yy--) memcpy(board[yy], board[yy - 1], TET_W);
    memset(board[0], 0, TET_W);
    cleared++;
    y++;   // re-check this row index (it now holds the row that fell)
  }
  return cleared;
}

inline int8_t tetDraw(TetState& s) {
  if (s.bagPos >= 7) {
    for (int i = 0; i < 7; i++) s.bag[i] = (uint8_t)i;
    for (int i = 6; i > 0; i--) {
      int j = (int)(rngNext(s.rng) % (uint32_t)(i + 1));
      uint8_t t = s.bag[i]; s.bag[i] = s.bag[j]; s.bag[j] = t;
    }
    s.bagPos = 0;
  }
  return (int8_t)s.bag[s.bagPos++];
}

inline void tetSpawn(TetState& s) {
  s.type = s.next;
  s.next = tetDraw(s);
  s.rot = 0;
  s.px = (int8_t)((TET_W - kTetShapes[(int)s.type].n) / 2);
  s.py = (s.type == TET_I) ? -1 : 0;   // I piece's filled row is row 1 of its box
  if (tetCollides(s.board, s.type, s.rot, s.px, s.py)) s.over = true;
}

inline void tetReset(TetState& s, uint32_t seed) {
  memset(&s, 0, sizeof s);
  s.rng = seed ? seed : 1;
  s.bagPos = 7;
  s.next = tetDraw(s);
  tetSpawn(s);
}

inline bool tetTryMove(TetState& s, int dx, int dy) {
  if (tetCollides(s.board, s.type, s.rot, s.px + dx, s.py + dy)) return false;
  s.px = (int8_t)(s.px + dx);
  s.py = (int8_t)(s.py + dy);
  return true;
}

// dir = +1 clockwise, -1 counter-clockwise. Tries small horizontal kicks.
inline bool tetTryRotate(TetState& s, int dir) {
  static const int8_t kicks[5] = {0, -1, 1, -2, 2};
  int nr = (s.rot + dir) & 3;
  for (int i = 0; i < 5; i++) {
    if (!tetCollides(s.board, s.type, nr, s.px + kicks[i], s.py)) {
      s.rot = (int8_t)nr;
      s.px = (int8_t)(s.px + kicks[i]);
      return true;
    }
  }
  return false;
}

inline int tetDropDistance(const TetState& s) {
  int d = 0;
  while (!tetCollides(s.board, s.type, s.rot, s.px, s.py + d + 1)) d++;
  return d;
}

// Locks the piece, clears lines, updates score/level, spawns the next piece.
// Returns lines cleared.
inline int tetLock(TetState& s) {
  int n = kTetShapes[(int)s.type].n;
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++)
      if (tetCell(s.type, s.rot, r, c)) {
        int x = s.px + c, y = s.py + r;
        if (y >= 0 && y < TET_H && x >= 0 && x < TET_W) s.board[y][x] = (uint8_t)(s.type + 1);
      }
  int cleared = tetClearLines(s.board);
  static const int pts[5] = {0, 40, 100, 300, 1200};
  s.score += (uint32_t)pts[cleared] * (uint32_t)(s.level + 1);
  s.lines += cleared;
  s.level = s.lines / 10;
  tetSpawn(s);
  return cleared;
}

// Frames (ticks) between gravity steps.
inline int tetGravityFrames(int level) {
  int f = 48 - level * 5;
  return f < 3 ? 3 : f;
}
