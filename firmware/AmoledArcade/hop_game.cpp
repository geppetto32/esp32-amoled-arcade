// hop_game.cpp - Hop, a Frogger with tilt hops on a neon road.
//   FLICK the board (quick tilt, then back) = hop one cell in that direction (up / left / right / down)
//   LEFT = hop up (forward)   RIGHT = hop sideways: toward the tilt side (down if tilted toward you); with no tilt,
//           right, or left while LEFT is held
//   TAP / SWIPE = hop toward the tap / swipe, relative to the frog
// Four lanes of traffic, a median, five river lanes of logs and (diving) turtles, five home bays, a timer per frog.
// Fill all five bays to start a faster level. Three lives. DIFF = EASY / NORMAL / HARD, SENSE = flick threshold.
#include "engine.h"
#include <math.h>

static const char *const HP_DIFF[] = {"NORMAL", "EASY", "HARD"};
static const char *const HP_SENSE[] = {"MEDIUM", "LOW", "HIGH"};
static Option hpOpts[] = {{"DIFF", HP_DIFF, 3, "hpDiff", 0}, {"SENSE", HP_SENSE, 3, "hpSense", 0}};

enum HpSt : uint8_t { HP_INTRO, HP_PLAY, HP_DYING, HP_LEVEL, HP_OVER };
enum : uint8_t { HPL_CAR, HPL_TRUCK, HPL_LOG, HPL_TURTLE };

static constexpr int HP_C = 28, HP_COLS = 16, HP_ROWS = 12, HP_TOP = 32, HP_MAXOBJ = 10;
static constexpr int HP_RIVER0 = 1, HP_RIVER1 = 5, HP_MEDIAN = 6, HP_ROAD0 = 7, HP_ROAD1 = 10, HP_START = 11;

struct HpLane { uint8_t type; int8_t dir; float speed, w, sp, off; uint8_t count; uint16_t col; };

static HpLane hpL[HP_ROWS];
static bool hpBay[5];
static HpSt hpSt;
static int hpRow, hpMinRow, hpLives, hpLevel, hpFromRow, hpDeadHow;
static float hpX, hpFromX, hpHopT, hpTime, hpTimeMax, hpSpeedMul, hpTh, hpGT;
static int8_t hpFace, hpPend;
static uint32_t hpScore, hpBest, hpStAt, hpLastT, hpLastRender;
static bool hpNewBest;
static Flick hpFlick;
static SnapDir hpSnap;

static int hpRowY(int r) { return HP_TOP + r * HP_C; }
static int hpBayX(int i) { return 20 + i * 88; }

static void hpSetupLevel() {
  float m = hpSpeedMul * (1.0f + 0.13f * (hpLevel - 1));
  auto L = [&](int r, uint8_t type, int dir, float spd, float w, float sp, uint16_t col) {
    HpLane &l = hpL[r];
    l.type = type; l.dir = (int8_t)dir; l.speed = spd * m; l.w = w; l.sp = sp;
    int n = (int)ceilf((W + w) / sp);
    l.count = (uint8_t)min(n, HP_MAXOBJ);
    l.off = (float)(esp_random() % 400); l.col = col;
  };
  memset(hpL, 0, sizeof hpL);
  L(1, HPL_LOG, 1, 38, 112, 190, rgb(200, 120, 50));
  L(2, HPL_TURTLE, -1, 52, 84, 150, rgb(60, 230, 160));
  L(3, HPL_LOG, 1, 66, 84, 168, rgb(210, 130, 55));
  L(4, HPL_TURTLE, -1, 44, 56, 130, rgb(70, 220, 200));
  L(5, HPL_LOG, 1, 54, 140, 230, rgb(190, 110, 50));
  L(7, HPL_CAR, -1, 78, 36, 150, rgb(255, 70, 120));
  L(8, HPL_TRUCK, 1, 58, 72, 230, rgb(255, 170, 40));
  L(9, HPL_CAR, -1, 112, 36, 190, rgb(90, 200, 255));
  L(10, HPL_CAR, 1, 70, 36, 120, rgb(200, 90, 255));
}

