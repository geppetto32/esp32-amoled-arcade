// game2048.cpp - 2048 with a tilt-flick gesture.
//   CONTROL = FLICK: flick the board in a direction (a quick tilt past the threshold, then back) = slide the tiles.
//                    A slow baseline follows a held tilt, so only quick changes count; re-arms when the tilt settles.
//   CONTROL = LEAN : lean the board past the threshold = one slide; keep leaning = repeats; return to level to re-arm.
//   TAP the screen = slide away from the board centre toward the tap (touch fallback).
//   LEFT = undo (up to 20 moves)   RIGHT = restart (press twice within 2 s)
// SIZE: 4X4 (classic, goal 2048), 3X3 (goal 256) or 5X5 (goal 8192). SENSE: flick threshold MEDIUM / LOW / HIGH.
// Neon tile gradient 2..2048+, slide + pop animation, rising blip on a merge (pitch follows the tile).
#include "engine.h"
#include <math.h>

static const char *const TF_SIZES[] = {"4X4", "3X3", "5X5"};
static const char *const TF_CTRLS[] = {"FLICK", "LEAN"};
static const char *const TF_SENSE[] = {"MEDIUM", "LOW", "HIGH"};
static Option tfOpts[] = {{"SIZE", TF_SIZES, 3, "tfSize", 0}, {"CONTROL", TF_CTRLS, 2, "tfCtrl", 0}, {"SENSE", TF_SENSE, 3, "tfSense", 0}};

enum TfSt : uint8_t { TF_INTRO, TF_PLAY, TF_OVER };

static constexpr int TF_MAXN = 5, TF_UNDO = 20, TF_SLIDE_MS = 120, TF_POP_MS = 170;
static const uint16_t TF_COL[13] = {
  rgb(30, 34, 56),                                                                      // 0 (unused)
  rgb(90, 160, 255), rgb(60, 200, 255), rgb(40, 230, 200), rgb(60, 240, 120), rgb(170, 240, 60), rgb(255, 230, 40),
  rgb(255, 170, 40), rgb(255, 110, 60), rgb(255, 60, 100), rgb(230, 60, 220), rgb(160, 90, 255), rgb(255, 255, 255)};

struct TfSlide { int8_t fx, fy, tx, ty; uint8_t e; };
struct TfState { uint8_t g[TF_MAXN][TF_MAXN]; uint32_t score; };

static uint8_t tfG[TF_MAXN][TF_MAXN];
static uint32_t tfPopAt[TF_MAXN][TF_MAXN];
static TfSlide tfSl[TF_MAXN * TF_MAXN];
static int tfNSl;
static TfState tfUndo[TF_UNDO];
static int tfNUndo;
static TfSt tfSt;
static int tfN, tfCell, tfGap = 6, tfBx, tfBy, tfBoard, tfMoves, tfWinExp, tfMaxExp;
static uint32_t tfScore, tfBest, tfBestTile, tfStAt, tfLastT, tfLastRender, tfSlideEnd, tfRestartAt;
static bool tfWon, tfNewBest, tfFlickMode;
static int8_t tfPending;
static uint32_t tfLeanAt;
static float tfTh;
static Flick tfFlick;
static SnapDir tfSnap;
static uint8_t tfSnapPrev;
static int tfWinBanner;

static char tfBestKey[8], tfTileKey[8];

static int tfCellPx(int i) { return tfBx + tfGap + i * (tfCell + tfGap); }
static int tfCellPy(int i) { return tfBy + tfGap + i * (tfCell + tfGap); }

