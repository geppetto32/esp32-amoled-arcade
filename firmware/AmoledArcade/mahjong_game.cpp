// mahjong_game.cpp - Mahjong solitaire (Shanghai): 82 tiles in 4 layers, match free pairs.
// Every deal is solvable: the layout is "played backwards" - pairs are assigned to tiles that are free at that
// moment of a simulated clearing, so that clearing order is a solution. SHUFFLE re-deals the remaining tiles
// the same way. A tile is FREE when nothing lies on top and its left or right side is open.
//
// CONTROL = TILT:    tilt left/right = move the cursor through the free tiles (reading order, then HINT/UNDO/SHUFFLE),
//                    tilt toward / away = skim 4 at a time. RIGHT = pick / match, LEFT = drop the pick.
// CONTROL = BUTTONS: LEFT = next free tile, RIGHT = pick / match (pick the same tile again = drop it).
// Touch: tap a free tile or one of the three buttons (alternative; not needed).
// Tiles are drawn with primitives (ivory face, shaded edge, coloured glyphs); no image assets.
#include "game.h"
#include <math.h>

static const char *const MJ_CTRL[] = {"TILT", "BUTTONS"};
static Option mjOpts[] = {{"CONTROL", MJ_CTRL, 2, "mjCtrl", 0}};

static constexpr int MJ_N = 82, MJ_PAIRS = 41, MJ_TYPES = 34;
static constexpr int TW = 36, TH = 44, HALF_X = 18, HALF_Y = 22, X0 = 44, Y0 = 46, DEPTH = 4;
static constexpr int ACT_Y = 318, ACT_H = 30;

struct MjTile { int8_t layer, hx, hy, type; };
static MjTile mjT[MJ_N];
static bool mjGone[MJ_N];
static int mjRemaining;
static int8_t mjUndo[MJ_PAIRS][2];
static int mjNUndo;

static int mjList[MJ_N], mjNList;          // free tiles in reading order
static int mjCurTile = -1, mjCurAct = 0;   // cursor: a tile (>= 0) or an action 0 HINT, 1 UNDO, 2 SHUFFLE
static int mjSel = -1;
static int mjHintA = -1, mjHintB = -1;
static uint32_t mjHintUntil, mjT0, mjEndMs, mjStAt, mjLastDraw, mjRepAt;
static int mjLastDir, mjBest;
static bool mjWon, mjRedraw, mjNoMoves, mjNewBest;
static int mjLastSec;

// ------------------------------------------------------------------ layout + solvable deal
static void mjBuildLayout() {
  int n = 0;
  auto add = [&](int layer, int hx, int hy) { mjT[n].layer = (int8_t)layer; mjT[n].hx = (int8_t)hx; mjT[n].hy = (int8_t)hy; mjT[n].type = 0; n++; };
  static const int w0[6] = {6, 8, 10, 10, 8, 6};
  for (int r = 0; r < 6; r++) for (int k = 0; k < w0[r]; k++) add(0, 10 - w0[r] + 2 * k, r * 2);
  for (int r = 0; r < 4; r++) for (int k = 0; k < 6; k++) add(1, 4 + 2 * k, 2 + r * 2);
  for (int r = 0; r < 2; r++) for (int k = 0; k < 4; k++) add(2, 6 + 2 * k, 4 + r * 2);
  add(3, 8, 5); add(3, 10, 5);
}

static bool mjIsFree(int i, const bool *gone) {
  bool left = false, right = false;
  for (int j = 0; j < MJ_N; j++) {
    if (j == i || gone[j]) continue;
    int dx = mjT[j].hx - mjT[i].hx, dy = mjT[j].hy - mjT[i].hy;
    int ady = dy < 0 ? -dy : dy, adx = dx < 0 ? -dx : dx;
    if (mjT[j].layer == mjT[i].layer + 1 && adx < 2 && ady < 2) return false;
    if (mjT[j].layer == mjT[i].layer && ady < 2) {
      if (dx == -2) left = true;
      if (dx == 2) right = true;
    }
  }
  return !(left && right);
}

static int mjFreeCount(const bool *gone) {
  int c = 0;
  for (int i = 0; i < MJ_N; i++) if (!gone[i] && mjIsFree(i, gone)) c++;
  return c;
}

