// runner_game.cpp - endless side runner (dino style). Either button jumps; HOLD it for a higher jump, tap for a hop.
// Cacti of different sizes, low drones you must run UNDER (don't jump into them), parallax mountains and clouds,
// speed climbs, and every 250 points the world flips between DAY and NIGHT (neon on AMOLED black).
#include "game.h"

enum RuSt : uint8_t { RU_COUNT, RU_PLAY, RU_OVER };

static constexpr int RU_GY = H - 64, RU_PX = 84, RU_MAXO = 6, RU_PART = 24, RU_MILE = 250;
// A tap must clear the tallest cactus even if the button's "held" state is missed: tap apex = 640^2/(2*2900) = 71 px,
// held apex = 640^2/(2*1500) = 136 px; tallest cactus 52 px (was 58 with a 49 px tap: the user could not pass it).
static constexpr float RU_JUMP_V = 640.0f, RU_G_HOLD = 1500.0f, RU_G_CUT = 2900.0f, RU_G_FALL = 2200.0f;

struct RuObs { float x, w; uint8_t type, n; uint8_t h[3]; bool on; };
struct RuPart { float x, y, vx, vy; uint32_t born; uint16_t c; };

static RuObs ruObs[RU_MAXO];
static RuPart ruParts[RU_PART];
static RuSt ruSt;
static bool ruNight;
static float ruSpeed, ruDist, ruScroll, ruJumpH, ruJumpV, ruSince, ruGap, ruPhase;
static uint32_t ruBest, ruT0, ruLastT, ruLastRender, ruJumpBuf, ruOverAt, ruRingAt, ruLabelAt, ruLastDust;
static int ruNextMile;
static bool ruOnGround, ruNewBest, ruDead;
static uint16_t *ruBg[2] = {nullptr, nullptr};

static inline uint32_t ruHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// ---------------- static sky + ground ----------------
static void ruBuildBg(int night) {
  if (!ruBg[night]) ruBg[night] = (uint16_t *)ps_malloc(FB_PIXELS * 2);
  if (!ruBg[night]) return;
  gfx->fillScreen(C_BG);
  uint16_t top = night ? rgb(2, 2, 16) : rgb(60, 140, 250), hor = night ? rgb(30, 14, 70) : rgb(205, 236, 255);
  for (int y = 32; y < RU_GY; y++) gfx->drawFastHLine(0, y, W, lerpColor(top, hor, (y - 32) / (float)(RU_GY - 32)));
  if (night) {
    for (int i = 0; i < 120; i++) {
      uint32_t h = ruHash(i, 3);
      int x = h % W, y = 36 + (h >> 9) % (RU_GY - 90), b = 80 + (h >> 20) % 170;
      gfx->drawPixel(x, y, rgb(b, b, min(255, b + 40)));
      if ((h >> 28) < 2) gfx->fillRect(x, y, 2, 2, rgb(240, 240, 255));
    }
    int mx = 356, my = 92;
    for (int r = 58; r > 22; r -= 4) gfx->fillCircle(mx, my, r, lerpColor(rgb(30, 14, 70), rgb(130, 120, 200), (58 - r) / 36.0f));
    gfx->fillCircle(mx, my, 22, rgb(250, 248, 224));
    gfx->fillCircle(mx - 6, my - 4, 5, rgb(214, 212, 194));
    gfx->fillCircle(mx + 7, my + 7, 4, rgb(220, 218, 200));
  } else {
    int sx = 356, sy = 88;
    for (int r = 64; r > 24; r -= 4) gfx->fillCircle(sx, sy, r, lerpColor(rgb(150, 200, 255), rgb(255, 248, 190), (64 - r) / 40.0f));
    gfx->fillCircle(sx, sy, 24, rgb(255, 252, 226));
  }
  for (int y = RU_GY; y < H; y++)
    gfx->drawFastHLine(0, y, W, lerpColor(night ? rgb(18, 10, 40) : rgb(224, 190, 130), night ? rgb(2, 2, 8) : rgb(150, 112, 66), (y - RU_GY) / 64.0f));
  memcpy(ruBg[night], frameBuffer(), FB_PIXELS * 2);
}

