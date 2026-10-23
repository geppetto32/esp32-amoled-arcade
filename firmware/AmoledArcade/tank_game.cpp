// tank_game.cpp - Tank: a glowing aquarium toy for the desk. No score, no game over - a gentle happiness meter only.
//   TILT      = current: fish and plankton drift toward the low side, the water surface sloshes. A sharp SHAKE scares them.
//   TAP       = tap water: drop food there; tap a fish: pet it (it wiggles, blows bubbles)
//   LEFT      = drop food in the middle        RIGHT = light: AUTO (RTC clock, or a fast fake day) / DAY / NIGHT
//   leave: triple-tap BOOT or touch-hold the top-left corner.
// Fish: neon tetras (they school), goldfish, angelfish, jellyfish. Schooling = cohesion + alignment + separation, with
// wander, wall / surface / floor avoidance, food seeking and a depth preference that moves down at night (they get
// sleepy and slow). Plants sway with the current. Bubbles rise from the plants and a little pump on the left.
// HAPPY = rises when fish eat or are petted, drains slowly (never hurts anything). FED = pellets eaten this session;
// the best session is saved. AWAKE option: ALWAYS keeps the screen from dimming while the tank runs.
#include "engine.h"
#include <math.h>

static const char *const TK_COUNT[] = {"FEW", "SOME", "MANY"};
static const char *const TK_AWAKE[] = {"AUTO DIM", "ALWAYS ON"};
static Option tkOpts[] = {{"FISH", TK_COUNT, 3, "tkFish", 1}, {"SCREEN", TK_AWAKE, 2, "tkAwake", 0}};

static constexpr int TK_MAXF = 14, TK_MAXFOOD = 16, TK_MAXB = 36, TK_NPLANK = 28, TK_NPLANT = 8, TK_WN = 57;
static constexpr float TK_SURF = 38.0f;                     // resting surface y
static constexpr int TK_FLOOR = H - 44;                     // sand top

struct TkFish { float x, y, vx, vy, wa, ph, face, size, happy; uint8_t sp; };
struct TkFood { float x, y; bool on; float age; };
struct TkBub { float x, y, vy, r, ph; bool on; };
struct TkPlant { int16_t x, h; float ph; uint8_t hue; };

static const uint8_t TK_SPECIES[TK_MAXF] = {0, 1, 0, 2, 3, 0, 0, 1, 0, 2, 3, 0, 0, 1};
static const float TK_SPEED[4] = {78, 58, 50, 28}, TK_SIZE[4] = {8, 12, 14, 15};
static const int TK_NF[3] = {6, 10, 14};

static TkFish tkF[TK_MAXF];
static TkFood tkFood[TK_MAXFOOD];
static TkBub tkB[TK_MAXB];
static TkPlant tkPl[TK_NPLANT];
static float tkPlank[TK_NPLANK][3];
static float tkWh[TK_WN], tkWv[TK_WN];
static int tkNF, tkFed, tkLight, tkAwake;
static float tkLastTf, tkHap, tkTf, tkTfy, tkPrevX, tkPrevY, tkScare, tkL, tkT, tkBubAt, tkHour;
static uint32_t tkBest, tkLastT, tkLastRender, tkStartAt, tkRtcAt;
static bool tkRtcOk;

static float tkRnd(float a, float b) { return a + (b - a) * (random(10001) / 10000.0f); }
static float tkSurfY(float x) { int i = constrain((int)(x / 8), 0, TK_WN - 1); return TK_SURF - tkTf * 46.0f * ((x - W / 2) / (W / 2.0f)) + tkWh[i]; }

static void tkBubble(float x, float y, float r = 0) {
  for (auto &b : tkB) if (!b.on) { b.on = true; b.x = x; b.y = y; b.vy = tkRnd(34, 70); b.r = r > 0 ? r : tkRnd(1.5f, 4.0f); b.ph = tkRnd(0, 6.28f); return; }
}
static void tkDropFood(float x, float y) {
  for (auto &f : tkFood) if (!f.on) { f.on = true; f.x = x + tkRnd(-6, 6); f.y = y; f.age = 0; return; }
}