static bool tfSpawn(uint32_t now) {
  int cnt = 0, xs[25], ys[25];
  for (int y = 0; y < tfN; y++) for (int x = 0; x < tfN; x++) if (!tfG[y][x]) { xs[cnt] = x; ys[cnt] = y; cnt++; }
  if (!cnt) return false;
  int k = (int)(esp_random() % (uint32_t)cnt);
  tfG[ys[k]][xs[k]] = (esp_random() % 10 == 0) ? 2 : 1;
  tfPopAt[ys[k]][xs[k]] = now;
  return true;
}
static bool tfCanMove() {
  for (int y = 0; y < tfN; y++)
    for (int x = 0; x < tfN; x++) {
      if (!tfG[y][x]) return true;
      if (x + 1 < tfN && tfG[y][x + 1] == tfG[y][x]) return true;
      if (y + 1 < tfN && tfG[y + 1][x] == tfG[y][x]) return true;
    }
  return false;
}
static int tfMax() { int m = 0; for (int y = 0; y < tfN; y++) for (int x = 0; x < tfN; x++) m = max(m, (int)tfG[y][x]); return m; }

static void tfSaveUndo() {
  if (tfNUndo == TF_UNDO) { for (int i = 1; i < TF_UNDO; i++) tfUndo[i - 1] = tfUndo[i]; tfNUndo--; }
  memcpy(tfUndo[tfNUndo].g, tfG, sizeof tfG);
  tfUndo[tfNUndo].score = tfScore;
  tfNUndo++;
}

// dir: 0 L, 1 R, 2 U, 3 D. Returns true when anything moved.
static bool tfMove(int dir, uint32_t now) {
  uint8_t ng[TF_MAXN][TF_MAXN] = {};
  bool moved = false, merged = false;
  int topMerge = 0;
  uint32_t gained = 0;
  tfNSl = 0;
  uint32_t popTime = now + TF_SLIDE_MS;
  for (int line = 0; line < tfN; line++) {
    int out = 0;
    bool mergedAt[TF_MAXN] = {};
    for (int k = 0; k < tfN; k++) {
      int x, y;
      switch (dir) {
        case 0: x = k; y = line; break;
        case 1: x = tfN - 1 - k; y = line; break;
        case 2: x = line; y = k; break;
        default: x = line; y = tfN - 1 - k; break;
      }
      uint8_t e = tfG[y][x];
      if (!e) continue;
      auto dest = [&](int o, int &dx, int &dy) {
        switch (dir) {
          case 0: dx = o; dy = line; break;
          case 1: dx = tfN - 1 - o; dy = line; break;
          case 2: dx = line; dy = o; break;
          default: dx = line; dy = tfN - 1 - o; break;
        }
      };
      int dx, dy;
      if (out > 0) {
        dest(out - 1, dx, dy);
        if (ng[dy][dx] == e && !mergedAt[out - 1]) {
          ng[dy][dx] = e + 1; mergedAt[out - 1] = true;
          gained += 1u << (e + 1); merged = true; topMerge = max(topMerge, (int)e + 1);
          tfSl[tfNSl++] = {(int8_t)x, (int8_t)y, (int8_t)dx, (int8_t)dy, e};
          tfPopAt[dy][dx] = popTime;
          moved = true;
          continue;
        }
      }
      dest(out, dx, dy);
      ng[dy][dx] = e;
      tfSl[tfNSl++] = {(int8_t)x, (int8_t)y, (int8_t)dx, (int8_t)dy, e};
      if (dx != x || dy != y) moved = true;
      out++;
    }
  }
  if (!moved) return false;
  tfSaveUndo();
  memcpy(tfG, ng, sizeof tfG);
  tfScore += gained;
  tfMoves++;
  tfSlideEnd = now + TF_SLIDE_MS;
  tfSpawn(popTime);
  if (tfScore > tfBest) tfBest = tfScore;
  tfMaxExp = tfMax();
  if (merged) { play((uint16_t)(330 + topMerge * 75), 38); if (topMerge >= 6) play((uint16_t)(660 + topMerge * 75), 50); }
  else play(240, 12);
  return true;
}

// ---------------- drawing ----------------
static uint16_t tfColor(int e) { return TF_COL[min(e, 12)]; }