// Assign the given pair types (nPairs, one per pair) to the tiles that are not yet gone, solvably.
static void mjAssign(const int8_t *pairTypes, int nPairs) {
  static bool gone[MJ_N];
  static int freeL[MJ_N];
  for (int attempt = 0; attempt < 300; attempt++) {
    memcpy(gone, mjGone, sizeof gone);
    bool ok = true;
    for (int p = 0; p < nPairs && ok; p++) {
      int nf = 0;
      for (int i = 0; i < MJ_N; i++) if (!gone[i] && mjIsFree(i, gone)) freeL[nf++] = i;
      bool placed = false;
      for (int tries = 0; tries < 40 && nf >= 2 && !placed; tries++) {
        int a = freeL[esp_random() % nf], b = freeL[esp_random() % nf];
        if (a == b) continue;
        gone[a] = gone[b] = true;
        if (p == nPairs - 1 || mjFreeCount(gone) >= 2) {
          mjT[a].type = mjT[b].type = pairTypes[p];
          placed = true;
        } else gone[a] = gone[b] = false;
      }
      if (!placed) ok = false;
    }
    if (ok) return;
  }
  // (practically unreachable) give up: assign in tile order so the game still runs
  int p = 0;
  for (int i = 0; i + 1 < MJ_N && p < nPairs; i++) if (!mjGone[i]) { mjT[i].type = pairTypes[p]; int j = i + 1; while (j < MJ_N && mjGone[j]) j++; if (j < MJ_N) { mjT[j].type = pairTypes[p]; i = j; } p++; }
}

static void mjShuffleArr(int8_t *a, int n) {
  for (int i = n - 1; i > 0; i--) { int j = esp_random() % (i + 1); int8_t t = a[i]; a[i] = a[j]; a[j] = t; }
}

// ------------------------------------------------------------------ state helpers
static void mjRebuildList() {
  mjNList = 0;
  for (int i = 0; i < MJ_N; i++) if (!mjGone[i] && mjIsFree(i, mjGone)) mjList[mjNList++] = i;
  for (int a = 1; a < mjNList; a++) {                       // insertion sort by (hy, hx, layer)
    int v = mjList[a];
    long key = (long)mjT[v].hy * 1000 + mjT[v].hx * 10 + mjT[v].layer;
    int b = a - 1;
    while (b >= 0 && (long)mjT[mjList[b]].hy * 1000 + mjT[mjList[b]].hx * 10 + mjT[mjList[b]].layer > key) { mjList[b + 1] = mjList[b]; b--; }
    mjList[b + 1] = v;
  }
}
static bool mjFindPair(int &a, int &b) {
  for (int i = 0; i < mjNList; i++)
    for (int j = i + 1; j < mjNList; j++)
      if (mjT[mjList[i]].type == mjT[mjList[j]].type) { a = mjList[i]; b = mjList[j]; return true; }
  return false;
}
static int mjTileX(int i) { return X0 + mjT[i].hx * HALF_X - DEPTH * mjT[i].layer; }
static int mjTileY(int i) { return Y0 + mjT[i].hy * HALF_Y - DEPTH * mjT[i].layer; }

static void mjAfterChange() {                               // list, cursor repair, stuck test
  mjRebuildList();
  if (mjCurTile >= 0) {
    bool inList = false;
    for (int i = 0; i < mjNList; i++) if (mjList[i] == mjCurTile) inList = true;
    if (!inList) {
      int best = -1, bd = 1 << 30, cx = mjCurTile >= 0 ? mjTileX(mjCurTile) : 0, cy = mjCurTile >= 0 ? mjTileY(mjCurTile) : 0;
      for (int i = 0; i < mjNList; i++) {
        int dx = mjTileX(mjList[i]) - cx, dy = mjTileY(mjList[i]) - cy, d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = mjList[i]; }
      }
      mjCurTile = best;
    }
  }
  if (mjCurTile < 0 && mjNList > 0 && mjCurAct < 0) mjCurAct = 0;
  if (mjSel >= 0) { bool ok = false; for (int i = 0; i < mjNList; i++) if (mjList[i] == mjSel) ok = true; if (!ok) mjSel = -1; }
  int a, b;
  mjNoMoves = mjRemaining > 0 && !mjFindPair(a, b);
  if (mjNoMoves) { mjCurTile = -1; mjCurAct = 2; }
  mjRedraw = true;
}