static void tkStart() {
  tkNF = TK_NF[tkOpts[0].value]; tkAwake = tkOpts[1].value;
  tkBest = loadBest("tkFed");
  tkFed = 0; tkHap = 0.6f; tkLastTf = 0; tkTf = tkTfy = tkPrevX = tkPrevY = 0; tkScare = 0; tkLight = 0; tkL = 1; tkT = 0; tkBubAt = 0;
  tkRtcOk = false; tkRtcAt = 0; tkHour = 12;
  for (int i = 0; i < TK_MAXF; i++) {
    TkFish &f = tkF[i];
    f.sp = TK_SPECIES[i]; f.size = TK_SIZE[f.sp];
    f.x = tkRnd(60, W - 60); f.y = tkRnd(90, TK_FLOOR - 60); f.vx = tkRnd(-30, 30); f.vy = 0;
    f.wa = tkRnd(0, 6.28f); f.ph = tkRnd(0, 6.28f); f.face = f.vx >= 0 ? 1.0f : -1.0f; f.happy = 0;
  }
  for (auto &f : tkFood) f.on = false;
  for (auto &b : tkB) b.on = false;
  for (int i = 0; i < TK_NPLANK; i++) { tkPlank[i][0] = tkRnd(0, W); tkPlank[i][1] = tkRnd(50, TK_FLOOR); tkPlank[i][2] = tkRnd(0, 6.28f); }
  for (int i = 0; i < TK_NPLANT; i++) {
    tkPl[i].x = 30 + i * 55 + random(-12, 13); tkPl[i].h = random(55, 135); tkPl[i].ph = tkRnd(0, 6.28f); tkPl[i].hue = random(3);
  }
  for (int i = 0; i < TK_WN; i++) tkWh[i] = tkWv[i] = 0;
  imuZero();
  tkLastT = millis(); tkLastRender = 0; tkStartAt = tkLastT;
  sfxStart();
}

