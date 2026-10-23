// duck_game.cpp - Duck-Hunt style shooter.
// Aim: TILT moves the crosshair (either button shoots) or BUTTONS (hold = sweep left/right, quick tap = shoot,
// the crosshair bobs up and down by itself). Rounds of 6 ducks with limited shots; hit 4 to advance, ducks get faster.
// THEME: DAY (sky gradient, sun) or NIGHT (moonlit, AMOLED black).
#include "game.h"

static const char *const DU_THEMES[] = {"DAY", "NIGHT"};
static const char *const DU_CONTROLS[] = {"TILT", "BUTTONS"};
static Option duOpts[] = {{"THEME", DU_THEMES, 2, "duTheme", 0}, {"CONTROL", DU_CONTROLS, 2, "duCtrl", 0}};

enum DuSt : uint8_t { DU_COUNT, DU_PLAY, DU_BANNER, DU_OVER };
enum DuckSt : uint8_t { DK_OFF, DK_FLY, DK_HIT, DK_FALL, DK_AWAY };

static constexpr int DU_TOP = 32, DU_GROUND = H - 64, DU_N = 6, DU_NEED = 4, DU_MAXD = 3, DU_PART = 40, DU_NREED = 30;
static constexpr int DU_SHOTS = DU_N + 3;

struct Duck {
  float x, y, vx, vy, t, life, amp, om, ph, speed, rot, spin;
  int8_t dir, idx;
  DuckSt s;
  uint8_t pal;
  bool entered;
  uint32_t hitAt;
};
struct DuPart { float x, y, vx, vy; uint32_t born; uint16_t c; uint8_t kind; };
struct DuPop { float x, y; int v; uint32_t born; };

static Duck duDucks[DU_MAXD];
static DuPart duParts[DU_PART];
static DuPop duPops[3];
static DuSt duSt;
static int duTheme, duCtrl, duRound, duShots, duSpawned, duHits, duBgTheme = -1;
static uint32_t duScore, duBest, duT0, duCountLen, duLastT, duLastRender, duLastSpawn, duShotAt, duBannerAt, duOverAt;
static float duX, duY, duShotX, duShotY;
static uint8_t duTally[DU_N];
static bool duNewBest, duRoundOk;
static int duBonus;
static uint16_t *duBg = nullptr;
// BUTTONS-mode input tracking
static uint32_t duPressAt[2];
static bool duSwept[2], duPrevDown[2], duBothFired;

static inline uint32_t duHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static const uint8_t DU_PAL[2][4][3] = {
    {{140, 92, 48}, {205, 170, 110}, {16, 130, 64}, {96, 60, 30}},
    {{84, 84, 108}, {150, 150, 176}, {30, 70, 170}, {52, 52, 76}},
};
static uint16_t duc(int r, int g, int b) {
  uint16_t c = rgb(r, g, b);
  return duTheme == 1 ? lerpColor(c, rgb(30, 60, 140), 0.22f) : c;
}

