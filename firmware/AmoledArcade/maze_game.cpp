// maze_game.cpp - tilt-ball labyrinth. A steel ball rolls with acceleration, friction and bouncy walls through a
// random maze (recursive backtracker + a few loops). From level 2 there are holes: falling costs a life.
// Levels get bigger (smaller cells), score = faster is better. TILT only. LEFT = re-zero, RIGHT = pause.
// Without the tilt sensor: LEFT pushes the ball along the arrow, RIGHT turns the arrow.
#include "game.h"

enum MzSt : uint8_t { MZ_COUNT, MZ_PLAY, MZ_PAUSE, MZ_FALL, MZ_WIN, MZ_OVER };

static constexpr int MZ_MAXC = 18, MZ_MAXR = 14, MZ_MAXN = MZ_MAXC * MZ_MAXR, MZ_WT = 4, MZ_TOP = 32;
static constexpr float MZ_ACC = 2400.0f, MZ_VMAX = 430.0f, MZ_FRIC = 1.8f, MZ_BOUNCE = 0.38f;
static const uint8_t MZ_CS[6] = {48, 40, 34, 30, 26, 24};
static const int MZ_DX[4] = {0, 1, 0, -1}, MZ_DY[4] = {-1, 0, 1, 0};

static uint8_t mzWall[MZ_MAXN];       // bit d set = wall on side d (0 N, 1 E, 2 S, 3 W)
static bool mzHole[MZ_MAXN];
static int16_t mzDist[MZ_MAXN];
static int mzCS, mzCols, mzRows, mzOx, mzOy, mzGoal, mzLevel, mzLives;
static float mzR, mzHoleR;
static float mzBx, mzBy, mzVx, mzVy, mzLvTime, mzFx, mzFy;
static int mzDir;
static MzSt mzSt;
static uint32_t mzScore, mzBest, mzT0, mzCountLen, mzLastT, mzLastRender, mzStateAt, mzLastBump;
static int mzGain;
static bool mzNewBest;
static uint16_t *mzBg = nullptr;
struct MzPart { float x, y, vx, vy; uint32_t born; uint16_t c; };
static MzPart mzParts[28];

static inline int mzCellX(int c) { return mzOx + (c % mzCols) * mzCS; }
static inline int mzCellY(int c) { return mzOy + (c / mzCols) * mzCS; }
static inline float mzCx(int c) { return mzCellX(c) + mzCS * 0.5f; }
static inline float mzCy(int c) { return mzCellY(c) + mzCS * 0.5f; }

// ---------------- generation ----------------
static void mzBfs() {                                   // distances from the start cell, holes block
  static uint16_t q[MZ_MAXN];
  int n = mzCols * mzRows, qh = 0, qt = 0;
  for (int i = 0; i < n; i++) mzDist[i] = -1;
  mzDist[0] = 0; q[qt++] = 0;
  while (qh < qt) {
    int c = q[qh++], cx = c % mzCols, cy = c / mzCols;
    for (int d = 0; d < 4; d++) {
      if (mzWall[c] & (1 << d)) continue;
      int nx = cx + MZ_DX[d], ny = cy + MZ_DY[d];
      if (nx < 0 || ny < 0 || nx >= mzCols || ny >= mzRows) continue;
      int nc = ny * mzCols + nx;
      if (mzDist[nc] >= 0 || mzHole[nc]) continue;
      mzDist[nc] = mzDist[c] + 1;
      q[qt++] = nc;
    }
  }
}

