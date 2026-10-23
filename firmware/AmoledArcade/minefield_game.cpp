// minefield_game.cpp - Minefield, Minesweeper with a tilt cursor and neon numerals.
//   TILT (or the analog stick) steps the cursor cell by cell (hold = repeat)
//   RIGHT = reveal the cell    LEFT tap = flag / unflag    LEFT hold = chord (on a number: open its neighbours when
//   the flags match it)
//   TOUCH: tap = reveal (on a revealed number: chord), long press = flag
// The first reveal is always safe (the mines are placed around it afterwards); zeros flood open. The timer starts at the
// first reveal. SIZE = SMALL 9x9 / MEDIUM 16x12 / LARGE 24x18, SPEED = cursor repeat rate. Best time per size is saved.
#include "engine.h"
#include <math.h>

static const char *const MF_SIZES[] = {"MEDIUM", "SMALL", "LARGE"};
static const char *const MF_SPEED[] = {"NORMAL", "SLOW", "FAST"};
static Option mfOpts[] = {{"SIZE", MF_SIZES, 3, "mfSize", 0}, {"SPEED", MF_SPEED, 3, "mfSpeed", 0}};

enum MfSt : uint8_t { MF_INTRO, MF_PLAY, MF_WIN, MF_LOSE };
enum : uint8_t { MFC_HID = 0, MFC_REV = 1, MFC_FLAG = 2 };

static constexpr int MF_MAXC = 24, MF_MAXR = 18;
static const uint16_t MF_NUM[9] = {0, rgb(90, 150, 255), rgb(60, 230, 110), rgb(255, 80, 90), rgb(190, 110, 255),
                                   rgb(255, 160, 50), rgb(60, 230, 230), rgb(255, 255, 255), rgb(150, 150, 170)};

static uint8_t mfMine[MF_MAXR][MF_MAXC], mfAdj[MF_MAXR][MF_MAXC], mfCell[MF_MAXR][MF_MAXC], mfShown[MF_MAXR][MF_MAXC];
static uint16_t mfStack[MF_MAXC * MF_MAXR];
static MfSt mfSt;
static int mfCols, mfRows, mfMines, mfCs, mfBx, mfBy, mfCx, mfCy, mfRevealed, mfFlags, mfSize, mfHitX, mfHitY, mfBoomR;
static int mfTouchCx, mfTouchCy;
static bool mfPlaced, mfNewBest, mfLeftActed, mfTouchActed;
static uint32_t mfElapsed, mfBestDs, mfStAt, mfLastT, mfLastRender, mfLeftAt, mfTouchAt, mfNextRep, mfIgnoreTap, mfBoomAt;
static uint32_t mfRepFirst, mfRepNext;
static SnapDir mfSnap;
static uint8_t mfSnapPrev;
static char mfKey[10];

static void mfLayout() {
  mfBx = (W - mfCols * mfCs) / 2;
  mfBy = 32 + (H - 32 - mfRows * mfCs) / 2;
}

static void mfStart() {
  mfSize = mfOpts[0].value;                            // 0 medium, 1 small, 2 large
  if (mfSize == 1) { mfCols = 9; mfRows = 9; mfMines = 10; mfCs = 36; }
  else if (mfSize == 2) { mfCols = 24; mfRows = 18; mfMines = 60; mfCs = 18; }
  else { mfCols = 16; mfRows = 12; mfMines = 30; mfCs = 28; }
  mfRepFirst = mfOpts[1].value == 1 ? 340 : mfOpts[1].value == 2 ? 220 : 280;
  mfRepNext = mfOpts[1].value == 1 ? 190 : mfOpts[1].value == 2 ? 95 : 140;
  mfLayout();
  snprintf(mfKey, sizeof mfKey, "mfBest%d", mfSize);
  uint32_t v = loadBest(mfKey);
  mfBestDs = v ? 100000 - v : 0;
  memset(mfMine, 0, sizeof mfMine); memset(mfAdj, 0, sizeof mfAdj);
  memset(mfCell, 0, sizeof mfCell); memset(mfShown, 0, sizeof mfShown);
  mfPlaced = false; mfRevealed = 0; mfFlags = 0; mfElapsed = 0; mfNewBest = false;
  mfCx = mfCols / 2; mfCy = mfRows / 2;
  mfLeftActed = true; mfTouchActed = true; mfIgnoreTap = 0; mfSnapPrev = DIR_NONE; mfNextRep = 0; mfBoomR = 0;
  ptClear();
  uint32_t now = millis();
  mfSt = MF_INTRO; mfStAt = now; mfLastT = now; mfLastRender = 0;
  sfxStart();
}