static void tfDrawTile(float cx, float cy, int e, float scale) {
  if (scale <= 0.02f) return;
  uint16_t col = tfColor(e);
  int s = (int)(tfCell * scale);
  if (s < 4) return;
  int x = (int)(cx - s / 2.0f), y = (int)(cy - s / 2.0f), r = max(4, s / 7);
  gfx->fillRoundRect(x - 3, y - 3, s + 6, s + 6, r + 2, lerpColor(col, C_BG, 0.82f));     // glow layers
  gfx->fillRoundRect(x - 1, y - 1, s + 2, s + 2, r + 1, lerpColor(col, C_BG, 0.55f));
  gfx->fillRoundRect(x, y, s, s, r, lerpColor(col, C_BG, 0.66f));
  gfx->drawRoundRect(x, y, s, s, r, col);
  gfx->drawRoundRect(x + 1, y + 1, s - 2, s - 2, r - 1, lerpColor(col, C_BG, 0.4f));
  if (scale < 0.6f) return;
  char b[12];
  snprintf(b, sizeof b, "%lu", 1ul << e);
  int len = (int)strlen(b), ts = 4;
  while (ts > 1 && len * 6 * ts > s - 10) ts--;
  uint16_t tc = lerpColor(col, C_TEXT, 0.78f);
  text(b, (int)cx - len * 3 * ts, (int)cy - 4 * ts, ts, tc);
}

static void tfRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  hudBar(tfScore, tfBest, rgb(10, 12, 30), C_TEXT);
  // board
  gfx->fillRoundRect(tfBx, tfBy, tfBoard, tfBoard, 10, rgb(12, 14, 28));
  gfx->drawRoundRect(tfBx, tfBy, tfBoard, tfBoard, 10, rgb(50, 60, 120));
  for (int y = 0; y < tfN; y++)
    for (int x = 0; x < tfN; x++) gfx->fillRoundRect(tfCellPx(x), tfCellPy(y), tfCell, tfCell, max(4, tfCell / 7), rgb(22, 26, 46));
  bool sliding = now < tfSlideEnd;
  if (sliding) {
    float t = 1.0f - (float)(tfSlideEnd - now) / TF_SLIDE_MS;
    t = 1.0f - (1.0f - t) * (1.0f - t);
    for (int i = 0; i < tfNSl; i++) {
      const TfSlide &s = tfSl[i];
      float fx = tfCellPx(s.fx) + (tfCellPx(s.tx) - tfCellPx(s.fx)) * t, fy = tfCellPy(s.fy) + (tfCellPy(s.ty) - tfCellPy(s.fy)) * t;
      tfDrawTile(fx + tfCell / 2.0f, fy + tfCell / 2.0f, s.e, 1.0f);
    }
  } else {
    for (int y = 0; y < tfN; y++)
      for (int x = 0; x < tfN; x++) {
        if (!tfG[y][x]) continue;
        float sc = 1.0f;
        int32_t age = (int32_t)(now - tfPopAt[y][x]);
        if (age < 0) continue;                                    // spawn waits for the slide
        if (age < TF_POP_MS) {
          float u = age / (float)TF_POP_MS;
          sc = tfPopAt[y][x] ? (u < 0.6f ? 0.3f + 0.95f * (u / 0.6f) : 1.25f - 0.25f * ((u - 0.6f) / 0.4f)) : 1.0f;
        }
        tfDrawTile(tfCellPx(x) + tfCell / 2.0f, tfCellPy(y) + tfCell / 2.0f, tfG[y][x], sc);
      }
  }
  // side panel
  int px = tfBx + tfBoard + 12;
  if (px < W - 20) {
    char b[24];
    text("BEST TILE", px, 48, 1, C_DIM);
    snprintf(b, sizeof b, "%lu", tfBestTile ? tfBestTile : 0ul);
    text(b, px, 60, 2, C_ACC);
    text("TILE", px, 92, 1, C_DIM);
    snprintf(b, sizeof b, "%lu", tfMaxExp ? (1ul << tfMaxExp) : 0ul);
    text(b, px, 104, 2, tfColor(tfMaxExp));
    text("MOVES", px, 136, 1, C_DIM);
    snprintf(b, sizeof b, "%d", tfMoves);
    text(b, px, 148, 2, C_TEXT);
    text("LEFT", px, 214, 1, C_ACC);
    text("undo", px, 226, 1, C_DIM);
    snprintf(b, sizeof b, "%d", tfNUndo);
    text(b, px + 38, 226, 1, C_DIM);
    text("RIGHT", px, 252, 1, C_ACC);
    text(tfRestartAt && now - tfRestartAt < 2000 ? "again?" : "restart", px, 264, 1, tfRestartAt && now - tfRestartAt < 2000 ? C_BAD : C_DIM);
    text(tfFlickMode ? "flick to" : "lean to", px, 296, 1, C_DIM);
    text("slide", px, 308, 1, C_DIM);
  }
  uint16_t acc = C_ACC;
  if (tfSt == TF_INTRO) {
    gfx->fillRoundRect(40, 120, W - 80, 116, 16, C_PANEL);
    gfx->drawRoundRect(40, 120, W - 80, 116, 16, acc);
    centerText("2048", 132, 4, acc);
    centerText(tfFlickMode ? "FLICK the board to slide tiles" : "LEAN the board to slide tiles", 178, 1, C_TEXT);
    centerText("LEFT undo   RIGHT restart   TAP = slide", 196, 1, C_DIM);
    char b[32];
    snprintf(b, sizeof b, "make %lu", 1ul << tfWinExp);
    centerText(b, 212, 1, C_DIM);
  } else if (tfSt == TF_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, tfNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)tfScore);
    panelBox("NO MOVES", l2, "RIGHT again   LEFT menu", acc);
  } else if (tfWinBanner > 0 && now - tfStAt < 2200) {
    panelBox("YOU DID IT", "keep going", nullptr, acc);
  }
  dirty = true;
}

