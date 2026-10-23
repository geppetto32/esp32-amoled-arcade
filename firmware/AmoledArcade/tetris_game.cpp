// tetris_game.cpp - Tetris on the 448x368 landscape screen: 10x20 well of 16 px cells, hold + 3 next pieces,
// ghost piece, SRS-lite wall kicks, 7-bag, line-clear flash, levels.
//
// CONTROL = TILT:    tilt left/right = move (auto-repeat), tilt toward you = soft drop,
//                    LEFT = rotate CCW, RIGHT = rotate CW, both buttons together = hard drop,
//                    flick the board away from you (or tap the screen) = HOLD.
// CONTROL = BUTTONS: LEFT / RIGHT = move (auto-repeat while held), both together = rotate CW,
//                    tilt toward you = soft drop, flick away = hard drop, tap the screen = HOLD.
#include "game.h"
#include <math.h>

enum TtSt : uint8_t { TT_PLAY, TT_CLEAR, TT_OVER };

static const char *const TT_CTRL[] = {"TILT", "BUTTONS"};
static Option ttOpts[] = {{"CONTROL", TT_CTRL, 2, "ttCtrl", 0}};

static constexpr int TT_W = 10, TT_H = 20, TT_C = 16, TT_X0 = 144, TT_Y0 = 44;
static const uint16_t TT_COL[7] = {rgb(0, 230, 255), rgb(255, 220, 40), rgb(190, 70, 255), rgb(60, 255, 120),
                                   rgb(255, 60, 80), rgb(70, 110, 255), rgb(255, 150, 30)};
enum { P_I, P_O, P_T, P_S, P_Z, P_J, P_L };

// base shapes (rotation 0) inside a 4x4 (I, O) or 3x3 box, x then y per cell
static const int8_t TT_BASE[7][4][2] = {
  {{0, 1}, {1, 1}, {2, 1}, {3, 1}},   // I
  {{1, 0}, {2, 0}, {1, 1}, {2, 1}},   // O
  {{1, 0}, {0, 1}, {1, 1}, {2, 1}},   // T
  {{1, 0}, {2, 0}, {0, 1}, {1, 1}},   // S
  {{0, 0}, {1, 0}, {1, 1}, {2, 1}},   // Z
  {{0, 0}, {0, 1}, {1, 1}, {2, 1}},   // J
  {{2, 0}, {0, 1}, {1, 1}, {2, 1}},   // L
};
static const int8_t TT_BOX[7] = {4, 4, 3, 3, 3, 3, 3};

// SRS kick offsets (x, y with y UP, as in the guideline): [dir 0=CW 1=CCW][from rotation][5 tests]
static const int8_t KICK_JLSTZ[2][4][5][2] = {
  {{{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}, {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
   {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}},
  {{{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},
   {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}}, {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}}}};
static const int8_t KICK_I[2][4][5][2] = {
  {{{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}, {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},
   {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}}, {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}},
  {{{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}}, {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},
   {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}}, {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}}}};

static const uint16_t TT_LVL_MS[15] = {800, 720, 630, 550, 470, 380, 300, 215, 130, 100, 80, 80, 70, 70, 60};

static int8_t ttCells[7][4][4][2];            // [piece][rotation][cell][x/y]
static uint8_t ttBoard[TT_H][TT_W];           // 0 empty, 1..7 piece colour
static TtSt ttSt;
static int ttType, ttRot, ttPx, ttPy, ttHold, ttScore, ttLines, ttLevel, ttResets;
static int ttQ[3], ttBag[7], ttBagPos;
static bool ttCanHold, ttNewBest, ttFlickArmed;
static uint32_t ttBest, ttStAt, ttLastFall, ttLockAt, ttLastDraw, ttRepAt, ttRawSince, ttPendT;
static int ttLastDir, ttRawPrev, ttPend;
static int ttFullRows[4], ttNFull;

static bool ttTilt() { return ttOpts[0].value == 0; }