static void mfPlaceMines(int sx, int sy) {
  int n = 0;
  while (n < mfMines) {
    int x = (int)(esp_random() % (uint32_t)mfCols), y = (int)(esp_random() % (uint32_t)mfRows);
    if (mfMine[y][x] || (abs(x - sx) <= 1 && abs(y - sy) <= 1)) continue;
    mfMine[y][x] = 1; n++;
  }
  for (int y = 0; y < mfRows; y++)
    for (int x = 0; x < mfCols; x++) {
      int c = 0;
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          int nx = x + dx, ny = y + dy;
          if ((dx || dy) && nx >= 0 && ny >= 0 && nx < mfCols && ny < mfRows) c += mfMine[ny][nx];
        }
      mfAdj[y][x] = (uint8_t)c;
    }
  mfPlaced = true;
}

static void mfCellCenter(int x, int y, float &px, float &py) { px = mfBx + x * mfCs + mfCs / 2.0f; py = mfBy + y * mfCs + mfCs / 2.0f; }

static void mfLose(int x, int y, uint32_t now) {
  mfSt = MF_LOSE; mfStAt = now; mfHitX = x; mfHitY = y; mfBoomR = 0; mfBoomAt = now;
  float px, py; mfCellCenter(x, y, px, py);
  ptBurst(px, py, 40, 220, rgb(255, 240, 160), rgb(255, 40, 20), 0.9f, 0, 3);
  play(160, 80); play(110, 120); play(70, 200);
}

static void mfWin(uint32_t now) {
  mfSt = MF_WIN; mfStAt = now;
  uint32_t ds = mfElapsed / 100;
  if (ds > 99999) ds = 99999;
  mfNewBest = saveBest(mfKey, 100000 - ds);
  if (mfNewBest) mfBestDs = ds;
  for (int i = 0; i < 4; i++)
    ptBurst((float)(60 + esp_random() % (W - 120)), (float)(80 + esp_random() % 200), 18, 160, rgb(255, 240, 120), rgb(60, 220, 160), 1.0f, 60);
  tunePlay(TUNE_WIN);
}

static void mfFlood(int sx, int sy) {
  int sp = 0;
  mfStack[sp++] = (uint16_t)(sy * mfCols + sx);
  mfCell[sy][sx] = MFC_REV; mfRevealed++;
  while (sp > 0) {
    int idx = mfStack[--sp], x = idx % mfCols, y = idx / mfCols;
    if (mfAdj[y][x] != 0) continue;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= mfCols || ny >= mfRows) continue;
        if (mfCell[ny][nx] != MFC_HID) continue;
        mfCell[ny][nx] = MFC_REV; mfRevealed++;
        mfStack[sp++] = (uint16_t)(ny * mfCols + nx);
      }
  }
}

static void mfReveal(int x, int y, uint32_t now) {
  if (mfCell[y][x] != MFC_HID) return;
  if (!mfPlaced) mfPlaceMines(x, y);
  if (mfMine[y][x]) { mfLose(x, y, now); return; }
  int before = mfRevealed;
  mfFlood(x, y);
  int got = mfRevealed - before;
  play((uint16_t)(420 + min(got, 14) * 25), 16);
  if (mfRevealed >= mfCols * mfRows - mfMines) mfWin(now);
}

static void mfFlag(int x, int y) {
  if (mfCell[y][x] == MFC_REV) return;
  if (mfCell[y][x] == MFC_FLAG) { mfCell[y][x] = MFC_HID; mfFlags--; play(300, 20); }
  else { mfCell[y][x] = MFC_FLAG; mfFlags++; play(700, 20); }
}