static void mjStart() {
  mjBuildLayout();
  memset(mjGone, 0, sizeof mjGone);
  mjRemaining = MJ_N; mjNUndo = 0;
  int8_t faces[MJ_TYPES];
  for (int i = 0; i < MJ_TYPES; i++) faces[i] = (int8_t)i;
  mjShuffleArr(faces, MJ_TYPES);
  int8_t pairs[MJ_PAIRS];
  int np = 0;
  for (int f = 0; f < 20; f++) { pairs[np++] = faces[f]; pairs[np++] = faces[f]; }
  pairs[np++] = faces[20];
  mjShuffleArr(pairs, MJ_PAIRS);
  mjAssign(pairs, MJ_PAIRS);
  mjSel = mjHintA = mjHintB = -1; mjHintUntil = 0;
  mjWon = false; mjNewBest = false; mjNoMoves = false;
  mjBest = (int)prefs.getUInt("mjBest", 0);
  mjLastDir = 0; mjLastSec = -1;
  mjCurTile = -1; mjCurAct = -1;
  mjRebuildList();
  mjCurTile = mjNList ? mjList[0] : -1;
  mjT0 = millis(); mjLastDraw = 0;
  mjAfterChange();
  gfx->fillScreen(C_BG);
  dirty = true;
}

static void mjRingStep(int d) {
  int p = mjCurTile >= 0 ? -1 : mjNList + mjCurAct;
  if (mjCurTile >= 0) for (int i = 0; i < mjNList; i++) if (mjList[i] == mjCurTile) p = i;
  if (p < 0) p = 0;
  int total = mjNList + 3;
  p = ((p + d) % total + total) % total;
  if (p < mjNList) { mjCurTile = mjList[p]; mjCurAct = -1; }
  else { mjCurTile = -1; mjCurAct = p - mjNList; }
  mjRedraw = true;
}

static void mjRemovePair(int a, int b, uint32_t now) {
  mjGone[a] = mjGone[b] = true;
  mjRemaining -= 2;
  if (mjNUndo < MJ_PAIRS) { mjUndo[mjNUndo][0] = (int8_t)a; mjUndo[mjNUndo][1] = (int8_t)b; mjNUndo++; }
  mjSel = -1; mjHintUntil = 0;
  if (mjRemaining == 0) {
    mjWon = true; mjStAt = now; mjEndMs = now - mjT0;
    int secs = (int)(mjEndMs / 1000);
    if (mjBest == 0 || secs < mjBest) { prefs.putUInt("mjBest", (uint32_t)secs); mjBest = secs; mjNewBest = true; }
    mjNewBest ? sfxBest() : sfxStart();
  } else sfxGood();
  mjAfterChange();
}

static void mjAction(int act, uint32_t now) {
  if (act == 0) {                                           // HINT
    int a, b;
    if (mjFindPair(a, b)) { mjHintA = a; mjHintB = b; mjHintUntil = now + 2500; sfxClick(); }
    else sfxBad();
  } else if (act == 1) {                                    // UNDO
    if (mjNUndo > 0) {
      mjNUndo--;
      mjGone[mjUndo[mjNUndo][0]] = mjGone[mjUndo[mjNUndo][1]] = false;
      mjRemaining += 2; mjSel = -1; mjHintUntil = 0;
      mjCurTile = mjUndo[mjNUndo][0];
      sfxClick();
      mjAfterChange();
    } else sfxBad();
  } else {                                                  // SHUFFLE (solvable re-deal of what is left)
    int cnt[MJ_TYPES] = {0};
    for (int i = 0; i < MJ_N; i++) if (!mjGone[i]) cnt[mjT[i].type]++;
    int8_t pairs[MJ_PAIRS];
    int np = 0;
    for (int t = 0; t < MJ_TYPES; t++) for (int k = 0; k < cnt[t] / 2 && np < MJ_PAIRS; k++) pairs[np++] = (int8_t)t;
    mjShuffleArr(pairs, np);
    mjAssign(pairs, np);
    mjNUndo = 0; mjSel = -1; mjHintUntil = 0;
    play(500, 40); play(700, 40); play(900, 40);
    mjAfterChange();
  }
  mjRedraw = true;
}

