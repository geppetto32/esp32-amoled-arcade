// swarm_game.cpp - Swarm, a Galaga-style shooter with a fixed formation and choreographed (Bezier) attack dives.
//   TILT (or the analog stick) moves the ship along the bottom; touch drag moves it too
//   Hold LEFT or RIGHT (or keep a finger on the screen) = rapid fire; FIRE = AUTO fires all the time
//   Press LEFT while holding RIGHT (or the other way round) = smart bomb (clears bullets and diving aliens, 3 per game)
// Formations fly in on curves, sway, and peel off to dive and loop. The magenta bosses take two hits; a boss shot while
// diving drops a DUAL pickup (twin shots for 12 s). Every 4th wave is a challenge stage (no shooting back, fly-through
// aliens, 100 each, perfect = 5000 bonus).
#include "engine.h"
#include <math.h>

static const char *const SW_FIRE[] = {"HOLD", "AUTO"};
static const char *const SW_DIFF[] = {"NORMAL", "EASY", "HARD"};
static Option swOpts[] = {{"FIRE", SW_FIRE, 2, "swFire", 0}, {"DIFF", SW_DIFF, 3, "swDiff", 0}};

enum SwSt : uint8_t { SW_INTRO, SW_BANNER, SW_PLAY, SW_DYING, SW_CLEAR, SW_OVER };
enum : uint8_t { SA_DEAD, SA_WAIT, SA_ENTER, SA_FORM, SA_DIVE, SA_RET, SA_CHAL };

static constexpr int SW_NA = 36, SW_NB = 5, SW_NE = 14, SW_NS = 40, SW_SHIPY = H - 34;
struct SwAl { uint8_t type, hp, st; int8_t c, r; float x, y, t, dur, delay, p[8], shoot; };
struct SwBul { float x, y, vx, vy; bool on; };
struct SwPick { float x, y; bool on; };

static SwAl swA[SW_NA];
static SwBul swB[SW_NB], swE[SW_NE];
static SwPick swP;
static float swStarX[SW_NS], swStarY[SW_NS], swStarS[SW_NS];
static SwSt swSt;
static int swWave, swLives, swBombs, swKills, swChalHits, swDiff;
static bool swChallenge, swAuto, swNewBest;
static uint32_t swScore, swBest, swStAt, swLastT, swLastRender;
static float swGT, swShipX, swInvuln, swFireCd, swDualT, swDiveT, swFlash;
static uint32_t swDragUntil;
static TiltCursor swCur;

static float swRand() { return (esp_random() & 0xFFFF) / 65535.0f; }

static const uint16_t SW_COL[4] = {rgb(255, 70, 230), rgb(255, 80, 90), rgb(255, 220, 60), rgb(70, 220, 255)};

static float swSlotX(int c) { return 224 + (c - 3.5f) * 40 + sinf(swGT * 0.8f) * 28; }
static float swSlotY(int r) { return 62 + r * 28 + sinf(swGT * 1.3f + r) * 2; }

static void swBez(const float *p, float t, float &x, float &y) {
  float u = 1 - t, a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
  x = a * p[0] + b * p[2] + c * p[4] + d * p[6];
  y = a * p[1] + b * p[3] + c * p[5] + d * p[7];
}

static const float SW_CHAL[4][8] = {
  {-20, 60, 300, 60, 150, 300, 470, 330},
  {468, 100, 100, 360, 350, -60, -30, 240},
  {224, -20, 520, 120, -70, 200, 224, 400},
  {-20, 300, 200, 300, 250, 0, 470, 60}};

static void swSpawnWave() {
  swChallenge = (swWave % 4) == 0;
  swKills = 0; swChalHits = 0;
  int k = 0;
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 8; c++) {
      if (r == 0 && (c < 2 || c > 5)) continue;
      SwAl &a = swA[k];
      memset(&a, 0, sizeof a);
      a.type = r == 0 ? 0 : r < 3 ? 1 : (r == 3 ? 2 : 3);
      a.hp = r == 0 ? 2 : 1;
      a.c = (int8_t)c; a.r = (int8_t)r; a.st = SA_WAIT;
      if (swChallenge) a.delay = (k / 6) * 1.4f + (k % 6) * 0.16f;
      else a.delay = (k / 8) * 2.1f + (k % 8) * 0.17f;
      a.t = (float)(k / (swChallenge ? 6 : 8));            // group index, reused at launch
      k++;
    }
  for (; k < SW_NA; k++) swA[k].st = SA_DEAD;
  for (auto &b : swE) b.on = false;
  swP.on = false;
  swDiveT = 3.5f;
}