static void mzGenerate() {
  mzCS = MZ_CS[min(mzLevel - 1, 5)];
  mzCols = min(W / mzCS, MZ_MAXC);
  mzRows = min((H - MZ_TOP) / mzCS, MZ_MAXR);
  mzOx = (W - mzCols * mzCS) / 2;
  mzOy = MZ_TOP + (H - MZ_TOP - mzRows * mzCS) / 2;
  mzR = fmaxf(6.5f, mzCS * 0.27f);
  mzHoleR = mzCS * 0.30f;
  int n = mzCols * mzRows;
  memset(mzWall, 15, n);
  memset(mzHole, 0, n);
  static uint8_t vis[MZ_MAXN];
  static uint16_t stk[MZ_MAXN];
  memset(vis, 0, n);
  int sp = 0;
  stk[sp++] = 0; vis[0] = 1;
  while (sp) {
    int c = stk[sp - 1], cx = c % mzCols, cy = c / mzCols, opt[4], no = 0;
    for (int d = 0; d < 4; d++) {
      int nx = cx + MZ_DX[d], ny = cy + MZ_DY[d];
      if (nx >= 0 && ny >= 0 && nx < mzCols && ny < mzRows && !vis[ny * mzCols + nx]) opt[no++] = d;
    }
    if (!no) { sp--; continue; }
    int d = opt[esp_random() % no], nc = (cy + MZ_DY[d]) * mzCols + cx + MZ_DX[d];
    mzWall[c] &= ~(1 << d);
    mzWall[nc] &= ~(1 << ((d + 2) & 3));
    vis[nc] = 1;
    stk[sp++] = nc;
  }
  if (mzLevel >= 2) {                                   // loops, so holes can be walked around
    for (int i = 0; i < n / 8; i++) {
      int c = esp_random() % n, d = esp_random() & 3, cx = c % mzCols, cy = c / mzCols;
      int nx = cx + MZ_DX[d], ny = cy + MZ_DY[d];
      if (nx < 0 || ny < 0 || nx >= mzCols || ny >= mzRows) continue;
      mzWall[c] &= ~(1 << d);
      mzWall[ny * mzCols + nx] &= ~(1 << ((d + 2) & 3));
    }
  }
  mzBfs();
  mzGoal = 0;
  for (int i = 0; i < n; i++) if (mzDist[i] > mzDist[mzGoal]) mzGoal = i;
  // holes: prefer cells on the shortest path, but always keep the goal reachable
  static uint16_t path[MZ_MAXN];
  int np = 0, cur = mzGoal;
  while (cur != 0 && np < n) {
    path[np++] = cur;
    int cx = cur % mzCols, cy = cur / mzCols, nxt = -1;
    for (int d = 0; d < 4 && nxt < 0; d++) {
      if (mzWall[cur] & (1 << d)) continue;
      int nx = cx + MZ_DX[d], ny = cy + MZ_DY[d];
      if (nx < 0 || ny < 0 || nx >= mzCols || ny >= mzRows) continue;
      if (mzDist[ny * mzCols + nx] == mzDist[cur] - 1) nxt = ny * mzCols + nx;
    }
    if (nxt < 0) break;
    cur = nxt;
  }
  int want = mzLevel < 2 ? 0 : min(1 + (mzLevel - 1) * 2, 14);
  for (int tries = 0; tries < 300 && want > 0; tries++) {
    int c = (np > 3 && (esp_random() % 10) < 6) ? path[esp_random() % np] : (int)(esp_random() % n);
    int cx = c % mzCols, cy = c / mzCols, gx = mzGoal % mzCols, gy = mzGoal / mzCols;
    if (c == 0 || c == mzGoal || mzHole[c]) continue;
    if (cx + cy < 3 || abs(cx - gx) + abs(cy - gy) < 2) continue;
    mzHole[c] = true;
    mzBfs();
    if (mzDist[mzGoal] < 0) { mzHole[c] = false; mzBfs(); continue; }
    want--;
  }
}

