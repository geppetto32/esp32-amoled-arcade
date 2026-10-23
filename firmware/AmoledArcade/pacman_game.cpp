// pacman_game.cpp - Pac-Man style maze chase on a 28x25 tile maze (13 px tiles, neon-blue double walls).
// CONTROL: TILT (lean to pick the next direction, the turn waits until the corridor allows it; either button = pause)
//          BUTTONS (LEFT / RIGHT turn relative to where you are heading, a turn is queued until possible).
// Ghosts: RED chases you, PINK ambushes 4 tiles ahead, CYAN wanders (part random), ORANGE chases from afar but
// runs to its corner when close. Power pellets turn them blue (flashing before it ends). Tunnel wraps. 3 lives.
#include "game.h"

static const char *const PM_CONTROLS[] = {"TILT", "BUTTONS"};
static Option pmOpts[] = {{"CONTROL", PM_CONTROLS, 2, "pmCtrl", 0}};

enum PmSt : uint8_t { PM_COUNT, PM_PLAY, PM_PAUSE, PM_DYING, PM_CLEAR, PM_OVER };
enum GhSt : uint8_t { GS_HOUSE, GS_LEAVE, GS_NORM, GS_EYES };

static constexpr int PM_C = 28, PM_R = 25, PM_T = 13, PM_OX = 42, PM_OY = 37, PM_TUN = 12, PM_DOORY = 10, PM_HOUSEY = 12;
static const char *const PM_MAP[PM_R] = {
    "############################",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o####.#####.##.#####.####o#",
    "#..........................#",
    "#.####.##.########.##.####.#",
    "#.####.##.########.##.####.#",
    "#......##....##....##......#",
    "######.##### ## #####.######",
    "XXXXX#.##          ##.#XXXXX",
    "XXXXX#.## ###--### ##.#XXXXX",
    "######.## #      # ##.######",
    "      .   #      #   .      ",
    "######.## #      # ##.######",
    "XXXXX#.## ######## ##.#XXXXX",
    "XXXXX#.##          ##.#XXXXX",
    "######.##### ## #####.######",
    "#............##............#",
    "#.####.#####.##.#####.####.#",
    "#o..##.......  .......##..o#",
    "###.##.##.########.##.##.###",
    "#......##....##....##......#",
    "#.##########.##.##########.#",
    "#..........................#",
    "############################"};
static const int PM_DX[4] = {0, -1, 0, 1}, PM_DY[4] = {-1, 0, 1, 0};   // up, left, down, right (tie order)

struct PmEnt { int ix, iy, dx, dy; float t; };
struct PmGhost { PmEnt e; GhSt st; bool fright; float releaseAt; };
struct PmPop { float x, y; int v; uint32_t born; };

static char pmGrid[PM_R][PM_C];
static int16_t pmExit[PM_R][PM_C], pmHome[PM_R][PM_C];      // BFS distance to above-door tile / house centre
static PmEnt pmPac;
static PmGhost pmG[4];
static PmPop pmPops[4];
static PmSt pmSt;
static int pmCtrl, pmLevel, pmLives, pmDots, pmDesX, pmDesY, pmHeadX, pmHeadY, pmMode, pmChain;
static uint32_t pmScore, pmBest, pmT0, pmCountLen, pmLastT, pmLastRender, pmStateAt, pmNextLife;
static float pmClock, pmModeT, pmFrightLeft;
static bool pmNewBest, pmEat2;
static uint16_t *pmBg = nullptr;
static int pmBgBuilt = -1;

static const uint16_t PM_GCOL[4] = {rgb(255, 20, 20), rgb(255, 150, 220), rgb(0, 230, 255), rgb(255, 170, 40)};
static const int PM_SCX[4] = {25, 2, 25, 2}, PM_SCY[4] = {0, 0, 24, 24};   // scatter corners

// ---------------- maze helpers ----------------
static inline bool pmWallTile(int x, int y) { return x < 0 || y < 0 || x >= PM_C || y >= PM_R || PM_MAP[y][x] == '#'; }

