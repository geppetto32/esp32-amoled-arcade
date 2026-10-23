// missile_game.cpp - Missile Command. Six cities and two silos defend against missile trails raining from the top.
//   TILT moves the crosshair (smoothed, proportional)
//   LEFT fires from the LEFT silo, RIGHT fires from the RIGHT silo (8 shots each per wave)
//   TAP the screen = fire from the nearest silo at that point
// Counter-blasts expand and fade; anything inside a blast explodes in a smaller chain blast.
// Waves add faster missiles, MIRVs (split in two or three), bombers that drop missiles, and smart bombs that dodge.
// After a wave: bonus for every surviving city and unused shot; a lost city comes back every 8000 points.
// THEME: NEON (cyan blasts on deep blue) or AMBER (retro orange on black).
#include "engine.h"
#include <math.h>

static const char *const MS_THEMES[] = {"NEON", "AMBER"};
static Option msOpts[] = {{"THEME", MS_THEMES, 2, "msTheme", 0}};

enum MsSt : uint8_t { MS_INTRO, MS_PLAY, MS_CLEAR, MS_OVER };

static constexpr int MS_GROUND = H - 36;
static constexpr int MS_NCITY = 6, MS_MAXE = 28, MS_MAXS = 10, MS_MAXB = 32, MS_MAXP = 2, MS_AMMO = 8;
static const int MS_CITY_X[MS_NCITY] = {96, 146, 196, 252, 302, 352};
static const int MS_SILO_X[2] = {34, 414};

struct MsEnemy { float x0, y0, x, y, vx, vy, tx, ty; bool on, mirv, smart, big; int8_t target; };
struct MsShot { float sx, sy, x, y, tx, ty, vx, vy; bool on; };
struct MsBlast { float x, y, maxR, t, dur; bool on, enemy; };
struct MsPlane { float x, y, vx, nextDrop; bool on; };

static MsEnemy msE[MS_MAXE];
static MsShot msS[MS_MAXS];
static MsBlast msB[MS_MAXB];
static MsPlane msP[MS_MAXP];
static bool msCityOn[MS_NCITY], msSiloOn[2];
static int msAmmo[2];
static MsSt msSt;
static int msTheme, msWave, msToSpawn, msPlanesLeft, msBonusCities, msBonusAmmo, msBonusPts;
static uint32_t msScore, msBest, msNextBonus, msStAt, msLastT, msLastRender, msIntroLen, msOverAt;
static float msSpawnT, msSpeed, msPlaneT;
static bool msNewBest;
static TiltCursor msCur;
static uint16_t *msBg = nullptr;
static int msBgTheme = -1;

static inline uint32_t msHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static float msRand() { return (esp_random() & 0xFFFF) / 65535.0f; }

// theme colours
static uint16_t msColPlayer() { return msTheme ? rgb(255, 180, 60) : rgb(70, 230, 255); }
static uint16_t msColEnemy() { return msTheme ? rgb(255, 90, 40) : rgb(255, 70, 110); }
static uint16_t msColCity() { return msTheme ? rgb(255, 170, 50) : rgb(60, 220, 200); }
static uint16_t msColAccent() { return msTheme ? rgb(255, 200, 90) : rgb(120, 230, 255); }

// ---------------- background ----------------
static void msBuildBg() {
  if (!msBg) msBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!msBg) return;
  bool a = msTheme == 1;
  uint16_t top = a ? rgb(0, 0, 0) : rgb(2, 3, 14), hor = a ? rgb(34, 14, 4) : rgb(18, 14, 52);
  for (int y = 0; y < MS_GROUND; y++) gfx->drawFastHLine(0, y, W, lerpColor(top, hor, powf(y / (float)MS_GROUND, 2.2f)));
  for (int i = 0; i < 90; i++) {
    uint32_t h = msHash(i, 3);
    int x = h % W, y = 34 + (h >> 9) % (MS_GROUND - 80), b = 50 + (h >> 20) % 150;
    gfx->drawPixel(x, y, a ? rgb(b, b * 3 / 4, b / 2) : rgb(b * 3 / 4, b * 3 / 4, b));
  }
  for (int x = 0; x < W; x++) {                                    // mountains
    int h1 = MS_GROUND - 18 - (int)(14 * sinf(x * 0.017f + 0.5f) + 8 * sinf(x * 0.05f + 2.0f));
    gfx->drawFastVLine(x, h1, MS_GROUND - h1, a ? rgb(26, 10, 4) : rgb(10, 8, 34));
  }
  for (int y = MS_GROUND; y < H; y++)                              // ground
    gfx->drawFastHLine(0, y, W, lerpColor(a ? rgb(60, 28, 8) : rgb(20, 40, 70), C_BG, (y - MS_GROUND) / (float)(H - MS_GROUND)));
  gfx->drawFastHLine(0, MS_GROUND, W, a ? rgb(255, 150, 40) : rgb(90, 200, 255));
  gfx->drawFastHLine(0, MS_GROUND + 1, W, a ? rgb(120, 60, 16) : rgb(30, 80, 130));
  memcpy(msBg, frameBuffer(), FB_PIXELS * 2);
  msBgTheme = msTheme;
}