// ---------------- background ----------------
static void duBuildBackground() {
  if (!duBg) duBg = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!duBg) return;
  bool n = duTheme == 1;
  gfx->fillScreen(C_BG);
  uint16_t top = n ? rgb(2, 3, 20) : rgb(30, 100, 210), hor = n ? rgb(20, 28, 80) : rgb(196, 228, 255);
  for (int y = DU_TOP; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(top, hor, constrain((y - DU_TOP) / (float)(DU_GROUND - DU_TOP), 0.0f, 1.0f)));
  if (n) {
    for (int i = 0; i < 110; i++) {
      uint32_t h = duHash(i, 5);
      int x = h % W, y = DU_TOP + (h >> 9) % (DU_GROUND - DU_TOP - 40), b = 70 + (h >> 20) % 160;
      gfx->drawPixel(x, y, rgb(b, b, min(255, b + 30)));
      if ((h >> 28) == 0) gfx->fillRect(x, y, 2, 2, rgb(230, 230, 255));
    }
    int mx = 350, my = 96;                                  // moon, layered glow
    for (int r = 64; r > 24; r -= 5) gfx->fillCircle(mx, my, r, lerpColor(rgb(20, 28, 80), rgb(110, 120, 190), (64 - r) / 40.0f));
    gfx->fillCircle(mx, my, 24, rgb(250, 250, 226));
    gfx->fillCircle(mx - 7, my - 5, 5, rgb(214, 214, 196));
    gfx->fillCircle(mx + 8, my + 7, 4, rgb(220, 220, 202));
    gfx->fillCircle(mx + 6, my - 10, 2, rgb(222, 222, 204));
  } else {
    int sx = 360, sy = 92;                                  // sun
    for (int r = 70; r > 26; r -= 4) gfx->fillCircle(sx, sy, r, lerpColor(rgb(120, 180, 250), rgb(255, 250, 200), (70 - r) / 44.0f));
    gfx->fillCircle(sx, sy, 26, rgb(255, 252, 220));
    for (int i = 0; i < 5; i++) {                           // clouds
      uint32_t h = duHash(i, 77);
      int cx = 20 + (h % 380), cy = DU_TOP + 22 + (h >> 10) % 110;
      gfx->fillEllipse(cx, cy, 30, 9, rgb(240, 248, 255));
      gfx->fillEllipse(cx - 14, cy - 6, 16, 9, rgb(246, 251, 255));
      gfx->fillEllipse(cx + 12, cy - 8, 18, 10, rgb(250, 253, 255));
    }
  }
  // two hill layers
  for (int x = 0; x < W; x++) {
    int h1 = DU_GROUND - 34 - (int)(20 * sinf(x * 0.012f + 1.0f) + 10 * sinf(x * 0.031f));
    int h2 = DU_GROUND - 12 - (int)(12 * sinf(x * 0.02f + 3.0f) + 6 * sinf(x * 0.05f));
    gfx->drawFastVLine(x, h1, H - h1, n ? rgb(8, 14, 44) : rgb(70, 130, 110));
    gfx->drawFastVLine(x, h2, H - h2, n ? rgb(6, 24, 34) : rgb(40, 110, 50));
  }
  for (int i = 0; i < 9; i++) {                             // distant trees
    uint32_t h = duHash(i, 41);
    int x = 10 + (h % (W - 20)), y = DU_GROUND - 14 - (int)(12 * sinf(x * 0.02f + 3.0f));
    gfx->fillRect(x - 1, y - 14, 3, 14, n ? rgb(8, 10, 24) : rgb(60, 60, 40));
    gfx->fillCircle(x, y - 20, 9, n ? rgb(6, 20, 30) : rgb(30, 100, 46));
    gfx->fillCircle(x - 3, y - 23, 4, n ? rgb(10, 30, 44) : rgb(54, 140, 66));
  }
  for (int y = DU_GROUND + 6; y < H; y++)                   // ground
    gfx->drawFastHLine(0, y, W, lerpColor(n ? rgb(10, 40, 44) : rgb(52, 128, 42), n ? rgb(2, 10, 14) : rgb(14, 56, 22), (y - DU_GROUND) / 64.0f));
  for (int i = 0; i < 260; i++) {                           // baked grass blades
    uint32_t h = duHash(i, 9);
    int x = h % W, y = DU_GROUND + 6 + (h >> 9) % 54, len = 3 + (h >> 20) % 8;
    uint16_t c = n ? lerpColor(rgb(10, 50, 54), rgb(30, 110, 100), ((h >> 24) & 7) / 7.0f)
                   : lerpColor(rgb(30, 100, 36), rgb(120, 200, 70), ((h >> 24) & 7) / 7.0f);
    gfx->drawLine(x, y, x + (int)((h >> 27) % 3) - 1, y - len, c);
  }
  memcpy(duBg, frameBuffer(), FB_PIXELS * 2);
  duBgTheme = duTheme;
}

