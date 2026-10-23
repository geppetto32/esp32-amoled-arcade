// AmoledArcade - a small games console on the Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1).
//
// Hold the board LANDSCAPE with the two buttons on TOP.
//   Launcher:  LEFT = next, RIGHT = open / choose, or just TAP a card / option on the screen
//   In games:  see each game's hint line; HOME = tap BOOT 3 times quickly (or hold the top-left screen corner 1.5 s)
// First start asks you to press the LEFT button (redo it in SETTINGS).
// Board: "Waveshare ESP32-S3-Touch-AMOLED-1.8", PSRAM enabled, USB CDC On Boot disabled.
// Flash with flash.cmd (two-step upload).

#include "hw.h"
#include "game.h"
#include "app_common.h"

// ---- game list: add new games here ----
Game *GAMES[] = {&GAME_SNAKE, &GAME_FLAP, &GAME_BREAKOUT, &GAME_ASTRO, &GAME_STACKER, &GAME_DUCK, &GAME_MAZE, &GAME_RUNNER, &GAME_REFLEX, &GAME_PACMAN, &GAME_TETRIS, &GAME_MAHJONG, &GAME_CUT, &GAME_MISSILE, &GAME_LANDER, &GAME_2048, &GAME_HOURGLASS,
                 &GAME_PINBALL, &GAME_FOURROW, &GAME_SOKOBAN, &GAME_MURMUR, &GAME_GLOWBEAT,
                 &GAME_ECHO, &GAME_LIGHTSOUT, &GAME_DUOPONG, &GAME_TANK, &GAME_WOBBLE,
                 &GAME_HOP, &GAME_SWARM, &GAME_RINGBREAKER, &GAME_GOLF, &GAME_MINEFIELD};
constexpr int NGAMES = sizeof(GAMES) / sizeof(GAMES[0]);

// ---- app list: add new apps here (same Game struct, shown under the APPS card) ----
Game *APPS[] = {&APP_LEVEL, &APP_CLOCK, &APP_METRO, &APP_WOOD, &APP_DICE, &APP_COUNTER, &APP_TORCH, &APP_SYSTEM};
constexpr int NAPPS = sizeof(APPS) / sizeof(APPS[0]);

enum Screen : uint8_t { S_SETUP, S_LIST, S_APPLIST, S_OPTIONS, S_SETTINGS, S_TILT, S_SDINFO, S_JOY, S_PLAY };
static Screen scr = S_LIST;
static int sel = 0;          // selection on the current screen
static int gameIdx = 0;      // chosen game (or app, see curIsApp)
static Game *cur = nullptr;  // the game / app being configured or played
static bool curIsApp = false;

// Main list rows: the games, then APPS, then SETTINGS. The APPS list: the apps, then BACK.
constexpr int ROW_APPS = NGAMES, ROW_SETTINGS = NGAMES + 1;

// idle-dim hooks for apps (see game.h)
bool keepAwake = false;
// Games / apps where quick repeated BOOT taps are normal play set this every frame: the triple-tap HOME gesture is ignored then.
bool homeLock = false;
static bool wakeReq = false;
void wakeScreen() { wakeReq = true; }

// =====================================================================================
// Helpers shared with games
// =====================================================================================
void hudBar(uint32_t score, uint32_t best, uint16_t bg, uint16_t fg) {
  gfx->fillRect(0, 0, W, 32, bg);
  char b[24];
  snprintf(b, sizeof b, "%lu", (unsigned long)score);
  text(b, 10, 9, 2, fg);
  snprintf(b, sizeof b, "BEST %lu", (unsigned long)best);
  text(b, 130, 9, 2, lerpColor(fg, bg, 0.45f));
  int bat = battery();
  if (bat >= 0) {
    snprintf(b, sizeof b, "%3d%%", bat);
    text(b, W - 64, 9, 2, charging() ? C_ACC : lerpColor(fg, bg, 0.45f));
  }
}
void panelBox(const char *title, const char *line2, const char *line3, uint16_t accent) {
  gfx->fillRoundRect(54, 104, W - 108, 160, 16, C_PANEL);
  gfx->drawRoundRect(54, 104, W - 108, 160, 16, accent);
  centerText(title, 124, 4, C_TEXT);
  if (line2) centerText(line2, 178, 3, accent);
  if (line3) centerText(line3, 226, 2, C_DIM);
}
uint32_t loadBest(const char *key) { return prefs.getUInt(key, 0); }
bool saveBest(const char *key, uint32_t score) {
  if (score <= prefs.getUInt(key, 0)) return false;
  prefs.putUInt(key, score);
  return true;
}