static void ruMountains(float off, int count, int spacing, int hmin, int hmax, uint16_t col, int seed, uint16_t edge, bool useEdge) {
  int period = count * spacing;
  for (int i = 0; i < count; i++) {
    int ph = hmin + (int)(ruHash(i, seed) % (uint32_t)(hmax - hmin));
    float cx = fmodf(i * spacing + spacing * 0.5f - off, (float)period);
    if (cx < 0) cx += period;
    for (int k = 0; k < 2; k++) {
      int x = (int)cx - k * period, hw = (int)(spacing * 0.85f);
      if (x < -hw || x > W + hw) continue;
      gfx->fillTriangle(x - hw, RU_GY, x + hw, RU_GY, x, RU_GY - ph, col);
      if (useEdge) {
        gfx->drawLine(x - hw, RU_GY, x, RU_GY - ph, edge);
        gfx->drawLine(x + hw, RU_GY, x, RU_GY - ph, edge);
      }
    }
  }
}

// ---------------- obstacles ----------------
static void ruSpawn() {
  for (auto &o : ruObs) {
    if (o.on) continue;
    o.on = true; o.x = W + 24;
    bool drone = ruSpeed > 330 && (esp_random() % 100) < 20;
    if (drone) { o.type = 4; o.n = 1; o.w = 36; }
    else {
      static const uint8_t HS[4] = {28, 36, 44, 52};
      o.type = 0;
      int hi = esp_random() % 4;
      o.n = (hi <= 1) ? 1 + esp_random() % 3 : 1;
      for (int i = 0; i < 3; i++) o.h[i] = HS[constrain(hi + (int)(esp_random() % 3) - 1, 0, 3)];
      if (o.n >= 2) for (int i = 0; i < 3; i++) o.h[i] = min<uint8_t>(o.h[i], 36);
      o.w = o.n * 16 + (o.n - 1) * 6;
    }
    ruSince = 0;
    ruGap = ruSpeed * 0.95f + o.w + (esp_random() % 1000) * 0.001f * ruSpeed * 0.8f;
    return;
  }
}

static void ruObsBox(const RuObs &o, float &x0, float &y0, float &x1, float &y1) {
  x0 = o.x + 3; x1 = o.x + o.w - 3;
  if (o.type == 4) { y0 = RU_GY - 70; y1 = RU_GY - 54; x0 -= 1; x1 += 1; }
  else {
    int mh = 0;
    for (int i = 0; i < o.n; i++) mh = max(mh, (int)o.h[i]);
    y0 = RU_GY - mh + 3; y1 = RU_GY;
  }
}

static void ruDrawCactus(float fx, int ph, int pw) {
  int x = (int)fx, top = RU_GY - ph;
  uint16_t col = ruNight ? rgb(255, 50, 190) : rgb(46, 150, 70), hi = ruNight ? rgb(255, 170, 240) : rgb(130, 224, 140);
  if (ruNight) {
    gfx->fillRoundRect(x - 4, top - 4, pw + 8, ph + 4, pw / 2 + 4, rgb(50, 0, 40));
    gfx->fillRoundRect(x - 2, top - 2, pw + 4, ph + 2, pw / 2 + 2, rgb(120, 0, 96));
  }
  int ay = RU_GY - (int)(ph * 0.55f), by = RU_GY - (int)(ph * 0.4f);
  if (ph >= 36) {
    if (ruNight) gfx->fillRect(x - 10, ay - 14, 11, 21, rgb(90, 0, 70));
    gfx->fillRect(x - 8, ay, 9, 5, col);
    gfx->fillRoundRect(x - 8, ay - 12, 5, 17, 2, col);
  }
  if (ph >= 46) {
    if (ruNight) gfx->fillRect(x + pw - 1, by - 12, 14, 20, rgb(90, 0, 70));
    gfx->fillRect(x + pw - 1, by, 9, 5, col);
    gfx->fillRoundRect(x + pw + 4, by - 10, 5, 15, 2, col);
  }
  gfx->fillRoundRect(x, top, pw, ph, pw / 2, col);
  gfx->fillRect(x + 3, top + 5, 2, ph - 10, hi);
  gfx->drawFastVLine(x + pw - 3, top + 6, ph - 12, lerpColor(col, C_BG, 0.4f));
}