static void ttInitCells() {
  for (int t = 0; t < 7; t++) {
    int n = TT_BOX[t];
    int8_t cur[4][2];
    for (int c = 0; c < 4; c++) { cur[c][0] = TT_BASE[t][c][0]; cur[c][1] = TT_BASE[t][c][1]; }
    for (int r = 0; r < 4; r++) {
      for (int c = 0; c < 4; c++) { ttCells[t][r][c][0] = cur[c][0]; ttCells[t][r][c][1] = cur[c][1]; }
      for (int c = 0; c < 4; c++) {                 // rotate clockwise inside the box
        int8_t x = cur[c][0], y = cur[c][1];
        cur[c][0] = (int8_t)(n - 1 - y);
        cur[c][1] = x;
      }
    }
  }
}

static bool ttFits(int t, int r, int px, int py) {
  for (int c = 0; c < 4; c++) {
    int x = px + ttCells[t][r][c][0], y = py + ttCells[t][r][c][1];
    if (x < 0 || x >= TT_W || y >= TT_H) return false;
    if (y >= 0 && ttBoard[y][x]) return false;
  }
  return true;
}

static int ttBagNext() {
  if (ttBagPos >= 7) {
    for (int i = 0; i < 7; i++) ttBag[i] = i;
    for (int i = 6; i > 0; i--) { int j = esp_random() % (i + 1); int t = ttBag[i]; ttBag[i] = ttBag[j]; ttBag[j] = t; }
    ttBagPos = 0;
  }
  return ttBag[ttBagPos++];
}

static void ttGameOver(uint32_t now) {
  ttSt = TT_OVER; ttStAt = now;
  ttNewBest = saveBest("tetBest", (uint32_t)ttScore);
  ttBest = max(ttBest, (uint32_t)ttScore);
  ttNewBest ? sfxBest() : sfxBad();
}

static bool ttSpawn(int t, uint32_t now) {            // false = game over
  ttType = t; ttRot = 0; ttPx = 3; ttPy = (t == P_I) ? -1 : 0;
  ttLockAt = 0; ttResets = 0; ttLastFall = now;
  if (!ttFits(ttType, ttRot, ttPx, ttPy)) { ttGameOver(now); return false; }
  return true;
}
static void ttSpawnNext(uint32_t now) {
  int t = ttQ[0];
  ttQ[0] = ttQ[1]; ttQ[1] = ttQ[2]; ttQ[2] = ttBagNext();
  ttSpawn(t, now);
}

static void ttStart() {
  ttInitCells();
  memset(ttBoard, 0, sizeof ttBoard);
  ttScore = ttLines = 0; ttLevel = 1; ttHold = -1; ttCanHold = true; ttNewBest = false;
  ttBagPos = 7;
  for (int i = 0; i < 3; i++) ttQ[i] = ttBagNext();
  ttBest = loadBest("tetBest");
  ttLastDir = 0; ttRawPrev = 0; ttPend = 0; ttFlickArmed = true; ttRawSince = 0;
  ttSt = TT_PLAY;
  uint32_t now = millis();
  ttLastDraw = 0;
  ttSpawnNext(now);
  gfx->fillScreen(C_BG);
  dirty = true;
}

static bool ttOnGround() { return !ttFits(ttType, ttRot, ttPx, ttPy + 1); }
static void ttTouched(uint32_t now) {                 // a successful move/rotate: restart the lock delay (max 12 times)
  if (ttOnGround() && ttLockAt && ttResets < 12) { ttLockAt = now; ttResets++; }
}
static bool ttTryMove(int dx, uint32_t now) {
  if (!ttFits(ttType, ttRot, ttPx + dx, ttPy)) return false;
  ttPx += dx;
  ttTouched(now);
  return true;
}
static void ttTryRotate(int dir, uint32_t now) {      // dir 0 = CW, 1 = CCW
  if (ttType == P_O) return;
  int nr = (ttRot + (dir == 0 ? 1 : 3)) & 3;
  const int8_t (*k)[2] = (ttType == P_I ? KICK_I : KICK_JLSTZ)[dir][ttRot];
  for (int i = 0; i < 5; i++) {
    int nx = ttPx + k[i][0], ny = ttPy - k[i][1];
    if (ttFits(ttType, nr, nx, ny)) {
      ttRot = nr; ttPx = nx; ttPy = ny;
      ttTouched(now);
      play(1200, 15);
      return;
    }
  }
}