// ---------------- ducks ----------------
static void duMark(Duck &d, uint8_t what) {
  if (d.idx >= 0 && d.idx < DU_N && duTally[d.idx] == 0) duTally[d.idx] = what;
}
static void duSpawnDuck(uint32_t now) {
  for (auto &d : duDucks) {
    if (d.s != DK_OFF) continue;
    d.s = DK_FLY;
    d.dir = (esp_random() & 1) ? 1 : -1;
    d.x = d.dir > 0 ? -34.0f : W + 34.0f;
    d.y = DU_TOP + 50 + (int)(esp_random() % (DU_GROUND - DU_TOP - 120));
    d.speed = fminf(120.0f + 22.0f * (duRound - 1), 330.0f) * (0.88f + (esp_random() % 25) * 0.01f);
    d.amp = 0.5f + (esp_random() % 50) * 0.01f;
    d.om = 1.4f + (esp_random() % 18) * 0.1f;
    d.ph = (esp_random() % 628) * 0.01f;
    d.t = 0;
    d.life = fmaxf(3.8f, 7.0f - 0.45f * (duRound - 1));
    d.idx = duSpawned++;
    d.pal = esp_random() & 1;
    d.entered = false;
    d.rot = d.spin = d.vx = d.vy = 0;
    duLastSpawn = now;
    return;
  }
}

static void duSpark(float x, float y, uint32_t now, bool duckHit) {
  int n = 0;
  for (auto &p : duParts) {
    if (n >= (duckHit ? 22 : 8)) break;
    if (p.born && now - p.born < 700) continue;
    float a = (esp_random() % 628) * 0.01f, s = 60.0f + (esp_random() % 180);
    p = {x, y, cosf(a) * s, sinf(a) * s - 40.0f, now,
         duckHit ? ((n & 3) == 0 ? rgb(255, 255, 255) : (n & 1) ? rgb(255, 210, 60) : rgb(190, 130, 70)) : rgb(255, 200, 120),
         (uint8_t)(duckHit && n > 12 ? 1 : 0)};
    n++;
  }
}

static void duPopup(float x, float y, int v, uint32_t now) {
  for (auto &p : duPops)
    if (p.born == 0 || now - p.born > 800) { p = {x, y, v, now}; return; }
}

static void duOutOfShots() {
  bool any = false;
  for (auto &d : duDucks)
    if (d.s == DK_FLY) { d.s = DK_AWAY; duMark(d, 2); any = true; }
  for (int i = duSpawned; i < DU_N; i++) { if (!duTally[i]) duTally[i] = 2; any = true; }
  duSpawned = DU_N;
  if (any) sfxBad();
}

static void duShoot(uint32_t now) {
  if (duShots <= 0) return;
  duShots--;
  duShotAt = now; duShotX = duX; duShotY = duY;
  play(220, 16); play(90, 40);
  Duck *hit = nullptr;
  float bd = 1e9f;
  for (auto &d : duDucks) {
    if (d.s != DK_FLY) continue;
    float dx = d.x - duX, dy = d.y - duY, dd = dx * dx + dy * dy;
    if (dd < 30.0f * 30.0f && dd < bd) { bd = dd; hit = &d; }
  }
  if (hit) {
    hit->s = DK_HIT; hit->hitAt = now; hit->vx = hit->vy = 0; hit->rot = 0;
    hit->spin = (hit->dir > 0 ? 1 : -1) * (6.0f + (esp_random() % 40) * 0.1f);
    duMark(*hit, 1);
    duHits++;
    int pts = 100 + 25 * (duRound - 1);
    duScore += pts;
    duPopup(hit->x, hit->y - 20, pts, now);
    duSpark(hit->x, hit->y, now, true);
    play(1100, 30); play(1500, 50);
  } else {
    duSpark(duX, duY, now, false);
  }
  if (duShots == 0) duOutOfShots();
}

// ---------------- round flow ----------------
static void duStartRound(uint32_t len) {
  for (auto &d : duDucks) d.s = DK_OFF;
  duSpawned = 0; duHits = 0; duShots = DU_SHOTS;
  memset(duTally, 0, sizeof duTally);
  duCountLen = len;
  duT0 = millis();
  duSt = DU_COUNT;
}
static void duFinishRound(uint32_t now) {
  duBannerAt = now;
  duRoundOk = duHits >= DU_NEED;
  if (duRoundOk) {
    duBonus = duShots * 30 + (duHits == DU_N ? 300 : 0);
    duScore += duBonus;
    sfxBest();
    saveBest(duCtrl ? "duBestB" : "duBestT", duScore);
    if (duScore > duBest) duBest = duScore;
    duSt = DU_BANNER;
  } else {
    duNewBest = saveBest(duCtrl ? "duBestB" : "duBestT", duScore);
    if (duNewBest) duBest = duScore;
    duNewBest ? sfxBest() : sfxBad();
    duOverAt = now;
    duSt = DU_OVER;
  }
}