static float tkSmooth(float a, float b, float x) { float t = constrain((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

static void tkUpdate(float dt, uint32_t now) {
  tkT += dt;
  // ---- clock / light ----
  if (now - tkRtcAt > 2000) {
    tkRtcAt = now;
    RtcTime t;
    if (rtcRead(t) && t.ok) { tkRtcOk = true; tkHour = t.hour + t.minute / 60.0f; }
    else { tkRtcOk = false; tkHour = fmodf(7.0f + now / 15000.0f, 24.0f); }       // no clock: a 6-minute fake day
  }
  float target = tkLight == 1 ? 1.0f : tkLight == 2 ? 0.0f : tkSmooth(5.5f, 8.5f, tkHour) * (1.0f - tkSmooth(18.0f, 21.0f, tkHour));
  tkL += (target - tkL) * fminf(1.0f, dt * 1.5f);
  // ---- tilt, shake ----
  // STICK: axisX()/axisY() return the analog stick (when deflected) instead of the tilt: a stick works as the current.
  float tx = axisX(), ty = axisY();
  float jerk = (fabsf(tx - tkPrevX) + fabsf(ty - tkPrevY)) / fmaxf(dt, 0.001f);
  tkPrevX = tx; tkPrevY = ty;
  if (jerk > 7.0f && tkScare < 0.5f) {
    tkScare = 1.0f;
    for (int i = 0; i < tkNF; i++) { tkF[i].vx += tkRnd(-140, 140); tkF[i].vy += tkRnd(-90, 90); tkF[i].wa = tkRnd(0, 6.28f); }
    for (int k = 0; k < 8; k++) tkBubble(tkRnd(40, W - 40), tkRnd(120, TK_FLOOR - 20));
    play(160, 40);
  }
  tkScare = fmaxf(0.0f, tkScare - dt * 0.6f);
  tkTf += (constrain(tx, -0.6f, 0.6f) - tkTf) * fminf(1.0f, dt * 5.0f);
  tkTfy += (constrain(ty, -0.6f, 0.6f) - tkTfy) * fminf(1.0f, dt * 5.0f);
  // ---- water waves (two sub-steps) ----
  float dtf = tkTf - tkLastTf; tkLastTf = tkTf;
  for (int i = 0; i < TK_WN; i++) tkWv[i] += -dtf * 520.0f * ((i - TK_WN / 2) / (float)(TK_WN / 2));
  for (int s = 0; s < 2; s++) {
    float h = dt * 0.5f;
    for (int i = 0; i < TK_WN; i++) {
      float l = tkWh[i > 0 ? i - 1 : i], r = tkWh[i < TK_WN - 1 ? i + 1 : i];
      float acc = 360.0f * (l + r - 2 * tkWh[i]) - 2.6f * tkWv[i] - 30.0f * tkWh[i];
      tkWv[i] += acc * h;
    }
    for (int i = 0; i < TK_WN; i++) tkWh[i] = constrain(tkWh[i] + tkWv[i] * h, -14.0f, 14.0f);
  }
  // ---- food ----
  for (auto &f : tkFood) {
    if (!f.on) continue;
    f.age += dt; f.y += (26.0f + 6 * sinf(f.age * 3)) * dt; f.x += tkTf * 12 * dt;
    if (f.y > TK_FLOOR - 4 || f.age > 22) f.on = false;
  }
  // ---- fish ----
  float sleepy = 1.0f - tkL;
  for (int i = 0; i < tkNF; i++) {
    TkFish &f = tkF[i];
    f.ph += dt * (f.sp == 3 ? 2.2f : 5.0f + sqrtf(f.vx * f.vx + f.vy * f.vy) * 0.05f);
    f.happy = fmaxf(0.0f, f.happy - dt);
    float mx = TK_SPEED[f.sp] * (0.35f + 0.65f * tkL) * (1.0f + 1.4f * tkScare) * (1.0f + f.happy * 0.8f);
    float ax = 0, ay = 0;
    f.wa += tkRnd(-1.0f, 1.0f) * dt * 3.2f;
    if (f.sp == 3) {                                         // jelly: pulse up, sink slowly
      float pulse = sinf(f.ph);
      ay += pulse > 0 ? -70.0f * pulse : 14.0f;
      ax += cosf(f.wa) * 14.0f;
      mx = 34.0f;
    } else {
      float cx = 0, cy = 0, avx = 0, avy = 0, sx = 0, sy = 0; int nc = 0;
      for (int j = 0; j < tkNF; j++) {
        if (j == i || tkF[j].sp != f.sp) continue;
        float dx = tkF[j].x - f.x, dy = tkF[j].y - f.y, d2 = dx * dx + dy * dy;
        if (d2 < 120.0f * 120.0f) { cx += tkF[j].x; cy += tkF[j].y; avx += tkF[j].vx; avy += tkF[j].vy; nc++; }
        if (d2 < 28.0f * 28.0f && d2 > 0.01f) { sx -= dx; sy -= dy; }
      }
      float coh = f.sp == 0 ? 0.55f : 0.12f;
      ax += cosf(f.wa) * 60.0f * (0.3f + 0.7f * tkL); ay += sinf(f.wa) * 34.0f * (0.3f + 0.7f * tkL);
      if (nc) { ax += (cx / nc - f.x) * coh + (avx / nc - f.vx) * 0.9f; ay += (cy / nc - f.y) * coh + (avy / nc - f.vy) * 0.9f; }
      ax += sx * 2.6f; ay += sy * 2.6f;
      // depth preference: sleepy fish go low
      float want = TK_FLOOR - 40.0f - 14.0f * (f.sp & 1);
      ay += sleepy * (want - f.y) * 0.9f;
      // food
      int bi = -1; float bd = 200.0f * 200.0f;
      for (int k = 0; k < TK_MAXFOOD; k++) {
        if (!tkFood[k].on) continue;
        float dx = tkFood[k].x - f.x, dy = tkFood[k].y - f.y, d2 = dx * dx + dy * dy;
        if (d2 < bd) { bd = d2; bi = k; }
      }
      if (bi >= 0 && tkL > 0.25f) {
        float dx = tkFood[bi].x - f.x, dy = tkFood[bi].y - f.y, d = sqrtf(bd) + 0.1f;
        ax += dx / d * 190.0f; ay += dy / d * 190.0f;
        if (d < f.size * 0.7f + 5) {
          tkFood[bi].on = false; tkFed++; tkHap = fminf(1.0f, tkHap + 0.10f); f.happy = 1.0f;
          tkBubble(f.x + f.face * f.size, f.y - 2, 2);
          play(900 + random(300), 6);
          if (tkFed % 10 == 0 && (uint32_t)tkFed > tkBest) { tkBest = tkFed; saveBest("tkFed", tkBest); }
        }
      }
    }
    // current + walls
    ax += tkTf * 70.0f; ay += tkTfy * 45.0f;
    float top = tkSurfY(f.x) + (f.sp == 3 ? 24.0f : 16.0f), bot = TK_FLOOR - 14.0f - (f.sp == 3 ? 20.0f : 0.0f);
    if (f.x < 30) ax += (30 - f.x) * 7;
    if (f.x > W - 30) ax -= (f.x - (W - 30)) * 7;
    if (f.y < top) ay += (top - f.y) * 9;
    if (f.y > bot) ay -= (f.y - bot) * 9;
    f.vx += ax * dt; f.vy += ay * dt;
    float sp2 = f.vx * f.vx + f.vy * f.vy;
    if (sp2 > mx * mx) { float k = mx / sqrtf(sp2); f.vx *= k; f.vy *= k; }
    float damp = 1.0f - fminf(0.9f, 1.1f * dt);
    f.vx *= damp; f.vy *= damp;
    f.x += f.vx * dt; f.y += f.vy * dt;
    f.x = constrain(f.x, 10.0f, W - 10.0f); f.y = constrain(f.y, 20.0f, (float)TK_FLOOR - 6);
    if (fabsf(f.vx) > 6) f.face += ((f.vx > 0 ? 1.0f : -1.0f) - f.face) * fminf(1.0f, dt * 6.0f);
    if (f.happy > 0.8f && random(100) < 4) tkBubble(f.x + f.face * f.size, f.y - 3, 2.5f);
  }
  // ---- bubbles / plankton ----
  tkBubAt -= dt;
  if (tkBubAt <= 0) {
    tkBubAt = tkRnd(0.25f, 0.9f);
    if (random(3) == 0) tkBubble(26 + tkRnd(-3, 3), TK_FLOOR - 6);                       // pump
    else { int p = random(TK_NPLANT); tkBubble(tkPl[p].x + tkRnd(-4, 4), TK_FLOOR - tkPl[p].h * 0.3f); }
  }
  for (auto &b : tkB) {
    if (!b.on) continue;
    b.y -= b.vy * dt; b.ph += dt * 4; b.x += sinf(b.ph) * 14 * dt + tkTf * 30 * dt;
    if (b.y < tkSurfY(b.x) + 2) { b.on = false; if (random(8) == 0) play(1100 + random(500), 5); }
  }
  for (int i = 0; i < TK_NPLANK; i++) {
    tkPlank[i][2] += dt;
    tkPlank[i][0] += (sinf(tkPlank[i][2] * 0.7f + i) * 6 + tkTf * 40) * dt;
    tkPlank[i][1] += (cosf(tkPlank[i][2] * 0.5f + i * 2) * 4 + tkTfy * 25 + 2) * dt;
    if (tkPlank[i][0] < 0) tkPlank[i][0] += W;
    if (tkPlank[i][0] > W) tkPlank[i][0] -= W;
    if (tkPlank[i][1] < 50) tkPlank[i][1] = TK_FLOOR - 10;
    if (tkPlank[i][1] > TK_FLOOR - 4) tkPlank[i][1] = 50;
  }
  tkHap = fmaxf(0.0f, tkHap - dt * 0.003f);
}

// ---------------- drawing ----------------
static uint16_t tkGlow(uint16_t c, float k) { return lerpColor(c, C_BG, k); }

static void tkDrawFish(const TkFish &f) {
  int x = (int)f.x, y = (int)f.y;
  int d = f.face >= 0 ? 1 : -1;
  float sz = f.size, wag = sinf(f.ph) * sz * 0.28f;
  float halo = 0.9f - 0.22f * (1.0f - tkL);                  // glow stronger at night
  float bright = 0.55f + 0.45f * tkL;                        // colours dim a bit at night (but keep a glow)
  switch (f.sp) {
    case 0: {
      gfx->fillEllipse(x, y, (int)(sz * 1.9f), (int)(sz * 1.0f), tkGlow(rgb(60, 200, 255), halo));
      gfx->fillTriangle(x - d * (int)(sz * 0.7f), y, x - d * (int)(sz * 1.6f), y - (int)(sz * 0.6f + wag), x - d * (int)(sz * 1.6f), y + (int)(sz * 0.6f - wag), lerpColor(rgb(255, 80, 80), C_BG, 1 - bright));
      gfx->fillEllipse(x, y, (int)sz, (int)(sz * 0.45f), lerpColor(rgb(40, 90, 230), C_BG, 1 - bright));
      gfx->fillEllipse(x - d * (int)(sz * 0.45f), y + 1, (int)(sz * 0.5f), (int)(sz * 0.36f), lerpColor(rgb(255, 50, 60), C_BG, 1 - bright));
      gfx->drawLine(x - d * (int)(sz * 0.2f), y - 1, x + d * (int)(sz * 0.85f), y - 1, rgb(110, 240, 255));
      gfx->fillCircle(x + d * (int)(sz * 0.62f), y - 1, 1, C_TEXT);
      break;
    }
    case 1: {
      gfx->fillEllipse(x, y, (int)(sz * 1.8f), (int)(sz * 1.2f), tkGlow(rgb(255, 150, 40), halo));
      uint16_t c = lerpColor(rgb(255, 130, 20), C_BG, 1 - bright);
      gfx->fillTriangle(x - d * (int)(sz * 0.7f), y, x - d * (int)(sz * 1.7f), y - (int)(sz * 0.8f + wag), x - d * (int)(sz * 1.3f), y, c);
      gfx->fillTriangle(x - d * (int)(sz * 0.7f), y, x - d * (int)(sz * 1.7f), y + (int)(sz * 0.8f - wag), x - d * (int)(sz * 1.3f), y, c);
      gfx->fillTriangle(x - d * (int)(sz * 0.2f), y - (int)(sz * 0.5f), x + d * (int)(sz * 0.2f), y - (int)(sz * 0.5f), x - d * (int)(sz * 0.5f), y - (int)(sz * 0.95f), lerpColor(c, C_BG, 0.25f));
      gfx->fillEllipse(x, y, (int)sz, (int)(sz * 0.62f), c);
      gfx->fillEllipse(x + d * (int)(sz * 0.1f), y - (int)(sz * 0.2f), (int)(sz * 0.6f), (int)(sz * 0.22f), lerpColor(rgb(255, 210, 120), C_BG, 1 - bright));
      gfx->fillCircle(x + d * (int)(sz * 0.62f), y - 2, 2, C_TEXT);
      gfx->fillCircle(x + d * (int)(sz * 0.66f), y - 2, 1, C_BG);
      break;
    }
    case 2: {
      gfx->fillEllipse(x, y, (int)(sz * 1.4f), (int)(sz * 1.5f), tkGlow(rgb(255, 235, 140), halo));
      uint16_t c = lerpColor(rgb(250, 230, 150), C_BG, 1 - bright), dk = lerpColor(rgb(170, 120, 20), C_BG, 1 - bright);
      gfx->fillTriangle(x - d * 2, y - (int)(sz * 0.4f), x + d * (int)(sz * 0.2f), y - (int)(sz * 1.5f), x - d * (int)(sz * 0.9f), y - (int)(sz * 0.2f), c);
      gfx->fillTriangle(x - d * 2, y + (int)(sz * 0.4f), x + d * (int)(sz * 0.2f), y + (int)(sz * 1.5f), x - d * (int)(sz * 0.9f), y + (int)(sz * 0.2f), c);
      gfx->fillTriangle(x - d * (int)(sz * 0.5f), y, x - d * (int)(sz * 1.4f), y - (int)(sz * 0.5f + wag), x - d * (int)(sz * 1.4f), y + (int)(sz * 0.5f - wag), c);
      gfx->fillEllipse(x, y, (int)(sz * 0.6f), (int)(sz * 0.8f), c);
      gfx->drawFastVLine(x - d * 3, y - (int)(sz * 0.7f), (int)(sz * 1.4f), dk);
      gfx->drawFastVLine(x + d * 3, y - (int)(sz * 0.6f), (int)(sz * 1.2f), dk);
      gfx->fillCircle(x + d * (int)(sz * 0.38f), y - 2, 1, C_BG);
      break;
    }
    default: {                                               // jellyfish
      float pulse = sinf(f.ph);
      int rx = (int)(sz * (1.0f - 0.14f * pulse)), ry = (int)(sz * (0.72f + 0.12f * pulse));
      gfx->fillEllipse(x, y, rx + 10, ry + 9, tkGlow(rgb(255, 90, 220), halo + 0.03f));
      uint16_t c = lerpColor(rgb(235, 80, 200), C_BG, 0.5f - 0.35f * tkL * 0.5f);
      for (int k = -2; k <= 2; k++) {
        int sx = x + k * rx / 3, y0 = y + ry / 2;
        int ex = sx + (int)(sinf(f.ph * 1.5f + k) * 5), my = y0 + 10, ey = my + 12 + (int)(3 * pulse);
        gfx->drawLine(sx, y0, ex, my, lerpColor(c, C_BG, 0.2f));
        gfx->drawLine(ex, my, sx + (int)(sinf(f.ph * 1.5f + k + 1.5f) * 6), ey, lerpColor(c, C_BG, 0.45f));
      }
      gfx->fillEllipse(x, y, rx, ry, c);
      gfx->fillEllipse(x, y - 2, rx * 6 / 10, ry * 5 / 9, lerpColor(rgb(255, 170, 245), C_BG, 0.2f));
      break;
    }
  }
}

static void tkDrawPlant(int i, bool front) {
  const TkPlant &p = tkPl[i];
  if (((i & 1) != 0) != front) return;
  static const uint16_t cols[3] = {rgb(40, 210, 110), rgb(90, 220, 60), rgb(40, 190, 170)};
  float px = p.x, py = TK_FLOOR + 4, ang = -1.5708f;
  int segs = 7; float sl = p.h / (float)segs;
  float sway = sinf(tkT * 1.1f + p.ph) * 0.16f + tkTf * 0.5f;
  for (int k = 0; k < segs; k++) {
    ang = -1.5708f + sway * (k + 1) / segs * 1.3f + sinf(tkT * 1.9f + p.ph + k * 0.7f) * 0.05f;
    float nx = px + cosf(ang) * sl, ny = py + sinf(ang) * sl;
    uint16_t c = lerpColor(cols[p.hue], C_BG, (0.45f - 0.3f * tkL) * (1.0f - k / (float)segs) + 0.12f);
    uint16_t tipc = lerpColor(c, rgb(190, 255, 200), 0.25f * k / segs);
    gfx->drawLine((int)px, (int)py, (int)nx, (int)ny, tipc);
    gfx->drawLine((int)px + 1, (int)py, (int)nx + 1, (int)ny, tipc);
    if (k & 1) gfx->drawLine((int)nx, (int)ny, (int)(nx + (k & 2 ? 9 : -9)), (int)(ny - 5), lerpColor(tipc, C_BG, 0.3f));
    px = nx; py = ny;
  }
  gfx->fillCircle((int)px, (int)py, 2, lerpColor(rgb(255, 120, 200), C_BG, 0.3f * (1 - tkL)));     // glowing tip bud
}

static void tkRender(uint32_t now) {
  // water gradient (colours follow the light level)
  uint16_t topC = lerpColor(rgb(2, 8, 30), rgb(14, 96, 160), tkL), botC = lerpColor(rgb(0, 2, 10), rgb(4, 34, 80), tkL);
  for (int y = 0; y < H; y++) gfx->drawFastHLine(0, y, W, lerpColor(topC, botC, y / (float)H));
  // light shafts (day only)
  if (tkL > 0.15f)
    for (int k = 0; k < 4; k++) {
      int x0 = 70 + k * 100 + (int)(sinf(tkT * 0.3f + k) * 14);
      gfx->drawLine(x0, (int)TK_SURF, x0 - 70, TK_FLOOR, lerpColor(topC, rgb(120, 200, 255), 0.07f * tkL));
      gfx->drawLine(x0 + 1, (int)TK_SURF, x0 - 69, TK_FLOOR, lerpColor(topC, rgb(120, 200, 255), 0.05f * tkL));
    }
  // plankton
  for (int i = 0; i < TK_NPLANK; i++) {
    int x = (int)tkPlank[i][0], y = (int)tkPlank[i][1];
    uint16_t c = lerpColor(rgb(120, 200, 190), C_BG, 0.55f + 0.3f * tkL);
    gfx->drawPixel(x, y, c);
    if (tkL < 0.5f && (i & 1)) gfx->drawPixel(x + 1, y, lerpColor(c, C_BG, 0.5f));
  }
  for (int i = 0; i < TK_NPLANT; i++) tkDrawPlant(i, false);
  // fish: far to near by size so big ones overlap small ones
  for (int pass = 0; pass < 4; pass++)
    for (int i = 0; i < tkNF; i++) if (tkF[i].sp == pass) tkDrawFish(tkF[i]);
  for (int i = 0; i < TK_NPLANT; i++) tkDrawPlant(i, true);
  // food
  for (auto &f : tkFood) if (f.on) { gfx->fillCircle((int)f.x, (int)f.y, 2, rgb(255, 190, 80)); gfx->drawPixel((int)f.x + 1, (int)f.y - 1, C_TEXT); }
  // bubbles
  for (auto &b : tkB) if (b.on) {
    int r = (int)(b.r + 0.5f);
    gfx->drawCircle((int)b.x, (int)b.y, r, lerpColor(rgb(150, 220, 255), C_BG, 0.3f));
    gfx->drawPixel((int)b.x - r / 2, (int)b.y - r / 2, C_TEXT);
  }
  // sand + pebbles
  uint16_t sandC = lerpColor(rgb(30, 22, 12), rgb(170, 140, 80), 0.25f + 0.75f * tkL);
  for (int x = 0; x < W; x += 4) {
    int h = (int)(4.0f * sinf(x * 0.03f) + 3.0f * sinf(x * 0.11f + 1.0f));
    gfx->fillRect(x, TK_FLOOR + h, 4, H - TK_FLOOR - h, sandC);
  }
  for (int i = 0; i < 12; i++) {
    int x = (i * 97 + 31) % W, y = TK_FLOOR + 14 + (i * 53) % 24;
    gfx->fillCircle(x, y, 3 + i % 3, lerpColor(rgb(80, 70, 60), C_BG, 0.6f - 0.3f * tkL));
  }
  // above the surface: black, surface line
  int prevY = 0;
  for (int i = 0; i < TK_WN; i++) {
    int x = i * 8, sy = (int)tkSurfY(x + 4);
    if (sy > 0) gfx->fillRect(x, 0, 8, sy, C_BG);
    if (i > 0) gfx->drawLine(x, prevY, x + 8, sy, lerpColor(rgb(150, 220, 255), C_BG, 0.35f - 0.2f * tkL));
    prevY = sy;
  }
  // HUD on the black strip
  char b[24];
  text("HAPPY", 8, 6, 1, C_DIM);
  gfx->drawRoundRect(8, 17, 84, 8, 4, lerpColor(C_DIM, C_BG, 0.3f));
  int w = (int)(80 * tkHap);
  if (w > 1) gfx->fillRoundRect(10, 19, w, 4, 2, lerpColor(rgb(255, 120, 150), C_GOOD, tkHap));
  if (tkRtcOk) snprintf(b, sizeof b, "%02d:%02d %s", (int)tkHour, (int)((tkHour - (int)tkHour) * 60), tkL > 0.5f ? "DAY" : "NIGHT");
  else snprintf(b, sizeof b, "%s", tkL > 0.5f ? "DAY" : "NIGHT");
  text(b, 130, 6, 1, lerpColor(C_TEXT, C_BG, 0.2f));
  if (tkLight) text(" (set)", 130 + (int)strlen(b) * 6, 6, 1, C_DIM);
  snprintf(b, sizeof b, "FED %d", tkFed);
  text(b, W - 8 - (int)strlen(b) * 12, 8, 2, C_ACC);
  snprintf(b, sizeof b, "BEST %lu", (unsigned long)tkBest);
  text(b, 130, 17, 1, C_DIM);
  // hint line for the first seconds
  uint32_t el = now - tkStartAt;
  if (el < 9000) {
    float f = el < 6500 ? 0.0f : (el - 6500) / 2500.0f;
    centerText("TILT = current  TAP = feed / pet  LEFT feed  RIGHT light", H - 12, 1, lerpColor(C_TEXT, C_BG, f));
  }
  dirty = true;
}

static bool tkFrame(uint32_t now) {
  if (tkAwake) keepAwake = true;
  float dt = fminf((now - tkLastT) / 1000.0f, 0.05f);
  tkLastT = now;
  tuneUpdate(now);
  if (in.left) { tkDropFood(W / 2 + tkRnd(-30, 30), tkSurfY(W / 2) + 4); tkDropFood(W / 2 + tkRnd(-30, 30), tkSurfY(W / 2) + 10); play(700, 8); }
  if (in.right) { tkLight = (tkLight + 1) % 3; play(tkLight == 2 ? 330 : 660, 30); }
  if (in.tap) {
    int best = -1; float bd = 34.0f * 34.0f;
    for (int i = 0; i < tkNF; i++) {
      float dx = tkF[i].x - in.tapX, dy = tkF[i].y - in.tapY, d2 = dx * dx + dy * dy;
      if (d2 < bd) { bd = d2; best = i; }
    }
    if (best >= 0) {                                         // pet
      tkF[best].happy = 1.4f; tkHap = fminf(1.0f, tkHap + 0.04f);
      for (int k = 0; k < 3; k++) tkBubble(tkF[best].x + tkRnd(-6, 6), tkF[best].y - 4, 2.5f);
      play(1000, 10); play(1300, 12);
    } else if (in.tapY > tkSurfY(in.tapX) - 4) {
      for (int k = 0; k < 3; k++) tkDropFood(in.tapX, tkSurfY(in.tapX) + 4 + k * 5);
      play(760, 8);
    }
  }
  tkUpdate(dt, now);
  if (now - tkLastRender >= 28) { tkLastRender = now; tkRender(now); }
  return true;
}

Game GAME_TANK = {"TANK", "aquarium toy: feed, tilt, relax", 0x07FF, tkOpts, 2, tkStart, tkFrame};