static void mjPick(uint32_t now) {                          // RIGHT / tap on the cursor
  if (mjCurTile < 0) { mjAction(mjCurAct < 0 ? 0 : mjCurAct, now); return; }
  if (mjSel < 0) { mjSel = mjCurTile; sfxClick(); mjRedraw = true; return; }
  if (mjSel == mjCurTile) { mjSel = -1; sfxClick(); mjRedraw = true; return; }
  if (mjT[mjSel].type == mjT[mjCurTile].type) { mjRemovePair(mjSel, mjCurTile, now); return; }
  sfxBad();
  mjSel = mjCurTile;
  mjRedraw = true;
}

// ------------------------------------------------------------------ drawing
static void mjGlyphPos(int n, int8_t p[9][2]) {
  static const int8_t t[10][9][2] = {
    {{0, 0}}, {{0, 0}}, {{0, -11}, {0, 11}}, {{-8, -12}, {0, 0}, {8, 12}},
    {{-8, -11}, {8, -11}, {-8, 11}, {8, 11}}, {{-8, -11}, {8, -11}, {0, 0}, {-8, 11}, {8, 11}},
    {{-8, -13}, {8, -13}, {-8, 0}, {8, 0}, {-8, 13}, {8, 13}},
    {{-9, -14}, {0, -14}, {9, -14}, {-8, 0}, {8, 0}, {-8, 14}, {8, 14}},
    {{-8, -15}, {8, -15}, {-8, -5}, {8, -5}, {-8, 5}, {8, 5}, {-8, 15}, {8, 15}},
    {{-9, -13}, {0, -13}, {9, -13}, {-9, 0}, {0, 0}, {9, 0}, {-9, 13}, {0, 13}, {9, 13}}};
  for (int i = 0; i < n; i++) { p[i][0] = t[n][i][0]; p[i][1] = t[n][i][1]; }
}

static void mjGlyph(int type, int cx, int cy) {
  const uint16_t red = rgb(200, 30, 30), grn = rgb(20, 140, 60), blu = rgb(30, 70, 200), ink = rgb(30, 30, 60);
  int8_t p[9][2];
  if (type < 9) {                                           // dots
    int n = type + 1;
    mjGlyphPos(n, p);
    static const uint16_t col[3] = {rgb(30, 70, 200), rgb(200, 30, 30), rgb(20, 140, 60)};
    for (int i = 0; i < n; i++) {
      int r = n == 1 ? 10 : 4;
      uint16_t c = n == 1 ? red : col[i % 3];
      gfx->fillCircle(cx + p[i][0], cy + p[i][1], r, c);
      gfx->drawCircle(cx + p[i][0], cy + p[i][1], r, ink);
      if (n == 1) gfx->fillCircle(cx, cy, 4, rgb(255, 240, 200));
    }
  } else if (type < 18) {                                   // bamboo
    int n = type - 8;
    mjGlyphPos(n, p);
    for (int i = 0; i < n; i++) {
      int w = n == 1 ? 8 : 5, h = n == 1 ? 26 : (n <= 6 ? 11 : 8);
      uint16_t c = (n > 1 && i % 3 == 1 && (n == 5 || n == 7 || n == 9)) ? red : grn;
      int x = cx + p[i][0] - w / 2, y = cy + p[i][1] - h / 2;
      gfx->fillRoundRect(x, y, w, h, 2, c);
      gfx->drawFastHLine(x, y + h / 2, w, rgb(255, 240, 200));
    }
  } else if (type < 27) {                                   // characters: red numeral + blue "WAN"
    char b[2] = {(char)('1' + type - 18), 0};
    text(b, cx - 9, cy - 16, 3, red);
    text("WAN", cx - 9, cy + 12, 1, blu);
  } else if (type < 31) {                                   // winds
    static const char w[4] = {'E', 'S', 'W', 'N'};
    char b[2] = {w[type - 27], 0};
    gfx->drawCircle(cx, cy, 15, blu);
    text(b, cx - 12, cy - 16, 4, ink);
  } else if (type == 31) {                                  // red dragon: stylised box with a bar
    gfx->drawRoundRect(cx - 9, cy - 13, 18, 26, 3, red);
    gfx->drawRoundRect(cx - 8, cy - 12, 16, 24, 3, red);
    gfx->fillRect(cx - 1, cy - 17, 3, 34, red);
  } else if (type == 32) {                                  // green dragon: diamond
    gfx->fillTriangle(cx, cy - 17, cx - 12, cy, cx + 12, cy, grn);
    gfx->fillTriangle(cx, cy + 17, cx - 12, cy, cx + 12, cy, grn);
    gfx->fillCircle(cx, cy, 3, rgb(255, 240, 200));
  } else {                                                  // white dragon: empty frame
    gfx->drawRoundRect(cx - 12, cy - 16, 24, 32, 4, blu);
    gfx->drawRoundRect(cx - 10, cy - 14, 20, 28, 3, blu);
  }
}