static void swLaunch(SwAl &a) {
  int g = (int)a.t;
  a.t = 0;
  if (swChallenge) {
    memcpy(a.p, SW_CHAL[g % 4], sizeof a.p);
    a.dur = 3.4f; a.st = SA_CHAL;
  } else {
    bool left = (g & 1) == 0;
    float sx = left ? -20.0f : W + 20.0f;
    a.p[0] = sx; a.p[1] = 110;
    a.p[2] = left ? 330 : 118; a.p[3] = 330;
    a.p[4] = left ? 420 : 28; a.p[5] = 120;
    a.dur = 2.0f; a.st = SA_ENTER;
  }
  a.x = a.p[0]; a.y = a.p[1];
}

static void swStartDive(SwAl &a) {
  float s = (a.x < swShipX) ? 1.0f : -1.0f;
  if (swRand() < 0.5f) s = -s;
  float tx = constrain(swShipX + (swRand() - 0.5f) * 120, 20.0f, W - 20.0f);
  a.p[0] = a.x; a.p[1] = a.y;
  a.p[2] = a.x + s * 170; a.p[3] = a.y + 70;
  a.p[4] = a.x - s * 90; a.p[5] = a.y + 230;
  a.p[6] = tx; a.p[7] = H + 30;
  a.t = 0; a.dur = fmaxf(1.7f, 2.7f - 0.08f * swWave) * (swDiff == 1 ? 1.2f : swDiff == 2 ? 0.88f : 1.0f);
  a.st = SA_DIVE; a.shoot = 0.5f + swRand() * 0.6f;
  play(900, 25);
}

static int swAlive() { int n = 0; for (auto &a : swA) if (a.st != SA_DEAD) n++; return n; }

static void swRespawn(uint32_t now) {
  swInvuln = 2.2f; swFireCd = 0; swShipX = W / 2.0f; swCur.set(swShipX, SW_SHIPY);
  imuZero(); (void)now;
  for (auto &b : swE) b.on = false;
}

static void swStart() {
  swDiff = swOpts[1].value;
  swAuto = swOpts[0].value == 1;
  swBest = loadBest("swBest");
  swDragUntil = 0; swScore = 0; swLives = swDiff == 1 ? 4 : 3; swBombs = 3; swWave = 1; swNewBest = false; swDualT = 0; swGT = 0; swFlash = 0;
  for (auto &b : swB) b.on = false;
  for (int i = 0; i < SW_NS; i++) { swStarX[i] = swRand() * W; swStarY[i] = swRand() * H; swStarS[i] = 20 + swRand() * 90; }
  ptClear();
  swSpawnWave();
  uint32_t now = millis();
  swRespawn(now);
  swSt = SW_INTRO; swStAt = now; swLastT = now; swLastRender = 0;
  sfxStart();
}

static void swAddScore(uint32_t s) { swScore += s; if (swScore > swBest) swBest = swScore; }

static void swKill(SwAl &a, uint32_t now) {
  bool diving = a.st == SA_DIVE || a.st == SA_RET;
  uint16_t c = SW_COL[a.type];
  if (swChallenge) { swAddScore(100); swChalHits++; }
  else {
    uint32_t base = a.type == 0 ? 150 : a.type == 1 ? 80 : 50;
    swAddScore(diving ? base * 2 + (a.type == 0 ? 100 : 0) : base);
  }
  ptBurst(a.x, a.y, 16, 150, lerpColor(c, C_TEXT, 0.5f), c, 0.55f, 0, 3);
  if (a.type == 0 && diving && !swChallenge && !swP.on) { swP.on = true; swP.x = a.x; swP.y = a.y; }
  a.st = SA_DEAD; swKills++;
  play((uint16_t)(500 + a.type * 120), 28);
  (void)now;
}

static void swShipHit(uint32_t now) {
  swSt = SW_DYING; swStAt = now; swDualT = 0;
  ptBurst(swShipX, (float)SW_SHIPY, 36, 200, rgb(255, 255, 200), rgb(255, 60, 30), 0.9f, 0, 3);
  swFlash = 0.25f;
  play(240, 80); play(160, 100); play(110, 160);
}

