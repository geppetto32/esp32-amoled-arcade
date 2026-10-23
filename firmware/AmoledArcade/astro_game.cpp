// astro_game.cpp - vertical space dodge/shooter. The ship auto-fires; asteroids fall in waves that get faster.
// CONTROL: BUTTONS (hold LEFT/RIGHT, a press also nudges) or TILT (tilt left/right; RIGHT = pause).
#include "game.h"

static const char *const CONTROLS[] = {"BUTTONS", "TILT"};
static Option asOpts[] = {{"CONTROL", CONTROLS, 2, "asCtrl", 0}};

enum St : uint8_t { ST_COUNT, ST_PLAY, ST_PAUSE, ST_OVER };
static constexpr int NAST = 16, NBUL = 18, NPART = 90, NVERT = 9, SHIP_Y = H - 50, FIRE_MS = 210;

struct Asteroid { float x, y, vx, vy, r, ang, spin, rad[NVERT]; int8_t hp; bool on; uint32_t flash; };
struct Bullet { float x, y; bool on; };
struct Particle { float x, y, vx, vy; uint32_t born; uint16_t life; uint16_t c; };

static Asteroid ast[NAST];
static Bullet bul[NBUL];
static Particle parts[NPART];
static int partIdx;
static St st;
static int ctrl, lives, wave, killsInWave;
static uint32_t score, best, t0, lastT, lastRender, overAt, lastFire, nextSpawn, invulUntil, bannerUntil;
static float sx, svx, starT;
static bool newBest;
static uint16_t *bg = nullptr;