static float hpObjX(const HpLane &l, int i) {
  float len = l.count * l.sp;
  float p = fmodf(l.off + i * l.sp, len);
  if (p < 0) p += len;
  return p - l.w;                        // [-w, len-w)
}
static bool hpDived(int row, int i) {
  if (hpL[row].type != HPL_TURTLE || hpLevel < 2) return false;
  return fmodf(hpGT + i * 1.7f + row * 0.9f, 6.5f) > 4.6f;
}
static bool hpSinking(int row, int i) {
  if (hpL[row].type != HPL_TURTLE || hpLevel < 2) return false;
  float p = fmodf(hpGT + i * 1.7f + row * 0.9f, 6.5f);
  return p > 3.8f && p <= 4.6f;
}

static void hpRespawn(uint32_t now) {
  hpRow = HP_START; hpX = 8 * HP_C; hpFromRow = hpRow; hpFromX = hpX; hpHopT = 1; hpMinRow = HP_START;
  hpTime = hpTimeMax; hpFace = 0; hpPend = -1;
  hpSnap.cur = DIR_NONE; imuZero(); hpFlick.reset(now);
}

static void hpStart() {
  hpSpeedMul = hpOpts[0].value == 1 ? 0.8f : hpOpts[0].value == 2 ? 1.25f : 1.0f;
  hpTimeMax = hpOpts[0].value == 1 ? 45 : hpOpts[0].value == 2 ? 28 : 36;
  hpTh = hpOpts[1].value == 1 ? 0.5f : hpOpts[1].value == 2 ? 0.28f : 0.38f;
  hpBest = loadBest("hpBest");
  hpScore = 0; hpLives = 3; hpLevel = 1; hpNewBest = false; hpGT = 0;
  memset(hpBay, 0, sizeof hpBay);
  hpSetupLevel();
  ptClear();
  hpRespawn(millis());
  uint32_t now = millis();
  hpSt = HP_INTRO; hpStAt = now; hpLastT = now; hpLastRender = 0;
  sfxStart();
}

static void hpKill(uint32_t now, int how) {      // 0 car, 1 water, 2 time, 3 wall
  hpSt = HP_DYING; hpStAt = now; hpDeadHow = how;
  float cx = hpX + HP_C / 2.0f, cy = hpRowY(hpRow) + HP_C / 2.0f;
  if (how == 1) { ptBurst(cx, cy, 24, 110, rgb(120, 200, 255), rgb(0, 20, 60), 0.7f, 120, 3); play(180, 80); play(120, 90); }
  else { ptBurst(cx, cy, 28, 150, rgb(120, 255, 90), rgb(255, 40, 40), 0.7f, 60, 3); play(300, 60); play(200, 70); play(140, 120); }
  tunePlay(TUNE_LOSE);
}

static void hpTryHop(int dx, int dy, uint32_t now) {   // dx,dy in cells
  if (hpSt != HP_PLAY) return;
  if (hpHopT < 1.0f) { hpPend = (int8_t)((dy < 0) ? 0 : (dx < 0) ? 1 : (dx > 0) ? 2 : 3); return; }
  int nr = hpRow + dy;
  float nx = hpX + dx * HP_C;
  if (nr < 0 || nr > HP_START) return;
  if (nx < 0 || nx > W - HP_C) return;
  hpFromRow = hpRow; hpFromX = hpX; hpRow = nr; hpX = nx; hpHopT = 0;
  hpFace = (int8_t)(dy < 0 ? 0 : dy > 0 ? 2 : dx < 0 ? 3 : 1);
  play(dy < 0 ? 620 : 480, 18);
  if (nr < hpMinRow) { hpMinRow = nr; hpScore += 10; }
  if (hpScore > hpBest) hpBest = hpScore;
  (void)now;
}
static void hpDirHop(int d, uint32_t now) {      // 0 up 1 left 2 right 3 down
  hpTryHop(d == 1 ? -1 : d == 2 ? 1 : 0, d == 0 ? -1 : d == 3 ? 1 : 0, now);
}

