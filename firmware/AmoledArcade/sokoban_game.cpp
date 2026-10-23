// sokoban_game.cpp - Sokoban box pusher with 24 built-in levels (sokoban_levels.h, original, XSB format).
//   TILT = one step in the snapped direction (hold the tilt to repeat);  swipe on the screen = one step
//   LEFT = undo (unlimited, ~2000 moves),  hold LEFT 0.7 s = restart the level
//   RIGHT = level select (LEFT / RIGHT or tilt change level, tap / both buttons / 2.5 s idle = go)
//   TAP a floor tile = walk there along the shortest path (never pushes a crate except the adjacent one you tapped)
// Levels unlock one by one (option LEVELS: PROGRESS / ALL OPEN). Best move count per level is saved ("sk<N>").
// TODO(SD): extra levels from /arcade/sokoban/*.txt (XSB). hw.h only exposes mount/status for the SD card (no file
//   read API), so a loader would have to add SD_MMC reads in hw.cpp; the parser below (skLoad) already takes any XSB string.
#include "engine.h"
#include <math.h>
#include "sokoban_levels.h"

static const char *const SK_STARTS[] = {"CONTINUE", "LEVEL 1"};
static const char *const SK_OPEN[] = {"PROGRESS", "ALL OPEN"};
static Option skOpts[] = {{"START", SK_STARTS, 2, "skStart", 0}, {"LEVELS", SK_OPEN, 2, "skOpen", 0}};

static constexpr int SK_MAXW = 16, SK_MAXH = 12;
enum : uint8_t { F_FLOOR = 1, F_WALL = 2, F_GOAL = 4, F_BOX = 8 };
enum SkSt : uint8_t { SK_PLAY, SK_SELECT, SK_SOLVED };

static uint8_t skG[SK_MAXH][SK_MAXW];
static int skW, skH, skPx, skPy, skLevel, skMoves, skPushes, skBestMoves, skTile, skOx, skOy, skFacing = DIR_D;
static int skSel;
static SkSt skSt;
static uint8_t skLog[2048];
static int skLogN;
static bool skNewBest, skAllOpen;
static uint32_t skLast, skLastRender, skStAt, skSelAt, skLeftAt, skRepeatAt, skWalkAt;
static SnapDir skSnap;
static uint8_t skPrevDir;
static int8_t skPath[80];
static int skPathN, skPathI;
static bool skLeftHandled;

static const int8_t SK_DX[5] = {0, -1, 1, 0, 0};                    // indexed by DIR_*
static const int8_t SK_DY[5] = {0, 0, 0, -1, 1};

static uint16_t skBestOf(int lvl) { char k[8]; snprintf(k, sizeof k, "sk%d", lvl); return (uint16_t)prefs.getUShort(k, 0); }
static void skBestSet(int lvl, uint16_t v) { char k[8]; snprintf(k, sizeof k, "sk%d", lvl); prefs.putUShort(k, v); }
static int skUnlocked() {                                            // highest playable level index
  if (skAllOpen) return SK_NLEVELS - 1;
  int i = 0;
  while (i < SK_NLEVELS - 1 && skBestOf(i) > 0) i++;
  return i;
}