static inline uint32_t hash2(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float wrapf(float v, float m) {
  v = fmodf(v, m);
  return v < 0 ? v + m : v;
}
static inline float frand() { return (float)(esp_random() % 10000) / 10000.0f; }

static void buildBackground() {
  if (!bg) bg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  gfx->fillScreen(C_BG);
  // faint nebula blobs: layered circles of decreasing darkness
  const int bxs[3] = {90, 360, 220}, bys[3] = {120, 250, 60};
  const uint16_t cols[3] = {rgb(60, 0, 90), rgb(0, 40, 90), rgb(80, 0, 50)};
  for (int b = 0; b < 3; b++)
    for (int k = 8; k >= 1; k--) gfx->fillCircle(bxs[b], bys[b], 14 + k * 12, lerpColor(cols[b], C_BG, 0.80f + 0.025f * k));
  memcpy(bg, frameBuffer(), FB_PIXELS * 2);
}

static void boom(float x, float y, int n, uint32_t now, float power) {
  for (int i = 0; i < n; i++) {
    float a = frand() * 6.2832f, s = (30.0f + frand() * 150.0f) * power;
    uint16_t c = i % 3 == 0 ? rgb(255, 240, 160) : i % 3 == 1 ? rgb(255, 150, 40) : rgb(255, 60, 30);
    parts[partIdx] = {x, y, cosf(a) * s, sinf(a) * s, now, (uint16_t)(350 + esp_random() % 400), c};
    partIdx = (partIdx + 1) % NPART;
  }
}

static void spawnAsteroid(float r, float x, float y, float vx, float vy) {
  for (int i = 0; i < NAST; i++) {
    if (ast[i].on) continue;
    Asteroid &a = ast[i];
    a.on = true; a.x = x; a.y = y; a.vx = vx; a.vy = vy; a.r = r;
    a.ang = frand() * 6.28f; a.spin = (frand() - 0.5f) * 3.0f; a.flash = 0;
    a.hp = r >= 28 ? 3 : r >= 18 ? 2 : 1;
    for (int k = 0; k < NVERT; k++) a.rad[k] = 0.72f + frand() * 0.42f;
    return;
  }
}
static float baseSpeed() { return fminf(80.0f + wave * 16.0f, 270.0f); }
static void spawnRandom() {
  float f = frand();
  float r = f < 0.35f ? 12.0f : f < 0.75f ? 20.0f : 30.0f;
  float vy = baseSpeed() * (0.8f + frand() * 0.5f) * (r < 15 ? 1.2f : r > 25 ? 0.85f : 1.0f);
  spawnAsteroid(r, 30 + frand() * (W - 60), -r - 4, (frand() - 0.5f) * (30.0f + wave * 6.0f), vy);
}

static void render(uint32_t now) {
  memcpy(frameBuffer(), bg, FB_PIXELS * 2);
  // starfield, three parallax layers
  for (int layer = 0; layer < 3; layer++) {
    float sp = (14.0f + layer * 22.0f) * (1.0f + wave * 0.04f);
    for (int i = 0; i < 36; i++) {
      uint32_t h = hash2(i, 70 + layer);
      int x = h % W;
      int y = (int)wrapf((float)((h >> 9) % H) + starT * sp, (float)H);
      uint16_t c = layer == 0 ? rgb(70, 70, 100) : layer == 1 ? rgb(140, 150, 200) : rgb(230, 235, 255);
      if (layer == 2) gfx->fillRect(x, y, 2, 3, c);
      else gfx->fillRect(x, y, layer + 1, layer + 1, c);
    }
  }
  // asteroids
  for (int i = 0; i < NAST; i++) {
    const Asteroid &a = ast[i];
    if (!a.on) continue;
    int vx[NVERT], vy[NVERT];
    for (int k = 0; k < NVERT; k++) {
      float an = a.ang + k * 6.2832f / NVERT, rr = a.r * a.rad[k];
      vx[k] = (int)(a.x + cosf(an) * rr);
      vy[k] = (int)(a.y + sinf(an) * rr);
    }
    bool fl = now < a.flash;
    uint16_t fill = fl ? rgb(120, 90, 80) : lerpColor(rgb(60, 44, 36), rgb(30, 22, 20), a.r / 30.0f * 0.5f);
    uint16_t line = fl ? C_TEXT : a.hp >= 3 ? rgb(255, 120, 60) : a.hp == 2 ? rgb(255, 170, 70) : rgb(255, 210, 110);
    for (int k = 0; k < NVERT; k++) gfx->fillTriangle((int)a.x, (int)a.y, vx[k], vy[k], vx[(k + 1) % NVERT], vy[(k + 1) % NVERT], fill);
    for (int k = 0; k < NVERT; k++) {
      int k2 = (k + 1) % NVERT;
      gfx->drawLine(vx[k] + 1, vy[k] + 1, vx[k2] + 1, vy[k2] + 1, lerpColor(line, C_BG, 0.7f));   // soft glow
      gfx->drawLine(vx[k], vy[k], vx[k2], vy[k2], line);
    }
    gfx->fillCircle((int)(a.x - a.r * 0.25f), (int)(a.y - a.r * 0.2f), (int)(a.r * 0.18f) + 1, lerpColor(fill, C_BG, 0.5f));
  }
  // bullets
  for (int i = 0; i < NBUL; i++) {
    if (!bul[i].on) continue;
    int x = (int)bul[i].x, y = (int)bul[i].y;
    gfx->fillRect(x - 3, y - 8, 7, 14, rgb(60, 40, 0));
    gfx->fillRect(x - 1, y - 7, 3, 12, rgb(255, 235, 110));
    gfx->fillRect(x, y - 7, 1, 8, C_TEXT);
  }
  // ship
  bool vis = st != ST_OVER && !(now < invulUntil && ((now / 90) & 1));
  if (vis) {
    int x = (int)sx, y = SHIP_Y;
    gfx->fillCircle(x, y + 2, 26, rgb(0, 12, 24));
    gfx->fillCircle(x, y + 2, 18, rgb(0, 34, 54));
    int fl = 8 + (int)((now / 40) % 3) * 3;
    gfx->fillTriangle(x - 6, y + 12, x + 6, y + 12, x, y + 12 + fl + 4, rgb(255, 90, 20));
    gfx->fillTriangle(x - 3, y + 12, x + 3, y + 12, x, y + 12 + fl, rgb(255, 230, 120));
    gfx->fillTriangle(x - 11, y + 14, x - 22, y + 18, x - 8, y + 2, rgb(0, 110, 170));
    gfx->fillTriangle(x + 11, y + 14, x + 22, y + 18, x + 8, y + 2, rgb(0, 110, 170));
    gfx->fillTriangle(x, y - 19, x - 12, y + 13, x + 12, y + 13, rgb(0, 200, 240));
    gfx->fillTriangle(x, y - 13, x - 6, y + 11, x + 6, y + 11, rgb(185, 250, 255));
    gfx->fillCircle(x, y - 3, 3, rgb(20, 60, 140));
  }
  // explosions
  for (int i = 0; i < NPART; i++) {
    uint32_t age = now - parts[i].born;
    if (age > parts[i].life) continue;
    float t = age / 1000.0f;
    int x = (int)(parts[i].x + parts[i].vx * t), y = (int)(parts[i].y + parts[i].vy * t);
    float f = (float)age / parts[i].life;
    int sz = f < 0.4f ? 4 : f < 0.75f ? 3 : 2;
    gfx->fillRect(x - sz / 2, y - sz / 2, sz, sz, lerpColor(parts[i].c, C_BG, f));
  }
  hudBar(score, best, C_BG, rgb(80, 240, 255));
  gfx->drawFastHLine(0, 31, W, rgb(40, 60, 120));
  char b[16];
  snprintf(b, sizeof b, "WAVE %d", wave);
  text(b, 240, 9, 2, C_ACC);
  for (int i = 0; i < lives; i++) gfx->fillTriangle(330 + i * 16, 8, 324 + i * 16, 24, 336 + i * 16, 24, rgb(0, 200, 240));
  uint16_t acc = rgb(80, 240, 255);
  if (st == ST_COUNT) {
    int n = 3 - (int)((now - t0) / 700);
    char c[2] = {(char)('0' + constrain(n, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, H / 2 - 54, 88, 88, 18, C_PANEL);
    centerText(c, H / 2 - 34, 6, acc);
    centerText(ctrl ? "tilt to move   auto-fire" : "LEFT / RIGHT move   auto-fire", H / 2 + 46, 2, C_DIM);
  } else if (st == ST_PLAY && now < bannerUntil) {
    snprintf(b, sizeof b, "WAVE %d", wave);
    centerText(b, 110, 4, C_ACC);
  } else if (st == ST_PAUSE) {
    panelBox("PAUSED", nullptr, "RIGHT resume   LEFT menu", acc);
  } else if (st == ST_OVER) {
    char s[32];
    snprintf(s, sizeof s, newBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)score);
    panelBox("GAME OVER", s, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void astroStart() {
  ctrl = asOpts[0].value;
  if (ctrl == 1 && !imuOk) ctrl = 0;
  if (!bg) buildBackground();
  memset(ast, 0, sizeof ast);
  memset(bul, 0, sizeof bul);
  memset(parts, 0, sizeof parts);
  partIdx = 0;
  score = 0; lives = 3; wave = 1; killsInWave = 0;
  sx = W / 2.0f; svx = 0; starT = 0;
  newBest = false; invulUntil = 0; bannerUntil = 0;
  best = loadBest(ctrl ? "asBestT" : "asBestB");
  st = ST_COUNT;
  t0 = lastT = millis();
  lastRender = 0; lastFire = 0; nextSpawn = t0 + 2600;
  play(660, 90);
  render(t0);
}

static void gameOver(uint32_t now) {
  st = ST_OVER;
  overAt = now;
  boom(sx, SHIP_Y, 40, now, 1.4f);
  newBest = saveBest(ctrl ? "asBestT" : "asBestB", score);
  if (newBest) best = score;
  newBest ? sfxBest() : sfxBad();
}

static void destroyAsteroid(Asteroid &a, uint32_t now, bool byShot) {
  a.on = false;
  boom(a.x, a.y, a.r >= 28 ? 26 : a.r >= 18 ? 16 : 9, now, a.r / 20.0f + 0.4f);
  if (!byShot) return;
  score += a.r >= 28 ? 30 : a.r >= 18 ? 20 : 10;
  killsInWave++;
  play(a.r >= 28 ? 180 : a.r >= 18 ? 260 : 380, 50);
  if (a.r >= 18) {
    float cr = a.r * 0.58f;
    spawnAsteroid(cr, a.x - 6, a.y, a.vx - 45.0f, a.vy * 0.9f);
    spawnAsteroid(cr, a.x + 6, a.y, a.vx + 45.0f, a.vy * 0.9f);
  }
  if (killsInWave >= 6 + wave * 2) {
    wave++;
    killsInWave = 0;
    score += 25;
    bannerUntil = now + 1400;
    sfxGood();
  }
}

static uint32_t asPauseAt;                         // a stray LEFT right after pausing must not exit
static bool astroFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = constrain((now - lastT) / 1000.0f, 0.0f, 0.05f);
  lastT = now;
  switch (st) {
    case ST_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - t0) / 700);
      if (n != lastN && n >= 1) play(660, 90);
      lastN = n;
      starT += dt;
      if (now - t0 >= 2100) { st = ST_PLAY; imuZero(); lastN = 3; lastFire = now; sfxStart(); }
      break;
    }
    case ST_PLAY: {
      starT += dt;
      // ship
      if (ctrl == 0) {
        int dir = (in.leftDown ? -1 : 0) + (in.rightDown ? 1 : 0);
        if (in.left) svx = -400.0f;
        if (in.right) svx = 400.0f;
        if (dir) svx += (dir * 300.0f - svx) * fminf(1.0f, dt * 12.0f);
        else svx *= expf(-dt * 6.0f);
        sx += svx * dt;
      } else {
        float target = W / 2.0f + axisX() * 620.0f;
        sx += (target - sx) * fminf(1.0f, dt * 14.0f);
        if (in.right) { st = ST_PAUSE; asPauseAt = now; sfxClick(); break; }
      }
      sx = constrain(sx, 24.0f, W - 24.0f);
      // fire
      if (now - lastFire >= FIRE_MS) {
        lastFire = now;
        for (int i = 0; i < NBUL; i++) if (!bul[i].on) { bul[i] = {sx, SHIP_Y - 20.0f, true}; play(1400, 8); break; }
      }
      for (int i = 0; i < NBUL; i++) {
        if (!bul[i].on) continue;
        bul[i].y -= 520.0f * dt;
        if (bul[i].y < 24) bul[i].on = false;
      }
      // spawn
      if ((int32_t)(now - nextSpawn) >= 0) {
        spawnRandom();
        float iv = fmaxf(320.0f, 1050.0f - wave * 80.0f);
        nextSpawn = now + (uint32_t)(iv * (0.7f + frand() * 0.6f));
      }
      // asteroids
      for (int i = 0; i < NAST; i++) {
        Asteroid &a = ast[i];
        if (!a.on) continue;
        a.x += a.vx * dt; a.y += a.vy * dt; a.ang += a.spin * dt;
        if (a.x < a.r && a.vx < 0) a.vx = -a.vx;
        if (a.x > W - a.r && a.vx > 0) a.vx = -a.vx;
        if (a.y > H + a.r + 4) { a.on = false; continue; }
        for (int k = 0; k < NBUL; k++) {
          if (!bul[k].on || a.y < 20) continue;
          float dx = bul[k].x - a.x, dy = bul[k].y - a.y, rr = a.r * 0.85f + 3;
          if (dx * dx + dy * dy < rr * rr) {
            bul[k].on = false;
            if (--a.hp <= 0) { destroyAsteroid(a, now, true); break; }
            a.flash = now + 70;
            play(700, 12);
          }
        }
        if (!a.on) continue;
        if (now >= invulUntil) {
          float dx = sx - a.x, dy = SHIP_Y - a.y, rr = a.r * 0.8f + 9;
          if (dx * dx + dy * dy < rr * rr) {
            destroyAsteroid(a, now, false);
            boom(sx, SHIP_Y, 22, now, 1.0f);
            lives--;
            if (lives <= 0) { gameOver(now); break; }
            invulUntil = now + 2000;
            sfxBad();
          }
        }
      }
      break;
    }
    case ST_PAUSE:
      if (in.right) { st = ST_PLAY; imuZero(); sfxClick(); lastT = now; lastFire = now; }
      if (in.left && now - asPauseAt > 500) return false;
      break;
    case ST_OVER:
      starT += dt;
      if (now - overAt > 600) {
        if (in.right) { astroStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - lastRender >= 16) { lastRender = now; render(now); }
  return true;
}

Game GAME_ASTRO = {"ASTRO", "dodge and blast the asteroids", 0x07FF, asOpts, 1, astroStart, astroFrame};