static void hpLanded(uint32_t now) {
  if (hpRow != 0) return;
  float c = hpX + HP_C / 2.0f;
  for (int i = 0; i < 5; i++) {
    float bx = (float)hpBayX(i);
    if (c >= bx - 2 && c <= bx + 58) {
      if (hpBay[i]) { hpKill(now, 3); return; }
      hpBay[i] = true;
      int bonus = (int)hpTime * 10;
      hpScore += 50 + bonus;
      if (hpScore > hpBest) hpBest = hpScore;
      ptBurst(bx + 28, hpRowY(0) + 14, 26, 130, rgb(255, 240, 120), rgb(40, 200, 80), 0.8f);
      bool all = true;
      for (int k = 0; k < 5; k++) all = all && hpBay[k];
      if (all) {
        hpScore += 500; hpSt = HP_LEVEL; hpStAt = now; tunePlay(TUNE_WIN);
        if (hpScore > hpBest) hpBest = hpScore;
      } else { tunePlay(TUNE_LEVEL); hpRespawn(now); }
      return;
    }
  }
  hpKill(now, 3);
}

static void hpUpdate(float dt, uint32_t now) {
  hpGT += dt;
  for (int r = 1; r <= 10; r++) {
    HpLane &l = hpL[r];
    if (!l.count) continue;
    l.off += l.dir * l.speed * dt;
    float len = l.count * l.sp;
    if (l.off > len) l.off -= len;
    if (l.off < -len) l.off += len;
  }
  if (hpSt != HP_PLAY) return;
  if (hpHopT < 1.0f) {
    hpHopT = fminf(1.0f, hpHopT + dt / 0.11f);
    if (hpHopT >= 1.0f) {
      hpLanded(now);
      if (hpSt != HP_PLAY) return;
      if (hpPend >= 0) { int p = hpPend; hpPend = -1; hpDirHop(p, now); }
    }
  }
  hpTime -= dt;
  if (hpTime <= 0) { hpKill(now, 2); return; }
  const HpLane &l = hpL[hpRow];
  float fx0 = hpX + 6, fx1 = hpX + HP_C - 6, c = hpX + HP_C / 2.0f;
  if (hpRow >= HP_ROAD0 && hpRow <= HP_ROAD1) {
    for (int i = 0; i < l.count; i++) {
      float ox = hpObjX(l, i);
      if (fx1 > ox + 2 && fx0 < ox + l.w - 2) { hpKill(now, 0); return; }
    }
  } else if (hpRow >= HP_RIVER0 && hpRow <= HP_RIVER1) {
    bool on = false;
    for (int i = 0; i < l.count; i++) {
      float ox = hpObjX(l, i);
      if (c >= ox + 1 && c <= ox + l.w - 1 && !hpDived(hpRow, i)) { on = true; break; }
    }
    if (!on) { hpKill(now, 1); return; }
    hpX += l.dir * l.speed * dt;
    hpFromX += l.dir * l.speed * dt;
    if (hpX < -HP_C / 2.0f || hpX > W - HP_C / 2.0f) { hpKill(now, 3); return; }
  }
}