// ---------------- drawing ----------------
static void duDrawDuck(const Duck &d, uint32_t now) {
  float f = d.dir > 0 ? 1.0f : -1.0f;
  bool flying = d.s == DK_FLY || d.s == DK_AWAY;
  float th = flying ? f * atan2f(d.vy, fabsf(d.vx) + 1.0f) * 0.7f : d.rot;
  float cs = cosf(th), sn = sinf(th);
  auto X = [&](float lx, float ly) { lx *= f; return (int)(d.x + lx * cs - ly * sn); };
  auto Y = [&](float lx, float ly) { lx *= f; return (int)(d.y + lx * sn + ly * cs); };
  auto circ = [&](float lx, float ly, int r, uint16_t c) { gfx->fillCircle(X(lx, ly), Y(lx, ly), r, c); };
  auto tri = [&](float ax, float ay, float bx, float by, float cx, float cy, uint16_t c) {
    gfx->fillTriangle(X(ax, ay), Y(ax, ay), X(bx, by), Y(bx, by), X(cx, cy), Y(cx, cy), c);
  };
  static const uint8_t seq[4] = {0, 1, 2, 1};
  int fr = d.s == DK_HIT ? 1 : seq[((now / (d.s == DK_FALL ? 60 : 85)) + d.idx) & 3];
  const uint8_t(*p)[3] = DU_PAL[d.pal];
  uint16_t body = duc(p[0][0], p[0][1], p[0][2]), belly = duc(p[1][0], p[1][1], p[1][2]);
  uint16_t head = duc(p[2][0], p[2][1], p[2][2]), wing = duc(p[3][0], p[3][1], p[3][2]);
  uint16_t wingFar = lerpColor(wing, C_BG, 0.4f), tip = lerpColor(wing, rgb(240, 240, 250), 0.55f);
  static const float WG[3][6] = {{-1, -3, -13, -3, -9, -22}, {-1, -1, -14, -2, -23, -9}, {-1, 1, -12, 2, -10, 19}};
  const float *w = WG[fr];
  tri(w[0] + 3, w[1] - 2, w[2] + 3, w[3] - 2, w[4] + 3, w[5] - 2, wingFar);           // far wing
  tri(-10, -1, -25, -6, -18, 4, lerpColor(body, C_BG, 0.3f));                       // tail
  circ(-6, 2, 7, body); circ(2, 1, 9, body); circ(9, -1, 7, body);                  // body
  circ(1, 5, 5, belly); circ(-5, 5, 4, belly);
  circ(12, -5, 5, head);                                                             // neck
  gfx->drawLine(X(8, -3), Y(8, -3), X(15, 0), Y(15, 0), duc(240, 240, 240));        // white collar
  circ(16, -9, 6, head);                                                             // head
  tri(20, -11, 29, -8, 20, -6, duc(250, 196, 30));                                   // beak
  if (d.s == DK_HIT || d.s == DK_FALL) {
    gfx->drawLine(X(16, -12), Y(16, -12), X(20, -8), Y(20, -8), C_TEXT);
    gfx->drawLine(X(20, -12), Y(20, -12), X(16, -8), Y(16, -8), C_TEXT);
  } else {
    circ(18, -11, 2, C_TEXT);
    circ(18.5f, -11, 1, C_BG);
  }
  tri(w[0], w[1], w[2], w[3], w[4], w[5], wing);                                     // near wing
  tri(w[4], w[5], (w[4] * 2 + w[2]) / 3, (w[5] * 2 + w[3]) / 3, (w[4] * 2 + w[0]) / 3, (w[5] * 2 + w[1]) / 3, tip);
}