// ---------------- flow ----------------
static void tfStart() {
  tfN = tfOpts[0].value == 1 ? 3 : tfOpts[0].value == 2 ? 5 : 4;
  tfFlickMode = tfOpts[1].value == 0;
  tfTh = tfOpts[2].value == 1 ? 0.5f : tfOpts[2].value == 2 ? 0.28f : 0.38f;
  tfWinExp = tfN == 3 ? 8 : tfN == 5 ? 13 : 11;
  snprintf(tfBestKey, sizeof tfBestKey, "tfBest%d", tfN);
  snprintf(tfTileKey, sizeof tfTileKey, "tfTile%d", tfN);
  tfBest = loadBest(tfBestKey);
  tfBestTile = loadBest(tfTileKey);
  tfCell = (326 - tfGap * (tfN + 1)) / tfN;
  tfBoard = tfN * tfCell + (tfN + 1) * tfGap;
  tfBx = 10; tfBy = 36 + (330 - tfBoard) / 2;
  memset(tfG, 0, sizeof tfG);
  memset(tfPopAt, 0, sizeof tfPopAt);
  tfNUndo = 0; tfNSl = 0; tfScore = 0; tfMoves = 0; tfMaxExp = 0; tfWon = false; tfWinBanner = 0; tfNewBest = false;
  tfPending = -1; tfRestartAt = 0; tfSlideEnd = 0; tfSnapPrev = DIR_NONE; tfLeanAt = 0;
  uint32_t now = millis();
  tfSpawn(now); tfSpawn(now);
  tfMaxExp = tfMax();
  tfSt = TF_INTRO; tfStAt = now; tfLastT = now; tfLastRender = 0;
  sfxStart();
}