static void mjDrawTile(int i, bool free, bool selected, bool hinted, bool cursor, uint32_t now) {
  int x = mjTileX(i), y = mjTileY(i);
  uint16_t face = free ? rgb(255, 250, 232) : rgb(176, 170, 150);
  uint16_t edge = free ? rgb(196, 170, 100) : rgb(110, 104, 84);
  if (selected) { face = rgb(150, 240, 255); edge = rgb(40, 150, 190); }
  if (hinted && (now / 200) % 2 == 0) { face = rgb(255, 210, 90); edge = rgb(200, 120, 20); }
  gfx->fillRoundRect(x + DEPTH, y + DEPTH, TW - DEPTH, TH - DEPTH, 5, edge);
  gfx->fillRoundRect(x, y, TW - DEPTH, TH - DEPTH, 5, face);
  gfx->drawRoundRect(x, y, TW - DEPTH, TH - DEPTH, 5, rgb(120, 110, 80));
  mjGlyph(mjT[i].type, x + (TW - DEPTH) / 2, y + (TH - DEPTH) / 2);
  if (cursor) {
    gfx->drawRoundRect(x - 2, y - 2, TW - DEPTH + 4, TH - DEPTH + 4, 6, C_CYAN);
    gfx->drawRoundRect(x - 3, y - 3, TW - DEPTH + 6, TH - DEPTH + 6, 7, C_CYAN);
  }
}

static void mjDraw(uint32_t now) {
  gfx->fillScreen(rgb(0, 24, 16));
  // header
  gfx->fillRect(0, 0, W, 32, rgb(8, 10, 24));
  char b[24];
  uint32_t el = (mjWon ? mjEndMs : now - mjT0) / 1000;
  snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(el / 60), (unsigned long)(el % 60));
  text(b, 10, 9, 2, C_TEXT);
  if (mjNoMoves && (now / 400) % 2 == 0) centerText("NO MOVES - SHUFFLE", 9, 2, C_BAD);
  else { snprintf(b, sizeof b, "%d LEFT", mjRemaining); centerText(b, 9, 2, C_DIM); }
  if (mjBest) snprintf(b, sizeof b, "BEST %d:%02d", mjBest / 60, mjBest % 60);
  else snprintf(b, sizeof b, "BEST --");
  text(b, W - 10 - (int)strlen(b) * 12, 9, 2, C_GOOD);

  // tiles: lower layers first, then top-to-bottom, left-to-right so edges overlap correctly
  for (int layer = 0; layer < 4; layer++)
    for (int hy = 0; hy <= 10; hy++)
      for (int i = 0; i < MJ_N; i++) {
        if (mjGone[i] || mjT[i].layer != layer || mjT[i].hy != hy) continue;
        bool free = false;
        for (int k = 0; k < mjNList; k++) if (mjList[k] == i) { free = true; break; }
        mjDrawTile(i, free, i == mjSel, (i == mjHintA || i == mjHintB) && now < mjHintUntil, i == mjCurTile && !mjWon, now);
      }

  // action buttons
  static const char *const act[3] = {"HINT", "UNDO", "SHUFFLE"};
  for (int i = 0; i < 3; i++) {
    int x = 8 + i * 148;
    bool on = mjCurTile < 0 && mjCurAct == i && !mjWon;
    bool alert = i == 2 && mjNoMoves;
    gfx->fillRoundRect(x, ACT_Y, 140, ACT_H, 8, on ? C_PANEL : C_BG);
    gfx->drawRoundRect(x, ACT_Y, 140, ACT_H, 8, on ? C_CYAN : (alert ? C_BAD : 0x2945));
    text(act[i], x + (140 - (int)strlen(act[i]) * 12) / 2, ACT_Y + 8, 2, on ? C_TEXT : (alert ? C_BAD : C_DIM));
  }
  const char *hint = mjOpts[0].value == 0 ? "tilt: move   R pick / match   L drop pick   tap works too"
                                          : "L next free tile   R pick / match (again = drop)   tap works too";
  centerText(hint, 354, 1, C_DIM);

  if (mjWon) {
    char l2[24], l3[28];
    snprintf(l2, sizeof l2, "TIME %lu:%02lu", (unsigned long)(mjEndMs / 60000), (unsigned long)((mjEndMs / 1000) % 60));
    snprintf(l3, sizeof l3, "%s", mjNewBest ? "NEW BEST!  R again" : "R again   L menu");
    panelBox("CLEARED!", l2, l3, C_GOOD);
  }
  dirty = true;
}

