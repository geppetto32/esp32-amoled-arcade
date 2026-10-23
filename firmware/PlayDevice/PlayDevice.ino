// PlayDevice - DIY handheld game console on a LilyGO T-Display-S3.
// Boot splash -> game menu -> games. In a game: START = pause, hold SELECT 1 s = menu.
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "storage.h"
#include "game.h"

static const Game* const kGames[] = {&gameSnake, &gameTetris, &gameBreakout, &gamePong, &gameInvaders};
static const int kGameCount = sizeof(kGames) / sizeof(kGames[0]);

enum State : uint8_t { ST_SPLASH, ST_MENU, ST_GAME };
static State state = ST_SPLASH;
static uint32_t stateSince;
static int sel;                 // menu selection
static const Game* cur;         // running game
static bool paused;
static uint32_t selDownAt;      // when SELECT went down (short-press detection)
static int batPercent = -1;     // -1 = unknown / USB powered
static uint32_t batAt;

// ---- battery -----------------------------------------------------------------------
static void batteryUpdate() {
  if (millis() - batAt < 2000 && batAt) return;
  batAt = millis();
  uint32_t mv = analogReadMilliVolts(PIN_BAT_ADC) * 2;   // x2 divider
  if (mv < 2500) { batPercent = -1; return; }             // no battery / USB only
  int p = ((int)mv - 3300) * 100 / (4200 - 3300);
  batPercent = p < 0 ? 0 : (p > 100 ? 100 : p);
}

// ---- screens -----------------------------------------------------------------------
static void drawSplash() {
  canvas->fillScreen(C_BG);
  uiTextC(50, "DIY PLAY", C_CYAN, 5);
  uiTextC(100, "handheld game console", C_DIM, 1);
  uint32_t t = millis() - stateSince;
  int w = (int)(t * 120 / 1500);
  if (w > 120) w = 120;
  canvas->drawRect(100, 125, 122, 6, C_DIM);
  canvas->fillRect(101, 126, w, 4, C_CYAN);
}

static void drawMenu() {
  canvas->fillScreen(C_BG);
  uiText(8, 6, "DIY PLAY", C_CYAN, 2);
  char buf[24];
  if (batPercent >= 0) snprintf(buf, sizeof buf, "BAT %d%%", batPercent);
  else snprintf(buf, sizeof buf, "USB");
  uiTextR(SCREEN_W - 6, 4, buf, batPercent >= 0 && batPercent < 20 ? C_RED : C_DIM);
  uiTextR(SCREEN_W - 6, 14, soundMuted() ? "MUTE" : "SND", C_DIM);

  for (int i = 0; i < kGameCount; i++) {
    int y = 30 + i * 24;
    if (i == sel) {
      canvas->fillRect(6, y - 2, SCREEN_W - 12, 22, C_GRID);
      canvas->drawRect(6, y - 2, SCREEN_W - 12, 22, C_CYAN);
    }
    uiText(16, y + 3, kGames[i]->name, i == sel ? C_WHITE : C_DIM, 2);
    uint32_t b = hiscoreGet(kGames[i]->key);
    if (b) {
      snprintf(buf, sizeof buf, "Best %lu", (unsigned long)b);
      uiTextR(SCREEN_W - 16, y + 7, buf, C_YELLOW);
    }
  }
  uiText(8, 158, "UP/DOWN pick   A start   SELECT sound", C_DIM);
}

static void goMenu() { state = ST_MENU; stateSince = millis(); paused = false; }

static void startGame(int i) {
  cur = kGames[i];
  cur->init();
  paused = false;
  state = ST_GAME;
  stateSince = millis();
  soundPlay(SFX_START);
}

// ---- fixed-timestep tick ---------------------------------------------------------------
static void tick() {
  inputUpdate();
  switch (state) {
    case ST_SPLASH:
      if (millis() - stateSince > 1500 || btnPressed(BTN_A) || btnPressed(BTN_START)) goMenu();
      break;
    case ST_MENU:
      batteryUpdate();
      if (btnRepeat(BTN_UP))   { sel = (sel + kGameCount - 1) % kGameCount; soundPlay(SFX_CLICK); }
      if (btnRepeat(BTN_DOWN)) { sel = (sel + 1) % kGameCount; soundPlay(SFX_CLICK); }
      if (btnPressed(BTN_SELECT)) soundToggleMute();
      if (btnPressed(BTN_A) || btnPressed(BTN_START)) startGame(sel);
      break;
    case ST_GAME:
      if (btnPressed(BTN_SELECT)) selDownAt = millis();
      if (btnHeld(BTN_SELECT) && btnHeldMs(BTN_SELECT) >= BACK_HOLD_MS) { goMenu(); break; }
      if (btnPressed(BTN_START)) { paused = !paused; soundPlay(SFX_CLICK); }
      if (paused) {
        if (btnReleased(BTN_SELECT) && millis() - selDownAt < BACK_HOLD_MS) soundToggleMute();
      } else {
        cur->update();
      }
      break;
  }
}

static void render() {
  switch (state) {
    case ST_SPLASH: drawSplash(); break;
    case ST_MENU:   drawMenu(); break;
    case ST_GAME:
      cur->draw();
      if (paused) uiOverlay("PAUSED", "START = resume   SELECT = sound", "hold SELECT 1 s = menu");
      if (btnHeld(BTN_SELECT) && btnHeldMs(BTN_SELECT) > 200) {   // back-to-menu progress bar
        int w = (int)(btnHeldMs(BTN_SELECT) * 60 / BACK_HOLD_MS);
        canvas->fillRect(SCREEN_W / 2 - 31, SCREEN_H - 8, 62, 7, C_BLACK);
        canvas->drawRect(SCREEN_W / 2 - 31, SCREEN_H - 8, 62, 7, C_WHITE);
        canvas->fillRect(SCREEN_W / 2 - 30, SCREEN_H - 7, w > 60 ? 60 : w, 5, C_RED);
      }
      break;
  }
  displayFlush();
}

void setup() {
  Serial.begin(115200);
  displayInit();            // powers the board (GPIO15) first
  storageInit();
  inputInit();
  soundInit();
  analogReadResolution(12);
  batteryUpdate();
  stateSince = millis();
  soundPlay(SFX_START);
}

void loop() {
  static uint32_t lastTick = millis();
  soundUpdate();
  uint32_t now = millis();
  int steps = 0;
  while (now - lastTick >= TICK_MS && steps < 3) {   // catch up at most 3 ticks
    lastTick += TICK_MS;
    tick();
    steps++;
  }
  if (now - lastTick >= TICK_MS) lastTick = now;     // fell too far behind: resync
  if (steps) render(); else delay(1);
}