// =====================================================================================
// Launcher screens
// =====================================================================================
static void drawSetup() {
  gfx->fillScreen(C_BG);
  centerText("BUTTON SETUP", 70, 4, C_GOOD);
  centerText("Hold the board with the", 150, 2, C_TEXT);
  centerText("two buttons on TOP.", 176, 2, C_TEXT);
  centerText("Press the LEFT button.", 230, 3, C_ACC);
  gfx->fillTriangle(40, 22, 70, 22, 55, 2, C_ACC);
  dirty = true;
}

static int cardCount() { return scr == S_APPLIST ? NAPPS + 1 : NGAMES + 2; }
static void cardInfo(int i, const char *&name, const char *&blurb, uint16_t &acc) {
  if (scr == S_APPLIST) {
    if (i < NAPPS) { name = APPS[i]->name; blurb = APPS[i]->blurb; acc = APPS[i]->accent; }
    else { name = "BACK"; blurb = "to the main list"; acc = C_DIM; }
  } else if (i < NGAMES) { name = GAMES[i]->name; blurb = GAMES[i]->blurb; acc = GAMES[i]->accent; }
  else if (i == ROW_APPS) { name = "APPS"; blurb = "wood calc, level, dice, tools"; acc = rgb(255, 120, 200); }
  else { name = "SETTINGS"; blurb = "sound, buttons, tilt check"; acc = C_DIM; }
}

// ---- settings rows ----
enum SettingsRow : uint8_t { R_SOUND, R_VOLUME, R_BRIGHT, R_DIM, R_SD, R_PAD, R_SETUP, R_TILT, R_BACK, R_COUNT };
constexpr int SET_Y0 = 64, SET_STEP = 32;            // text size 2 rows; the list scrolls when it does not fit
static const uint32_t DIM_MS[4] = {30000, 60000, 180000, 0};   // 0 = never
static const char *const DIM_LABEL[4] = {"30 S", "60 S", "3 MIN", "OFF"};
static uint8_t dimIdx = 1;
static const int DIM_PCT = 10;
static SdStatus sdSt;                                  // refreshed when the settings screen is entered

// =====================================================================================
// Scrolling lists (main list, APPS list, SETTINGS): drag with the finger, fling with momentum, rubber-band at the ends.
// Content coordinates: row i starts at scRowTop(i) below the viewport top. scOff = how far the content moved up (px).
// =====================================================================================
constexpr int LIST_VT = 62, LIST_VB = H - 30, LIST_PITCH = 54, LIST_CARD = 48;   // card list viewport
constexpr int SET_VT = 56, SET_VB = H - 28;                                      // settings viewport
constexpr float SC_FRICTION = 3.2f;       // 1/s: fling speed decays by e^-3.2 per second
constexpr float SC_SPRING = 220.0f;       // rubber-band pull-back stiffness (1/s^2)
constexpr float SC_DAMP = 26.0f;          // rubber-band damping (1/s)
constexpr float SC_RESIST = 30.0f;        // px of overshoot at which a drag only moves half as far
constexpr float SC_MAXFLING = 2600.0f;    // px/s
static float scOff = 0, scVel = 0, scTgt = 0;
static bool scTgtOn = false, scBlockTap = false, scPending = false;
static uint32_t scLastT = 0, scDrawT = 0;

static bool scIsSet() { return scr == S_SETTINGS; }
static int scRowN() { return scIsSet() ? (int)R_COUNT : cardCount(); }
static int scPitch() { return scIsSet() ? SET_STEP : LIST_PITCH; }
static int scVT() { return scIsSet() ? SET_VT : LIST_VT; }
static int scVB() { return scIsSet() ? SET_VB : LIST_VB; }
static int scViewH() { return scVB() - scVT(); }
static int scRowTop(int i) { return (scIsSet() ? 2 : 8) + i * scPitch(); }
static int scRowH() { return scIsSet() ? 28 : LIST_CARD; }
static float scTotal() { return (float)(scRowTop(scRowN()) + (scIsSet() ? 2 : 0)); }
static float scMax() { float m = scTotal() - scViewH(); return m > 0 ? m : 0; }
static bool scMoving() { return fabsf(scVel) > 1.0f || scTgtOn || in.dragging; }

static void scEnsure(int i, bool animate) {            // bring row i fully into view
  float top = (float)scRowTop(i), bot = top + scRowH(), vh = (float)scViewH(), t = scOff;
  if (top - 6 < t) t = top - 6;
  else if (bot + 6 > t + vh) t = bot + 6 - vh;
  t = constrain(t, 0.0f, scMax());
  scVel = 0;
  if (animate) { scTgt = t; scTgtOn = fabsf(t - scOff) > 0.5f; }
  else { scOff = t; scTgtOn = false; }
}

