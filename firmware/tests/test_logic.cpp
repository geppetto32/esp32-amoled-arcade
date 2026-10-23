// Host-side tests for the display-free game logic (Tetris + Snake).
// Build: see build_and_run.cmd (MSVC) or:  g++ -std=c++17 -Wall -o test_logic test_logic.cpp && ./test_logic
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../PlayDevice/snake_logic.h"
#include "../PlayDevice/tetris_logic.h"

static int g_checks = 0, g_fail = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    g_checks++;                                                              \
    if (!(cond)) { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #cond); } \
  } while (0)

static int boardCount(const uint8_t b[TET_H][TET_W]) {
  int n = 0;
  for (int y = 0; y < TET_H; y++) for (int x = 0; x < TET_W; x++) if (b[y][x]) n++;
  return n;
}
static void fillRow(uint8_t b[TET_H][TET_W], int y, uint8_t v = 1) { for (int x = 0; x < TET_W; x++) b[y][x] = v; }

// ---------------------------------------------------------------- Tetris
static void test_tetris_line_clear() {
  printf("tetris: line clear\n");
  uint8_t b[TET_H][TET_W] = {};
  b[17][2] = 3; b[17][7] = 3;            // partial row above two full rows
  fillRow(b, 18); fillRow(b, 19);
  CHECK(tetClearLines(b) == 2);
  CHECK(b[19][2] == 3 && b[19][7] == 3 && boardCount(b) == 2);   // partial row fell to the bottom
  for (int y = 0; y < 19; y++) for (int x = 0; x < TET_W; x++) CHECK(b[y][x] == 0);

  // Non-adjacent full rows with partial rows between them.
  uint8_t c[TET_H][TET_W] = {};
  fillRow(c, 15); c[16][0] = 1; c[17][1] = 2; c[18][2] = 3; fillRow(c, 19);
  CHECK(tetClearLines(c) == 2);
  CHECK(c[19][2] == 3 && c[18][1] == 2 && c[17][0] == 1 && boardCount(c) == 3);

  // Four full rows, and no full rows.
  uint8_t d[TET_H][TET_W] = {};
  for (int y = 16; y < 20; y++) fillRow(d, y);
  CHECK(tetClearLines(d) == 4 && boardCount(d) == 0);
  uint8_t e[TET_H][TET_W] = {};
  fillRow(e, 19); e[19][4] = 0;
  CHECK(tetClearLines(e) == 0 && boardCount(e) == 9);
}

static void test_tetris_lock_scoring() {
  printf("tetris: lock + scoring (tetris with I piece)\n");
  TetState s; tetReset(s, 1234);
  memset(s.board, 0, sizeof s.board);
  for (int y = 16; y < 20; y++) { fillRow(s.board, y); s.board[y][9] = 0; }   // 1-wide well at x=9
  s.type = TET_I; s.rot = 1;            // vertical: filled column is box column 2
  s.px = 7; s.py = 0;                   // -> x = 9
  CHECK(!tetCollides(s.board, s.type, s.rot, s.px, s.py));
  s.py = (int8_t)(s.py + tetDropDistance(s));
  int cleared = tetLock(s);
  CHECK(cleared == 4);
  CHECK(s.score == 1200u && s.lines == 4 && s.level == 0);
  CHECK(boardCount(s.board) == 0);
}