// ---------------- static drawing ----------------
static void mzWallRect(int c, int d, int &x0, int &y0, int &x1, int &y1) {
  int cx0 = mzCellX(c), cy0 = mzCellY(c), cx1 = cx0 + mzCS, cy1 = cy0 + mzCS, h = MZ_WT / 2;
  switch (d) {
    case 0: x0 = cx0 - h; x1 = cx1 + h; y0 = cy0 - h; y1 = cy0 + h; break;
    case 1: x0 = cx1 - h; x1 = cx1 + h; y0 = cy0 - h; y1 = cy1 + h; break;
    case 2: x0 = cx0 - h; x1 = cx1 + h; y0 = cy1 - h; y1 = cy1 + h; break;
    default: x0 = cx0 - h; x1 = cx0 + h; y0 = cy0 - h; y1 = cy1 + h; break;
  }
}
static void mzBuildBg() {
  if (!mzBg) mzBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!mzBg) return;
  gfx->fillScreen(C_BG);
  int n = mzCols * mzRows;
  for (int c = 0; c < n; c++) {
    bool alt = ((c % mzCols) + (c / mzCols)) & 1;
    gfx->fillRect(mzCellX(c), mzCellY(c), mzCS, mzCS, alt ? rgb(10, 12, 24) : rgb(13, 16, 30));
  }
  gfx->drawCircle((int)mzCx(0), (int)mzCy(0), (int)(mzCS * 0.34f), rgb(30, 90, 60));
  gfx->drawCircle((int)mzCx(0), (int)mzCy(0), (int)(mzCS * 0.34f) - 1, rgb(14, 44, 30));
  for (int c = 0; c < n; c++) {
    if (!mzHole[c]) continue;
    int hx = (int)mzCx(c), hy = (int)mzCy(c), hr = (int)mzHoleR;
    gfx->fillCircle(hx, hy + 1, hr + 3, rgb(4, 4, 8));
    gfx->fillCircle(hx, hy, hr + 2, rgb(58, 62, 84));
    for (int r = hr; r > 0; r--) gfx->fillCircle(hx, hy, r, lerpColor(rgb(0, 0, 0), rgb(22, 24, 40), (float)r / hr));
    gfx->drawCircle(hx, hy, hr + 3, rgb(90, 96, 130));
  }
  for (int pass = 0; pass < 3; pass++) {
    for (int c = 0; c < n; c++) {
      int cx = c % mzCols, cy = c / mzCols;
      for (int d = 0; d < 4; d++) {
        if (!(mzWall[c] & (1 << d))) continue;
        bool own = d == 0 || d == 3 || (d == 2 && cy == mzRows - 1) || (d == 1 && cx == mzCols - 1);
        if (!own) continue;
        int x0, y0, x1, y1;
        mzWallRect(c, d, x0, y0, x1, y1);
        if (pass == 0) gfx->fillRect(x0 - 3, y0 - 3, x1 - x0 + 6, y1 - y0 + 6, rgb(0, 18, 46));
        else if (pass == 1) gfx->fillRect(x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2, rgb(0, 60, 130));
        else {
          gfx->fillRect(x0, y0, x1 - x0, y1 - y0, rgb(60, 150, 255));
          if (x1 - x0 > y1 - y0) gfx->drawFastHLine(x0 + 1, (y0 + y1) / 2 - 1, x1 - x0 - 2, rgb(190, 230, 255));
          else gfx->drawFastVLine((x0 + x1) / 2 - 1, y0 + 1, y1 - y0 - 2, rgb(190, 230, 255));
        }
      }
    }
  }
  memcpy(mzBg, frameBuffer(), FB_PIXELS * 2);
}

// ---------------- ball ----------------
static void mzDrawBall(float fx, float fy, float scale) {
  float R = mzR * scale;
  int x = (int)fx, y = (int)fy, r = max(1, (int)R);
  gfx->fillEllipse(x + 2, y + (int)(R * 0.7f) + 2, (int)(R * 0.9f) + 1, (int)(R * 0.45f) + 1, rgb(2, 3, 6));
  for (int k = r; k >= 1; k--) {
    float t = 1.0f - (float)k / r;
    gfx->fillCircle(x - (int)((r - k) * 0.30f), y - (int)((r - k) * 0.34f), k,
                    lerpColor(rgb(54, 62, 78), rgb(214, 226, 240), t * t * 0.9f + t * 0.1f));
  }
  gfx->drawCircle(x, y, r, rgb(30, 36, 48));
  if (r >= 4) gfx->fillCircle(x - r / 3, y - r / 3 - 1, max(1, r / 4), C_TEXT);
}