// Advance the scroll physics one frame. Returns true when the picture changed.
static bool scTick(uint32_t now) {
  static bool wasTouch = false;
  float dt = (now - scLastT) * 0.001f;
  scLastT = now;
  if (dt > 0.05f) dt = 0.05f;
  if (dt < 0.001f) dt = 0.001f;
  float mx = scMax(), before = scOff;
  if (in.touching) {
    if (!wasTouch) { scBlockTap = fabsf(scVel) > 40.0f || scTgtOn; scVel = 0; scTgtOn = false; }   // finger stops a fling
    if (in.dragging && in.dragDY != 0) {
      float d = -(float)in.dragDY;                      // content follows the finger
      float over = scOff < 0 ? -scOff : (scOff > mx ? scOff - mx : 0);
      if ((scOff < 0 && d < 0) || (scOff > mx && d > 0)) d /= 1.0f + over / SC_RESIST;
      scOff += d;
      scTgtOn = false;
    }
  } else {
    if (in.dragEnd) { scVel = constrain(-in.flingVY, -SC_MAXFLING, SC_MAXFLING); scTgtOn = false; }
    if (scTgtOn) {
      scOff += (scTgt - scOff) * fminf(1.0f, 14.0f * dt);
      if (fabsf(scTgt - scOff) < 0.4f) { scOff = scTgt; scTgtOn = false; }
    } else {
      float over = scOff < 0 ? scOff : (scOff > mx ? scOff - mx : 0);
      if (over != 0) scVel += (-SC_SPRING * over - SC_DAMP * scVel) * dt;   // rubber-band back
      else scVel *= expf(-SC_FRICTION * dt);                                 // fling friction
      scOff += scVel * dt;
      float over2 = scOff < 0 ? scOff : (scOff > mx ? scOff - mx : 0);
      if (fabsf(scVel) < 12.0f && fabsf(over2) < 0.4f) { scVel = 0; if (over2 != 0) scOff = constrain(scOff, 0.0f, mx); }
    }
  }
  wasTouch = in.touching;
  return fabsf(scOff - before) > 0.05f;
}

static void bandFill(int y0, int y1) {                  // background + faint scanlines (covers rows scrolled under it)
  gfx->fillRect(0, y0, W, y1 - y0, C_BG);
  for (int y = (y0 + 3) & ~3; y < y1; y += 4) gfx->drawFastHLine(0, y, W, 0x0841);
}
static void drawScrollbar() {
  float mx = scMax();
  if (mx <= 0) return;
  int vt = scVT(), th = scViewH() - 8;
  int bh = max(18, (int)(th * scViewH() / scTotal()));
  float f = constrain(scOff / mx, 0.0f, 1.0f);
  gfx->fillRoundRect(W - 14, vt + 4, 3, th, 1, 0x1082);
  gfx->fillRoundRect(W - 14, vt + 4 + (int)((th - bh) * f), 3, bh, 1, scMoving() ? C_DIM : 0x4A69);
}

static void drawList() {                                // main list or APPS list, depending on scr
  gfx->fillScreen(C_BG);
  for (int y = 0; y < H; y += 4) gfx->drawFastHLine(0, y, W, 0x0841);   // faint scanlines
  int n = cardCount(), off = (int)lroundf(scOff);
  for (int i = 0; i < n; i++) {
    int y = LIST_VT + scRowTop(i) - off;
    if (y + LIST_CARD < LIST_VT - 2 || y >= LIST_VB + 2) continue;
    bool on = i == sel;
    const char *name, *blurb;
    uint16_t acc;
    cardInfo(i, name, blurb, acc);
    gfx->fillRoundRect(30, y, W - 60, LIST_CARD, 10, on ? C_PANEL : C_BG);
    gfx->drawRoundRect(30, y, W - 60, LIST_CARD, 10, on ? acc : 0x2104);
    gfx->fillRoundRect(40, y + 12, 8, LIST_CARD - 24, 3, acc);
    text(name, 60, y + 8, 3, on ? C_TEXT : C_DIM);
    text(blurb, 60, y + 34, 1, on ? acc : 0x6B4D);
  }
  bandFill(0, LIST_VT - 2);                             // header and footer cover the cards sliding under them
  centerText(scr == S_APPLIST ? "APPS" : "ESP ARCADE", 18, 4, scr == S_APPLIST ? rgb(255, 120, 200) : C_CYAN);
  bandFill(LIST_VB, H);
  centerText(scr == S_APPLIST ? "LEFT next  RIGHT open  tap BOOT 3x = back" : "LEFT next   RIGHT open", H - 26,
             scr == S_APPLIST ? 1 : 2, C_DIM);
  drawScrollbar();
  dirty = true;
}