static void ruDrawDrone(float fx, uint32_t now) {
  int cx = (int)fx + 18, cy = RU_GY - 62;
  uint16_t col = ruNight ? rgb(255, 230, 40) : rgb(220, 60, 50), light = ruNight ? rgb(255, 250, 170) : rgb(255, 160, 140);
  if (ruNight) {
    gfx->fillEllipse(cx, cy, 24, 14, rgb(50, 44, 0));
    gfx->fillEllipse(cx, cy, 20, 10, rgb(110, 98, 0));
  }
  gfx->fillEllipse(cx, cy, 17, 8, col);
  gfx->fillEllipse(cx + 5, cy - 4, 8, 5, light);
  gfx->fillCircle(cx - 9, cy + 1, 3, ruNight ? C_BG : rgb(50, 12, 12));
  gfx->fillCircle(cx - 9, cy + 1, 1, rgb(255, 60, 60));
  gfx->drawFastVLine(cx, cy - 11, 4, light);
  int rl = ((now / 40) & 1) ? 16 : 9;
  gfx->drawFastHLine(cx - rl, cy - 12, rl * 2, light);
  gfx->drawFastHLine(cx - rl + 2, cy - 11, rl * 2 - 4, lerpColor(light, C_BG, 0.5f));
}

// ---------------- player ----------------
static void ruThick(int x0, int y0, int x1, int y1, uint16_t c) {
  gfx->drawLine(x0, y0, x1, y1, c);
  gfx->drawLine(x0 + 1, y0, x1 + 1, y1, c);
  gfx->drawLine(x0 - 1, y0, x1 - 1, y1, c);
}
static void ruDrawPlayer(uint32_t now) {
  int foot = RU_GY - (int)ruJumpH;
  uint16_t suit = ruNight ? rgb(0, 240, 210) : rgb(255, 128, 40), dark = ruNight ? rgb(0, 130, 120) : rgb(176, 70, 20);
  uint16_t visor = ruNight ? rgb(10, 20, 50) : rgb(30, 34, 70), shine = ruNight ? rgb(255, 120, 230) : rgb(120, 220, 255);
  int sh = max(5, 13 - (int)(ruJumpH * 0.08f));
  gfx->fillEllipse(RU_PX, RU_GY + 3, sh, 3, ruNight ? rgb(0, 40, 36) : rgb(120, 86, 46));
  bool air = !ruOnGround;
  float a1 = air ? -0.7f : sinf(ruPhase) * 0.95f, a2 = air ? 0.55f : sinf(ruPhase + 3.14159f) * 0.95f;
  int hipY = foot - 15;
  if (ruNight) {                                         // glow behind the body
    gfx->fillRoundRect(RU_PX - 15, foot - 52, 30, 54, 10, rgb(0, 30, 28));
    gfx->fillRoundRect(RU_PX - 12, foot - 49, 24, 48, 8, rgb(0, 70, 64));
  }
  ruThick(RU_PX - 2, hipY, RU_PX - 2 + (int)(sinf(a2) * 14), hipY + (int)(cosf(a2) * 14), dark);
  ruThick(RU_PX + 1, hipY, RU_PX + 1 + (int)(sinf(a1) * 14), hipY + (int)(cosf(a1) * 14), suit);
  gfx->fillRoundRect(RU_PX - 14, foot - 36, 7, 14, 3, dark);                     // backpack
  gfx->fillRoundRect(RU_PX - 9, foot - 36, 19, 22, 6, suit);
  gfx->fillRect(RU_PX - 9, foot - 20, 19, 3, dark);
  ruThick(RU_PX, foot - 31, RU_PX + (int)(sinf(a1 + 3.14159f) * 9), foot - 31 + (int)(cosf(a1) * 9) + 4, dark);
  gfx->fillRoundRect(RU_PX - 8, foot - 50, 18, 15, 7, suit);                     // head
  gfx->fillRoundRect(RU_PX, foot - 46, 11, 8, 3, visor);
  gfx->fillRect(RU_PX + 6, foot - 45, 3, 2, shine);
  (void)now;
}