static bool pmOpenFor(int x, int y, bool door) {            // door: may the mover use the ghost-house door?
  if (x < 0 || x >= PM_C) return y == PM_TUN;
  if (y < 0 || y >= PM_R) return false;
  char c = pmGrid[y][x];
  if (c == '#' || c == 'X') return false;
  if (c == '-') return door;
  return true;
}
static void pmBfs(int tx, int ty, int16_t (*d)[PM_C]) {
  static uint16_t q[PM_R * PM_C];
  for (int y = 0; y < PM_R; y++) for (int x = 0; x < PM_C; x++) d[y][x] = -1;
  int qh = 0, qt = 0;
  d[ty][tx] = 0; q[qt++] = ty * PM_C + tx;
  while (qh < qt) {
    int c = q[qh++], x = c % PM_C, y = c / PM_C;
    for (int k = 0; k < 4; k++) {
      int nx = x + PM_DX[k], ny = y + PM_DY[k];
      if (!pmOpenFor(nx, ny, true)) continue;
      if (nx < 0) nx = PM_C - 1; else if (nx >= PM_C) nx = 0;
      if (d[ny][nx] >= 0) continue;
      d[ny][nx] = d[y][x] + 1;
      q[qt++] = ny * PM_C + nx;
    }
  }
}
static inline float pmPx(float fx) { return PM_OX + (fx + 0.5f) * PM_T; }
static inline float pmPy(float fy) { return PM_OY + (fy + 0.5f) * PM_T; }
static void pmPos(const PmEnt &e, float &fx, float &fy) { fx = e.ix + e.dx * e.t; fy = e.iy + e.dy * e.t; }
static inline void pmWrap(PmEnt &e) { if (e.ix < 0) e.ix = PM_C - 1; else if (e.ix >= PM_C) e.ix = 0; }
static void pmReverse(PmEnt &e) {
  if (!e.dx && !e.dy) return;
  e.ix += e.dx; e.iy += e.dy;
  pmWrap(e);
  e.t = 1.0f - e.t;
  e.dx = -e.dx; e.dy = -e.dy;
}

// ---------------- background (walls) ----------------
static void pmBuildBg(bool flash) {
  if (!pmBg) pmBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!pmBg) return;
  uint16_t outer = flash ? rgb(255, 255, 255) : rgb(70, 120, 255), inner = flash ? rgb(150, 170, 255) : rgb(20, 45, 170);
  gfx->fillScreen(C_BG);
  for (int pass = 0; pass < 3; pass++)
    for (int y = 0; y < PM_R; y++)
      for (int x = 0; x < PM_C; x++) {
        if (PM_MAP[y][x] != '#') continue;
        bool wl = pmWallTile(x - 1, y), wr = pmWallTile(x + 1, y), wu = pmWallTile(x, y - 1), wd = pmWallTile(x, y + 1);
        int X = PM_OX + x * PM_T, Y = PM_OY + y * PM_T, ins = pass;
        int x0 = X + (wl ? 0 : ins), x1 = X + PM_T - (wr ? 0 : ins), y0 = Y + (wu ? 0 : ins), y1 = Y + PM_T - (wd ? 0 : ins);
        gfx->fillRect(x0, y0, x1 - x0, y1 - y0, pass == 0 ? outer : pass == 1 ? inner : C_BG);
        if (pass == 2) {                                  // outside corners that would leave a gap
          if (wl && wu && !pmWallTile(x - 1, y - 1) && x > 0 && y > 0) gfx->fillRect(X, Y, 2, 2, outer);
          if (wr && wu && !pmWallTile(x + 1, y - 1) && x < PM_C - 1 && y > 0) gfx->fillRect(X + PM_T - 2, Y, 2, 2, outer);
          if (wl && wd && !pmWallTile(x - 1, y + 1) && x > 0 && y < PM_R - 1) gfx->fillRect(X, Y + PM_T - 2, 2, 2, outer);
          if (wr && wd && !pmWallTile(x + 1, y + 1) && x < PM_C - 1 && y < PM_R - 1) gfx->fillRect(X + PM_T - 2, Y + PM_T - 2, 2, 2, outer);
        }
      }
  for (int y = 0; y < PM_R; y++)
    for (int x = 0; x < PM_C; x++)
      if (PM_MAP[y][x] == '-') gfx->fillRect(PM_OX + x * PM_T, PM_OY + y * PM_T + 5, PM_T, 3, rgb(255, 160, 220));
  memcpy(pmBg, frameBuffer(), FB_PIXELS * 2);
  pmBgBuilt = flash ? 1 : 0;
}