static void drawOptions() {
  Game *g = cur;
  gfx->fillScreen(C_BG);
  centerText(g->name, 24, 4, g->accent);
  int rows = g->nopts + 2;                              // options + START + BACK
  for (int i = 0; i < rows; i++) {
    int y = 90 + i * 44;
    char b[48];
    if (i < g->nopts) snprintf(b, sizeof b, "%s: %s", g->opts[i].label, g->opts[i].values[g->opts[i].value]);
    else snprintf(b, sizeof b, "%s", i == g->nopts ? "START" : "BACK");
    if (i == sel) gfx->fillRoundRect(50, y - 8, W - 100, 36, 10, C_PANEL);
    centerText(b, y, 3, i == sel ? (i == g->nopts ? C_GOOD : C_TEXT) : C_DIM);
  }
  centerText("LEFT next   RIGHT change / go", H - 26, 2, C_DIM);
  dirty = true;
}

static void drawSettings() {
  gfx->fillScreen(C_BG);
  char b[R_COUNT][28];
  snprintf(b[R_SOUND], 28, "SOUND: %s", soundOn ? "ON" : "OFF");
  snprintf(b[R_VOLUME], 28, "VOLUME: %d%%", volumePct);
  snprintf(b[R_BRIGHT], 28, "BRIGHTNESS: %d%%", brightPct);
  snprintf(b[R_DIM], 28, "DIM AFTER: %s", DIM_LABEL[dimIdx]);
  if (!sdSt.present) snprintf(b[R_SD], 28, "SD CARD: NO CARD");
  else if (sdSt.totalMB >= 1024) snprintf(b[R_SD], 28, "SD CARD: %.1f GB", sdSt.totalMB / 1024.0f);
  else snprintf(b[R_SD], 28, "SD CARD: %lu MB", (unsigned long)sdSt.totalMB);
  snprintf(b[R_PAD], 28, "JOYSTICK: %s", joyKind());
  snprintf(b[R_SETUP], 28, "BUTTON SETUP");
  snprintf(b[R_TILT], 28, "TILT CHECK");
  snprintf(b[R_BACK], 28, "BACK");
  int off = (int)lroundf(scOff);
  for (int i = 0; i < R_COUNT; i++) {
    int top = SET_VT + scRowTop(i) - off;
    if (top + 28 < SET_VT - 2 || top >= SET_VB + 2) continue;
    if (i == sel) gfx->fillRoundRect(60, top, W - 120, 28, 8, C_PANEL);
    bool dimRow = (i == R_SD && !sdSt.present) || (i == R_PAD && !gamepadConnected() && !stickPresent());
    centerText(b[i], top + 6, 2, i == sel ? C_TEXT : (dimRow ? 0x6B4D : C_DIM));
  }
  bandFill(0, SET_VT - 2);
  centerText("SETTINGS", 16, 4, C_CYAN);
  bandFill(SET_VB, H);
  centerText("LEFT next   RIGHT choose", H - 22, 2, C_DIM);
  drawScrollbar();
  dirty = true;
}
static void redrawCur() { if (scr == S_SETTINGS) drawSettings(); else drawList(); scDrawT = millis(); scPending = false; }

// Enter a scrolling screen with row `row` selected and visible (no animation).
static void listEnter(int s, int row) {
  scr = (Screen)s; sel = row;
  scOff = 0; scVel = 0; scTgtOn = false; scBlockTap = false;
  scLastT = millis();
  scEnsure(sel, false);
  redrawCur();
}
static void selMove(int step) {                         // move the selection (wraps) and scroll it into view
  int n = scRowN();
  sel = ((sel + step) % n + n) % n;
  sfxClick();
  scEnsure(sel, true);
  redrawCur();
}

static void drawSdInfo() {
  gfx->fillScreen(C_BG);
  centerText("SD CARD", 24, 4, C_CYAN);
  if (!sdSt.present) {
    centerText("NO CARD", 110, 4, C_BAD);
    centerText("Insert a FAT32 micro-SD", 180, 2, C_TEXT);
    centerText("and open this screen again.", 206, 2, C_TEXT);
  } else {
    char b[40];
    snprintf(b, sizeof b, "TYPE: %s", sdSt.type);
    centerText(b, 90, 3, C_TEXT);
    snprintf(b, sizeof b, "SIZE: %lu MB", (unsigned long)sdSt.totalMB);
    centerText(b, 130, 3, C_TEXT);
    snprintf(b, sizeof b, "USED: %lu MB", (unsigned long)sdSt.usedMB);
    centerText(b, 170, 3, C_DIM);
    snprintf(b, sizeof b, "FREE: %lu MB", (unsigned long)sdSt.freeMB);
    centerText(b, 210, 3, C_GOOD);
    snprintf(b, sizeof b, "FILES IN ROOT: %d", sdSt.files);
    centerText(b, 260, 2, C_TEXT);
    centerText("folder /arcade ready", 290, 2, C_DIM);
  }
  centerText("any button or tap = back", H - 22, 2, C_DIM);
  dirty = true;
}