// ---------------- particles ----------------
static void ruDust(float x, float y, int n, uint32_t now, uint16_t c) {
  for (int i = 0; i < n; i++)
    for (auto &p : ruParts)
      if (!p.born || now - p.born > 500) {
        p = {x, y, -60.0f - esp_random() % 80, -20.0f - esp_random() % 80, now, c};
        break;
      }
}
static void ruBurst(float x, float y, uint32_t now) {
  for (int i = 0; i < RU_PART; i++) {
    float a = (esp_random() % 628) * 0.01f, s = 80.0f + esp_random() % 240;
    ruParts[i] = {x, y, cosf(a) * s, sinf(a) * s - 80, now, (i & 1) ? rgb(255, 220, 80) : (ruNight ? rgb(0, 255, 220) : rgb(255, 120, 40))};
  }
}

// ---------------- render ----------------
static void ruRender(uint32_t now) {
  uint16_t *bgp = ruBg[ruNight ? 1 : 0];
  if (bgp) memcpy(frameBuffer(), bgp, FB_PIXELS * 2);
  else gfx->fillScreen(C_BG);
  if (!ruNight) {                                        // clouds
    for (int i = 0; i < 4; i++) {
      uint32_t h = ruHash(i, 11);
      float cx = fmodf((float)(h % 560) - ruScroll * 0.06f, 560.0f);
      if (cx < 0) cx += 560;
      cx -= 60;
      int cy = 56 + (h >> 10) % 80;
      gfx->fillEllipse((int)cx, cy, 30, 9, rgb(244, 250, 255));
      gfx->fillEllipse((int)cx - 13, cy - 6, 15, 9, rgb(250, 253, 255));
      gfx->fillEllipse((int)cx + 12, cy - 8, 17, 10, C_TEXT);
    }
  }
  ruMountains(ruScroll * 0.10f, 6, 150, 50, 100, ruNight ? rgb(24, 12, 56) : rgb(150, 172, 226), 21, rgb(120, 40, 200), ruNight);
  ruMountains(ruScroll * 0.28f, 5, 130, 28, 62, ruNight ? rgb(38, 16, 84) : rgb(106, 142, 190), 22, rgb(0, 190, 220), ruNight);
  if (ruNight) {                                         // neon horizon line
    gfx->drawFastHLine(0, RU_GY - 1, W, rgb(0, 70, 90));
    gfx->drawFastHLine(0, RU_GY, W, rgb(0, 230, 255));
    gfx->drawFastHLine(0, RU_GY + 1, W, rgb(0, 90, 110));
  } else {
    gfx->drawFastHLine(0, RU_GY, W, rgb(110, 76, 40));
    gfx->drawFastHLine(0, RU_GY + 1, W, rgb(150, 110, 64));
  }
  for (int i = 0; i < 30; i++) {                         // ground pebbles scroll with the world
    uint32_t h = ruHash(i, 5);
    float x = fmodf((float)(h % (W + 40)) - ruScroll * (0.9f + (h % 3) * 0.1f), (float)(W + 40));
    if (x < 0) x += W + 40;
    int y = RU_GY + 8 + (h >> 8) % 48, w = 2 + (h >> 4) % 6;
    gfx->fillRect((int)x - 10, y, w, 2, ruNight ? rgb(0, 80 + (h % 60), 90 + (h % 60)) : rgb(110, 80, 46));
  }
  for (int i = 0; i < 9; i++) {                          // fast ticks on the line
    float x = fmodf(i * 56.0f - ruScroll * 1.0f, (float)(W + 56));
    if (x < 0) x += W + 56;
    gfx->drawFastHLine((int)x - 20, RU_GY + 4, 14, ruNight ? rgb(0, 140, 160) : rgb(130, 96, 56));
  }
  for (auto &o : ruObs) {
    if (!o.on) continue;
    if (o.type == 4) ruDrawDrone(o.x, now);
    else for (int i = 0; i < o.n; i++) ruDrawCactus(o.x + i * 22, o.h[i], 16);
  }
  if (!ruDead || ((now / 80) & 1)) ruDrawPlayer(now);
  for (auto &p : ruParts) {
    if (!p.born) continue;
    uint32_t age = now - p.born;
    if (age > 600) continue;
    float t = age * 0.001f;
    gfx->fillRect((int)(p.x + p.vx * t), (int)(p.y + p.vy * t + 500 * t * t), 3, 3, lerpColor(p.c, ruNight ? C_BG : rgb(190, 150, 100), age / 600.0f));
  }
  uint32_t ra = now - ruRingAt;                           // shock ring at a day/night flip
  if (ruRingAt && ra < 650) {
    int r = (int)(ra * 0.7f);
    uint16_t c = lerpColor(C_TEXT, ruNight ? rgb(80, 0, 160) : rgb(255, 200, 80), ra / 650.0f);
    for (int k = 0; k < 4; k++) gfx->drawCircle(RU_PX, RU_GY - 24, r + k, c);
  }
  if (ruLabelAt && now - ruLabelAt < 1200) centerText(ruNight ? "NIGHT RUN" : "DAYBREAK", 60, 3, ruNight ? rgb(0, 240, 255) : rgb(255, 120, 30));
  hudBar((uint32_t)ruDist, ruBest, ruNight ? C_BG : rgb(40, 28, 12), ruNight ? rgb(0, 240, 220) : rgb(255, 220, 160));
  uint16_t acc = ruNight ? rgb(0, 240, 220) : rgb(255, 140, 40);
  if (ruSt == RU_COUNT) {
    int cnt = 3 - (int)((now - ruT0) / 700);
    char d[2] = {(char)('0' + constrain(cnt, 1, 3)), 0};
    gfx->fillRoundRect(W / 2 - 44, 80, 88, 88, 18, C_PANEL);
    gfx->drawRoundRect(W / 2 - 44, 80, 88, 88, 18, acc);
    centerText(d, 100, 6, acc);
    centerText("BUTTON jumps, hold = higher", 186, 2, ruNight ? C_TEXT : C_BG);
  } else if (ruSt == RU_OVER) {
    char b[32];
    snprintf(b, sizeof b, ruNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)ruDist);
    panelBox("CRASHED", b, "RIGHT again   LEFT menu", acc);
  }
  dirty = true;
}