// ---------------- speeds ----------------
static float pmPacSpeed() { return fminf(7.2f + 0.35f * (pmLevel - 1), 9.5f); }
static float pmGhostSpeed(const PmGhost &g) {
  float s = fminf(6.6f + 0.45f * (pmLevel - 1), 9.0f);
  if (g.st == GS_EYES) return 13.0f;
  if (g.st == GS_HOUSE || g.st == GS_LEAVE) return 4.5f;
  if (g.fright) return s * 0.55f;
  if (g.e.iy == PM_TUN && (g.e.ix < 6 || g.e.ix > 21)) return s * 0.55f;
  return s;
}

// ---------------- movement ----------------
static void pmPacArrive(PmEnt &e, int) {
  char &c = pmGrid[e.iy][e.ix];
  if (c == '.' || c == 'o') {
    bool pellet = c == 'o';
    c = ' ';
    pmDots--;
    pmScore += pellet ? 50 : 10;
    pmEat2 = !pmEat2;
    play(pmEat2 ? 330 : 440, 14);
    if (pellet) {
      pmFrightLeft = fmaxf(1.5f, 6.5f - 0.6f * (pmLevel - 1));
      pmChain = 0;
      for (auto &g : pmG) {
        if (g.st == GS_EYES) continue;
        if (!g.fright && g.st == GS_NORM) pmReverse(g.e);
        g.fright = true;
      }
      play(260, 40); play(520, 60);
    }
  }
  if ((pmDesX || pmDesY) && pmOpenFor(e.ix + pmDesX, e.iy + pmDesY, false)) { e.dx = pmDesX; e.dy = pmDesY; }
  else if ((e.dx || e.dy) && pmOpenFor(e.ix + e.dx, e.iy + e.dy, false)) { /* keep going */ }
  else { e.dx = e.dy = 0; }
  if (e.dx || e.dy) { pmHeadX = e.dx; pmHeadY = e.dy; }
}

static void pmTarget(int id, const PmGhost &g, int &tx, int &ty) {
  float fx, fy;
  pmPos(pmPac, fx, fy);
  int px = pmPac.ix, py = pmPac.iy;
  bool scatter = (pmMode & 1) == 0;
  tx = PM_SCX[id]; ty = PM_SCY[id];
  if (scatter) return;
  switch (id) {
    case 0: tx = px; ty = py; break;
    case 1: tx = px + 4 * pmHeadX; ty = py + 4 * pmHeadY; break;
    case 2: tx = px; ty = py; break;
    default: {
      int dx = g.e.ix - px, dy = g.e.iy - py;
      if (dx * dx + dy * dy > 64) { tx = px; ty = py; }
      break;
    }
  }
}