static int settingsHit(int ty) {                    // row index, -1 = none
  if (ty < SET_VT || ty >= SET_VB) return -1;
  int off = (int)lroundf(scOff);
  for (int i = 0; i < R_COUNT; i++) {
    int top = SET_VT + scRowTop(i) - off;
    if (ty >= top && ty < top + 30) return i;
  }
  return -1;
}

static void drawTiltCheck() {        // live dot: shows how the game reads your tilt
  gfx->fillScreen(C_BG);
  centerText("TILT CHECK", 16, 3, C_CYAN);
  if (!imuOk) { centerText("IMU not found", 170, 3, C_BAD); dirty = true; return; }
  int cx = W / 2, cy = 200, R = 120;
  gfx->drawCircle(cx, cy, R, 0x2945);
  gfx->drawCircle(cx, cy, R / 4, 0x2945);
  gfx->drawFastHLine(cx - R, cy, 2 * R, 0x2104);
  gfx->drawFastVLine(cx, cy - R, 2 * R, 0x2104);
  text("RIGHT", cx + R - 54, cy - 18, 2, C_DIM);
  text("UP", cx - 10, cy - R + 6, 2, C_DIM);
  int x = cx + (int)constrain(tiltX() * 200, -R, R), y = cy + (int)constrain(tiltY() * 200, -R, R);
  gfx->fillCircle(x, y, 10, C_ACC);
  int dx, dy;
  if (tiltDir(dx, dy)) {
    const char *d = dx > 0 ? "RIGHT" : dx < 0 ? "LEFT" : dy > 0 ? "DOWN" : "UP";
    centerText(d, 52, 2, C_GOOD);
  }
  centerText("dot must move where you tilt   RIGHT = re-zero   LEFT = back", H - 14, 1, C_DIM);
  dirty = true;
}

static const char *joyKind() {
  bool a = stickPresent(), p = gamepadConnected();
  return a && p ? "ANALOG + I2C PAD" : a ? "ANALOG" : p ? "I2C PAD" : "NONE";
}
static void drawJoyTest() {          // live view of the analog stick / I2C gamepad (same Input fields the games use)
  gfx->fillScreen(C_BG);
  centerText("JOYSTICK TEST", 12, 3, C_CYAN);
  centerText(joyKind(), 42, 2, stickPresent() || gamepadConnected() ? C_GOOD : C_BAD);
  if (!stickPresent() && !gamepadConnected()) {
    centerText("No stick found at boot.", 120, 2, C_TEXT);
    centerText("Analog: X=GPIO17 Y=GPIO18 push=GPIO38", 160, 1, C_DIM);
    centerText("I2C pad: seesaw at 0x50 (SDA/SCL pads)", 180, 1, C_DIM);
    centerText("Connect, then restart the board.", 210, 2, C_DIM);
    centerText("LEFT / tap / B = back", H - 16, 1, C_DIM);
    dirty = true;
    return;
  }
  int cx = 120, cy = 190, R = 100;
  gfx->drawCircle(cx, cy, R, 0x2945);
  gfx->drawCircle(cx, cy, (int)(R * 0.08f * 3), 0x2945);
  gfx->drawFastHLine(cx - R, cy, 2 * R, 0x2104);
  gfx->drawFastVLine(cx, cy - R, 2 * R, 0x2104);
  gfx->fillCircle(cx + (int)(in.ax * R), cy + (int)(in.ay * R), 9, in.aDown ? C_GOOD : C_ACC);
  char b[40];
  snprintf(b, sizeof b, "AX %+.2f", in.ax);
  text(b, 250, 80, 2, C_TEXT);
  snprintf(b, sizeof b, "AY %+.2f", in.ay);
  text(b, 250, 104, 2, C_TEXT);
  int rx, ry;
  stickRaw(rx, ry);
  if (stickPresent()) { snprintf(b, sizeof b, "RAW %d %d", rx, ry); text(b, 250, 132, 1, C_DIM); }
  const char *dir = in.leftDown ? "LEFT" : in.rightDown ? "RIGHT" : "";
  const char *dv = in.upDown ? "UP" : in.downDown ? "DOWN" : "";
  snprintf(b, sizeof b, "%s %s", dv, dir);
  text(b, 250, 152, 2, C_GOOD);
  struct { const char *n; bool on; } bt[] = {{"A/PUSH", in.aDown}, {"B", in.bDown}, {"X", in.xDown}, {"Y", in.yDown},
                                              {"START", in.startDown}, {"SELECT", in.selectDown}};
  for (int i = 0; i < 6; i++) {
    int y = 184 + (i / 2) * 26, x = 250 + (i % 2) * 96;
    gfx->fillRoundRect(x, y, 90, 22, 6, bt[i].on ? C_GOOD : C_PANEL);
    text(bt[i].n, x + 6, y + 6, 1, bt[i].on ? C_BG : C_DIM);
  }
  centerText("dot must follow the stick   LEFT / tap / B = back", H - 14, 1, C_DIM);
  dirty = true;
}