static void ttLock(uint32_t now) {
  for (int c = 0; c < 4; c++) {
    int x = ttPx + ttCells[ttType][ttRot][c][0], y = ttPy + ttCells[ttType][ttRot][c][1];
    if (y >= 0) ttBoard[y][x] = (uint8_t)(ttType + 1);
  }
  ttNFull = 0;
  for (int y = 0; y < TT_H; y++) {
    bool full = true;
    for (int x = 0; x < TT_W; x++) if (!ttBoard[y][x]) { full = false; break; }
    if (full && ttNFull < 4) ttFullRows[ttNFull++] = y;
  }
  ttCanHold = true;
  if (ttNFull) { ttSt = TT_CLEAR; ttStAt = now; ttNFull >= 4 ? sfxBest() : sfxGood(); }
  else { play(300, 20); ttSpawnNext(now); }
}

static void ttHardDrop(uint32_t now) {
  int d = 0;
  while (ttFits(ttType, ttRot, ttPx, ttPy + 1)) { ttPy++; d++; }
  ttScore += 2 * d;
  ttLock(now);
}

static void ttDoHold(uint32_t now) {
  if (!ttCanHold) { play(200, 40); return; }
  int t = ttType;
  if (ttHold < 0) { ttHold = t; ttSpawnNext(now); }
  else { int h = ttHold; ttHold = t; ttSpawn(h, now); }
  ttCanHold = false;
  play(700, 30);
}

// ---------------------------------------------------------------------------- drawing
static void ttCellPx(int x, int y, int s, uint16_t col) {
  gfx->fillRect(x, y, s, s, lerpColor(col, C_BG, 0.65f));
  gfx->fillRect(x + 3, y + 3, s - 6, s - 6, lerpColor(col, C_BG, 0.2f));
  gfx->drawRect(x, y, s, s, col);
  gfx->drawFastHLine(x + 1, y + 1, s - 2, lerpColor(col, C_TEXT, 0.55f));
}

static void ttPreview(int t, int bx, int by, int bw, int bh, int s) {   // piece centred in a box
  int minx = 9, maxx = -9, miny = 9, maxy = -9;
  for (int c = 0; c < 4; c++) {
    int x = ttCells[t][0][c][0], y = ttCells[t][0][c][1];
    minx = min(minx, x); maxx = max(maxx, x); miny = min(miny, y); maxy = max(maxy, y);
  }
  int pw = (maxx - minx + 1) * s, ph = (maxy - miny + 1) * s;
  int ox = bx + (bw - pw) / 2, oy = by + (bh - ph) / 2;
  for (int c = 0; c < 4; c++)
    ttCellPx(ox + (ttCells[t][0][c][0] - minx) * s, oy + (ttCells[t][0][c][1] - miny) * s, s, TT_COL[t]);
}

static void ttBox(int x, int y, int w, int h, const char *label) {
  gfx->drawRoundRect(x, y, w, h, 8, 0x2945);
  text(label, x + 8, y + 6, 2, C_DIM);
}