// ---------------- physics ----------------
static float mzImpact;
static void mzCollideWalls() {
  int cx = (int)floorf((mzBx - mzOx) / mzCS), cy = (int)floorf((mzBy - mzOy) / mzCS);
  for (int yy = cy - 1; yy <= cy + 1; yy++)
    for (int xx = cx - 1; xx <= cx + 1; xx++) {
      if (xx < 0 || yy < 0 || xx >= mzCols || yy >= mzRows) continue;
      int c = yy * mzCols + xx;
      for (int d = 0; d < 4; d++) {
        if (!(mzWall[c] & (1 << d))) continue;
        int x0, y0, x1, y1;
        mzWallRect(c, d, x0, y0, x1, y1);
        float qx = constrain(mzBx, (float)x0, (float)x1), qy = constrain(mzBy, (float)y0, (float)y1);
        float dx = mzBx - qx, dy = mzBy - qy, d2 = dx * dx + dy * dy, nx, ny, pen;
        if (d2 >= mzR * mzR) continue;
        if (d2 > 1e-6f) {
          float dd = sqrtf(d2);
          nx = dx / dd; ny = dy / dd; pen = mzR - dd;
        } else {
          float l = mzBx - x0, r = x1 - mzBx, u = mzBy - y0, dn = y1 - mzBy, m = fminf(fminf(l, r), fminf(u, dn));
          nx = ny = 0;
          if (m == l) nx = -1; else if (m == r) nx = 1; else if (m == u) ny = -1; else ny = 1;
          pen = m + mzR;
        }
        mzBx += nx * pen; mzBy += ny * pen;
        float vn = mzVx * nx + mzVy * ny;
        if (vn < 0) {
          mzVx -= (1 + MZ_BOUNCE) * vn * nx;
          mzVy -= (1 + MZ_BOUNCE) * vn * ny;
          mzImpact = fmaxf(mzImpact, -vn);
        }
      }
    }
}

static void mzPhysics(float dt, float ax, float ay) {
  mzVx += ax * dt; mzVy += ay * dt;
  int n = mzCols * mzRows;
  for (int c = 0; c < n; c++) {                         // holes tug the ball a little
    if (!mzHole[c]) continue;
    float dx = mzCx(c) - mzBx, dy = mzCy(c) - mzBy, d = sqrtf(dx * dx + dy * dy), range = mzHoleR * 1.35f;
    if (d < range && d > 0.5f) {
      float k = (1.0f - d / range) * 700.0f * dt / d;
      mzVx += dx * k; mzVy += dy * k;
    }
  }
  float fr = expf(-MZ_FRIC * dt);
  mzVx *= fr; mzVy *= fr;
  float sp = sqrtf(mzVx * mzVx + mzVy * mzVy);
  if (sp > MZ_VMAX) { mzVx *= MZ_VMAX / sp; mzVy *= MZ_VMAX / sp; sp = MZ_VMAX; }
  int steps = max(1, (int)ceilf(sp * dt / 2.5f));
  float h = dt / steps;
  mzImpact = 0;
  for (int i = 0; i < steps; i++) { mzBx += mzVx * h; mzBy += mzVy * h; mzCollideWalls(); }
}

// ---------------- flow ----------------
static void mzPlaceBall() {
  mzBx = mzCx(0); mzBy = mzCy(0); mzVx = mzVy = 0;
}
static void mzNewLevel(uint32_t countLen) {
  mzGenerate();
  mzBuildBg();
  mzPlaceBall();
  mzLvTime = 0;
  memset(mzParts, 0, sizeof mzParts);
  mzCountLen = countLen;
  mzT0 = millis();
  mzSt = MZ_COUNT;
}

static void mzBurst(float x, float y, uint32_t now) {
  for (int i = 0; i < 28; i++) {
    float a = (esp_random() % 628) * 0.01f, s = 40.0f + esp_random() % 160;
    mzParts[i] = {x, y, cosf(a) * s, sinf(a) * s, now, (i & 1) ? rgb(120, 255, 170) : rgb(255, 255, 255)};
  }
}

static void mzFinish(uint32_t now) {
  mzNewBest = saveBest("mzBest", mzScore);
  if (mzNewBest) mzBest = mzScore;
  mzNewBest ? sfxBest() : sfxBad();
  mzStateAt = now;
  mzSt = MZ_OVER;
}