static void duDrawReeds(uint32_t now) {
  bool n = duTheme == 1;
  for (int i = 0; i < DU_NREED; i++) {
    uint32_t h = duHash(i, 31);
    bool cat = (h & 3) == 0;
    float x = i * (W / (float)DU_NREED) + (h % 14);
    float ht = (cat ? 70.0f : 36.0f) + (h >> 8) % 40;
    float sw = sinf(now * 0.0021f + i * 1.7f) * (4.0f + ht * 0.06f);
    int x0 = (int)x, y0 = H - 20, tx = x0 + (int)sw, ty = y0 - (int)ht;
    float k = ((h >> 16) & 7) / 7.0f;
    uint16_t c = n ? lerpColor(rgb(8, 50, 56), rgb(30, 110, 110), k) : lerpColor(rgb(30, 110, 40), rgb(110, 190, 60), k);
    gfx->fillTriangle(x0 - 3, y0, x0 + 3, y0, tx, ty, c);
    gfx->drawLine(x0 + 1, y0, tx, ty, lerpColor(c, C_TEXT, n ? 0.15f : 0.25f));
    if (cat) {
      gfx->fillEllipse(tx, ty + 5, 3, 10, n ? rgb(70, 46, 46) : rgb(112, 62, 30));
      gfx->drawLine(tx, ty - 5, tx, ty - 12, c);
    }
  }
}

static void duDrawCrosshair(uint32_t now) {
  int x = (int)duX, y = (int)duY;
  uint16_t cc = duTheme == 1 ? rgb(80, 255, 170) : rgb(255, 60, 40);
  bool ready = duShots > 0;
  if (!ready) cc = lerpColor(cc, C_DIM, 0.7f);
  gfx->drawCircle(x, y, 22, lerpColor(cc, C_BG, 0.8f));
  gfx->drawCircle(x, y, 21, lerpColor(cc, C_BG, 0.55f));
  gfx->drawCircle(x, y, 19, cc);
  gfx->drawCircle(x, y, 18, cc);
  gfx->drawFastHLine(x - 30, y, 18, cc); gfx->drawFastHLine(x + 13, y, 18, cc);
  gfx->drawFastVLine(x, y - 30, 18, cc); gfx->drawFastVLine(x, y + 13, 18, cc);
  gfx->fillCircle(x, y, 2, C_TEXT);
  (void)now;
}