// Which row did a tap hit? (rows drawn at y0 + i*step, highlight band y-8 .. y+28)
static int rowHit(int ty, int y0, int step, int n) {
  for (int i = 0; i < n; i++) if (ty >= y0 + i * step - 10 && ty < y0 + i * step + 32) return i;
  return -1;
}
static int listHit(int tx, int ty) {               // card index in the launcher list (scroll-aware), -1 = none
  if (tx < 30 || tx > W - 30 || ty < LIST_VT || ty >= LIST_VB) return -1;
  int n = cardCount(), off = (int)lroundf(scOff);
  for (int i = 0; i < n; i++) {
    int y = LIST_VT + scRowTop(i) - off;
    if (ty >= y && ty < y + LIST_CARD) return i;
  }
  return -1;
}

static void openGame(int i, bool app) {
  gameIdx = i;
  curIsApp = app;
  Game *g = cur = app ? APPS[i] : GAMES[i];
  for (int k = 0; k < g->nopts; k++) {
    uint8_t v = prefs.getUChar(g->opts[k].key, g->opts[k].value);
    g->opts[k].value = v < g->opts[k].count ? v : 0;
  }
  if (app && g->nopts == 0) { scr = S_PLAY; g->start(); return; }   // nothing to choose: start at once
  sel = g->nopts;                                     // cursor on START
  scr = S_OPTIONS;
  drawOptions();
}
static void backToList() {                            // after a game / app / its options page
  listEnter(curIsApp ? S_APPLIST : S_LIST, gameIdx);
}


#if REMOTE_ENABLED
// ---- USB-serial remote hooks (see hw.h / tools/capture.py): they drive the same launcher state machine as the user ----
void remoteListEntries(Print &out) {
  for (int i = 0; i < NGAMES; i++) out.printf("G %d %s\n", i, GAMES[i]->name);
  for (int i = 0; i < NAPPS; i++) out.printf("A %d %s\n", i, APPS[i]->name);
}
const char *remoteNav(char op, int idx) {
  if (scr == S_SETUP) return "button setup active";
  auto leave = [] { keepAwake = false; homeLock = false; applyBrightness(brightPct); };
  switch (op) {
    case 'S':
      if (scr != S_OPTIONS) return "not on an options page";
      scr = S_PLAY;
      cur->start();
      return nullptr;
    case 'L':
      if (scr != S_LIST && scr != S_APPLIST && scr != S_SETTINGS) return "not on a list";
      if (idx < 0 || idx >= scRowN()) return "row out of range";
      sel = idx; scEnsure(sel, false); redrawCur();
      return nullptr;
    case 'G':
      if (idx < 0 || idx >= NGAMES) return "game index out of range";
      leave(); openGame(idx, false);
      return nullptr;
    case 'A':
      if (idx < 0 || idx >= NAPPS) return "app index out of range";
      leave(); openGame(idx, true);
      return nullptr;
    case 'H': leave(); listEnter(S_LIST, 0); return nullptr;
    case 'P': leave(); listEnter(S_APPLIST, 0); return nullptr;
    case 'T': leave(); sdSt = sdInfo(); listEnter(S_SETTINGS, 0); return nullptr;
  }
  return "bad op";
}
#endif
// =====================================================================================
void setup() {
  hwBegin();
  dimIdx = prefs.getUChar("dim", 1);
  if (dimIdx > 3) dimIdx = 1;
  if (!prefs.isKey("leftBoot")) { scr = S_SETUP; drawSetup(); }
  else { listEnter(S_LIST, 0); }
  present();
  sfxStart();
}