// ---------------- level loading (XSB) ----------------
static bool skLoad(const char *xsb) {
  memset(skG, 0, sizeof skG);
  int x = 0, y = 0, w = 0, px = -1, py = -1;
  for (const char *p = xsb; *p; p++) {
    char c = *p;
    if (c == '\r') continue;
    if (c == '\n') { if (x > w) w = x; x = 0; y++; if (y >= SK_MAXH) break; continue; }
    if (x >= SK_MAXW) continue;
    uint8_t f = 0;
    switch (c) {
      case '#': f = F_WALL; break;
      case '$': f = F_BOX; break;
      case '*': f = F_BOX | F_GOAL; break;
      case '.': f = F_GOAL; break;
      case '@': px = x; py = y; break;
      case '+': f = F_GOAL; px = x; py = y; break;
      default: break;
    }
    skG[y][x] = f;
    x++;
  }
  if (x > w) w = x;
  int h = y + (x > 0 ? 1 : 0);
  if (h > SK_MAXH) h = SK_MAXH;
  if (px < 0 || w == 0) return false;
  skW = w; skH = h; skPx = px; skPy = py;
  // flood from the player: reachable non-wall cells are floor, the rest is void
  static int8_t qx[SK_MAXW * SK_MAXH], qy[SK_MAXW * SK_MAXH];
  int qh = 0, qt = 0;
  skG[py][px] |= F_FLOOR; qx[qt] = (int8_t)px; qy[qt++] = (int8_t)py;
  while (qh < qt) {
    int cx = qx[qh], cy = qy[qh++];
    for (int d = 1; d <= 4; d++) {
      int nx = cx + SK_DX[d], ny = cy + SK_DY[d];
      if (nx < 0 || ny < 0 || nx >= skW || ny >= skH) continue;
      if (skG[ny][nx] & (F_WALL | F_FLOOR)) continue;
      skG[ny][nx] |= F_FLOOR; qx[qt] = (int8_t)nx; qy[qt++] = (int8_t)ny;
    }
  }
  int avail = min(W / skW, (H - 38 - 26) / skH);
  skTile = constrain(avail, 14, 40);
  skOx = (W - skW * skTile) / 2;
  skOy = 38 + (H - 38 - 26 - skH * skTile) / 2;
  return true;
}

static void skBegin(int lvl, uint32_t now) {
  skLevel = constrain(lvl, 0, SK_NLEVELS - 1);
  skLoad(SK_LEVELS[skLevel]);
  skMoves = skPushes = skLogN = 0; skPathN = skPathI = 0;
  skBestMoves = skBestOf(skLevel);
  skSt = SK_PLAY; skStAt = now; skFacing = DIR_D; skNewBest = false;
  skPrevDir = DIR_NONE; skLeftHandled = false;
  imuZero();
  prefs.putUShort("skLast", (uint16_t)skLevel);
}

static void skStart() {
  skAllOpen = skOpts[1].value == 1;
  uint32_t now = millis();
  int lvl = skOpts[0].value == 0 ? (int)prefs.getUShort("skLast", 0) : 0;
  lvl = min(lvl, skUnlocked());
  skBegin(lvl, now);
  skLast = now; skLastRender = 0;
  sfxStart();
}

// ---------------- rules ----------------
static bool skSolved() {
  for (int y = 0; y < skH; y++) for (int x = 0; x < skW; x++)
    if ((skG[y][x] & F_BOX) && !(skG[y][x] & F_GOAL)) return false;
  return true;
}

static bool skStep(int d) {
  int nx = skPx + SK_DX[d], ny = skPy + SK_DY[d];
  skFacing = d;
  if (nx < 0 || ny < 0 || nx >= skW || ny >= skH || (skG[ny][nx] & F_WALL) || !(skG[ny][nx] & F_FLOOR)) return false;
  bool push = false;
  if (skG[ny][nx] & F_BOX) {
    int bx = nx + SK_DX[d], by = ny + SK_DY[d];
    if (bx < 0 || by < 0 || bx >= skW || by >= skH) return false;
    if ((skG[by][bx] & (F_WALL | F_BOX)) || !(skG[by][bx] & F_FLOOR)) return false;
    skG[ny][nx] &= (uint8_t)~F_BOX; skG[by][bx] |= F_BOX;
    push = true; skPushes++;
  }
  skPx = nx; skPy = ny; skMoves++;
  if (skLogN >= (int)sizeof skLog) { memmove(skLog, skLog + 256, sizeof skLog - 256); skLogN -= 256; }
  skLog[skLogN++] = (uint8_t)(d | (push ? 0x80 : 0));
  if (push) { play(190, 10); play(140, 10); } else play(520, 4);
  return true;
}

static void skDoUndo() {
  if (skLogN == 0) { play(150, 30); return; }
  uint8_t e = skLog[--skLogN];
  bool push = (e & 0x80) != 0;
  int d = e & 7;
  if (push) {
    int bx = skPx + SK_DX[d], by = skPy + SK_DY[d];
    skG[by][bx] &= (uint8_t)~F_BOX; skG[skPy][skPx] |= F_BOX;
    skPushes--;
  }
  skPx -= SK_DX[d]; skPy -= SK_DY[d];
  skMoves--; skPathN = skPathI = 0;
  play(380, 8);
}