static void duRender(uint32_t now) {
  if (duBgTheme != duTheme) duBuildBackground();
  if (duBg) memcpy(frameBuffer(), duBg, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  bool n = duTheme == 1;
  uint16_t acc = n ? rgb(120, 220, 255) : C_ACC;
  for (auto &d : duDucks) if (d.s != DK_OFF) duDrawDuck(d, now);
  for (auto &p : duParts) {                                  // sparks and feathers
    if (!p.born) continue;
    uint32_t age = now - p.born;
    if (age > 650) continue;
    float t = age * 0.001f, x = p.x + p.vx * t, y = p.y + p.vy * t + (p.kind ? 120.0f : 380.0f) * t * t;
    uint16_t c = lerpColor(p.c, n ? C_BG : rgb(60, 110, 60), age / 650.0f);
    if (p.kind) gfx->fillRect((int)x, (int)y, 4, 2, c);
    else gfx->fillRect((int)x - 1, (int)y - 1, 3, 3, c);
  }
  uint32_t fa = now - duShotAt;                              // muzzle flash
  if (duShotAt && fa < 120) {
    float t = fa / 120.0f;
    int r = (int)(8 + 26 * t);
    gfx->fillCircle((int)duShotX, (int)duShotY, (int)(24 * (1 - t)) + 3, lerpColor(C_TEXT, rgb(255, 150, 40), t));
    gfx->drawCircle((int)duShotX, (int)duShotY, r, lerpColor(rgb(255, 220, 120), C_BG, t));
    gfx->drawCircle((int)duShotX, (int)duShotY, r + 3, lerpColor(rgb(255, 160, 60), C_BG, t));
  }
  duDrawReeds(now);
  for (auto &p : duPops) {
    if (!p.born) continue;
    uint32_t age = now - p.born;
    if (age > 800) continue;
    char b[12];
    snprintf(b, sizeof b, "+%d", p.v);
    text(b, (int)p.x - 18, (int)(p.y - age * 0.03f), 2, lerpColor(C_TEXT, rgb(255, 200, 60), age / 800.0f));
  }
  // bottom strip: shots left, duck tally
  gfx->fillRect(0, H - 26, W, 26, n ? rgb(2, 6, 12) : rgb(8, 22, 10));
  gfx->drawFastHLine(0, H - 26, W, n ? rgb(30, 90, 110) : rgb(70, 150, 60));
  for (int i = 0; i < DU_SHOTS; i++) {
    int x = 12 + i * 14;
    if (i < duShots) {
      gfx->fillRoundRect(x, H - 21, 8, 15, 3, rgb(255, 200, 60));
      gfx->fillRect(x + 1, H - 21, 2, 15, rgb(255, 240, 170));
    } else gfx->drawRoundRect(x, H - 21, 8, 15, 3, 0x2945);
  }
  for (int i = 0; i < DU_N; i++) {
    int x = W - 18 - (DU_N - 1 - i) * 24, y = H - 14;
    uint8_t t = duTally[i];
    if (t == 1) { gfx->fillCircle(x, y, 8, rgb(255, 90, 60)); gfx->fillCircle(x - 2, y - 3, 2, rgb(255, 220, 200)); }
    else if (t == 2) gfx->fillCircle(x, y, 8, 0x4208);
    else gfx->drawCircle(x, y, 8, C_DIM);
  }
  hudBar(duScore, duBest, n ? C_BG : rgb(10, 30, 60), n ? rgb(150, 200, 255) : C_TEXT);
  char rb[12];
  snprintf(rb, sizeof rb, "ROUND %d", duRound);
  text(rb, 262, 9, 2, acc);
  if (duSt == DU_PLAY || duSt == DU_BANNER) duDrawCrosshair(now);
  if (duSt == DU_COUNT) {
    char c[24];
    snprintf(c, sizeof c, "ROUND %d", duRound);
    gfx->fillRoundRect(W / 2 - 110, 90, 220, 120, 16, C_PANEL);
    gfx->drawRoundRect(W / 2 - 110, 90, 220, 120, 16, acc);
    centerText(c, 108, 4, acc);
    int cnt = 3 - (int)((now - duT0) * 3 / duCountLen);
    char d[2] = {(char)('0' + constrain(cnt, 1, 3)), 0};
    centerText(d, 156, 5, C_TEXT);
    centerText(duCtrl ? "HOLD = sweep   TAP = shoot" : "TILT to aim   BUTTON shoots", 232, 2, C_DIM);
  } else if (duSt == DU_BANNER) {
    char b[32];
    snprintf(b, sizeof b, "HITS %d/%d  +%d", duHits, DU_N, duBonus);
    panelBox("ROUND CLEAR", b, nullptr, acc);
  } else if (duSt == DU_OVER) {
    char b[32];
    snprintf(b, sizeof b, duNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)duScore);
    panelBox("GAME OVER", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

// ---------------- control ----------------
static void duAim(uint32_t now, float dt) {
  const float yMin = DU_TOP + 16, yMax = H - 34;
  if (duCtrl == 0) {
    auto shape = [](float t, float dz) { return fabsf(t) < dz ? 0.0f : (t > 0 ? t - dz : t + dz); };
    float tx = W / 2 + shape(axisX(), 0.03f) * 800.0f;
    float ty = (yMin + yMax) / 2 + shape(axisY(), 0.03f) * 720.0f;
    float k = 1.0f - expf(-dt * 18.0f);
    duX += (constrain(tx, 0.0f, (float)W - 1) - duX) * k;
    duY += (constrain(ty, yMin, yMax) - duY) * k;
  } else {
    bool d[2] = {in.leftDown, in.rightDown};
    bool shoot = false;
    for (int i = 0; i < 2; i++) {
      if (d[i] && !duPrevDown[i]) { duPressAt[i] = now; duSwept[i] = false; }
      uint32_t held = now - duPressAt[i];
      if (d[i] && held > 150) {
        duSwept[i] = true;
        float sp = 170.0f + 380.0f * fminf(1.0f, (held - 150) / 700.0f);
        duX += (i ? 1 : -1) * sp * dt;
      }
      if (!d[i] && duPrevDown[i] && !duSwept[i]) shoot = true;
      duPrevDown[i] = d[i];
    }
    if (d[0] && d[1]) { if (!duBothFired) { duBothFired = true; shoot = true; duSwept[0] = duSwept[1] = true; } }
    else duBothFired = false;
    duX = constrain(duX, 0.0f, (float)W - 1);
    float mid = (yMin + yMax) / 2, amp = (yMax - yMin) / 2 - 10.0f;
    duY = mid + amp * sinf(now * (0.0016f + 0.00015f * duRound));
    if (shoot) duShoot(now);
  }
}

static void duStart() {
  duTheme = duOpts[0].value;
  duCtrl = duOpts[1].value;
  if (duCtrl == 0 && !imuOk) duCtrl = 1;
  duBest = loadBest(duCtrl ? "duBestB" : "duBestT");
  duScore = 0; duRound = 1; duNewBest = false; duBonus = 0;
  duX = W / 2; duY = (DU_TOP + H) / 2;
  duShotAt = 0; duLastT = millis();
  memset(duParts, 0, sizeof duParts);
  memset(duPops, 0, sizeof duPops);
  duPrevDown[0] = duPrevDown[1] = false; duBothFired = false;
  duStartRound(2100);
  play(660, 90);
  duRender(duT0);
}

static bool duFrame(uint32_t now) {
  homeLock = true;                                 // quick repeated BOOT taps are normal play here: no triple-tap home
  float dt = fminf((now - duLastT) / 1000.0f, 0.05f);
  duLastT = now;
  switch (duSt) {
    case DU_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - duT0) * 3 / duCountLen);
      if (n != lastN && n >= 1) play(660, 70);
      lastN = n;
      if (now - duT0 >= duCountLen) {
        duSt = DU_PLAY; lastN = 3;
        imuZero();
        duLastSpawn = now - 600;
        duPrevDown[0] = duPrevDown[1] = false;
        sfxStart();
      }
      break;
    }
    case DU_PLAY: {
      duAim(now, dt);
      if (duCtrl == 0 && (in.left || in.right)) duShoot(now);
      int active = 0;
      for (auto &d : duDucks) if (d.s != DK_OFF) active++;
      int maxActive = min(1 + duRound / 3, DU_MAXD);
      if (duShots > 0 && duSpawned < DU_N && active < maxActive && now - duLastSpawn > 700) duSpawnDuck(now);
      for (auto &d : duDucks) {
        switch (d.s) {
          case DK_FLY: {
            d.t += dt;
            float a = d.amp * sinf(d.om * d.t + d.ph);
            d.vx = d.dir * d.speed * cosf(a);
            d.vy = d.speed * sinf(a);
            if (d.y < DU_TOP + 40) d.vy = fmaxf(d.vy, 60.0f);
            if (d.y > DU_GROUND - 30) d.vy = fminf(d.vy, -60.0f);
            d.x += d.vx * dt; d.y += d.vy * dt;
            if (!d.entered) { if (d.x > 30 && d.x < W - 30) d.entered = true; }
            else {
              if (d.x < 24) { d.x = 24; d.dir = 1; }
              if (d.x > W - 24) { d.x = W - 24; d.dir = -1; }
            }
            if (d.t > d.life) { d.s = DK_AWAY; duMark(d, 2); sfxBad(); }
            break;
          }
          case DK_HIT:
            if (now - d.hitAt > 280) { d.s = DK_FALL; d.vy = 0; }
            break;
          case DK_FALL:
            d.vy += 900.0f * dt; d.y += d.vy * dt; d.rot += d.spin * dt;
            if (d.y > DU_GROUND + 24) d.s = DK_OFF;
            break;
          case DK_AWAY:
            d.vy = -280.0f; d.y += d.vy * dt; d.x += d.dir * d.speed * 0.6f * dt;
            if (d.y < -40) d.s = DK_OFF;
            break;
          default: break;
        }
      }
      active = 0;
      for (auto &d : duDucks) if (d.s != DK_OFF) active++;
      if (duSpawned >= DU_N && active == 0) duFinishRound(now);
      break;
    }
    case DU_BANNER:
      duAim(now, dt);
      if (now - duBannerAt > 2200) { duRound++; duStartRound(1300); }
      break;
    case DU_OVER:
      if (now - duOverAt > 700) {
        if (in.right) { duStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - duLastRender >= 16) { duLastRender = now; duRender(now); }
  return true;
}

Game GAME_DUCK = {"DUCK", "tilt to aim, shoot the ducks", 0xFD20, duOpts, 2, duStart, duFrame};