static void tfFinish(uint32_t now) {
  tfSt = TF_OVER; tfStAt = now;
  tfNewBest = saveBest(tfBestKey, tfScore);
  uint32_t bt = tfMaxExp ? (1u << tfMaxExp) : 0;
  if (bt > tfBestTile) { tfBestTile = bt; prefs.putUInt(tfTileKey, bt); }
  if (tfNewBest) { tfBest = tfScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
}

static int tfDirOf(uint8_t d) { return d == DIR_L ? 0 : d == DIR_R ? 1 : d == DIR_U ? 2 : 3; }

static bool tfFrame(uint32_t now) {
  tfLastT = now;
  tuneUpdate(now);
  switch (tfSt) {
    case TF_INTRO:
      if (now - tfStAt >= 1800) { tfSt = TF_PLAY; imuZero(); tfFlick.reset(now); tfSnapPrev = DIR_NONE; }
      break;
    case TF_PLAY: {
      int dir = -1;
      if (imuOk) {
        if (tfFlickMode) {
          uint8_t d = tfFlick.update(now, tfTh);
          if (d != DIR_NONE) dir = tfDirOf(d);
        } else {
          uint8_t d = tfSnap.update(axisX(), axisY(), tfTh * 0.7f, tfTh * 0.4f);
          if (d != DIR_NONE && (d != tfSnapPrev || now - tfLeanAt > 420)) { dir = tfDirOf(d); tfLeanAt = now; }
          tfSnapPrev = d;
        }
      }
      if (in.tap) {
        float cx = tfBx + tfBoard / 2.0f, cy = tfBy + tfBoard / 2.0f, dx = in.tapX - cx, dy = in.tapY - cy;
        if (fabsf(dx) > 24 || fabsf(dy) > 24) dir = fabsf(dx) > fabsf(dy) ? (dx > 0 ? 1 : 0) : (dy > 0 ? 3 : 2);
      }
      if (in.dragEnd && (abs(in.dragTX) > 30 || abs(in.dragTY) > 30)) {   // a swipe (a drag never counts as a tap)
        int sx = in.dragTX, sy = in.dragTY;
        dir = abs(sx) > abs(sy) ? (sx > 0 ? 1 : 0) : (sy > 0 ? 3 : 2);
      }
      if (dir >= 0 && now < tfSlideEnd) { tfPending = (int8_t)dir; dir = -1; }
      else if (dir < 0 && tfPending >= 0 && now >= tfSlideEnd) { dir = tfPending; tfPending = -1; }
      if (dir >= 0) {
        tfPending = -1;
        if (tfMove(dir, now)) {
          if (!tfWon && tfMaxExp >= tfWinExp) { tfWon = true; tfWinBanner = 1; tfStAt = now; tunePlay(TUNE_WIN); }
          if (!tfCanMove()) tfFinish(now);
        } else play(120, 25);
      }
      if (in.left && tfNUndo > 0 && now >= tfSlideEnd) {
        tfNUndo--;
        memcpy(tfG, tfUndo[tfNUndo].g, sizeof tfG);
        tfScore = tfUndo[tfNUndo].score;
        tfMaxExp = tfMax();
        memset(tfPopAt, 0, sizeof tfPopAt);
        tfSlideEnd = 0; tfPending = -1;
        sfxClick(); play(500, 30);
      }
      if (in.right) {
        if (tfRestartAt && now - tfRestartAt < 2000) { tfStart(); tfSt = TF_PLAY; imuZero(); tfFlick.reset(now); return true; }
        tfRestartAt = now; sfxClick();
      }
      break;
    }
    case TF_OVER:
      if (now - tfStAt > 900) {
        if (in.right) { tfStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  bool animating = now < tfSlideEnd + TF_POP_MS + 40 || tfSt == TF_INTRO;
  if (now - tfLastRender >= (animating ? 16u : 60u)) { tfLastRender = now; tfRender(now); }
  return true;
}

Game GAME_2048 = {"2048", "flick the board, merge tiles", 0xFE40, tfOpts, 3, tfStart, tfFrame};