static void ttDraw(uint32_t now) {
  gfx->fillScreen(C_BG);
  if (ttTilt()) {
    centerText("L rot CCW   R rot CW   both = DROP", 4, 2, C_TEXT);
    centerText("tilt = move / soft drop   flick away or tap = HOLD", 26, 1, C_DIM);
  } else {
    centerText("L / R move     both = ROTATE", 4, 2, C_TEXT);
    centerText("tilt toward = soft drop   flick away = DROP   tap = HOLD", 26, 1, C_DIM);
  }
  const int wx = TT_X0, wy = TT_Y0, ww = TT_W * TT_C, wh = TT_H * TT_C;
  gfx->drawRect(wx - 3, wy - 3, ww + 6, wh + 6, C_CYAN);
  gfx->drawRect(wx - 5, wy - 5, ww + 10, wh + 10, lerpColor(C_CYAN, C_BG, 0.75f));
  for (int x = 1; x < TT_W; x++) gfx->drawFastVLine(wx + x * TT_C, wy, wh, 0x0841);
  for (int y = 1; y < TT_H; y++) gfx->drawFastHLine(wx, wy + y * TT_C, ww, 0x0841);

  bool flashOn = ((now - ttStAt) / 70) % 2 == 0;
  for (int y = 0; y < TT_H; y++) {
    bool fl = false;
    if (ttSt == TT_CLEAR) for (int i = 0; i < ttNFull; i++) if (ttFullRows[i] == y) fl = true;
    for (int x = 0; x < TT_W; x++) {
      if (!ttBoard[y][x]) continue;
      uint16_t c = TT_COL[ttBoard[y][x] - 1];
      if (fl) c = flashOn ? C_TEXT : lerpColor(c, C_TEXT, 0.4f);
      ttCellPx(wx + x * TT_C, wy + y * TT_C, TT_C, c);
    }
  }
  if (ttSt == TT_PLAY) {
    int gy = ttPy;
    while (ttFits(ttType, ttRot, ttPx, gy + 1)) gy++;
    for (int c = 0; c < 4; c++) {                                  // ghost: outline only
      int x = ttPx + ttCells[ttType][ttRot][c][0], y = gy + ttCells[ttType][ttRot][c][1];
      if (y >= 0) gfx->drawRect(wx + x * TT_C, wy + y * TT_C, TT_C, TT_C, lerpColor(TT_COL[ttType], C_BG, 0.45f));
    }
    for (int c = 0; c < 4; c++) {
      int x = ttPx + ttCells[ttType][ttRot][c][0], y = ttPy + ttCells[ttType][ttRot][c][1];
      if (y >= 0) ttCellPx(wx + x * TT_C, wy + y * TT_C, TT_C, TT_COL[ttType]);
    }
  }

  // left: hold + stats
  ttBox(16, 50, 112, 82, "HOLD");
  if (ttHold >= 0) ttPreview(ttHold, 16, 74, 112, 56, 14);
  char b[16];
  text("SCORE", 20, 146, 1, C_DIM);
  snprintf(b, sizeof b, "%d", ttScore);
  text(b, 20, 158, 3, C_TEXT);
  text("LEVEL", 20, 200, 1, C_DIM);
  snprintf(b, sizeof b, "%d", ttLevel);
  text(b, 20, 212, 3, C_ACC);
  text("LINES", 20, 254, 1, C_DIM);
  snprintf(b, sizeof b, "%d", ttLines);
  text(b, 20, 266, 3, C_TEXT);
  text("BEST", 20, 308, 1, C_DIM);
  snprintf(b, sizeof b, "%lu", (unsigned long)ttBest);
  text(b, 20, 320, 2, C_GOOD);

  // right: next 3
  ttBox(316, 50, 116, 190, "NEXT");
  for (int i = 0; i < 3; i++) ttPreview(ttQ[i], 316, 74 + i * 56, 116, 52, i == 0 ? 14 : 11);
  int bat = battery();
  if (bat >= 0) { snprintf(b, sizeof b, "%3d%%", bat); text(b, 366, 340, 2, charging() ? C_ACC : C_DIM); }

  if (ttSt == TT_OVER) {
    char l2[24];
    snprintf(l2, sizeof l2, "SCORE %d", ttScore);
    const char *l3 = ttNewBest ? "NEW BEST!  R again" : "R again   L menu";
    panelBox("GAME OVER", l2, l3, ttNewBest ? C_GOOD : C_BAD);
  }
  dirty = true;
}