static void test_tetris_rotation() {
  printf("tetris: rotation / walls\n");
  // Every piece has exactly 4 cells in every rotation.
  for (int t = 0; t < TET_TYPES; t++)
    for (int r = 0; r < 4; r++) {
      int n = kTetShapes[t].n, cnt = 0;
      for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) if (tetCell(t, r, y, x)) cnt++;
      CHECK(cnt == 4);
    }
  // T rotated clockwise points right: base ".#." / "###" -> rot1 has (1,2) and not (1,0).
  CHECK(tetCell(TET_T, 1, 1, 2) && !tetCell(TET_T, 1, 1, 0));
  // Four CW rotations return to the start.
  for (int t = 0; t < TET_TYPES; t++) {
    int n = kTetShapes[t].n;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) CHECK(tetCell(t, 4, y, x) == tetCell(t, 0, y, x));
  }

  uint8_t empty[TET_H][TET_W] = {};
  // Horizontal I sticking out of either wall collides.
  CHECK(tetCollides(empty, TET_I, 0, -1, 5));
  CHECK(tetCollides(empty, TET_I, 0, 7, 5));
  CHECK(!tetCollides(empty, TET_I, 0, 0, 5));
  CHECK(!tetCollides(empty, TET_I, 0, 6, 5));
  // Floor and stacked blocks collide.
  CHECK(tetCollides(empty, TET_O, 0, 4, 19));
  uint8_t blk[TET_H][TET_W] = {}; blk[10][5] = 1;
  CHECK(tetCollides(blk, TET_O, 0, 4, 9) && !tetCollides(blk, TET_O, 0, 4, 7));

  // Wall kick: vertical I against the left wall rotates to horizontal by shifting right.
  TetState s; tetReset(s, 7);
  memset(s.board, 0, sizeof s.board);
  s.type = TET_I; s.rot = 1; s.px = -2; s.py = 5;      // filled column at x = 0
  CHECK(!tetCollides(s.board, s.type, s.rot, s.px, s.py));
  CHECK(tetCollides(s.board, s.type, 0, s.px, s.py));  // plain rotation would hit the wall
  CHECK(tetTryRotate(s, 1) && s.rot == 2 && s.px >= 0 && s.px + 3 < TET_W);

  // Rotation fully blocked: 1-wide well, no kick can make the horizontal I fit.
  TetState w; tetReset(w, 9);
  memset(w.board, 0, sizeof w.board);
  for (int y = 0; y < TET_H; y++) { fillRow(w.board, y); w.board[y][5] = 0; }
  w.type = TET_I; w.rot = 1; w.px = 3; w.py = 4;       // filled column at x = 5
  CHECK(!tetCollides(w.board, w.type, w.rot, w.px, w.py));
  int8_t rot0 = w.rot, px0 = w.px, py0 = w.py;
  CHECK(!tetTryRotate(w, 1));
  CHECK(!tetTryRotate(w, -1));
  CHECK(w.rot == rot0 && w.px == px0 && w.py == py0);  // state untouched on failure
}

static void test_tetris_misc() {
  printf("tetris: spawn / drop / gravity\n");
  TetState s; tetReset(s, 42);
  CHECK(!s.over && s.type >= 0 && s.type < TET_TYPES && s.next >= 0 && s.next < TET_TYPES);
  // Hard-drop distance on an empty board: horizontal I (filled row 1 of its box) lands on row 19.
  TetState i; tetReset(i, 3);
  memset(i.board, 0, sizeof i.board);
  i.type = TET_I; i.rot = 0; i.px = 3; i.py = -1;
  CHECK(tetDropDistance(i) == 19);
  // Spawn into a full top => game over.
  TetState o; tetReset(o, 5);
  for (int y = 0; y < 4; y++) fillRow(o.board, y);
  tetSpawn(o);
  CHECK(o.over);
  // 7-bag: each run of 7 draws contains every piece exactly once.
  TetState b; tetReset(b, 99); b.bagPos = 7;
  for (int round = 0; round < 20; round++) {
    int seen[TET_TYPES] = {};
    for (int k = 0; k < 7; k++) seen[(int)tetDraw(b)]++;
    for (int t = 0; t < TET_TYPES; t++) CHECK(seen[t] == 1);
  }
  CHECK(tetGravityFrames(0) > tetGravityFrames(5) && tetGravityFrames(100) == 3);
}

// ---------------------------------------------------------------- Snake
static void snakeSet(SnakeState& s, const int (*cells)[2], int n, int dx, int dy) {
  s.len = n;
  for (int i = 0; i < n; i++) { s.sx[i] = (uint8_t)cells[i][0]; s.sy[i] = (uint8_t)cells[i][1]; }
  s.dx = s.ndx = (int8_t)dx; s.dy = s.ndy = (int8_t)dy;
  s.alive = true; s.score = 0; s.stepMs = 140; s.fx = 30; s.fy = 14;
}