static void mfChord(int x, int y, uint32_t now) {
  if (mfCell[y][x] != MFC_REV || mfAdj[y][x] == 0) return;
  int f = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx, ny = y + dy;
      if ((dx || dy) && nx >= 0 && ny >= 0 && nx < mfCols && ny < mfRows && mfCell[ny][nx] == MFC_FLAG) f++;
    }
  if (f != mfAdj[y][x]) { play(150, 30); return; }
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      int nx = x + dx, ny = y + dy;
      if ((dx || dy) && nx >= 0 && ny >= 0 && nx < mfCols && ny < mfRows && mfCell[ny][nx] == MFC_HID) {
        mfReveal(nx, ny, now);
        if (mfSt != MF_PLAY) return;
      }
    }
}

// ---------------- drawing ----------------
static void mfDrawMine(int cx, int cy, int r, uint16_t c) {
  gfx->fillCircle(cx, cy, r, c);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.7853982f;
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * (r + 3)), cy + (int)(sinf(a) * (r + 3)), c);
  }
  gfx->fillCircle(cx - r / 3, cy - r / 3, max(1, r / 4), C_TEXT);
}

static void mfDrawCell(int x, int y, uint32_t now) {
  int px = mfBx + x * mfCs, py = mfBy + y * mfCs, s = mfCs - 2;
  int cx = px + mfCs / 2, cy = py + mfCs / 2;
  uint8_t st = mfCell[y][x];
  bool reveal = st == MFC_REV;
  bool showMine = (mfSt == MF_LOSE && mfMine[y][x] && mfShown[y][x]) || (mfSt == MF_WIN && mfMine[y][x]);
  if (reveal || showMine) {
    gfx->fillRect(px + 1, py + 1, s, s, rgb(8, 10, 22));
    gfx->drawRect(px + 1, py + 1, s, s, rgb(22, 26, 48));
    if (showMine && st != MFC_FLAG) {
      bool hit = x == mfHitX && y == mfHitY && mfSt == MF_LOSE;
      if (hit) gfx->fillRect(px + 1, py + 1, s, s, rgb(120, 20, 20));
      mfDrawMine(cx, cy, mfCs / 5 + 1, hit ? rgb(255, 120, 90) : rgb(255, 70, 110));
    } else if (reveal && mfAdj[y][x]) {
      uint16_t c = MF_NUM[mfAdj[y][x]];
      char b[2] = {(char)('0' + mfAdj[y][x]), 0};
      int ts = mfCs >= 34 ? 3 : mfCs >= 26 ? 2 : 1;
      gfx->fillRect(px + 2, py + 2, s - 2, s - 2, lerpColor(c, C_BG, 0.93f));
      text(b, cx - 3 * ts, cy - 4 * ts, ts, c);
      if (ts == 1) text(b, cx - 2, cy - 4, 1, c);
    }
  } else {
    uint16_t base = rgb(24, 40, 96), edge = rgb(70, 110, 230);
    gfx->fillRect(px + 1, py + 1, s, s, lerpColor(base, C_BG, 0.3f));
    gfx->drawRect(px + 1, py + 1, s, s, edge);
    gfx->drawFastHLine(px + 2, py + 2, s - 2, lerpColor(edge, C_TEXT, 0.35f));
    gfx->drawFastVLine(px + 2, py + 2, s - 2, lerpColor(edge, C_TEXT, 0.2f));
    if (st == MFC_FLAG) {
      bool wrong = mfSt == MF_LOSE && mfShown[y][x] && !mfMine[y][x];
      int u = max(2, mfCs / 6);
      gfx->drawFastVLine(cx - u / 2, cy - 3 * u, 6 * u, C_TEXT);
      gfx->fillTriangle(cx - u / 2, cy - 3 * u, cx + 3 * u, cy - 3 * u / 2, cx - u / 2, cy, wrong ? C_ACC : rgb(255, 60, 90));
      gfx->fillRect(cx - 2 * u, cy + 3 * u - 1, 4 * u, 2, rgb(200, 200, 220));
      if (wrong) { gfx->drawLine(px + 3, py + 3, px + s - 2, py + s - 2, C_BAD); gfx->drawLine(px + s - 2, py + 3, px + 3, py + s - 2, C_BAD); }
    }
  }
  (void)now;
}