// ------------------------------------------------------------------ frame
static bool mjFrame(uint32_t now) {
  if (mjWon) {
    if (now - mjStAt > 700) {
      if (in.right) { mjStart(); return true; }
      if (in.left) return false;
    }
    if (now - mjLastDraw >= 150) { mjLastDraw = now; mjDraw(now); }
    return true;
  }

  // tap: free tile = pick it, button = run it
  if (in.tap) {
    if (in.tapY >= ACT_Y - 4 && in.tapY < ACT_Y + ACT_H + 4) {
      int z = constrain((in.tapX - 8) / 148, 0, 2);
      mjCurTile = -1; mjCurAct = z;
      mjAction(z, now);
    } else {
      int hit = -1;
      for (int layer = 3; layer >= 0 && hit < 0; layer--)
        for (int k = 0; k < mjNList; k++) {
          int i = mjList[k];
          if (mjT[i].layer != layer) continue;
          int x = mjTileX(i), y = mjTileY(i);
          if (in.tapX >= x && in.tapX < x + TW && in.tapY >= y && in.tapY < y + TH) { hit = i; break; }
        }
      if (hit >= 0) { mjCurTile = hit; mjCurAct = -1; mjPick(now); }
      else sfxBad();
    }
  }

  bool tilt = mjOpts[0].value == 0;
  if (tilt) {
    float tx = axisX(), ty = axisY();
    int d = tx > 0.25f ? 1 : tx < -0.25f ? -1 : 0;
    if (d == 0) d = ty > 0.32f ? 4 : ty < -0.32f ? -4 : 0;
    if (d != mjLastDir) { mjLastDir = d; if (d) { mjRingStep(d); sfxClick(); mjRepAt = now + 320; } }
    else if (d && now >= mjRepAt) { mjRingStep(d); sfxClick(); mjRepAt = now + 170; }
    if (in.left && mjSel >= 0) { mjSel = -1; sfxClick(); mjRedraw = true; }
  } else if (in.left) { mjRingStep(1); sfxClick(); }
  if (in.right) mjPick(now);

  int sec = (int)((now - mjT0) / 1000);
  bool hintOn = now < mjHintUntil;
  if (sec != mjLastSec) { mjLastSec = sec; mjRedraw = true; }
  if ((mjRedraw || hintOn || mjNoMoves) && now - mjLastDraw >= 60) {
    mjLastDraw = now; mjRedraw = false;
    mjDraw(now);
  }
  return true;
}

Game GAME_MAHJONG = {"MAHJONG", "match free tile pairs", 0x07F0, mjOpts, 1, mjStart, mjFrame};