// ---------------- path walking ----------------
static bool skPlanPath(int tx, int ty) {
  static int8_t prev[SK_MAXH][SK_MAXW];
  static int8_t qx[SK_MAXW * SK_MAXH], qy[SK_MAXW * SK_MAXH];
  skPathN = skPathI = 0;
  if (tx < 0 || ty < 0 || tx >= skW || ty >= skH || !(skG[ty][tx] & F_FLOOR)) return false;
  if (tx == skPx && ty == skPy) return false;
  if (skG[ty][tx] & F_BOX) {                                        // adjacent crate in line: just push it
    for (int d = 1; d <= 4; d++) if (skPx + SK_DX[d] == tx && skPy + SK_DY[d] == ty) { skPath[0] = (int8_t)d; skPathN = 1; return true; }
    return false;
  }
  memset(prev, 0, sizeof prev);
  int qh = 0, qt = 0;
  prev[skPy][skPx] = 9;
  qx[qt] = (int8_t)skPx; qy[qt++] = (int8_t)skPy;
  while (qh < qt) {
    int cx = qx[qh], cy = qy[qh++];
    if (cx == tx && cy == ty) break;
    for (int d = 1; d <= 4; d++) {
      int nx = cx + SK_DX[d], ny = cy + SK_DY[d];
      if (nx < 0 || ny < 0 || nx >= skW || ny >= skH || prev[ny][nx]) continue;
      if ((skG[ny][nx] & (F_WALL | F_BOX)) || !(skG[ny][nx] & F_FLOOR)) continue;
      prev[ny][nx] = (int8_t)d; qx[qt] = (int8_t)nx; qy[qt++] = (int8_t)ny;
    }
  }
  if (!prev[ty][tx]) return false;
  int n = 0, cx = tx, cy = ty;
  static int8_t rev[SK_MAXW * SK_MAXH];
  while (!(cx == skPx && cy == skPy) && n < (int)sizeof rev) { int d = prev[cy][cx]; rev[n++] = (int8_t)d; cx -= SK_DX[d]; cy -= SK_DY[d]; }
  if (n > (int)sizeof skPath) n = sizeof skPath;
  for (int i = 0; i < n; i++) skPath[i] = rev[n - 1 - i];
  skPathN = n; skPathI = 0;
  return true;
}

// ---------------- render ----------------
static bool skIsFloor(int x, int y) { return x >= 0 && y >= 0 && x < skW && y < skH && (skG[y][x] & F_FLOOR); }