static void mfRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  for (int y = 0; y < mfRows; y++)
    for (int x = 0; x < mfCols; x++) mfDrawCell(x, y, now);
  // cursor
  if (mfSt == MF_PLAY || mfSt == MF_INTRO) {
    float p = 0.5f + 0.5f * sinf(now * 0.008f);
    int px = mfBx + mfCx * mfCs, py = mfBy + mfCy * mfCs;
    uint16_t c = lerpColor(rgb(255, 200, 40), rgb(255, 250, 190), p);
    gfx->drawRect(px, py, mfCs, mfCs, c);
    gfx->drawRect(px + 1, py + 1, mfCs - 2, mfCs - 2, c);
    gfx->drawRect(px - 1, py - 1, mfCs + 2, mfCs + 2, lerpColor(c, C_BG, 0.75f));
  }
  ptDraw();
  // HUD
  gfx->fillRect(0, 0, W, 32, rgb(8, 10, 30));
  char b[32];
  snprintf(b, sizeof b, "M %d", mfMines - mfFlags);
  text(b, 10, 9, 2, rgb(255, 80, 110));
  uint32_t s = mfElapsed / 1000;
  snprintf(b, sizeof b, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  text(b, 140, 9, 2, C_TEXT);
  if (mfBestDs) snprintf(b, sizeof b, "BEST %lu.%lu", (unsigned long)(mfBestDs / 10), (unsigned long)(mfBestDs % 10));
  else snprintf(b, sizeof b, "BEST --");
  text(b, 250, 9, 2, C_DIM);
  int bat = battery();
  if (bat >= 0) { snprintf(b, sizeof b, "%3d%%", bat); text(b, W - 64, 9, 2, C_DIM); }
  if (mfSt == MF_INTRO) {
    gfx->fillRoundRect(40, 110, W - 80, 148, 16, C_PANEL);
    gfx->drawRoundRect(40, 110, W - 80, 148, 16, rgb(255, 80, 110));
    centerText("MINEFIELD", 120, 3, rgb(255, 80, 110));
    centerText("tilt = cursor   RIGHT = reveal", 164, 1, C_TEXT);
    centerText("LEFT = flag   hold LEFT = chord", 180, 1, C_TEXT);
    centerText("touch: tap = reveal, hold = flag", 196, 1, C_DIM);
    int left = 3 - (int)((now - mfStAt) / 700);
    if (left < 1) left = 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 214, 3, C_ACC);
  } else if (mfSt == MF_WIN && now - mfStAt > 600) {
    char l2[32];
    snprintf(l2, sizeof l2, "%lu.%lu s", (unsigned long)(mfElapsed / 1000), (unsigned long)((mfElapsed / 100) % 10));
    panelBox(mfNewBest ? "NEW BEST" : "CLEARED", l2, "RIGHT again  LEFT menu", C_GOOD);
  } else if (mfSt == MF_LOSE && now - mfStAt > 1500) {
    char l2[32];
    snprintf(l2, sizeof l2, "%d/%d opened", mfRevealed, mfCols * mfRows - mfMines);
    panelBox("BOOM", l2, "RIGHT again  LEFT menu", C_BAD);
  }
  dirty = true;
}

static void mfMoveCursor(int dx, int dy) {
  mfCx = constrain(mfCx + dx, 0, mfCols - 1);
  mfCy = constrain(mfCy + dy, 0, mfRows - 1);
}

static bool mfCellAt(int px, int py, int &cx, int &cy) {
  int x = (px - mfBx), y = (py - mfBy);
  if (x < 0 || y < 0) return false;
  cx = x / mfCs; cy = y / mfCs;
  return cx < mfCols && cy < mfRows;
}