// ---------------- drawing ----------------
static void hpDrawObj(const HpLane &l, int row, int i) {
  float ox = hpObjX(l, i);
  int x = (int)ox, y = hpRowY(row) + 3, h = HP_C - 6, w = (int)l.w;
  if (x > W || x + w < 0) return;
  uint16_t c = l.col;
  switch (l.type) {
    case HPL_CAR: case HPL_TRUCK:
      gfx->fillRoundRect(x - 2, y - 2, w + 4, h + 4, 7, lerpColor(c, C_BG, 0.78f));
      gfx->fillRoundRect(x, y, w, h, 5, lerpColor(c, C_BG, 0.55f));
      gfx->drawRoundRect(x, y, w, h, 5, c);
      {
        int hx = l.dir > 0 ? x + w - 9 : x + 4;
        gfx->fillRect(hx, y + 3, 5, h - 6, lerpColor(c, C_TEXT, 0.7f));           // windscreen
        gfx->fillRect(l.dir > 0 ? x + w - 2 : x, y + 3, 2, 3, rgb(255, 255, 200));   // headlights
        gfx->fillRect(l.dir > 0 ? x + w - 2 : x, y + h - 6, 2, 3, rgb(255, 255, 200));
        if (l.type == HPL_TRUCK) gfx->drawFastVLine(l.dir > 0 ? x + w - 22 : x + 22, y + 2, h - 4, c);
      }
      break;
    case HPL_LOG:
      gfx->fillRoundRect(x - 2, y - 1, w + 4, h + 2, 8, lerpColor(c, C_BG, 0.8f));
      gfx->fillRoundRect(x, y, w, h, 7, lerpColor(c, C_BG, 0.5f));
      gfx->drawRoundRect(x, y, w, h, 7, c);
      for (int k = 14; k < w - 8; k += 22) gfx->drawFastVLine(x + k, y + 5, h - 10, lerpColor(c, C_BG, 0.35f));
      break;
    default: {
      bool dv = hpDived(row, i), sk = hpSinking(row, i);
      int n = (int)(l.w / HP_C);
      for (int k = 0; k < n; k++) {
        int cx = x + k * HP_C + HP_C / 2, cy = hpRowY(row) + HP_C / 2;
        if (dv) { gfx->drawCircle(cx, cy, 6, lerpColor(c, C_BG, 0.85f)); continue; }
        if (sk && ((int)(hpGT * 8) & 1)) { gfx->drawCircle(cx, cy, 10, c); continue; }
        gfx->fillCircle(cx, cy, 12, lerpColor(c, C_BG, 0.8f));
        gfx->fillCircle(cx, cy, 10, lerpColor(c, C_BG, 0.5f));
        gfx->drawCircle(cx, cy, 10, c);
        gfx->drawLine(cx - 5, cy, cx + 5, cy, lerpColor(c, C_BG, 0.3f));
        gfx->drawLine(cx, cy - 5, cx, cy + 5, lerpColor(c, C_BG, 0.3f));
      }
    }
  }
}

static void hpDrawFrog(float x, float y, int face, float lift, uint16_t body) {
  int cx = (int)(x + HP_C / 2), cy = (int)(y + HP_C / 2);
  int r = 10 + (int)(lift * 3);
  gfx->fillCircle(cx, cy, r + 3, lerpColor(body, C_BG, 0.82f));
  gfx->fillCircle(cx, cy, r, lerpColor(body, C_BG, 0.35f));
  gfx->drawCircle(cx, cy, r, body);
  int ex = face == 1 ? 1 : face == 3 ? -1 : 0, ey = face == 0 ? -1 : face == 2 ? 1 : 0;
  int px = -ey, py = ex;                                       // perpendicular
  for (int s = -1; s <= 1; s += 2) {
    int e0x = cx + ex * 6 + px * 5 * s, e0y = cy + ey * 6 + py * 5 * s;
    gfx->fillCircle(e0x, e0y, 3, C_TEXT);
    gfx->fillCircle(e0x + ex, e0y + ey, 1, C_BG);
  }
}