static void pmGhostArrive(PmEnt &e, int id) {
  PmGhost &g = pmG[id];
  if (g.st == GS_LEAVE && e.ix == 13 && e.iy == PM_DOORY - 1) g.st = GS_NORM;
  if (g.st == GS_EYES && e.ix == 13 && e.iy == PM_HOUSEY) { g.st = GS_LEAVE; g.fright = false; }
  bool door = g.st == GS_LEAVE || g.st == GS_EYES;
  if (door) {                                               // follow the BFS distance map
    int16_t (*map)[PM_C] = g.st == GS_EYES ? pmHome : pmExit;
    int bestD = 32000, bk = -1;
    for (int k = 0; k < 4; k++) {
      int nx = e.ix + PM_DX[k], ny = e.iy + PM_DY[k];
      if (!pmOpenFor(nx, ny, true)) continue;
      int wx = nx < 0 ? PM_C - 1 : nx >= PM_C ? 0 : nx;
      if (map[ny][wx] >= 0 && map[ny][wx] < bestD) { bestD = map[ny][wx]; bk = k; }
    }
    if (bk >= 0) { e.dx = PM_DX[bk]; e.dy = PM_DY[bk]; }
    else e.dx = e.dy = 0;
    return;
  }
  int cand[4], nc = 0;
  for (int k = 0; k < 4; k++) {
    if ((e.dx || e.dy) && e.dx == -PM_DX[k] && e.dy == -PM_DY[k]) continue;
    if (!pmOpenFor(e.ix + PM_DX[k], e.iy + PM_DY[k], false)) continue;
    cand[nc++] = k;
  }
  if (nc == 0) {
    for (int k = 0; k < 4; k++)
      if (pmOpenFor(e.ix + PM_DX[k], e.iy + PM_DY[k], false)) { cand[nc++] = k; break; }
    if (nc == 0) { e.dx = e.dy = 0; return; }
  }
  int pick = cand[0];
  bool rnd = g.fright || (id == 2 && (pmMode & 1) && (esp_random() % 100) < 30);
  if (rnd) pick = cand[esp_random() % nc];
  else {
    int tx, ty, bd = 1 << 30;
    pmTarget(id, g, tx, ty);
    for (int i = 0; i < nc; i++) {
      int nx = e.ix + PM_DX[cand[i]], ny = e.iy + PM_DY[cand[i]], d = (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty);
      if (d < bd) { bd = d; pick = cand[i]; }
    }
  }
  e.dx = PM_DX[pick]; e.dy = PM_DY[pick];
}

static void pmAdvance(PmEnt &e, float dist, void (*arrive)(PmEnt &, int), int id) {
  if (!e.dx && !e.dy) { arrive(e, id); if (!e.dx && !e.dy) return; }
  e.t += dist;
  while (e.t >= 1.0f) {
    e.t -= 1.0f;
    e.ix += e.dx; e.iy += e.dy;
    pmWrap(e);
    arrive(e, id);
    if (!e.dx && !e.dy) { e.t = 0; break; }
  }
}

// ---------------- setup ----------------
static void pmResetPositions() {
  pmPac = {13, 19, -1, 0, 0.0f};
  pmDesX = -1; pmDesY = 0; pmHeadX = -1; pmHeadY = 0;
  pmG[0] = {{13, 9, -1, 0, 0.0f}, GS_NORM, false, 0};
  pmG[1] = {{13, 12, 0, 0, 0.0f}, GS_HOUSE, false, 1.5f};
  pmG[2] = {{12, 12, 0, 0, 0.0f}, GS_HOUSE, false, 5.0f};
  pmG[3] = {{15, 12, 0, 0, 0.0f}, GS_HOUSE, false, 9.0f};
  float k = fmaxf(0.4f, 1.0f - 0.12f * (pmLevel - 1));
  for (int i = 1; i < 4; i++) pmG[i].releaseAt *= k;
  pmClock = 0; pmModeT = 0; pmMode = 0; pmFrightLeft = 0; pmChain = 0;
  memset(pmPops, 0, sizeof pmPops);
}
static void pmLoadMaze() {
  pmDots = 0;
  for (int y = 0; y < PM_R; y++)
    for (int x = 0; x < PM_C; x++) {
      pmGrid[y][x] = PM_MAP[y][x];
      if (pmGrid[y][x] == '.' || pmGrid[y][x] == 'o') pmDots++;
    }
}
static void pmBeginRound(uint32_t len) {
  pmResetPositions();
  pmCountLen = len;
  pmT0 = millis();
  pmSt = PM_COUNT;
}

static void pmFinish(uint32_t now) {
  pmNewBest = saveBest(pmCtrl ? "pmBestB" : "pmBestT", pmScore);
  if (pmNewBest) pmBest = pmScore;
  pmNewBest ? sfxBest() : sfxBad();
  pmStateAt = now;
  pmSt = PM_OVER;
}