void loop() {
#if REMOTE_ENABLED
  remotePoll();
#endif
  uint32_t now = millis();
  imuUpdate();

  if (scr == S_SETUP) {              // whichever physical button comes first becomes LEFT
    static bool waitRelease = true;
    bool boot = digitalRead(0) == LOW;
    if (waitRelease) { if (!boot) waitRelease = false; delay(5); return; }
    inputUpdate();                   // sees PWR presses as left or right depending on old mapping
    bool pwr = (leftIsBoot ? in.right : in.left);
    if (boot || pwr) {
      leftIsBoot = boot;
      prefs.putBool("leftBoot", leftIsBoot);
      sfxClick();
      while (digitalRead(0) == LOW) delay(5);
      waitRelease = true;
      inputUpdate();
      listEnter(S_LIST, 0); present();
    }
    delay(5);
    return;
  }

  inputUpdate();
  if (scr != S_PLAY) { in.left = in.btnLeft; in.right = in.btnRight; }   // the pad stick must not drive menus (up/down/A/B do)

  // ---- idle dim: any input wakes the screen and is swallowed (it must not also act) ----
  {
    static uint32_t lastAct = 0;
    static bool dimmed = false, swallow = false;
    static float refX = 0, refY = 0;
    bool act = in.left || in.right || in.tap || in.touching || in.home || in.leftDown || in.rightDown || in.padAny;
    if (fabsf(tiltX() - refX) > 0.08f || fabsf(tiltY() - refY) > 0.08f) { refX = tiltX(); refY = tiltY(); act = true; }
    if (scr == S_PLAY && !curIsApp && (fabsf(tiltX()) > 0.22f || fabsf(tiltY()) > 0.22f)) act = true;   // tilt held = playing
    if (scr != S_PLAY) keepAwake = false;
    if (act) {
      lastAct = now;
      if (dimmed) { dimmed = false; swallow = true; applyBrightness(brightPct); }
    } else if (wakeReq) {                      // an app asked for the screen (alarm): wake, nothing to swallow
      lastAct = now;
      if (dimmed) { dimmed = false; applyBrightness(brightPct); }
    } else if (keepAwake) {                    // level / running timer, stopwatch, metronome: never dim
      lastAct = now;
    } else if (!dimmed && DIM_MS[dimIdx] && now - lastAct > DIM_MS[dimIdx]) {
      dimmed = true;
      applyBrightness(DIM_PCT);
    }
    wakeReq = false;
    if (swallow) {
      in.left = in.right = in.tap = in.home = false;
      in.up = in.down = in.a = in.b = in.x = in.y = in.start = in.select = false;
      in.dragging = in.dragEnd = false;
      in.dragDX = in.dragDY = 0;
      if (!in.touching && !in.leftDown && !in.rightDown && !in.padAny) swallow = false;
    }
  }

  // gamepad up/down navigate the menus (held = repeat)
  int padStep = 0;
  if (scr != S_PLAY) {
    static Repeat repU, repD;
    padStep = repeatStep(repD, in.down, in.downDown, now, false) - repeatStep(repU, in.up, in.upDown, now, false);
  }

  static uint32_t lastLog = 0;
#if REMOTE_ENABLED
  const bool logQuiet = remoteSeen();          // a PC is talking to us: keep the stream clean for screenshots
#else
  const bool logQuiet = false;
#endif
  if (now - lastLog >= 1000 && !logQuiet) {
    lastLog = now;
    USBSerial.printf("scr=%d game=%d imu=%d audio=%d bat=%d tilt=%+.2f,%+.2f L=%d R=%d leftIsBoot=%d pad=%d heap=%u psram=%u\n",
                     scr, gameIdx, imuOk, audioOk, battery(), tiltX(), tiltY(), in.leftDown, in.rightDown,
                     leftIsBoot, gamepadConnected(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  }

  switch (scr) {
    case S_LIST: {
      if (scTick(now)) scPending = true;
      if (in.tap && !scBlockTap) { int h = listHit(in.tapX, in.tapY); if (h >= 0) { sel = h; in.right = true; } }
      if (in.left) selMove(1);
      if (padStep) selMove(padStep > 0 ? 1 : -1);
      if (in.a) in.right = true;
      if (in.right) {
        sfxClick();
        if (sel < NGAMES) openGame(sel, false);
        else if (sel == ROW_APPS) listEnter(S_APPLIST, 0);
        else { sdSt = sdInfo(); listEnter(S_SETTINGS, 0); }
      } else if (scPending && now - scDrawT >= 20) redrawCur();
      break;
    }
    case S_APPLIST: {
      if (scTick(now)) scPending = true;
      if (in.home || in.b) { listEnter(S_LIST, ROW_APPS); sfxClick(); break; }
      if (in.tap && !scBlockTap) { int h = listHit(in.tapX, in.tapY); if (h >= 0) { sel = h; in.right = true; } }
      if (in.left) selMove(1);
      if (padStep) selMove(padStep > 0 ? 1 : -1);
      if (in.a) in.right = true;
      if (in.right) {
        sfxClick();
        if (sel < NAPPS) openGame(sel, true);
        else listEnter(S_LIST, ROW_APPS);
      } else if (scPending && now - scDrawT >= 20) redrawCur();
      break;
    }
    case S_OPTIONS: {
      Game *g = cur;
      int rows = g->nopts + 2;
      if (in.tap) { int h = rowHit(in.tapY, 90, 44, rows); if (h >= 0) { sel = h; in.right = true; } }
      if (in.left) { sel = (sel + 1) % rows; sfxClick(); drawOptions(); }
      if (padStep) { sel = ((sel + (padStep > 0 ? 1 : -1)) % rows + rows) % rows; sfxClick(); drawOptions(); }
      if (in.a) in.right = true;
      if (in.b) { sfxClick(); backToList(); break; }
      if (in.right) {
        sfxClick();
        if (sel < g->nopts) {
          Option &o = g->opts[sel];
          o.value = (o.value + 1) % o.count;
          prefs.putUChar(o.key, o.value);
          drawOptions();
        } else if (sel == g->nopts) {
          scr = S_PLAY;
          g->start();
        } else {
          backToList();
        }
      }
      break;
    }
    case S_SETTINGS:
      if (scTick(now)) scPending = true;
      if (in.tap && !scBlockTap) { int h = settingsHit(in.tapY); if (h >= 0) { sel = h; in.right = true; } }
      if (in.left) selMove(1);
      if (padStep) selMove(padStep > 0 ? 1 : -1);
      if (in.a) in.right = true;
      if (in.b) { sfxClick(); listEnter(S_LIST, ROW_SETTINGS); break; }
      if (in.right) {
        sfxClick();
        switch (sel) {
          case R_SOUND: soundOn = !soundOn; prefs.putBool("sound", soundOn); drawSettings(); break;
          case R_VOLUME:
            applyVolume(volumePct >= 100 ? 0 : volumePct + 10);
            prefs.putUChar("vol", volumePct);
            play(1000, 60);                          // beep at the new volume
            drawSettings();
            break;
          case R_BRIGHT: {
            uint8_t bi = brightPct / 25;             // 1..4 -> next 25 %
            brightPct = (uint8_t)(25 * (bi % 4 + 1));
            prefs.putUChar("bright", brightPct / 25 - 1);
            applyBrightness(brightPct);
            drawSettings();
            break;
          }
          case R_DIM: dimIdx = (dimIdx + 1) % 4; prefs.putUChar("dim", dimIdx); drawSettings(); break;
          case R_SD:
            if (!sdPresent()) sdBegin();             // card inserted after boot?
            sdSt = sdInfo();
            scr = S_SDINFO; drawSdInfo();
            break;
          case R_PAD: scr = S_JOY; break;            // live joystick test screen
          case R_SETUP: scr = S_SETUP; drawSetup(); break;
          case R_TILT: scr = S_TILT; imuZero(); break;
          default: listEnter(S_LIST, ROW_SETTINGS); break;
        }
      } else if (scPending && now - scDrawT >= 20) redrawCur();
      break;
    case S_SDINFO:
      if (in.left || in.right || in.tap || in.a || in.b) { scr = S_SETTINGS; sel = R_SD; scVel = 0; scEnsure(sel, false); drawSettings(); }
      break;
    case S_TILT: {
      static uint32_t lastDraw = 0;
      if (in.right || in.a) { imuZero(); sfxClick(); }
      if (in.left || in.tap || in.b) { scr = S_SETTINGS; sel = R_TILT; scVel = 0; scEnsure(sel, false); drawSettings(); break; }
      if (now - lastDraw >= 40) { lastDraw = now; drawTiltCheck(); }
      break;
    }
    case S_JOY: {
      static uint32_t lastDraw = 0;
      if (in.left || in.right || in.tap || in.b || in.home) { scr = S_SETTINGS; sel = R_PAD; scVel = 0; scEnsure(sel, false); drawSettings(); break; }
      if (now - lastDraw >= 33) { lastDraw = now; drawJoyTest(); }
      break;
    }
    case S_PLAY:
      keepAwake = false;                               // apps set it again in their frame
      {
        // HOME: triple-tap BOOT (detected in inputUpdate; ignored while the previous frame set homeLock),
        // gamepad START+SELECT, or a touch held on the top-left corner for 1.5 s.
        static uint32_t cornerSince = 0;
        bool lock = homeLock;                          // set by the previous frame
        homeLock = false;
        if (in.touching && in.touchX < 72 && in.touchY < 60) { if (!cornerSince) cornerSince = now; }
        else cornerSince = 0;
        bool corner = cornerSince && now - cornerSince >= 1500;
        bool leave = (in.home && !lock) || corner;
        if (leave || !cur->frame(now)) {
          if (corner) play(1200, 40);
          cornerSince = 0;
          keepAwake = false;
          homeLock = false;
          applyBrightness(brightPct);                  // an app (torch) may have changed it
          backToList();
        }
      }
      break;
    default:
      break;
  }
  present();
  delay(1);
}