static void hpRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  // river and road backgrounds
  uint32_t t = now / 90;
  for (int r = 0; r < HP_ROWS; r++) {
    int y = hpRowY(r);
    if (r >= HP_RIVER0 && r <= HP_RIVER1) {
      gfx->fillRect(0, y, W, HP_C, rgb(4, 14, 44));
      for (int k = 0; k < 7; k++) {
        int rx = (int)((k * 67 + r * 31 + (hpL[r].dir > 0 ? t : -(int)t) * 2) % (W + 20));
        if (rx < 0) rx += W + 20;
        gfx->drawFastHLine(rx - 10, y + 6 + (k * 5 + r * 3) % 16, 10, rgb(20, 50, 110));
      }
    } else if (r >= HP_ROAD0 && r <= HP_ROAD1) {
      gfx->fillRect(0, y, W, HP_C, rgb(10, 10, 22));
      if (r != HP_ROAD1) for (int x = 0; x < W; x += 36) gfx->drawFastHLine(x, y + HP_C - 1, 18, rgb(60, 60, 20));
    } else if (r == HP_MEDIAN || r == HP_START) {
      gfx->fillRect(0, y, W, HP_C, rgb(10, 30, 16));
      for (int x = 8; x < W; x += 28) gfx->drawPixel(x + (r * 7) % 9, y + 6 + (x * 3) % 14, rgb(40, 110, 50));
    }
  }
  gfx->drawFastHLine(0, hpRowY(HP_MEDIAN), W, rgb(40, 200, 90));
  gfx->drawFastHLine(0, hpRowY(HP_ROAD0), W, rgb(255, 60, 100));
  gfx->drawFastHLine(0, hpRowY(HP_START), W, rgb(40, 200, 90));
  // home row: hedge with bays
  int y0 = hpRowY(0);
  gfx->fillRect(0, y0, W, HP_C, rgb(6, 36, 14));
  gfx->drawFastHLine(0, y0, W, rgb(40, 200, 90));
  float pulse = 0.5f + 0.5f * sinf(now * 0.005f);
  for (int i = 0; i < 5; i++) {
    int bx = hpBayX(i);
    gfx->fillRect(bx, y0 + 2, 56, HP_C - 2, rgb(4, 14, 44));
    uint16_t bc = hpBay[i] ? C_GOOD : lerpColor(rgb(40, 120, 60), rgb(120, 255, 140), pulse);
    gfx->drawRect(bx - 1, y0 + 1, 58, HP_C - 1, bc);
    if (hpBay[i]) hpDrawFrog((float)(bx + 14), (float)y0, 2, 0, rgb(120, 255, 90));
    else { gfx->fillCircle(bx + 28, y0 + 15, 6, lerpColor(rgb(0, 40, 20), rgb(60, 220, 120), pulse)); }
  }
  // objects
  for (int r = HP_RIVER0; r <= HP_ROAD1; r++) {
    if (!hpL[r].count) continue;
    for (int i = 0; i < hpL[r].count; i++) hpDrawObj(hpL[r], r, i);
  }
  // frog
  if (hpSt == HP_PLAY || hpSt == HP_INTRO || hpSt == HP_LEVEL) {
    float u = hpHopT, e = u * (2 - u);
    float fx = hpFromX + (hpX - hpFromX) * e, fy = hpRowY(hpFromRow) + (hpRowY(hpRow) - hpRowY(hpFromRow)) * e;
    hpDrawFrog(fx, fy, hpFace, u < 1 ? sinf(u * (float)M_PI) : 0, rgb(120, 255, 90));
  }
  ptDraw();
  hudBar(hpScore, hpBest, rgb(8, 22, 18), C_GOOD);
  char b[16];
  snprintf(b, sizeof b, "L%d", hpLevel);
  text(b, 300, 9, 2, C_ACC);
  for (int i = 0; i < hpLives; i++) gfx->fillCircle(246 + i * 16, 16, 5, rgb(120, 255, 90));
  // timer bar
  float tf = constrain(hpTime / hpTimeMax, 0.0f, 1.0f);
  gfx->fillRect(0, H - 4, (int)(W * tf), 4, tf < 0.25f ? C_BAD : C_GOOD);
  if (hpSt == HP_INTRO) {
    gfx->fillRoundRect(50, 120, W - 100, 116, 16, C_PANEL);
    gfx->drawRoundRect(50, 120, W - 100, 116, 16, C_GOOD);
    centerText("HOP", 130, 4, C_GOOD);
    centerText("flick = hop   LEFT up   RIGHT side", 176, 1, C_TEXT);
    centerText("tap / swipe also hops", 192, 1, C_DIM);
    int left = 3 - (int)((now - hpStAt) / 800);
    if (left < 1) left = 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 206, 3, C_ACC);
  } else if (hpSt == HP_LEVEL) {
    panelBox("LEVEL CLEAR", "+500", nullptr, C_GOOD);
  } else if (hpSt == HP_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, hpNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)hpScore);
    panelBox("GAME OVER", l2, "RIGHT again  LEFT menu", C_GOOD);
  }
  dirty = true;
}