static void test_snake_death() {
  printf("snake: death\n");
  SnakeState s; snakeReset(s, 1);
  CHECK(s.len == 3 && s.dx == 1 && s.dy == 0 && s.alive && s.stepMs == 140);
  // Walls.
  s.sx[0] = SN_COLS - 1; s.sy[0] = 5; s.sx[1] = SN_COLS - 2; s.sy[1] = 5; s.sx[2] = SN_COLS - 3; s.sy[2] = 5;
  CHECK(snakeStep(s) == SN_DEAD && !s.alive);

  // Self hit: head (5,5) heads down into body cell (5,6).
  const int coil[5][2] = {{5, 5}, {4, 5}, {4, 6}, {5, 6}, {6, 6}};
  SnakeState a; snakeReset(a, 2); snakeSet(a, coil, 5, 1, 0);
  snakeSetDir(a, 0, 1);
  CHECK(snakeStep(a) == SN_DEAD && !a.alive);

  // Moving into the cell the tail occupies is also fatal (same as the browser prototype).
  const int loop4[4][2] = {{6, 5}, {6, 6}, {5, 6}, {5, 5}};
  SnakeState t; snakeReset(t, 3); snakeSet(t, loop4, 4, 0, -1);
  snakeSetDir(t, -1, 0);
  CHECK(snakeStep(t) == SN_DEAD);

  // A normal move into free space is fine.
  SnakeState f; snakeReset(f, 4);
  CHECK(snakeStep(f) == SN_MOVED && f.alive && f.sx[0] == SN_COLS / 2 + 1);

  // Reversal is ignored: heading right, asking left keeps going right (no instant self-hit).
  SnakeState r; snakeReset(r, 5);
  snakeSetDir(r, -1, 0);
  CHECK(snakeStep(r) == SN_MOVED && r.dx == 1);

  // Self-collision via real steering: 5-long line heading right, then down, left, up.
  SnakeState g; snakeReset(g, 6);
  const int line5[5][2] = {{10, 8}, {9, 8}, {8, 8}, {7, 8}, {6, 8}};
  snakeSet(g, line5, 5, 1, 0);
  snakeSetDir(g, 0, 1); CHECK(snakeStep(g) == SN_MOVED);   // down
  snakeSetDir(g, -1, 0); CHECK(snakeStep(g) == SN_MOVED);   // left
  snakeSetDir(g, 0, -1); CHECK(snakeStep(g) == SN_DEAD);    // up into own body at (9,8)
}

static void test_snake_eat_and_food() {
  printf("snake: eating / food placement\n");
  SnakeState s; snakeReset(s, 11);
  s.fx = (uint8_t)(s.sx[0] + 1); s.fy = s.sy[0];
  CHECK(snakeStep(s) == SN_ATE);
  CHECK(s.len == 4 && s.score == 10 && s.stepMs == 136);
  CHECK(!(s.fx == s.sx[0] && s.fy == s.sy[0]) && !snakeOccupies(s, s.fx, s.fy));
  s.stepMs = 52; s.fx = (uint8_t)(s.sx[0] + 1); s.fy = s.sy[0];
  snakeStep(s);
  CHECK(s.stepMs == 50);
  s.fx = (uint8_t)(s.sx[0] + 1); s.fy = s.sy[0];
  snakeStep(s);
  CHECK(s.stepMs == 50);   // floor

  // Food never lands on the snake: serpentine snakes of many lengths, many seeds.
  long placements = 0;
  for (int len = 3; len <= SN_MAX - 1; len += 7) {
    SnakeState q; snakeReset(q, 1000u + (uint32_t)len);
    q.len = len;
    for (int i = 0; i < len; i++) {
      int row = i / SN_COLS, col = i % SN_COLS;
      q.sx[i] = (uint8_t)((row & 1) ? SN_COLS - 1 - col : col);
      q.sy[i] = (uint8_t)row;
    }
    for (int k = 0; k < 200; k++) {
      bool ok = snakePlaceFood(q);
      placements++;
      if (!ok || q.fx >= SN_COLS || q.fy >= SN_ROWS || snakeOccupies(q, q.fx, q.fy)) { CHECK(false); break; }
    }
  }
  CHECK(placements > 10000);

  // Exactly one free cell left: food must go there. Full board: no placement possible.
  SnakeState full; snakeReset(full, 77);
  full.len = SN_MAX - 1;
  for (int i = 0; i < full.len; i++) {
    int row = i / SN_COLS, col = i % SN_COLS;
    full.sx[i] = (uint8_t)((row & 1) ? SN_COLS - 1 - col : col);
    full.sy[i] = (uint8_t)row;
  }
  for (int k = 0; k < 20; k++) {
    CHECK(snakePlaceFood(full));
    CHECK(!snakeOccupies(full, full.fx, full.fy));
  }
  full.len = SN_MAX;
  full.sx[SN_MAX - 1] = 0; full.sy[SN_MAX - 1] = SN_ROWS - 1;   // the last free cell
  CHECK(!snakePlaceFood(full));

  // All cells reachable: over many placements on a short snake every free cell shows up.
  SnakeState d; snakeReset(d, 5);
  static bool seen[SN_ROWS][SN_COLS];
  memset(seen, 0, sizeof seen);
  for (int k = 0; k < 20000; k++) { snakePlaceFood(d); seen[d.fy][d.fx] = true; }
  int missing = 0;
  for (int y = 0; y < SN_ROWS; y++) for (int x = 0; x < SN_COLS; x++)
    if (!seen[y][x] && !snakeOccupies(d, x, y)) missing++;
  CHECK(missing == 0);
}

int main() {
  test_tetris_line_clear();
  test_tetris_lock_scoring();
  test_tetris_rotation();
  test_tetris_misc();
  test_snake_death();
  test_snake_eat_and_food();
  printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