static void ruStart() {
  ruNight = false;
  if (!ruBg[0]) ruBuildBg(0);
  if (!ruBg[1]) ruBuildBg(1);
  memset(ruObs, 0, sizeof ruObs);
  memset(ruParts, 0, sizeof ruParts);
  ruSpeed = 250; ruDist = 0; ruScroll = 0; ruJumpH = 0; ruJumpV = 0; ruPhase = 0;
  ruSince = 0; ruGap = 420; ruNextMile = RU_MILE;
  ruOnGround = true; ruDead = false; ruNewBest = false; ruJumpBuf = 0;
  ruRingAt = ruLabelAt = 0;
  ruBest = loadBest("ruBest");
  ruLastT = millis(); ruT0 = ruLastT;
  ruSt = RU_COUNT;
  play(660, 90);
  ruRender(ruT0);
}

static bool ruFrame(uint32_t now) {
  float dt = fminf((now - ruLastT) / 1000.0f, 0.05f);
  ruLastT = now;
  bool running = ruSt != RU_OVER;
  if (running) {
    ruScroll += ruSpeed * dt;
    if (ruOnGround) ruPhase += ruSpeed * dt * 0.055f;
  }
  switch (ruSt) {
    case RU_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - ruT0) / 700);
      if (n != lastN && n >= 1) play(660, 70);
      lastN = n;
      if (now - ruT0 >= 2100) { ruSt = RU_PLAY; lastN = 3; sfxStart(); }
      break;
    }
    case RU_PLAY: {
      bool held = in.leftDown || in.rightDown;
      if (in.left || in.right) ruJumpBuf = now;
      if (ruOnGround && ruJumpBuf && now - ruJumpBuf < 120) {
        ruJumpV = RU_JUMP_V; ruOnGround = false; ruJumpBuf = 0;
        play(520, 25); play(780, 35);
        ruDust(RU_PX, RU_GY, 4, now, ruNight ? rgb(0, 140, 140) : rgb(200, 170, 120));
      }
      if (!ruOnGround) {
        float g = ruJumpV > 0 ? (held ? RU_G_HOLD : RU_G_CUT) : RU_G_FALL;
        ruJumpV -= g * dt;
        ruJumpH += ruJumpV * dt;
        if (ruJumpH <= 0) {
          ruJumpH = 0; ruJumpV = 0; ruOnGround = true;
          ruDust(RU_PX, RU_GY, 5, now, ruNight ? rgb(0, 140, 140) : rgb(200, 170, 120));
          if (ruJumpBuf && now - ruJumpBuf < 120) { ruJumpV = RU_JUMP_V; ruOnGround = false; ruJumpBuf = 0; play(520, 25); }
        }
      } else if (now - ruLastDust > 110) {
        ruLastDust = now;
        ruDust(RU_PX - 6, RU_GY, 1, now, ruNight ? rgb(0, 100, 110) : rgb(190, 160, 110));
      }
      ruSpeed = fminf(250.0f + 0.18f * ruDist, 620.0f);
      ruDist += ruSpeed * dt / 18.0f;
      if ((int)ruDist >= ruNextMile) {                    // milestone: flip day/night
        ruNextMile += RU_MILE;
        ruNight = !ruNight;
        ruRingAt = now; ruLabelAt = now;
        sfxGood();
      }
      ruSince += ruSpeed * dt;
      bool any = false;
      for (auto &o : ruObs) {
        if (!o.on) continue;
        any = true;
        o.x -= ruSpeed * dt;
        if (o.x < -90) o.on = false;
      }
      if (ruSince >= ruGap || !any) { if (ruSince >= ruGap * 0.5f || !any) ruSpawn(); }
      float px0 = RU_PX - 8, px1 = RU_PX + 9, py0 = RU_GY - ruJumpH - 44, py1 = RU_GY - ruJumpH - 2;
      for (auto &o : ruObs) {
        if (!o.on) continue;
        float x0, y0, x1, y1;
        ruObsBox(o, x0, y0, x1, y1);
        if (px1 > x0 && px0 < x1 && py1 > y0 && py0 < y1) {
          ruDead = true; ruSt = RU_OVER; ruOverAt = now;
          ruBurst(RU_PX, RU_GY - ruJumpH - 22, now);
          ruNewBest = saveBest("ruBest", (uint32_t)ruDist);
          if (ruNewBest) ruBest = (uint32_t)ruDist;
          ruNewBest ? sfxBest() : sfxBad();
          break;
        }
      }
      break;
    }
    case RU_OVER:
      if (now - ruOverAt > 700) {
        if (in.right) { ruStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (now - ruLastRender >= 16) { ruLastRender = now; ruRender(now); }
  return true;
}

Game GAME_RUNNER = {"RUNNER", "jump the cacti, run forever", 0xFD20, nullptr, 0, ruStart, ruFrame};