// ---------------- helpers ----------------
static void msAddBlast(float x, float y, float maxR, float dur, bool enemy) {
  for (auto &b : msB) {
    if (b.on) continue;
    b = {x, y, maxR, 0, dur, true, enemy};
    return;
  }
}
static int msSiloNearest(float x) {
  bool l = msSiloOn[0] && msAmmo[0] > 0, r = msSiloOn[1] && msAmmo[1] > 0;
  if (l && r) return fabsf(x - MS_SILO_X[0]) <= fabsf(x - MS_SILO_X[1]) ? 0 : 1;
  return l ? 0 : (r ? 1 : -1);
}
static void msFire(int silo) {
  if (silo < 0 || !msSiloOn[silo] || msAmmo[silo] <= 0) { play(180, 40); return; }
  for (auto &s : msS) {
    if (s.on) continue;
    s.sx = s.x = (float)MS_SILO_X[silo]; s.sy = s.y = (float)(MS_GROUND - 12);
    s.tx = msCur.x; s.ty = msCur.y;
    float dx = s.tx - s.x, dy = s.ty - s.y, d = sqrtf(dx * dx + dy * dy);
    if (d < 4) { s.tx = s.x; s.ty = s.y - 10; dx = 0; dy = -10; d = 10; }
    s.vx = dx / d * 560.0f; s.vy = dy / d * 560.0f;
    s.on = true;
    msAmmo[silo]--;
    play(1500, 10); play(1050, 12); play(720, 16);                  // descending whoosh
    return;
  }
}
static int msPickTarget(float &tx, float &ty) {                     // returns target id: 0..5 city, 6..7 silo
  int ids[MS_NCITY + 2], n = 0;
  for (int i = 0; i < MS_NCITY; i++) if (msCityOn[i]) ids[n++] = i;
  for (int i = 0; i < 2; i++) if (msSiloOn[i]) ids[n++] = MS_NCITY + i;
  if (n == 0) { tx = W / 2; ty = MS_GROUND; return -1; }
  int id = ids[esp_random() % (uint32_t)n];
  if (id < MS_NCITY) { tx = MS_CITY_X[id] + (msRand() - 0.5f) * 10; ty = MS_GROUND - 6; }
  else { tx = MS_SILO_X[id - MS_NCITY] + (msRand() - 0.5f) * 8; ty = MS_GROUND - 6; }
  return id;
}
static void msLaunch(float x0, float y0, float speed, bool mirv, bool smart) {
  for (auto &e : msE) {
    if (e.on) continue;
    float tx, ty;
    e.target = (int8_t)msPickTarget(tx, ty);
    e.x0 = e.x = x0; e.y0 = e.y = y0; e.tx = tx; e.ty = ty;
    float dx = tx - x0, dy = ty - y0, d = sqrtf(dx * dx + dy * dy);
    float sp = speed * (smart ? 0.85f : 0.9f + 0.2f * msRand());
    e.vx = dx / d * sp; e.vy = dy / d * sp;
    e.on = true; e.mirv = mirv; e.smart = smart; e.big = false;
    return;
  }
}
static void msDestroyTarget(int id) {
  if (id < 0) return;
  float x, y = MS_GROUND - 6;
  if (id < MS_NCITY) { msCityOn[id] = false; x = MS_CITY_X[id]; }
  else { msSiloOn[id - MS_NCITY] = false; msAmmo[id - MS_NCITY] = 0; x = MS_SILO_X[id - MS_NCITY]; }
  ptBurst(x, y, 36, 160, rgb(255, 220, 120), rgb(120, 30, 10), 0.9f, 260.0f, 2);
  ptSpray(x, y, -1.5708f, 0.7f, 20, 120, rgb(140, 140, 150), C_BG, 1.4f, -30.0f, 3);
  play(120, 90); play(80, 160); play(55, 220);                      // boom
}