static bool mfFrame(uint32_t now) {
  homeLock = true;                                         // LEFT/RIGHT presses are rapid and constant here
  uint32_t dtms = min<uint32_t>(now - mfLastT, 100); mfLastT = now;
  tuneUpdate(now);
  ptUpdate(dtms / 1000.0f);
  switch (mfSt) {
    case MF_INTRO:
      if (now - mfStAt >= 2200) { mfSt = MF_PLAY; imuZero(); mfSnap.cur = DIR_NONE; }
      break;
    case MF_PLAY: {
      if (mfPlaced) mfElapsed += dtms;
      // tilt cursor
      uint8_t d = mfSnap.update(axisX(), axisY(), 0.24f, 0.13f);
      if (d != DIR_NONE) {
        if (d != mfSnapPrev) { mfNextRep = now + mfRepFirst; mfMoveCursor(d == DIR_L ? -1 : d == DIR_R ? 1 : 0, d == DIR_U ? -1 : d == DIR_D ? 1 : 0); }
        else if ((int32_t)(now - mfNextRep) >= 0) { mfNextRep = now + mfRepNext; mfMoveCursor(d == DIR_L ? -1 : d == DIR_R ? 1 : 0, d == DIR_U ? -1 : d == DIR_D ? 1 : 0); }
      }
      mfSnapPrev = d;
      // buttons
      if (in.right) mfReveal(mfCx, mfCy, now);
      if (mfSt != MF_PLAY) break;
      if (in.left) { mfLeftAt = now; mfLeftActed = false; }
      if (!mfLeftActed) {
        if (in.leftDown && now - mfLeftAt >= 450) { mfLeftActed = true; mfChord(mfCx, mfCy, now); play(500, 25); }
        else if (!in.leftDown) { mfLeftActed = true; mfFlag(mfCx, mfCy); }
      }
      if (mfSt != MF_PLAY) break;
      // touch
      if (in.touching && !in.dragging) {
        int cx, cy;
        if (mfTouchActed) {                                     // a new touch starts
          mfTouchActed = false; mfTouchAt = now;
          if (mfCellAt(in.touchX, in.touchY, cx, cy)) { mfTouchCx = cx; mfTouchCy = cy; mfCx = cx; mfCy = cy; }
          else mfTouchCx = mfTouchCy = -1;
        } else if (mfTouchCx >= 0 && now - mfTouchAt >= 450 && mfTouchAt) {
          mfTouchAt = 0; mfIgnoreTap = now + 600;
          mfFlag(mfTouchCx, mfTouchCy);
        }
      }
      if (!in.touching) mfTouchActed = true;
      if (in.tap && (int32_t)(now - mfIgnoreTap) >= 0) {
        int cx, cy;
        if (mfCellAt(in.tapX, in.tapY, cx, cy)) {
          mfCx = cx; mfCy = cy;
          if (mfCell[cy][cx] == MFC_REV) mfChord(cx, cy, now); else mfReveal(cx, cy, now);
        }
      }
      break;
    }
    case MF_LOSE:
      if ((int32_t)(now - mfBoomAt) >= 0 && mfBoomR <= MF_MAXC + 1) {
        mfBoomAt = now + 45;
        mfBoomR++;
        for (int y = 0; y < mfRows; y++)
          for (int x = 0; x < mfCols; x++)
            if (!mfShown[y][x] && max(abs(x - mfHitX), abs(y - mfHitY)) <= mfBoomR) {
              mfShown[y][x] = 1;
              if (mfMine[y][x] && mfCell[y][x] != MFC_FLAG && !(x == mfHitX && y == mfHitY)) {
                float px, py; mfCellCenter(x, y, px, py);
                ptBurst(px, py, 6, 90, rgb(255, 220, 120), rgb(255, 50, 30), 0.5f);
                play((uint16_t)(220 - mfBoomR * 4), 18);
              }
            }
      }
      if (now - mfStAt > 1500) {
        if (in.right) { mfStart(); return true; }
        if (in.left) return false;
      }
      break;
    case MF_WIN:
      if (now - mfStAt > 900) {
        if (in.right) { mfStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - mfLastRender >= 30) { mfLastRender = now; mfRender(now); }
  return true;
}

Game GAME_MINEFIELD = {"Minefield", "minesweeper, tilt or touch", 0xF9E6, mfOpts, 2, mfStart, mfFrame};