static void swFire() {
  int cnt = 0;
  for (auto &b : swB) if (b.on) cnt++;
  bool dual = swDualT > 0;
  if (cnt > SW_NB - (dual ? 2 : 1)) return;
  int shots = dual ? 2 : 1;
  for (int s = 0; s < shots; s++)
    for (auto &b : swB) if (!b.on) {
      b.on = true; b.x = swShipX + (dual ? (s ? 9 : -9) : 0); b.y = SW_SHIPY - 12.0f; b.vx = 0; b.vy = -540; break;
    }
  play(1400, 8);
}

static void swEnemyShoot(const SwAl &a) {
  for (auto &b : swE) if (!b.on) {
    float dx = swShipX - a.x, dy = SW_SHIPY - a.y;
    float n = fmaxf(1.0f, sqrtf(dx * dx + dy * dy));
    float sp = 170 + swWave * 8 + (swDiff == 2 ? 30 : 0);
    float k = 0.35f;
    b.on = true; b.x = a.x; b.y = a.y + 8; b.vx = dx / n * sp * k; b.vy = sqrtf(fmaxf(0.0f, sp * sp - b.vx * b.vx));
    break;
  }
}

static void swBomb(uint32_t now) {
  if (swBombs <= 0) return;
  swBombs--; swFlash = 0.3f;
  for (auto &b : swE) b.on = false;
  for (auto &a : swA)
    if (a.st == SA_DIVE || a.st == SA_RET || a.st == SA_ENTER) { a.hp = 1; swKill(a, now); }
  play(200, 60); play(400, 60); play(800, 90);
}

static void swUpdate(float dt, uint32_t now) {
  swGT += dt;
  if (swInvuln > 0) swInvuln -= dt;
  if (swFlash > 0) swFlash -= dt;
  for (int i = 0; i < SW_NS; i++) { swStarY[i] += swStarS[i] * dt; if (swStarY[i] > H) { swStarY[i] -= H; swStarX[i] = swRand() * W; } }
  bool live = swSt == SW_PLAY || swSt == SW_BANNER || swSt == SW_DYING;
  if (!live) return;
  // aliens
  int diving = 0;
  for (auto &a : swA) {
    switch (a.st) {
      case SA_WAIT:
        if (swSt == SW_PLAY) { a.delay -= dt; if (a.delay <= 0) swLaunch(a); }
        break;
      case SA_ENTER: case SA_RET: {
        a.t += dt / a.dur;
        a.p[6] = swSlotX(a.c); a.p[7] = swSlotY(a.r);
        if (a.t >= 1) { a.st = SA_FORM; a.x = a.p[6]; a.y = a.p[7]; }
        else swBez(a.p, a.t, a.x, a.y);
        if (a.st == SA_RET) diving++;
        break;
      }
      case SA_FORM: a.x = swSlotX(a.c); a.y = swSlotY(a.r); break;
      case SA_DIVE: {
        diving++;
        a.t += dt / a.dur;
        swBez(a.p, a.t < 1 ? a.t : 1.0f, a.x, a.y);
        a.shoot -= dt;
        if (a.shoot <= 0 && a.y < SW_SHIPY - 60 && swSt == SW_PLAY) {
          swEnemyShoot(a);
          a.shoot = fmaxf(0.35f, 1.0f - 0.05f * swWave) * (0.7f + swRand() * 0.6f);
        }
        if (a.t >= 1) {
          float x = constrain(a.x, 20.0f, W - 20.0f);
          a.p[0] = x; a.p[1] = -20; a.p[2] = x; a.p[3] = 40; a.p[4] = swSlotX(a.c); a.p[5] = 20;
          a.t = 0; a.dur = 1.3f; a.st = SA_RET; a.x = x; a.y = -20;
        }
        break;
      }
      case SA_CHAL:
        a.t += dt / a.dur;
        if (a.t >= 1) a.st = SA_DEAD; else swBez(a.p, a.t, a.x, a.y);
        break;
      default: break;
    }
  }
  if (swSt != SW_PLAY) return;
  // dive scheduler (not in challenge, only when the formation is in)
  if (!swChallenge) {
    swDiveT -= dt;
    int maxDive = min(2 + swWave / 2, 5);
    if (swDiveT <= 0 && diving < maxDive) {
      int cand[SW_NA], n = 0;
      for (int i = 0; i < SW_NA; i++) if (swA[i].st == SA_FORM) cand[n++] = i;
      if (n > 0) {
        int pick = cand[esp_random() % (uint32_t)n];
        swStartDive(swA[pick]);
        if (swWave >= 3 && n > 3) {                       // an escort follows
          for (int i = 0; i < SW_NA; i++)
            if (swA[i].st == SA_FORM && abs(swA[i].c - swA[pick].c) == 1 && swA[i].r >= swA[pick].r - 1 && swRand() < 0.6f) {
              swStartDive(swA[i]); swA[i].t = -0.12f; break;
            }
        }
      }
      float base = fmaxf(0.55f, 1.6f - 0.1f * swWave) * (swDiff == 1 ? 1.3f : swDiff == 2 ? 0.8f : 1.0f);
      swDiveT = base * (0.7f + swRand() * 0.6f);
    }
  }
  // player bullets
  for (auto &b : swB) {
    if (!b.on) continue;
    b.y += b.vy * dt;
    if (b.y < 30) { b.on = false; continue; }
    for (auto &a : swA) {
      if (a.st < SA_ENTER || a.st == SA_WAIT) continue;
      if (fabsf(a.x - b.x) < 12 && fabsf(a.y - b.y) < 11) {
        b.on = false;
        if (a.hp > 1) { a.hp--; play(300, 20); ptBurst(a.x, a.y, 5, 90, C_TEXT, SW_COL[a.type], 0.25f); }
        else swKill(a, now);
        break;
      }
    }
  }
  // enemy bullets
  for (auto &b : swE) {
    if (!b.on) continue;
    b.x += b.vx * dt; b.y += b.vy * dt;
    if (b.y > H + 6 || b.x < -6 || b.x > W + 6) { b.on = false; continue; }
    if (swInvuln <= 0 && fabsf(b.x - swShipX) < 9 && fabsf(b.y - SW_SHIPY) < 11) { b.on = false; swShipHit(now); return; }
  }
  // aliens ramming the ship
  if (swInvuln <= 0)
    for (auto &a : swA)
      if ((a.st == SA_DIVE || a.st == SA_RET || a.st == SA_ENTER || a.st == SA_CHAL) && fabsf(a.x - swShipX) < 15 && fabsf(a.y - SW_SHIPY) < 13) {
        a.hp = 1; if (!swChallenge) swKill(a, now); else a.st = SA_DEAD;
        swShipHit(now); return;
      }
  // pickup
  if (swP.on) {
    swP.y += 70 * dt;
    if (swP.y > H + 10) swP.on = false;
    else if (fabsf(swP.x - swShipX) < 18 && fabsf(swP.y - SW_SHIPY) < 18) {
      swP.on = false; swDualT = 12; swAddScore(200); play(900, 40); play(1300, 60);
    }
  }
  if (swDualT > 0) swDualT -= dt;
  // wave clear
  if (swAlive() == 0) {
    swSt = SW_CLEAR; swStAt = now;
    if (swChallenge && swChalHits >= SW_NA) swAddScore(5000);
    tunePlay(TUNE_WIN);
  }
}