static void skRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  int t = skTile;
  float pulse = 0.5f + 0.5f * sinf(now * 0.005f);
  uint16_t wallC = rgb(50, 100, 255);
  for (int y = 0; y < skH; y++)
    for (int x = 0; x < skW; x++) {
      int px = skOx + x * t, py = skOy + y * t;
      uint8_t f = skG[y][x];
      if (f & F_WALL) {
        bool near = false;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (skIsFloor(x + dx, y + dy)) near = true;
        if (!near) continue;
        gfx->fillRect(px, py, t, t, rgb(8, 14, 44));
        if (skIsFloor(x, y - 1)) { gfx->drawFastHLine(px, py + t - 1, t, wallC); gfx->drawFastHLine(px, py + t - 2, t, lerpColor(wallC, C_BG, 0.6f)); }
        if (skIsFloor(x, y + 1)) { gfx->drawFastHLine(px, py, t, wallC); gfx->drawFastHLine(px, py + 1, t, lerpColor(wallC, C_BG, 0.6f)); }
        if (skIsFloor(x - 1, y)) { gfx->drawFastVLine(px + t - 1, py, t, wallC); gfx->drawFastVLine(px + t - 2, py, t, lerpColor(wallC, C_BG, 0.6f)); }
        if (skIsFloor(x + 1, y)) { gfx->drawFastVLine(px, py, t, wallC); gfx->drawFastVLine(px + 1, py, t, lerpColor(wallC, C_BG, 0.6f)); }
        continue;
      }
      if (!(f & F_FLOOR)) continue;
      gfx->fillRect(px, py, t, t, rgb(9, 11, 24));
      gfx->drawRect(px, py, t, t, rgb(18, 22, 46));
      if (f & F_GOAL) {
        uint16_t g = lerpColor(rgb(60, 255, 150), C_BG, 0.35f + 0.35f * (1 - pulse));
        gfx->drawCircle(px + t / 2, py + t / 2, t / 4, g);
        gfx->drawCircle(px + t / 2, py + t / 2, t / 4 + 2, lerpColor(g, C_BG, 0.6f));
        gfx->fillCircle(px + t / 2, py + t / 2, 2, g);
      }
    }
  for (int y = 0; y < skH; y++)                                     // crates
    for (int x = 0; x < skW; x++) {
      uint8_t f = skG[y][x];
      if (!(f & F_BOX)) continue;
      int px = skOx + x * t, py = skOy + y * t;
      bool on = f & F_GOAL;
      uint16_t c = on ? rgb(70, 255, 150) : rgb(255, 150, 40);
      gfx->fillRoundRect(px + 2, py + 2, t - 4, t - 4, 4, lerpColor(c, C_BG, on ? 0.55f - 0.15f * pulse : 0.7f));
      gfx->drawRoundRect(px + 2, py + 2, t - 4, t - 4, 4, c);
      gfx->drawRoundRect(px + 1, py + 1, t - 2, t - 2, 5, lerpColor(c, C_BG, 0.65f));
      gfx->drawLine(px + 6, py + 6, px + t - 7, py + t - 7, lerpColor(c, C_BG, 0.35f));
      gfx->drawLine(px + t - 7, py + 6, px + 6, py + t - 7, lerpColor(c, C_BG, 0.35f));
    }
  {                                                                 // player
    int cx = skOx + skPx * t + t / 2, cy = skOy + skPy * t + t / 2, r = t * 36 / 100;
    gfx->fillCircle(cx, cy, r + 4, lerpColor(C_CYAN, C_BG, 0.82f));
    gfx->fillCircle(cx, cy, r, C_CYAN);
    gfx->drawCircle(cx, cy, r + 2, lerpColor(C_CYAN, C_BG, 0.5f));
    int ex = SK_DX[skFacing] * 3, ey = SK_DY[skFacing] * 3;
    gfx->fillCircle(cx - 4 + ex, cy - 2 + ey, 2, C_BG); gfx->fillCircle(cx + 4 + ex, cy - 2 + ey, 2, C_BG);
  }
  hudBar(skMoves, skBestMoves, rgb(8, 8, 22), C_TEXT);
  char b[32];
  snprintf(b, sizeof b, "LV %d/%d", skLevel + 1, SK_NLEVELS);
  text(b, 262, 9, 2, C_ACC);
  snprintf(b, sizeof b, "pushes %d", skPushes);
  text(b, 10, 346, 1, C_DIM);
  centerText("tilt = move   LEFT undo (hold = restart)   RIGHT levels   tap = walk", 356, 1, C_DIM);
  if (skSt == SK_SELECT) {
    gfx->fillRoundRect(60, 112, W - 120, 140, 16, C_PANEL);
    gfx->drawRoundRect(60, 112, W - 120, 140, 16, C_ACC);
    snprintf(b, sizeof b, "LEVEL %d", skSel + 1);
    centerText(b, 124, 4, C_TEXT);
    uint16_t bm = skBestOf(skSel);
    if (bm) snprintf(b, sizeof b, "best %u moves", bm); else snprintf(b, sizeof b, skSel <= skUnlocked() ? "not solved yet" : "LOCKED");
    centerText(b, 178, 2, skSel <= skUnlocked() ? C_GOOD : C_BAD);
    centerText("LEFT / RIGHT change   tap or BOTH = go", 214, 1, C_DIM);
    int pr = (int)((2500 - (now - skSelAt)) / 100);
    if (pr < 0) pr = 0;
    gfx->fillRect(80, 236, (W - 160) * pr / 25, 3, C_ACC);
  } else if (skSt == SK_SOLVED) {
    char l2[40], l3[48];
    snprintf(l2, sizeof l2, "%d MOVES  %d PUSHES", skMoves, skPushes);
    if (skNewBest) snprintf(l3, sizeof l3, "NEW BEST!"); else snprintf(l3, sizeof l3, "best %d", skBestMoves);
    panelBox("SOLVED", l2, l3, C_GOOD);
    centerText(skLevel + 1 < SK_NLEVELS ? "RIGHT next   LEFT replay" : "all levels done - RIGHT = level 1", 244, 1, C_DIM);
  }
  dirty = true;
}

