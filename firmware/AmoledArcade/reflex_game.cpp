// reflex_game.cpp - two-button reaction test. After a random delay one big target lights up on the LEFT or the RIGHT.
// Press the matching button as fast as you can. Pressing early or on the wrong side is a foul (trial repeats).
// 5 trials -> bar chart + average; the best average is saved. WAIT and GO screens are static (no flushing while you
// react) so the timing is not delayed by the display.
#include "game.h"

enum RfSt : uint8_t { RF_COUNT, RF_WAIT, RF_GO, RF_SHOW, RF_FOUL, RF_DONE };

static constexpr int RF_N = 5, RF_CY = 200, RF_LX = 112, RF_RX = 336, RF_R = 70;
static const uint16_t RF_COL[2] = {rgb(0, 220, 255), rgb(255, 150, 20)};

static RfSt rfSt;
static int rfTrial, rfSide, rfFouls, rfAvg;
static int rfMs[RF_N];
static uint32_t rfT0, rfStateAt, rfWaitUntil, rfGoAt, rfLastRender, rfBestAvg;
static bool rfNewBest, rfEarly, rfSlow;

static uint16_t rfSpeedCol(int ms) {
  if (ms < 200) return rgb(80, 255, 140);
  if (ms < 250) return rgb(150, 255, 60);
  if (ms < 320) return rgb(255, 220, 50);
  if (ms < 450) return rgb(255, 150, 40);
  return rgb(255, 70, 60);
}
static const char *rfGrade(int ms) {
  return ms < 200 ? "LIGHTNING" : ms < 250 ? "FAST" : ms < 300 ? "GOOD" : ms < 400 ? "OK" : "SLEEPY";
}

static void rfGlow(int cx, int cy, uint16_t col, float scale) {
  for (int k = 0; k < 8; k++) {
    int r = (int)((RF_R + 34 - k * 4) * scale);
    gfx->fillCircle(cx, cy, r, lerpColor(C_BG, col, 0.06f + 0.08f * k));
  }
  gfx->fillCircle(cx, cy, (int)(RF_R * scale * 0.9f), col);
  gfx->fillCircle(cx, cy, (int)(RF_R * scale * 0.6f), lerpColor(col, C_TEXT, 0.55f));
  gfx->fillCircle(cx, cy, (int)(RF_R * scale * 0.28f), C_TEXT);
}
static void rfIdle(int side, uint16_t tint) {            // dim target rings
  int cx = side ? RF_RX : RF_LX;
  uint16_t c = lerpColor(tint, C_BG, 0.75f);
  gfx->drawCircle(cx, RF_CY, RF_R, c);
  gfx->drawCircle(cx, RF_CY, RF_R - 1, c);
  gfx->drawCircle(cx, RF_CY, RF_R + 9, lerpColor(c, C_BG, 0.6f));
  gfx->fillCircle(cx, RF_CY, 5, lerpColor(tint, C_BG, 0.7f));
}
static void rfArrows(int lit, uint32_t now) {              // triangles pointing at the two buttons
  (void)now;
  for (int s = 0; s < 2; s++) {
    int x = s ? W - 40 : 40;
    uint16_t c = (lit == s) ? RF_COL[s] : lerpColor(RF_COL[s], C_BG, 0.72f);
    gfx->fillTriangle(x - 14, 60, x + 14, 60, x, 38, c);
  }
}

static void rfBar(const char *mid) {
  gfx->fillRect(0, 0, W, 32, rgb(8, 10, 24));
  char b[32];
  if (rfSt == RF_DONE) snprintf(b, sizeof b, "RESULTS");
  else snprintf(b, sizeof b, "TRIAL %d/%d", min(rfTrial + 1, RF_N), RF_N);
  text(b, 10, 9, 2, C_TEXT);
  if (rfBestAvg) snprintf(b, sizeof b, "BEST AVG %lu", (unsigned long)rfBestAvg);
  else snprintf(b, sizeof b, "BEST AVG --");
  text(b, 150, 9, 2, C_DIM);
  int bat = battery();
  if (bat >= 0) { snprintf(b, sizeof b, "%3d%%", bat); text(b, W - 64, 9, 2, charging() ? C_ACC : C_DIM); }
  (void)mid;
}
static void rfDots() {
  for (int i = 0; i < RF_N; i++) {
    int x = W / 2 + (i - 2) * 44, y = H - 30;
    if (i < rfTrial) gfx->fillCircle(x, y, 9, rfSpeedCol(rfMs[i]));
    else if (i == rfTrial && rfSt != RF_DONE) { gfx->drawCircle(x, y, 9, C_TEXT); gfx->drawCircle(x, y, 10, C_DIM); }
    else gfx->drawCircle(x, y, 9, 0x4208);
  }
}