// ---------------- waves ----------------
static void msWaveSetup() {
  msToSpawn = min(6 + 2 * msWave, 28);
  msSpeed = fminf(34.0f + 7.5f * msWave, 125.0f);
  msPlanesLeft = msWave >= 5 ? min(1 + (msWave - 5) / 3, 3) : 0;
  msPlaneT = 4.0f + msRand() * 4.0f;
  msSpawnT = 0.8f;
  msAmmo[0] = msAmmo[1] = MS_AMMO;
  msSiloOn[0] = msSiloOn[1] = true;
  if (msScore >= msNextBonus) {                                     // bonus city
    for (int i = 0; i < MS_NCITY; i++) if (!msCityOn[i]) { msCityOn[i] = true; break; }
    msNextBonus += 8000;
  }
  for (auto &e : msE) e.on = false;
  for (auto &s : msS) s.on = false;
  for (auto &b : msB) b.on = false;
  for (auto &p : msP) p.on = false;
}
static void msStartIntro(uint32_t now, uint32_t len) {
  msSt = MS_INTRO; msStAt = now; msIntroLen = len;
  msWaveSetup();
}
static int msCities() { int n = 0; for (bool c : msCityOn) n += c; return n; }

static void msStart() {
  msTheme = msOpts[0].value;
  if (msBgTheme != msTheme) msBuildBg();
  msBest = loadBest("msBest");
  msScore = 0; msWave = 1; msNextBonus = 8000; msNewBest = false;
  for (auto &c : msCityOn) c = true;
  ptClear();
  msCur.set(W / 2, 170);
  msLastT = millis(); msLastRender = 0;
  imuZero();
  msStartIntro(millis(), 2300);
  sfxStart();
}

// ---------------- render ----------------
static void msDrawCities(uint32_t now) {
  uint16_t cc = msColCity();
  for (int i = 0; i < MS_NCITY; i++) {
    int cx = MS_CITY_X[i];
    for (int k = 0; k < 4; k++) {
      uint32_t h = msHash(i * 7 + k, 11);
      int bw = 7 + h % 4, bh = 10 + (h >> 4) % 16, bx = cx - 16 + k * 9, by = MS_GROUND - bh;
      if (msCityOn[i]) {
        gfx->fillRect(bx, by, bw, bh, lerpColor(cc, C_BG, 0.8f));
        gfx->drawRect(bx, by, bw, bh, cc);
        for (int wy = by + 3; wy < MS_GROUND - 3; wy += 5)
          if (msHash(i * 31 + k, wy) % 3) gfx->drawPixel(bx + 2 + (wy & 1), wy, C_TEXT);
      } else {
        gfx->fillRect(bx, MS_GROUND - 3 - (int)(h % 3), bw, 3 + (int)(h % 3), rgb(60, 56, 64));
      }
    }
  }
  (void)now;
}
static void msDrawSilos() {
  uint16_t pc = msColPlayer();
  for (int s = 0; s < 2; s++) {
    int x = MS_SILO_X[s];
    if (msSiloOn[s]) {
      gfx->fillTriangle(x - 18, MS_GROUND, x + 18, MS_GROUND, x, MS_GROUND - 16, lerpColor(pc, C_BG, 0.75f));
      gfx->drawTriangle(x - 18, MS_GROUND, x + 18, MS_GROUND, x, MS_GROUND - 16, pc);
      int idx = 0;                                                   // ammo pips: 4 + 3 + 1
      static const int8_t rowN[3] = {4, 3, 1};
      for (int r = 0; r < 3; r++)
        for (int k = 0; k < rowN[r]; k++, idx++) {
          int px = x + (int)((k - (rowN[r] - 1) * 0.5f) * 7), py = MS_GROUND - 5 - r * 5;
          if (idx < msAmmo[s]) gfx->fillRect(px - 1, py - 1, 3, 3, C_TEXT);
          else gfx->drawPixel(px, py, 0x4208);
        }
      if (msAmmo[s] == 0) text("0", x - 5, MS_GROUND + 6, 2, C_BAD);
    } else {
      gfx->fillRect(x - 12, MS_GROUND - 3, 24, 3, rgb(60, 56, 64));
    }
  }
}
static void msDrawBlast(const MsBlast &b) {
  float t = b.t / b.dur, r;
  if (t < 0.32f) { float u = t / 0.32f; r = b.maxR * (1.0f - (1.0f - u) * (1.0f - u)); }
  else if (t < 0.5f) r = b.maxR;
  else r = b.maxR * (1.0f - (t - 0.5f) / 0.5f);
  if (r < 1.0f) return;
  uint16_t c = b.enemy ? msColEnemy() : msColPlayer();
  float fade = t > 0.5f ? (t - 0.5f) / 0.5f : 0.0f;
  int ri = (int)r;
  gfx->fillCircle((int)b.x, (int)b.y, ri, lerpColor(lerpColor(c, C_BG, 0.55f), C_BG, fade));
  gfx->fillCircle((int)b.x, (int)b.y, (int)(r * 0.8f), lerpColor(c, C_BG, fade * 0.8f));
  gfx->fillCircle((int)b.x, (int)b.y, (int)(r * 0.55f), lerpColor(lerpColor(c, C_TEXT, 0.6f), C_BG, fade * 0.6f));
  gfx->fillCircle((int)b.x, (int)b.y, (int)(r * 0.28f), lerpColor(C_TEXT, C_BG, fade * 0.4f));
  gfx->drawCircle((int)b.x, (int)b.y, ri + 2, lerpColor(c, C_BG, 0.45f + fade * 0.55f));
}
static void msDrawPlane(const MsPlane &p) {
  int x = (int)p.x, y = (int)p.y, d = p.vx > 0 ? 1 : -1;
  uint16_t c = rgb(255, 220, 90);
  gfx->fillTriangle(x + d * 14, y, x - d * 10, y - 6, x - d * 10, y + 6, c);
  gfx->fillTriangle(x - d * 2, y, x - d * 10, y - 12, x - d * 8, y, lerpColor(c, C_BG, 0.4f));
  gfx->fillCircle(x + d * 4, y, 2, C_TEXT);
}
static void msDrawCrosshair() {
  int x = (int)msCur.x, y = (int)msCur.y;
  uint16_t c = msColAccent();
  gfx->drawCircle(x, y, 10, lerpColor(c, C_BG, 0.5f));
  gfx->drawFastHLine(x - 18, y, 11, c); gfx->drawFastHLine(x + 8, y, 11, c);
  gfx->drawFastVLine(x, y - 18, 11, c); gfx->drawFastVLine(x, y + 8, 11, c);
  gfx->fillCircle(x, y, 2, C_TEXT);
}