// ---------------- drawing ----------------
static void swDrawAlien(const SwAl &a) {
  if (a.st == SA_DEAD || a.st == SA_WAIT) return;
  int x = (int)a.x, y = (int)a.y;
  uint16_t c = SW_COL[a.type], g = lerpColor(c, C_BG, 0.75f), m = lerpColor(c, C_BG, 0.45f);
  if (a.type == 0 && a.hp == 1) c = lerpColor(c, C_TEXT, 0.4f);
  int flap = ((int)(swGT * 6 + a.c) & 1) ? 2 : 0;
  switch (a.type) {
    case 0:                                                    // boss: diamond with a bright core
      gfx->fillTriangle(x, y - 13, x - 13, y, x + 13, y, g);
      gfx->fillTriangle(x, y + 11, x - 13, y, x + 13, y, g);
      gfx->fillTriangle(x, y - 10, x - 10, y, x + 10, y, m);
      gfx->fillTriangle(x, y + 8, x - 10, y, x + 10, y, m);
      gfx->drawTriangle(x, y - 10, x - 10, y, x + 10, y, c);
      gfx->drawTriangle(x, y + 8, x - 10, y, x + 10, y, c);
      gfx->fillCircle(x, y, 3, C_TEXT);
      break;
    case 1:                                                    // butterfly: two wing triangles
      gfx->fillTriangle(x, y, x - 13, y - 8 - flap, x - 9, y + 8, g);
      gfx->fillTriangle(x, y, x + 13, y - 8 - flap, x + 9, y + 8, g);
      gfx->fillTriangle(x, y, x - 11, y - 6 - flap, x - 8, y + 6, m);
      gfx->fillTriangle(x, y, x + 11, y - 6 - flap, x + 8, y + 6, m);
      gfx->drawTriangle(x, y, x - 11, y - 6 - flap, x - 8, y + 6, c);
      gfx->drawTriangle(x, y, x + 11, y - 6 - flap, x + 8, y + 6, c);
      gfx->fillRect(x - 1, y - 5, 3, 11, C_TEXT);
      break;
    case 2:                                                    // bee: round body, flapping wings
      gfx->fillCircle(x, y, 9, g);
      gfx->fillCircle(x, y, 7, m);
      gfx->drawCircle(x, y, 7, c);
      gfx->drawLine(x - 7, y - 2, x - 13, y - 6 - flap, c);
      gfx->drawLine(x + 7, y - 2, x + 13, y - 6 - flap, c);
      gfx->drawFastHLine(x - 5, y, 11, C_BG);
      gfx->fillRect(x - 3, y - 3, 2, 2, C_TEXT); gfx->fillRect(x + 2, y - 3, 2, 2, C_TEXT);
      break;
    default:                                                   // dart: arrow pointing down
      gfx->fillTriangle(x - 11, y - 8, x + 11, y - 8, x, y + 10, g);
      gfx->fillTriangle(x - 8, y - 6, x + 8, y - 6, x, y + 7, m);
      gfx->drawTriangle(x - 8, y - 6, x + 8, y - 6, x, y + 7, c);
      gfx->fillCircle(x, y - 2, 2, C_TEXT);
  }
}