// ---------------------------------------------------------------------------- frame
static bool ttFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  if (ttSt == TT_OVER) {
    if (now - ttStAt > 700) {
      if (in.right) { ttStart(); return true; }
      if (in.left) return false;
    }
    if (now - ttLastDraw >= 100) { ttLastDraw = now; ttDraw(now); }
    return true;
  }

  if (ttSt == TT_CLEAR) {
    if (now - ttStAt >= 380) {
      for (int i = 0; i < ttNFull; i++) {                         // rows are listed top to bottom; remove each
        int row = ttFullRows[i];
        for (int y = row; y > 0; y--) memcpy(ttBoard[y], ttBoard[y - 1], TT_W);
        memset(ttBoard[0], 0, TT_W);
      }
      static const int pts[5] = {0, 100, 300, 500, 800};
      ttScore += pts[ttNFull] * ttLevel;
      ttLines += ttNFull;
      ttLevel = min(15, ttLines / 10 + 1);
      ttSt = TT_PLAY;
      ttSpawnNext(now);
    }
    if (now - ttLastDraw >= 33) { ttLastDraw = now; ttDraw(now); }
    return true;
  }

  // ---- input ----
  float tx = ttTilt() ? axisX() : 0.0f, ty = axisY();   // axisX only in tilt mode: it hands the stick left/right to steering
  int raw;
  if (ttTilt()) raw = tx > 0.22f ? 1 : tx < -0.22f ? -1 : 0;
  else {
    raw = (in.leftDown && !in.rightDown) ? -1 : (in.rightDown && !in.leftDown) ? 1 : 0;
    if (raw != ttRawPrev) { ttRawPrev = raw; ttRawSince = now; }
    if (raw != 0 && now - ttRawSince < 45) raw = 0;               // wait a moment: maybe both buttons (= rotate)
  }
  if (raw != ttLastDir) {
    ttLastDir = raw;
    if (raw) { ttTryMove(raw, now); ttRepAt = now + (ttTilt() ? 260 : 200); }
  } else if (raw && now >= ttRepAt) {
    ttTryMove(raw, now);
    ttRepAt = now + (ttTilt() ? 130 : 70);
  }

  if (ttTilt()) {
    if (in.left && in.right) { ttHardDrop(now); return true; }
    if (in.left) { if (ttPend == 1 && now - ttPendT < 80) { ttPend = 0; ttHardDrop(now); return true; } ttPend = -1; ttPendT = now; }
    else if (in.right) { if (ttPend == -1 && now - ttPendT < 80) { ttPend = 0; ttHardDrop(now); return true; } ttPend = 1; ttPendT = now; }
    if (ttPend && now - ttPendT >= 60) { ttTryRotate(ttPend > 0 ? 0 : 1, now); ttPend = 0; }
  } else {
    if ((in.left && in.rightDown) || (in.right && in.leftDown)) ttTryRotate(0, now);
  }

  bool flick = false;
  if (ty < -0.45f && ttFlickArmed) { flick = true; ttFlickArmed = false; }
  if (ty > -0.2f) ttFlickArmed = true;
  if (flick) {
    if (ttTilt()) { ttDoHold(now); if (ttSt != TT_PLAY) return true; }
    else { ttHardDrop(now); return true; }
  }
  if (in.tap) { ttDoHold(now); if (ttSt != TT_PLAY) return true; }

  // ---- gravity + lock delay ----
  bool soft = ty > 0.30f;
  uint32_t iv = TT_LVL_MS[constrain(ttLevel - 1, 0, 14)];
  if (soft && iv > 45) iv = 45;
  bool ground = ttOnGround();
  if (ground) { if (!ttLockAt) ttLockAt = now; }
  else ttLockAt = 0;
  if (!ground && now - ttLastFall >= iv) {
    ttPy++;
    if (soft) ttScore++;
    ttLastFall = now;
  } else if (ground) ttLastFall = now;
  if (ttLockAt && now - ttLockAt >= 500) { ttLock(now); return true; }

  if (now - ttLastDraw >= 33) { ttLastDraw = now; ttDraw(now); }
  return true;
}

Game GAME_TETRIS = {"TETRIS", "stack, clear lines, hold & ghost", 0xA81F, ttOpts, 1, ttStart, ttFrame};