static void msRender(uint32_t now) {
  if (msBgTheme != msTheme) msBuildBg();
  if (msBg) memcpy(frameBuffer(), msBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  uint16_t ec = msColEnemy(), pc = msColPlayer();
  for (auto &e : msE) {
    if (!e.on) continue;
    gfx->drawLine((int)e.x0, (int)e.y0, (int)e.x, (int)e.y, lerpColor(ec, C_BG, e.smart ? 0.35f : 0.55f));
    gfx->fillCircle((int)e.x, (int)e.y, e.smart ? 3 : 2, e.smart ? rgb(255, 80, 255) : C_TEXT);
    gfx->drawCircle((int)e.x, (int)e.y, e.smart ? 5 : 3, lerpColor(ec, C_BG, 0.3f));
  }
  for (auto &s : msS) {
    if (!s.on) continue;
    gfx->drawLine((int)s.sx, (int)s.sy, (int)s.x, (int)s.y, lerpColor(pc, C_BG, 0.5f));
    gfx->fillCircle((int)s.x, (int)s.y, 2, C_TEXT);
    gfx->drawRect((int)s.tx - 2, (int)s.ty - 2, 5, 5, lerpColor(pc, C_BG, 0.3f));      // target marker
  }
  for (auto &p : msP) if (p.on) msDrawPlane(p);
  msDrawCities(now);
  msDrawSilos();
  for (auto &b : msB) if (b.on) msDrawBlast(b);
  ptDraw();
  if (msSt == MS_PLAY || msSt == MS_INTRO) msDrawCrosshair();
  hudBar(msScore, msBest, rgb(8, 8, 22), C_TEXT);
  char b[32];
  snprintf(b, sizeof b, "WAVE %d", msWave);
  text(b, 262, 9, 2, msColAccent());

  if (msSt == MS_INTRO) {
    snprintf(b, sizeof b, "WAVE %d", msWave);
    gfx->fillRoundRect(60, 96, W - 120, 120, 16, C_PANEL);
    gfx->drawRoundRect(60, 96, W - 120, 120, 16, msColAccent());
    centerText(b, 112, 4, msColAccent());
    centerText("TILT aims", 160, 2, C_TEXT);
    centerText("LEFT / RIGHT = left / right silo", 184, 1, C_DIM);
  } else if (msSt == MS_CLEAR) {
    char l2[40], l3[48];
    snprintf(l2, sizeof l2, "BONUS +%d", msBonusPts);
    snprintf(l3, sizeof l3, "%d cities x100   %d shots x5", msBonusCities, msBonusAmmo);
    panelBox("WAVE CLEAR", l2, l3, msColAccent());
  } else if (msSt == MS_OVER) {
    char l2[32], l3[64];
    snprintf(l2, sizeof l2, msNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)msScore);
    snprintf(l3, sizeof l3, "reached wave %d   RIGHT again   LEFT menu", msWave);
    panelBox("THE END", l2, nullptr, msColEnemy());
    centerText(l3, 236, 1, C_DIM);
  }
  dirty = true;
}