static bool hpFrame(uint32_t now) {
  homeLock = true;
  float dt = fminf((now - hpLastT) / 1000.0f, 0.05f);
  hpLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  switch (hpSt) {
    case HP_INTRO:
      hpUpdate(dt, now);
      if (now - hpStAt >= 2400) { hpSt = HP_PLAY; imuZero(); hpFlick.reset(now); }
      break;
    case HP_PLAY: {
      if (in.left) { hpDirHop(0, now); }
      if (in.right) {
        uint8_t d = hpSnap.cur;
        if (d == DIR_L) hpDirHop(1, now);
        else if (d == DIR_R) hpDirHop(2, now);
        else if (d == DIR_D) hpDirHop(3, now);
        else hpDirHop(in.leftDown ? 1 : 2, now);
      }
      hpSnap.update(axisX(), axisY());
      uint8_t f = hpFlick.update(now, hpTh);
      if (f == DIR_U) hpDirHop(0, now);
      else if (f == DIR_L) hpDirHop(1, now);
      else if (f == DIR_R) hpDirHop(2, now);
      else if (f == DIR_D) hpDirHop(3, now);
      auto rel = [&](float dx, float dy) {
        if (fabsf(dx) < 12 && fabsf(dy) < 12) return;
        if (fabsf(dx) > fabsf(dy)) hpDirHop(dx > 0 ? 2 : 1, now); else hpDirHop(dy > 0 ? 3 : 0, now);
      };
      if (in.tap) rel(in.tapX - (hpX + HP_C / 2.0f), in.tapY - (hpRowY(hpRow) + HP_C / 2.0f));
      if (in.dragEnd && (abs(in.dragTX) > 30 || abs(in.dragTY) > 30)) rel((float)in.dragTX, (float)in.dragTY);
      hpUpdate(dt, now);
      break;
    }
    case HP_DYING:
      hpUpdate(dt, now);
      if (now - hpStAt > 900) {
        if (--hpLives <= 0) {
          hpSt = HP_OVER; hpStAt = now;
          hpNewBest = saveBest("hpBest", hpScore);
          if (hpNewBest) { hpBest = hpScore; tunePlay(TUNE_WIN); }
        } else { hpRespawn(now); hpSt = HP_PLAY; }
      }
      break;
    case HP_LEVEL:
      hpUpdate(dt, now);
      if (now - hpStAt > 1800) {
        hpLevel++; memset(hpBay, 0, sizeof hpBay); hpSetupLevel(); hpRespawn(now); hpSt = HP_PLAY;
      }
      break;
    case HP_OVER:
      hpUpdate(dt, now);
      if (now - hpStAt > 900) {
        if (in.right) { hpStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - hpLastRender >= 16) { hpLastRender = now; hpRender(now); }
  return true;
}

Game GAME_HOP = {"Hop", "cross road and river, tilt hops", 0x87F0, hpOpts, 2, hpStart, hpFrame};