// ---------------- frame ----------------
static void skCheckSolved(uint32_t now) {
  if (skSt != SK_PLAY || !skSolved()) return;
  skSt = SK_SOLVED; skStAt = now; skPathN = skPathI = 0;
  uint16_t old = skBestOf(skLevel);
  skNewBest = old == 0 || skMoves < old;
  if (skNewBest) { skBestSet(skLevel, (uint16_t)skMoves); skBestMoves = skMoves; }
  tunePlay(TUNE_LEVEL);
}

static void skDoStep(int d, uint32_t now) {
  skPathN = skPathI = 0;
  if (skStep((uint8_t)d)) skCheckSolved(now); else play(120, 25);
}

static bool skFrame(uint32_t now) {
  homeLock = true;                                                  // LEFT/RIGHT taps are normal play (undo, levels)
  skLast = now;
  tuneUpdate(now);
  bool leftEdge = in.left, rightEdge = in.right;

  if (skSt == SK_SELECT) {
    uint8_t d = skSnap.update(axisX(), axisY());
    bool tiltMove = d != skPrevDir && (d == DIR_L || d == DIR_R);
    skPrevDir = d;
    int mx = skUnlocked();
    if (leftEdge || (tiltMove && d == DIR_L)) { skSel = max(0, skSel - 1); skSelAt = now; play(500, 8); }
    if (rightEdge || (tiltMove && d == DIR_R)) { skSel = min(SK_NLEVELS - 1, skSel + 1); skSelAt = now; play(600, 8); }
    if ((in.leftDown && in.rightDown) || in.tap || now - skSelAt > 2500) {
      if (skSel <= mx) skBegin(skSel, now);
      else { skSel = mx; skSelAt = now; play(150, 40); }
      if (skSt == SK_PLAY) { skLeftHandled = true; skLeftAt = now; }
    }
  } else if (skSt == SK_SOLVED) {
    if (now - skStAt > 700) {
      if (rightEdge || in.tap) { skBegin(skLevel + 1 < SK_NLEVELS ? skLevel + 1 : 0, now); }
      else if (leftEdge) skBegin(skLevel, now);
    }
  } else {
    // ---- play ----
    if (leftEdge) { skDoUndo(); skLeftAt = now; skLeftHandled = false; }
    if (in.leftDown && !skLeftHandled && now - skLeftAt > 700) {
      skBegin(skLevel, now); skLeftHandled = true; play(300, 40); play(200, 60);
    }
    if (!in.leftDown) skLeftHandled = false;
    if (rightEdge) { skSt = SK_SELECT; skSel = skLevel; skSelAt = now; skPathN = skPathI = 0; skPrevDir = DIR_NONE; skSnap.cur = DIR_NONE; }
    uint8_t d = skSnap.update(axisX(), axisY());
    if (d != DIR_NONE && d != skPrevDir) { skDoStep(d, now); skRepeatAt = now + 360; }
    else if (d != DIR_NONE && now >= skRepeatAt) { skDoStep(d, now); skRepeatAt = now + 170; }
    skPrevDir = d;
    if (skSt == SK_PLAY && in.dragEnd) {                            // swipe = one step
      int dx = in.dragTX, dy = in.dragTY;
      if (abs(dx) > 36 || abs(dy) > 36) skDoStep(abs(dx) >= abs(dy) ? (dx > 0 ? DIR_R : DIR_L) : (dy > 0 ? DIR_D : DIR_U), now);
    }
    if (skSt == SK_PLAY && in.tap && in.tapY > 36) {
      int tx = (in.tapX - skOx) / skTile, ty = (in.tapY - skOy) / skTile;
      if (in.tapX >= skOx && in.tapY >= skOy && skPlanPath(tx, ty)) skWalkAt = now;
    }
    if (skSt == SK_PLAY && skPathI < skPathN && now >= skWalkAt) {
      int d2 = skPath[skPathI++];
      skWalkAt = now + 75;
      if (skStep((uint8_t)d2)) skCheckSolved(now); else skPathN = skPathI = 0;
    }
  }
  if (now - skLastRender >= 16) { skLastRender = now; skRender(now); }
  return true;
}

Game GAME_SOKOBAN = {"SOKOBAN", "push the crates, 24 levels", 0xFC60, skOpts, 2, skStart, skFrame};