// ---------------- update ----------------
static int msMult() { return min(6, 1 + (msWave - 1) / 2); }

static void msKill(MsEnemy &e, int pts) {
  e.on = false;
  msScore += pts * msMult();
  msAddBlast(e.x, e.y, 24, 0.7f, false);                            // chain blast
  ptBurst(e.x, e.y, 8, 90, C_TEXT, msColEnemy(), 0.5f, 0, 2);
  play(300, 16);
}

static void msUpdate(float dt, uint32_t now) {
  // spawning
  if (msToSpawn > 0) {
    msSpawnT -= dt;
    if (msSpawnT <= 0) {
      int grp = 1 + (int)(esp_random() % 3);
      for (int g = 0; g < grp && msToSpawn > 0; g++, msToSpawn--) {
        bool mirv = msWave >= 3 && msRand() < 0.18f + 0.02f * msWave;
        bool smart = msWave >= 7 && msRand() < 0.14f;
        msLaunch(14.0f + msRand() * (W - 28), 34.0f, msSpeed, mirv && !smart, smart);
      }
      msSpawnT = fmaxf(0.5f, 2.3f - 0.14f * msWave) * (0.5f + msRand());
    }
  }
  if (msPlanesLeft > 0) {
    msPlaneT -= dt;
    if (msPlaneT <= 0) {
      for (auto &p : msP) {
        if (p.on) continue;
        bool fromLeft = esp_random() & 1;
        p.x = fromLeft ? -16.0f : W + 16.0f; p.y = 60.0f + msRand() * 80.0f;
        p.vx = (fromLeft ? 1 : -1) * (60.0f + 5.0f * msWave); p.nextDrop = 0.8f + msRand();
        p.on = true; msPlanesLeft--;
        break;
      }
      msPlaneT = 5.0f + msRand() * 4.0f;
    }
  }
  for (auto &p : msP) {
    if (!p.on) continue;
    p.x += p.vx * dt;
    p.nextDrop -= dt;
    if (p.nextDrop <= 0 && p.x > 20 && p.x < W - 20) { msLaunch(p.x, p.y, msSpeed * 0.9f, false, false); p.nextDrop = 1.0f + msRand() * 0.8f; }
    if (p.x < -40 || p.x > W + 40) p.on = false;
  }
  // enemy missiles
  for (auto &e : msE) {
    if (!e.on) continue;
    if (e.smart) {                                                  // dodge the nearest blast
      for (auto &b : msB) {
        if (!b.on || b.enemy) continue;
        float dx = e.x - b.x, dy = e.y - b.y, d2 = dx * dx + dy * dy;
        if (d2 < 70.0f * 70.0f && d2 > 1) { float d = sqrtf(d2); e.x += dx / d * 110.0f * dt; }
      }
    }
    e.x += e.vx * dt; e.y += e.vy * dt;
    if (e.mirv && e.y > 100.0f + (float)((int)e.x % 60)) {          // split
      int n = 2 + (esp_random() & 1);
      float x = e.x, y = e.y, sp = sqrtf(e.vx * e.vx + e.vy * e.vy);
      e.on = false;
      for (int k = 0; k < n; k++) msLaunch(x, y, sp, false, false);
      ptBurst(x, y, 6, 60, C_TEXT, msColEnemy(), 0.4f, 0, 2);
      play(500, 20);
      continue;
    }
    if (e.y >= e.ty) {                                              // impact
      e.on = false;
      msAddBlast(e.tx, e.ty, 22, 0.7f, true);
      msDestroyTarget(e.target);
    }
  }
  // player missiles
  for (auto &s : msS) {
    if (!s.on) continue;
    s.x += s.vx * dt; s.y += s.vy * dt;
    float dx = s.tx - s.x, dy = s.ty - s.y;
    if (dx * s.vx + dy * s.vy <= 0) {                               // arrived
      s.on = false;
      msAddBlast(s.tx, s.ty, 36, 1.05f, false);
      play(220, 30); play(160, 40);
    }
  }
  // blasts kill things
  for (auto &b : msB) {
    if (!b.on) continue;
    b.t += dt;
    if (b.t >= b.dur) { b.on = false; continue; }
    if (b.enemy) continue;
    float t = b.t / b.dur, r = t < 0.32f ? b.maxR * (1.0f - (1.0f - t / 0.32f) * (1.0f - t / 0.32f)) : (t < 0.5f ? b.maxR : b.maxR * (1.0f - (t - 0.5f) / 0.5f));
    float r2 = r * r;
    for (auto &e : msE) {
      if (!e.on) continue;
      float dx = e.x - b.x, dy = e.y - b.y;
      if (dx * dx + dy * dy <= r2) msKill(e, e.smart ? 125 : 25);
    }
    for (auto &p : msP) {
      if (!p.on) continue;
      float dx = p.x - b.x, dy = p.y - b.y;
      if (dx * dx + dy * dy <= r2) {
        p.on = false; msScore += 100 * msMult();
        msAddBlast(p.x, p.y, 30, 0.8f, false);
        ptBurst(p.x, p.y, 20, 140, rgb(255, 220, 90), C_BG, 0.8f, 200.0f, 2);
        play(260, 40); play(180, 60);
      }
    }
  }
  (void)now;
}