static void swDrawShip(float fx, uint16_t c) {
  int x = (int)fx, y = SW_SHIPY;
  gfx->fillTriangle(x, y - 15, x - 15, y + 10, x + 15, y + 10, lerpColor(c, C_BG, 0.8f));
  gfx->fillTriangle(x, y - 12, x - 11, y + 8, x + 11, y + 8, lerpColor(c, C_BG, 0.5f));
  gfx->drawTriangle(x, y - 12, x - 11, y + 8, x + 11, y + 8, c);
  gfx->fillRect(x - 1, y - 6, 3, 8, C_TEXT);
  if (swDualT > 0) { gfx->fillRect(x - 13, y - 2, 3, 10, c); gfx->fillRect(x + 11, y - 2, 3, 10, c); }
  gfx->fillRect(x - 3, y + 10, 7, 2 + ((int)(swGT * 30) & 1) * 3, rgb(255, 160, 40));      // flame
}

static void swRender(uint32_t now) {
  gfx->fillScreen(swFlash > 0 ? rgb(40, 40, 60) : C_BG);
  for (int i = 0; i < SW_NS; i++) {
    uint16_t c = swStarS[i] > 80 ? rgb(150, 160, 220) : swStarS[i] > 50 ? rgb(80, 90, 150) : rgb(40, 46, 90);
    gfx->drawPixel((int)swStarX[i], (int)swStarY[i], c);
  }
  for (auto &a : swA) swDrawAlien(a);
  for (auto &b : swB) if (b.on) {
    gfx->fillRect((int)b.x - 2, (int)b.y - 6, 5, 12, rgb(10, 60, 70));
    gfx->fillRect((int)b.x - 1, (int)b.y - 6, 3, 12, C_CYAN);
  }
  for (auto &b : swE) if (b.on) {
    gfx->fillCircle((int)b.x, (int)b.y, 5, rgb(70, 10, 20));
    gfx->fillCircle((int)b.x, (int)b.y, 3, rgb(255, 90, 80));
    gfx->drawPixel((int)b.x, (int)b.y, C_TEXT);
  }
  if (swP.on) {
    int x = (int)swP.x, y = (int)swP.y;
    gfx->fillRoundRect(x - 14, y - 8, 28, 16, 6, rgb(0, 50, 20));
    gfx->drawRoundRect(x - 14, y - 8, 28, 16, 6, C_GOOD);
    text("2x", x - 6, y - 4, 1, C_GOOD);
  }
  if (swSt != SW_DYING && swSt != SW_OVER) {
    bool blink = swInvuln > 0 && ((int)(now / 90) & 1);
    if (!blink) swDrawShip(swShipX, C_CYAN);
  }
  ptDraw();
  hudBar(swScore, swBest, rgb(14, 8, 30), C_TEXT);
  char b[16];
  for (int i = 0; i < swLives; i++) gfx->fillTriangle(236 + i * 14, 8, 230 + i * 14, 24, 242 + i * 14, 24, C_CYAN);
  snprintf(b, sizeof b, "W%d", swWave);
  text(b, 300, 9, 2, swChallenge ? C_ACC : C_TEXT);
  snprintf(b, sizeof b, "B%d", swBombs);
  text(b, 346, 9, 2, swBombs ? C_GOOD : C_DIM);
  if (swSt == SW_INTRO) {
    gfx->fillRoundRect(40, 120, W - 80, 128, 16, C_PANEL);
    gfx->drawRoundRect(40, 120, W - 80, 128, 16, C_CYAN);
    centerText("SWARM", 130, 4, C_CYAN);
    centerText("tilt = move   hold LEFT/RIGHT = fire", 176, 1, C_TEXT);
    centerText("LEFT + RIGHT together = bomb", 192, 1, C_DIM);
    int left = 3 - (int)((now - swStAt) / 700);
    if (left < 1) left = 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 210, 3, C_ACC);
  } else if (swSt == SW_BANNER) {
    char l[24];
    snprintf(l, sizeof l, swChallenge ? "CHALLENGE %d" : "WAVE %d", swWave);
    centerText(l, 160, 4, swChallenge ? C_ACC : C_TEXT);
    centerText(swChallenge ? "no return fire - shoot them all" : "get ready", 206, 1, C_DIM);
  } else if (swSt == SW_CLEAR) {
    char l2[32];
    if (swChallenge) snprintf(l2, sizeof l2, "HIT %d/%d", swChalHits, SW_NA);
    else snprintf(l2, sizeof l2, "WAVE %d", swWave);
    panelBox(swChallenge ? "CHALLENGE" : "CLEARED", l2, swChallenge && swChalHits >= SW_NA ? "PERFECT +5000" : nullptr, C_CYAN);
  } else if (swSt == SW_OVER) {
    char l2[32];
    snprintf(l2, sizeof l2, swNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)swScore);
    panelBox("GAME OVER", l2, "RIGHT again  LEFT menu", C_CYAN);
  }
  dirty = true;
}