static void rfDrawChart(uint32_t now) {
  const int base = 224, maxH = 140, bw = 56, gap = 24, x0 = (W - (RF_N * bw + (RF_N - 1) * gap)) / 2;
  float g = constrain((now - rfStateAt) / 700.0f, 0.0f, 1.0f);
  g = 1.0f - (1 - g) * (1 - g);
  gfx->drawFastHLine(x0 - 10, base, RF_N * (bw + gap) - gap + 20, 0x4208);
  for (int i = 0; i < RF_N; i++) {
    int ms = rfMs[i], h = (int)(constrain(ms / 600.0f, 0.08f, 1.0f) * maxH * g), x = x0 + i * (bw + gap);
    uint16_t c = rfSpeedCol(ms);
    for (int yy = 0; yy < h; yy++) gfx->drawFastHLine(x, base - 1 - yy, bw, lerpColor(c, lerpColor(c, C_BG, 0.75f), (float)yy / max(h, 1) * 0.0f + (1.0f - (float)yy / max(h, 1)) * 0.7f));
    gfx->drawRect(x, base - h, bw, h, lerpColor(c, C_TEXT, 0.3f));
    char b[8];
    snprintf(b, sizeof b, "%d", ms);
    if (g > 0.8f) text(b, x + (bw - (int)strlen(b) * 12) / 2, base - h - 20, 2, c);
    snprintf(b, sizeof b, "#%d", i + 1);
    text(b, x + 20, base + 6, 1, C_DIM);
  }
  int ah = (int)(constrain(rfAvg / 600.0f, 0.08f, 1.0f) * maxH * g);
  for (int x = x0 - 10; x < x0 + RF_N * (bw + gap) - gap + 10; x += 10) gfx->drawFastHLine(x, base - ah, 5, C_TEXT);
  text("AVG", x0 - 36 < 0 ? 0 : x0 - 36, base - ah - 4, 1, C_TEXT);
  centerText(rfGrade(rfAvg), 44, 3, rfSpeedCol(rfAvg));
  char b[32];
  snprintf(b, sizeof b, "AVG %d ms", rfAvg);
  centerText(b, 252, 4, C_TEXT);
  if (rfNewBest) { if ((now / 350) & 1) centerText("NEW BEST AVERAGE!", 296, 2, C_ACC); }
  else if (rfBestAvg) { snprintf(b, sizeof b, "best %lu ms   fouls %d", (unsigned long)rfBestAvg, rfFouls); centerText(b, 296, 2, C_DIM); }
  centerText("RIGHT again   LEFT menu", H - 22, 2, C_DIM);
}

static void rfRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  char b[32];
  switch (rfSt) {
    case RF_COUNT: {
      rfIdle(0, RF_COL[0]); rfIdle(1, RF_COL[1]);
      rfArrows(-1, now);
      float f = ((now - rfT0) % 700) / 700.0f;
      int cnt = 3 - (int)((now - rfT0) / 700);
      char d[2] = {(char)('0' + constrain(cnt, 1, 3)), 0};
      int r = 46 - (int)(20 * f);
      gfx->drawCircle(W / 2, RF_CY, r + 2, lerpColor(C_ACC, C_BG, f));
      gfx->drawCircle(W / 2, RF_CY, r, lerpColor(C_TEXT, C_BG, f * 0.8f));
      centerText(d, RF_CY - 24, 6, C_ACC);
      centerText("Press the button on the lit side", 296, 2, C_DIM);
      break;
    }
    case RF_WAIT:
      rfIdle(0, RF_COL[0]); rfIdle(1, RF_COL[1]);
      rfArrows(-1, now);
      centerText("WAIT...", 44, 3, C_DIM);
      break;
    case RF_GO:
      rfIdle(rfSide ^ 1, RF_COL[rfSide ^ 1]);
      rfGlow(rfSide ? RF_RX : RF_LX, RF_CY, RF_COL[rfSide], 1.0f);
      rfArrows(rfSide, now);
      centerText("NOW!", 44, 4, RF_COL[rfSide]);
      break;
    case RF_SHOW: {
      int ms = rfMs[rfTrial - 1];
      float f = constrain((now - rfStateAt) / 1100.0f, 0.0f, 1.0f);
      rfIdle(rfSide ^ 1, RF_COL[rfSide ^ 1]);
      rfGlow(rfSide ? RF_RX : RF_LX, RF_CY, rfSpeedCol(ms), 1.0f - 0.25f * f);
      rfArrows(-1, now);
      snprintf(b, sizeof b, "%d ms", ms);
      centerText(b, 40, 4, rfSpeedCol(ms));
      centerText(rfSlow ? "TOO SLOW" : ms < 200 ? "INCREDIBLE!" : ms < 250 ? "FAST!" : ms < 320 ? "GOOD" : ms < 450 ? "OK" : "SLOW", 78, 2, C_TEXT);
      break;
    }
    case RF_FOUL: {
      uint16_t red = rgb(255, 50, 50);
      rfIdle(0, red); rfIdle(1, red);
      for (int k = 0; k < 3; k++) gfx->drawRect(k, 33 + k, W - 2 * k, H - 33 - 2 * k, lerpColor(red, C_BG, k * 0.35f));
      centerText(rfEarly ? "TOO EARLY!" : "WRONG SIDE!", 120, 4, red);
      centerText("trial repeats", 180, 2, C_DIM);
      break;
    }
    case RF_DONE:
      rfBar(nullptr);
      rfDrawChart(now);
      dirty = true;
      return;
  }
  rfBar(nullptr);
  rfDots();
  dirty = true;
}