static bool msWaveDone() {
  if (msToSpawn > 0 || msPlanesLeft > 0) return false;
  for (auto &e : msE) if (e.on) return false;
  for (auto &p : msP) if (p.on) return false;
  for (auto &s : msS) if (s.on) return false;
  for (auto &b : msB) if (b.on) return false;
  return true;
}

static bool msFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = fminf((now - msLastT) / 1000.0f, 0.05f);
  msLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  if (msSt != MS_OVER) {
    if (imuOk) msCur.update(dt, W / 2, (40 + MS_GROUND - 14) / 2.0f, 820.0f, 760.0f, 0.03f, 18.0f, 0, W - 1, 40, MS_GROUND - 14);
    else { msCur.x = W / 2 + 190 * sinf(now * 0.0012f); msCur.y = 150 + 70 * sinf(now * 0.0017f); }
  }
  switch (msSt) {
    case MS_INTRO:
      if (now - msStAt >= msIntroLen) { msSt = MS_PLAY; imuZero(); }
      break;
    case MS_PLAY: {
      if (in.left) msFire(0);
      if (in.right) msFire(1);
      if (in.tap) {
        float sx = msCur.x, sy = msCur.y;
        msCur.x = constrain((float)in.tapX, 0.0f, (float)W - 1); msCur.y = constrain((float)in.tapY, 40.0f, (float)MS_GROUND - 14);
        msFire(msSiloNearest(msCur.x));
        msCur.x = sx; msCur.y = sy;
      }
      msUpdate(dt, now);
      if (msCities() == 0) {
        msSt = MS_OVER; msOverAt = now;
        msNewBest = saveBest("msBest", msScore);
        if (msNewBest) { msBest = msScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
      } else if (msWaveDone()) {
        msBonusCities = msCities();
        msBonusAmmo = msAmmo[0] * msSiloOn[0] + msAmmo[1] * msSiloOn[1];
        msBonusPts = (msBonusCities * 100 + msBonusAmmo * 5) * msMult();
        msScore += msBonusPts;
        if (msScore > msBest) msBest = msScore;
        msSt = MS_CLEAR; msStAt = now;
        tunePlay(TUNE_LEVEL);
      }
      break;
    }
    case MS_CLEAR:
      if (now - msStAt > 2800) { msWave++; msStartIntro(now, 1400); }
      break;
    case MS_OVER:
      if (now - msOverAt > 900) {
        if (in.right) { msStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - msLastRender >= 16) { msLastRender = now; msRender(now); }
  return true;
}

Game GAME_MISSILE = {"MISSILE", "tilt aim, defend the cities", 0xF8A0, msOpts, 1, msStart, msFrame};