// ---------------- drawing ----------------
static void pmFan(int cx, int cy, int R, float face, float mouth, uint16_t col) {
  if (mouth >= 3.1f) return;
  const int N = 14;
  float span = 6.28318f - 2 * mouth, step = span / N, a = face + mouth;
  for (int i = 0; i < N; i++) {
    float a1 = a + step;
    gfx->fillTriangle(cx, cy, cx + (int)lroundf(cosf(a) * R), cy + (int)lroundf(sinf(a) * R),
                      cx + (int)lroundf(cosf(a1) * R), cy + (int)lroundf(sinf(a1) * R), col);
    a = a1;
  }
}

static void pmDrawGhost(int id, uint32_t now) {
  const PmGhost &g = pmG[id];
  float fx, fy;
  pmPos(g.e, fx, fy);
  int px = (int)pmPx(fx), py = (int)pmPy(fy);
  if (g.st == GS_HOUSE) py += (int)(sinf(now * 0.012f + id * 1.7f) * 3);
  int f = (now / 130) & 1;
  uint16_t col = PM_GCOL[id];
  bool eyesOnly = g.st == GS_EYES, fr = g.fright && !eyesOnly;
  if (fr) {
    bool white = pmFrightLeft < 2.0f && ((now / 150) & 1);
    col = white ? rgb(255, 255, 255) : rgb(30, 50, 255);
  }
  if (!eyesOnly) {
    gfx->fillCircle(px, py - 1, 6, col);
    gfx->fillRect(px - 6, py - 1, 13, 7, col);
    for (int s = -1; s <= 1; s += 2) {
      int c = px + s * 3 - s * f;
      gfx->fillTriangle(c - 2, py + 6, c + 2, py + 6, c, py + 3, C_BG);
    }
  }
  if (fr) {
    uint16_t fc = (col == rgb(255, 255, 255)) ? rgb(255, 40, 40) : rgb(255, 190, 170);
    gfx->fillRect(px - 3, py - 3, 2, 2, fc);
    gfx->fillRect(px + 2, py - 3, 2, 2, fc);
    for (int i = 0; i < 4; i++) gfx->drawLine(px - 5 + i * 3, py + 2 + (i & 1), px - 2 + i * 3, py + 2 + ((i + 1) & 1), fc);
  } else {
    gfx->fillCircle(px - 3, py - 2, 2, C_TEXT);
    gfx->fillCircle(px + 3, py - 2, 2, C_TEXT);
    int ex = g.e.dx, ey = g.e.dy;
    gfx->fillRect(px - 3 + ex - (ex == 0), py - 2 + ey - (ey == 0), 2, 2, rgb(20, 20, 220));
    gfx->fillRect(px + 3 + ex - (ex == 0), py - 2 + ey - (ey == 0), 2, 2, rgb(20, 20, 220));
  }
}

static void pmDrawPac(uint32_t now) {
  float fx, fy;
  pmPos(pmPac, fx, fy);
  int px = (int)pmPx(fx), py = (int)pmPy(fy);
  float face = atan2f((float)pmHeadY, (float)pmHeadX), mouth;
  if (pmSt == PM_DYING) mouth = 0.3f + constrain((now - pmStateAt) / 1200.0f, 0.0f, 1.0f) * 2.9f, face = -1.5708f;
  else if (pmPac.dx || pmPac.dy) mouth = fabsf(sinf(now * 0.022f)) * 0.75f;
  else mouth = 0.35f;
  pmFan(px, py, 6, face, mouth, rgb(255, 235, 0));
}