static bool swFrame(uint32_t now) {
  homeLock = true;
  float dt = fminf((now - swLastT) / 1000.0f, 0.05f);
  swLastT = now;
  tuneUpdate(now);
  ptUpdate(dt);
  // ship steering (always, so the ship moves during the intro)
  if (swSt == SW_PLAY || swSt == SW_BANNER || swSt == SW_INTRO) {
    if (in.dragging) { swCur.x = constrain(swCur.x + in.dragDX, 18.0f, W - 18.0f); swDragUntil = now + 700; }
    if ((int32_t)(now - swDragUntil) >= 0)
      swCur.update(dt, W / 2.0f, (float)SW_SHIPY, 440, 0, 0.04f, 14, 18, W - 18, (float)SW_SHIPY, (float)SW_SHIPY);
    swShipX = swCur.x;
  }
  switch (swSt) {
    case SW_INTRO:
      swUpdate(dt, now);
      if (now - swStAt >= 2100) { swSt = SW_BANNER; swStAt = now; imuZero(); }
      break;
    case SW_BANNER:
      swUpdate(dt, now);
      if (now - swStAt >= 1600) swSt = SW_PLAY;
      break;
    case SW_PLAY: {
      bool hold = in.leftDown || in.rightDown || in.touching;
      if ((in.left && in.rightDown) || (in.right && in.leftDown)) swBomb(now);
      swFireCd -= dt;
      if ((hold || swAuto) && swFireCd <= 0) { swFire(); swFireCd = 0.17f; }
      swUpdate(dt, now);
      break;
    }
    case SW_DYING:
      swUpdate(dt, now);
      if (now - swStAt > 1300) {
        if (--swLives <= 0) {
          swSt = SW_OVER; swStAt = now;
          swNewBest = saveBest("swBest", swScore);
          if (swNewBest) { swBest = swScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LOSE);
        } else { swRespawn(now); swSt = SW_PLAY; }
      }
      break;
    case SW_CLEAR:
      swUpdate(dt, now);
      if (now - swStAt > 2200) {
        swWave++;
        if (swWave % 4 == 1 && swBombs < 5) swBombs++;
        swSpawnWave(); swSt = SW_BANNER; swStAt = now;
        for (auto &b : swB) b.on = false;
      }
      break;
    case SW_OVER:
      swUpdate(dt, now);
      if (now - swStAt > 900) {
        if (in.right) { swStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - swLastRender >= 16) { swLastRender = now; swRender(now); }
  return true;
}

Game GAME_SWARM = {"Swarm", "formations dive, shoot them down", 0x07FF, swOpts, 2, swStart, swFrame};