static void rfEnterWait(uint32_t now) {
  rfSt = RF_WAIT;
  rfStateAt = now;
  rfSide = esp_random() & 1;
  rfWaitUntil = now + 1200 + esp_random() % 2600;
  rfRender(now);
}

static void rfStart() {
  rfTrial = 0; rfFouls = 0; rfNewBest = false;
  rfBestAvg = prefs.getUInt("rfBestAvg", 0);
  rfT0 = millis(); rfStateAt = rfT0;
  rfSt = RF_COUNT;
  play(660, 90);
  rfRender(rfT0);
}

static void rfFoul(uint32_t now, bool early) {
  rfSt = RF_FOUL; rfEarly = early; rfStateAt = now; rfFouls++;
  sfxBad();
  rfRender(now);
}

static bool rfFrame(uint32_t now) {
  switch (rfSt) {
    case RF_COUNT: {
      static int lastN = 3;
      int n = 3 - (int)((now - rfT0) / 700);
      if (n != lastN && n >= 1) play(660, 60);
      lastN = n;
      if (now - rfT0 >= 2100) { lastN = 3; rfEnterWait(now); break; }
      if (now - rfLastRender >= 16) { rfLastRender = now; rfRender(now); }
      break;
    }
    case RF_WAIT:
      if ((in.left || in.right) && now - rfStateAt > 100) { rfFoul(now, true); break; }
      if ((int32_t)(now - rfWaitUntil) >= 0) {
        rfSt = RF_GO;
        rfRender(now);
        rfGoAt = millis();
        play(1568, 60);
      }
      break;
    case RF_GO: {
      bool l = in.left, r = in.right;
      if (l || r) {
        int pressed = (l && rfSide == 0) ? 0 : (r && rfSide == 1) ? 1 : (l ? 0 : 1);
        if (pressed != rfSide) { rfFoul(now, false); break; }
        int ms = (int)(now - rfGoAt);
        rfMs[rfTrial++] = max(ms, 1);
        rfSlow = false;
        rfSt = RF_SHOW; rfStateAt = now;
        ms < 250 ? sfxGood() : play(660, 80);
        rfRender(now);
      } else if (now - rfGoAt > 1500) {
        rfMs[rfTrial++] = 1500;
        rfSlow = true;
        rfSt = RF_SHOW; rfStateAt = now;
        sfxBad();
        rfRender(now);
      }
      break;
    }
    case RF_SHOW:
      if (now - rfStateAt >= 1100) {
        if (rfTrial >= RF_N) {
          int sum = 0;
          for (int i = 0; i < RF_N; i++) sum += rfMs[i];
          rfAvg = sum / RF_N;
          if (rfBestAvg == 0 || (uint32_t)rfAvg < rfBestAvg) {
            prefs.putUInt("rfBestAvg", rfAvg);
            rfBestAvg = rfAvg;
            rfNewBest = true;
          }
          rfSt = RF_DONE; rfStateAt = now;
          rfNewBest ? sfxBest() : sfxStart();
          rfRender(now);
        } else rfEnterWait(now);
      } else if (now - rfLastRender >= 16) { rfLastRender = now; rfRender(now); }
      break;
    case RF_FOUL:
      if (now - rfStateAt >= 1300) rfEnterWait(now);
      break;
    case RF_DONE:
      if (now - rfStateAt > 700) {
        if (in.right) { rfStart(); return true; }
        if (in.left) return false;
      }
      if (now - rfLastRender >= 16) { rfLastRender = now; rfRender(now); }
      break;
  }
  return true;
}

Game GAME_REFLEX = {"REFLEX", "press the side that lights up", 0x07E0, nullptr, 0, rfStart, rfFrame};