static void pmRender(uint32_t now) {
  bool flash = pmSt == PM_CLEAR && ((now - pmStateAt) / 250) % 2 == 1;
  if (pmBgBuilt != (flash ? 1 : 0)) pmBuildBg(flash);
  if (pmBg) memcpy(frameBuffer(), pmBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  bool blink = (now / 220) & 1;
  if (pmSt != PM_CLEAR || !flash)
    for (int y = 0; y < PM_R; y++)
      for (int x = 0; x < PM_C; x++) {
        char c = pmGrid[y][x];
        if (c == '.') gfx->fillRect((int)pmPx(x) - 1, (int)pmPy(y) - 1, 3, 3, rgb(255, 190, 170));
        else if (c == 'o' && blink) {
          int cx = (int)pmPx(x), cy = (int)pmPy(y);
          gfx->drawCircle(cx, cy, 5, rgb(120, 70, 50));
          gfx->fillCircle(cx, cy, 4, rgb(255, 210, 190));
        }
      }
  if (pmSt != PM_COUNT || true) {
    if (pmSt != PM_DYING || (now - pmStateAt) < 1400) pmDrawPac(now);
  }
  if (pmSt != PM_DYING && pmSt != PM_CLEAR) for (int i = 0; i < 4; i++) pmDrawGhost(i, now);
  else if (pmSt == PM_CLEAR) for (int i = 0; i < 4; i++) pmDrawGhost(i, now);
  for (auto &p : pmPops) {
    if (!p.born || now - p.born > 900) continue;
    char b[8];
    snprintf(b, sizeof b, "%d", p.v);
    text(b, (int)p.x - (int)strlen(b) * 3, (int)(p.y - (now - p.born) * 0.01f), 1, rgb(0, 240, 255));
  }
  hudBar(pmScore, pmBest, C_BG, rgb(255, 230, 120));
  char b[16];
  snprintf(b, sizeof b, "L%d", pmLevel);
  text(b, 262, 9, 2, rgb(120, 170, 255));
  for (int i = 0; i < pmLives - (pmSt == PM_DYING ? 0 : 0); i++) pmFan(318 + i * 16, 16, 6, 0.0f, 0.5f, rgb(255, 235, 0));
  uint16_t acc = rgb(255, 235, 0);
  if (pmSt == PM_COUNT) {
    gfx->fillRoundRect(W / 2 - 130, 120, 260, 104, 16, C_PANEL);
    gfx->drawRoundRect(W / 2 - 130, 120, 260, 104, 16, acc);
    centerText("READY!", 136, 4, acc);
    centerText(pmCtrl ? "LEFT / RIGHT turn" : "TILT to steer", 188, 2, C_TEXT);
  } else if (pmSt == PM_PAUSE) {
    panelBox("PAUSED", nullptr, "RIGHT resume   LEFT menu", acc);
  } else if (pmSt == PM_CLEAR && now - pmStateAt > 1300) {
    panelBox("LEVEL CLEAR", nullptr, nullptr, acc);
  } else if (pmSt == PM_OVER) {
    snprintf(b, sizeof b, pmNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)pmScore);
    panelBox("GAME OVER", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void pmStart() {
  pmCtrl = pmOpts[0].value;
  if (pmCtrl == 0 && !imuOk) pmCtrl = 1;
  pmLevel = 1; pmLives = 3; pmScore = 0; pmNextLife = 10000; pmNewBest = false;
  pmBest = loadBest(pmCtrl ? "pmBestB" : "pmBestT");
  pmLoadMaze();
  pmBfs(13, PM_DOORY - 1, pmExit);
  pmBfs(13, PM_HOUSEY, pmHome);
  pmLastT = millis();
  pmBgBuilt = -1;
  pmBeginRound(2100);
  play(660, 90);
  pmRender(pmT0);
}

static void pmPopup(float fx, float fy, int v, uint32_t now) {
  for (auto &p : pmPops) if (!p.born || now - p.born > 900) { p = {pmPx(fx), pmPy(fy) - 8, v, now}; return; }
}

static uint32_t pmPauseAt;                         // a stray press right after pausing must not exit
static bool pmFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = fminf((now - pmLastT) / 1000.0f, 0.05f);
  pmLastT = now;
  switch (pmSt) {
    case PM_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - pmT0) * 3 / pmCountLen);
      if (n != lastN && n >= 1) play(660, 60);
      lastN = n;
      if (now - pmT0 >= pmCountLen) { pmSt = PM_PLAY; lastN = 3; imuZero(); sfxStart(); }
      break;
    }
    case PM_PLAY: {
      // ---- input ----
      if (pmCtrl == 0) {
        int dx, dy;
        if (tiltDir(dx, dy, 0.17f)) { pmDesX = dx; pmDesY = dy; }
        if (in.left || in.right) { pmSt = PM_PAUSE; pmPauseAt = now; sfxClick(); break; }
      } else {
        if (in.left) { int hx = pmDesX, hy = pmDesY; pmDesX = hy; pmDesY = -hx; play(1500, 12); }
        if (in.right) { int hx = pmDesX, hy = pmDesY; pmDesX = -hy; pmDesY = hx; play(1500, 12); }
      }
      if ((pmDesX == -pmPac.dx && pmDesY == -pmPac.dy) && (pmPac.dx || pmPac.dy)) {
        pmReverse(pmPac);
        pmHeadX = pmPac.dx; pmHeadY = pmPac.dy;
      }
      // ---- timers ----
      pmClock += dt;
      if (pmFrightLeft > 0) {
        pmFrightLeft -= dt;
        if (pmFrightLeft <= 0) { pmFrightLeft = 0; for (auto &g : pmG) g.fright = false; }
      } else {
        pmModeT += dt;
        float sc = fmaxf(0.4f, 1.0f - 0.12f * (pmLevel - 1));
        static const float DUR[8] = {7, 20, 7, 20, 5, 20, 5, 1e9f};
        float len = DUR[pmMode] * ((pmMode & 1) ? 1.0f : sc);
        if (pmMode < 7 && pmModeT >= len) {
          pmModeT = 0; pmMode++;
          for (auto &g : pmG) if (g.st == GS_NORM) pmReverse(g.e);
        }
      }
      // ---- movement ----
      pmAdvance(pmPac, pmPacSpeed() * dt, pmPacArrive, 0);
      for (int i = 0; i < 4; i++) {
        PmGhost &g = pmG[i];
        if (g.st == GS_HOUSE) {
          if (pmClock >= g.releaseAt) { g.st = GS_LEAVE; }
          else continue;
        }
        pmAdvance(g.e, pmGhostSpeed(g) * dt, pmGhostArrive, i);
      }
      if (pmScore >= pmNextLife) { pmNextLife += 10000; if (pmLives < 5) pmLives++; sfxBest(); }
      // ---- collisions ----
      float px, py;
      pmPos(pmPac, px, py);
      for (int i = 0; i < 4 && pmSt == PM_PLAY; i++) {
        PmGhost &g = pmG[i];
        if (g.st == GS_HOUSE || g.st == GS_EYES) continue;
        float gx, gy;
        pmPos(g.e, gx, gy);
        if ((gx - px) * (gx - px) + (gy - py) * (gy - py) > 0.55f * 0.55f) continue;
        if (g.fright) {
          g.st = GS_EYES; g.fright = false;
          int pts = 200 << min(pmChain, 3);
          pmChain++;
          pmScore += pts;
          pmPopup(gx, gy, pts, now);
          play(800, 40); play(1200, 80);
        } else {
          pmSt = PM_DYING; pmStateAt = now;
          for (int n = 0; n < 8; n++) play(700 - n * 70, 70);
        }
      }
      if (pmSt == PM_PLAY && pmDots <= 0) { pmSt = PM_CLEAR; pmStateAt = now; if (saveBest(pmCtrl ? "pmBestB" : "pmBestT", pmScore)) pmBest = pmScore; sfxBest(); }
      break;
    }
    case PM_PAUSE:
      if (in.right) { pmSt = PM_PLAY; imuZero(); sfxClick(); }
      if (in.left && now - pmPauseAt > 500) return false;
      break;
    case PM_DYING:
      if (now - pmStateAt > 1700) {
        if (--pmLives <= 0) { pmLives = 0; pmFinish(now); }
        else pmBeginRound(1500);
      }
      break;
    case PM_CLEAR:
      if (now - pmStateAt > 2200) {
        pmLevel++;
        pmLoadMaze();
        pmBgBuilt = -1;
        pmBeginRound(1800);
      }
      break;
    case PM_OVER:
      if (now - pmStateAt > 700) {
        if (in.right) { pmStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - pmLastRender >= 16) { pmLastRender = now; pmRender(now); }
  return true;
}

Game GAME_PACMAN = {"PACMAN", "eat the dots, dodge the ghosts", 0xFFE0, pmOpts, 1, pmStart, pmFrame};