static void mzRender(uint32_t now) {
  if (mzBg) memcpy(frameBuffer(), mzBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  uint16_t acc = rgb(90, 200, 255);
  // goal
  float gx = mzCx(mzGoal), gy = mzCy(mzGoal), p = 0.5f + 0.5f * sinf(now * 0.006f), r0 = mzCS * 0.30f;
  gfx->fillCircle((int)gx, (int)gy, (int)(r0 + 6 + 2 * p), rgb(0, 30, 16));
  gfx->fillCircle((int)gx, (int)gy, (int)(r0 + 3 + p), rgb(0, 70, 36));
  gfx->fillCircle((int)gx, (int)gy, (int)r0, rgb(0, 150, 70));
  gfx->fillCircle((int)gx, (int)gy, (int)(r0 * 0.6f), rgb(120, 255, 160));
  gfx->fillCircle((int)gx, (int)gy, max(1, (int)(r0 * 0.25f)), C_TEXT);
  // ball
  if (mzSt == MZ_FALL) {
    float t = constrain((now - mzStateAt) / 450.0f, 0.0f, 1.0f);
    mzDrawBall(mzBx + (mzFx - mzBx) * t, mzBy + (mzFy - mzBy) * t, 1.0f - t);
  } else if (mzSt == MZ_WIN) {
    float t = constrain((now - mzStateAt) / 500.0f, 0.0f, 1.0f);
    mzDrawBall(mzBx + (gx - mzBx) * t, mzBy + (gy - mzBy) * t, 1.0f - t);
  } else if (mzSt != MZ_OVER) {
    mzDrawBall(mzBx, mzBy, 1.0f);
    if (!imuOk) {                                       // push-direction arrow
      int ax = (int)mzBx + MZ_DX[mzDir] * (int)(mzR + 10), ay = (int)mzBy + MZ_DY[mzDir] * (int)(mzR + 10);
      gfx->fillCircle(ax, ay, 4, C_ACC);
    }
  }
  for (auto &q : mzParts) {
    if (!q.born) continue;
    uint32_t age = now - q.born;
    if (age > 700) continue;
    float t = age * 0.001f;
    gfx->fillRect((int)(q.x + q.vx * t) - 1, (int)(q.y + q.vy * t + 200 * t * t) - 1, 3, 3, lerpColor(q.c, C_BG, age / 700.0f));
  }
  hudBar(mzScore, mzBest, rgb(6, 12, 30), rgb(150, 210, 255));
  char b[24];
  snprintf(b, sizeof b, "%.1f", mzLvTime);
  text(b, 262, 5, 2, C_TEXT);
  snprintf(b, sizeof b, "LV %d", mzLevel);
  text(b, 262, 22, 1, acc);
  for (int i = 0; i < 3; i++) {
    int x = 338 + i * 14;
    if (i < mzLives) gfx->fillCircle(x, 16, 5, rgb(200, 215, 235));
    else gfx->drawCircle(x, 16, 5, 0x4208);
  }
  if (mzSt == MZ_COUNT) {
    snprintf(b, sizeof b, "LEVEL %d", mzLevel);
    gfx->fillRoundRect(W / 2 - 120, 120, 240, 96, 16, C_PANEL);
    gfx->drawRoundRect(W / 2 - 120, 120, 240, 96, 16, acc);
    centerText(b, 136, 4, acc);
    centerText(imuOk ? "TILT to roll   RIGHT pause" : "LEFT push  RIGHT turn arrow", 244, 2, C_DIM);
    int cnt = 3 - (int)((now - mzT0) * 3 / mzCountLen);
    char d[2] = {(char)('0' + constrain(cnt, 1, 3)), 0};
    centerText(d, 176, 4, C_TEXT);
  } else if (mzSt == MZ_PAUSE) {
    panelBox("PAUSED", nullptr, "RIGHT resume   LEFT menu", acc);
  } else if (mzSt == MZ_WIN && now - mzStateAt > 500) {
    snprintf(b, sizeof b, "+%d", mzGain);
    panelBox("LEVEL CLEAR", b, "time bonus", acc);
  } else if (mzSt == MZ_OVER) {
    snprintf(b, sizeof b, mzNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)mzScore);
    panelBox("GAME OVER", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void mzStart() {
  mzLevel = 1; mzLives = 3; mzScore = 0; mzNewBest = false; mzDir = 1;
  mzBest = loadBest("mzBest");
  mzLastT = millis();
  mzNewLevel(2100);
  play(660, 90);
  mzRender(mzT0);
}

static uint32_t mzPauseAt;                         // a stray LEFT right after pausing must not exit
static bool mzFrame(uint32_t now) {
  float dt = fminf((now - mzLastT) / 1000.0f, 0.04f);
  mzLastT = now;
  switch (mzSt) {
    case MZ_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - mzT0) * 3 / mzCountLen);
      if (n != lastN && n >= 1) play(660, 70);
      lastN = n;
      if (now - mzT0 >= mzCountLen) { mzSt = MZ_PLAY; lastN = 3; imuZero(); sfxStart(); }
      break;
    }
    case MZ_PLAY: {
      float ax = 0, ay = 0;
      if (imuOk) {
        auto dz = [](float t) { return fabsf(t) < 0.02f ? 0.0f : (t > 0 ? t - 0.02f : t + 0.02f); };
        ax = dz(axisX()) * MZ_ACC; ay = dz(axisY()) * MZ_ACC;
        if (in.left) { imuZero(); sfxClick(); }
        if (in.right) { mzSt = MZ_PAUSE; mzPauseAt = now; sfxClick(); break; }
      } else {
        if (in.right) { mzDir = (mzDir + 1) & 3; sfxClick(); }
        if (in.leftDown) { ax = MZ_DX[mzDir] * 1500.0f; ay = MZ_DY[mzDir] * 1500.0f; }
      }
      mzLvTime += dt;
      mzPhysics(dt, ax, ay);
      if (mzImpact > 130.0f && now - mzLastBump > 70) { mzLastBump = now; play(150 + (int)fminf(mzImpact, 400.0f), 10); }
      int n = mzCols * mzRows;
      for (int c = 0; c < n; c++) {
        if (!mzHole[c]) continue;
        float dx = mzCx(c) - mzBx, dy = mzCy(c) - mzBy;
        if (dx * dx + dy * dy < mzHoleR * mzHoleR * 0.45f) {
          mzSt = MZ_FALL; mzStateAt = now; mzFx = mzCx(c); mzFy = mzCy(c);
          play(500, 60); play(300, 80); play(150, 120);
          break;
        }
      }
      float gx = mzCx(mzGoal) - mzBx, gy = mzCy(mzGoal) - mzBy;
      if (mzSt == MZ_PLAY && gx * gx + gy * gy < (mzCS * 0.22f) * (mzCS * 0.22f)) {
        mzSt = MZ_WIN; mzStateAt = now;
        mzGain = max(100, 300 + 120 * mzLevel - (int)(7 * mzLvTime));
        mzScore += mzGain;
        if (saveBest("mzBest", mzScore)) mzBest = mzScore;
        mzBurst(mzCx(mzGoal), mzCy(mzGoal), now);
        sfxBest();
      }
      break;
    }
    case MZ_PAUSE:
      if (in.right) { mzSt = MZ_PLAY; imuZero(); sfxClick(); }
      if (in.left && now - mzPauseAt > 500) return false;
      break;
    case MZ_FALL:
      if (now - mzStateAt > 450) {
        if (--mzLives <= 0) { mzLives = 0; mzFinish(now); }
        else { mzPlaceBall(); mzSt = MZ_PLAY; imuZero(); sfxBad(); }
      }
      break;
    case MZ_WIN:
      if (now - mzStateAt > 1800) { mzLevel++; mzNewLevel(1400); }
      break;
    case MZ_OVER:
      if (now - mzStateAt > 700) {
        if (in.right) { mzStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - mzLastRender >= 16) { mzLastRender = now; mzRender(now); }
  return true;
}

Game GAME_MAZE = {"MAZE", "tilt the ball to the goal", 0x5D7F, nullptr, 0, mzStart, mzFrame};
