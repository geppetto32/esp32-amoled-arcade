// AmoledSnake - Snake for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1: SH8601 + AXP2101 + QMI8658 + ES8311).
//
// Hold the board LANDSCAPE with its two buttons on TOP (like shoulder buttons).
// The first start asks you to press the LEFT button so it knows which is which.
//   Menu:      LEFT = next item, RIGHT = choose / toggle
//   Buttons:   LEFT = turn left, RIGHT = turn right (relative to the snake's heading)
//   Tilt:      tilt the board to steer, RIGHT = pause
//   Hold BOOT (whichever side it is) 1 s = back to menu. Game over: RIGHT = again, LEFT = menu.
// Don't hold PWR for ~6 s: the power chip switches the board off.
//
// Board: "Waveshare ESP32-S3-Touch-AMOLED-1.8", PSRAM enabled, USB CDC On Boot disabled.
// Flash with flash.cmd (two-step upload; a plain upload can fail halfway while the game runs).

#include <Arduino.h>
#include <Wire.h>
#include <HWCDC.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>
#include "ESP_I2S.h"
#include "es8311.h"

#define SN_COLS 28
#define SN_ROWS 21
#include "snake_logic.h"

// ---------------- pins / addresses ----------------
constexpr int LCD_SDIO0 = 4, LCD_SDIO1 = 5, LCD_SDIO2 = 6, LCD_SDIO3 = 7, LCD_SCLK = 11, LCD_CS = 12;
constexpr int PANEL_W = 368, PANEL_H = 448;          // physical panel (portrait)
constexpr int W = 448, H = 368;                       // what the game draws (landscape)
constexpr int I2C_SDA = 15, I2C_SCL = 14;
constexpr int I2S_MCK = 16, I2S_BCK = 9, I2S_DIN = 10, I2S_WS = 45, I2S_DOUT = 8, SPK_EN = 46;
constexpr int PIN_BOOT = 0;
constexpr uint8_t AXP_ADDR = 0x34, QMI_ADDR = 0x6B;
constexpr uint8_t ROTATION = 1;                       // 1 = top of picture on the buttons' edge; 3 = opposite

// ---------------- tilt tuning (flip these if tilt steers the wrong way) ----------------
#define TILT_SWAP_XY 1      // landscape: sensor X/Y swap relative to the picture
#define TILT_INVERT_X 0     // 1 = invert left/right
#define TILT_INVERT_Y 1     // 1 = invert up/down
constexpr float TILT_ON = 0.22f;             // g needed to pick a direction (about 13 degrees)
constexpr int START_MS = 230, MIN_MS = 90;   // step time: start slow, 4 ms faster per food

// ---------------- layout ----------------
constexpr int CELL = 16, HUD = 32;           // 28 x 21 cells of 16 px under a 32 px status bar
constexpr uint16_t C_BG = 0x0000, C_HUD = 0x10A2, C_GRID = 0x0841, C_HEAD = 0x87F0, C_BODY = 0x05E0,
                   C_BODY2 = 0x0480, C_FOOD = 0xF8A0, C_TEXT = 0xFFFF, C_DIM = 0x8410, C_ACC = 0xFEA0;

HWCDC USBSerial;
Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_SH8601 *panel = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, PANEL_W, PANEL_H);
Arduino_Canvas *gfx = new Arduino_Canvas(PANEL_W, PANEL_H, panel, 0, 0, ROTATION);   // frame buffer in PSRAM
bool dirty = false;                          // something was drawn -> send the frame
Preferences prefs;
I2SClass i2s;

// =====================================================================================
// I2C helpers (plain-register style)
// =====================================================================================
int i2cRead(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}
bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// =====================================================================================
// Power chip: PWR button (short-press IRQ) and battery
// =====================================================================================
void axpInit() {
  int en2 = i2cRead(AXP_ADDR, 0x41);              // INTEN2, bit3 = PWR key short press
  if (en2 >= 0) i2cWrite(AXP_ADDR, 0x41, en2 | 0x08);
  i2cWrite(AXP_ADDR, 0x49, 0xFF);                  // clear pending status
}
bool axpPwrPressed() {                             // true once per short press
  int st = i2cRead(AXP_ADDR, 0x49);
  if (st < 0 || !(st & 0x08)) return false;
  i2cWrite(AXP_ADDR, 0x49, 0x08);                  // write 1 to clear
  return true;
}
int axpBattery() { return i2cRead(AXP_ADDR, 0xA4); }   // 0-100 %, -1 on error
bool axpCharging() {
  int s = i2cRead(AXP_ADDR, 0x01);
  return s >= 0 && ((s & 0x60) >> 5) == 1;
}

// =====================================================================================
// IMU (QMI8658): accelerometer only, +-4 g, 125 Hz
// =====================================================================================
bool imuOk = false;
float tiltX = 0, tiltY = 0;          // filtered, in g, picture coordinates (x right, y down)
float zeroX = 0, zeroY = 0;          // neutral position captured at game start

bool imuInit() {
  if (i2cRead(QMI_ADDR, 0x00) != 0x05) return false;   // WHO_AM_I
  i2cWrite(QMI_ADDR, 0x60, 0xB0);                         // soft reset
  delay(20);
  i2cWrite(QMI_ADDR, 0x02, 0x40);                         // CTRL1: address auto-increment
  i2cWrite(QMI_ADDR, 0x03, (1 << 4) | 6);                 // CTRL2: 4 g, ODR 125 Hz
  i2cWrite(QMI_ADDR, 0x08, 0x01);                         // CTRL7: accelerometer on
  return true;
}
bool imuReadRaw(float &x, float &y) {
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(0x35);                                       // AX_L
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(QMI_ADDR, (uint8_t)6) != 6) return false;
  int16_t ax = Wire.read() | (Wire.read() << 8);
  int16_t ay = Wire.read() | (Wire.read() << 8);
  Wire.read(); Wire.read();
  float fx = ax * (4.0f / 32768.0f), fy = ay * (4.0f / 32768.0f);
#if TILT_SWAP_XY
  float t = fx; fx = fy; fy = t;
#endif
#if TILT_INVERT_X
  fx = -fx;
#endif
#if TILT_INVERT_Y
  fy = -fy;
#endif
  x = fx; y = fy;
  return true;
}
void imuUpdate() {
  float x, y;
  if (!imuOk || !imuReadRaw(x, y)) return;
  tiltX += 0.35f * (x - tiltX);
  tiltY += 0.35f * (y - tiltY);
}
void imuZero() { zeroX = tiltX; zeroY = tiltY; }
bool tiltDir(int &dx, int &dy) {     // false = held level
  float x = tiltX - zeroX, y = tiltY - zeroY;
  if (fabsf(x) < TILT_ON && fabsf(y) < TILT_ON) return false;
  if (fabsf(x) > fabsf(y)) { dx = x > 0 ? 1 : -1; dy = 0; }
  else { dx = 0; dy = y > 0 ? 1 : -1; }
  return true;
}

// =====================================================================================
// Sound: ES8311 codec + speaker amp, square-wave notes played by a background task
// =====================================================================================
constexpr int SR = 16000;
struct Note { uint16_t hz, ms; };
QueueHandle_t noteQ;
bool soundOn = true, audioOk = false;

void soundTask(void *) {
  static int16_t buf[2 * 256];
  Note n;
  for (;;) {
    if (xQueueReceive(noteQ, &n, portMAX_DELAY) != pdTRUE) continue;
    int total = SR * n.ms / 1000, done = 0;
    uint32_t phase = 0, step = n.hz ? (uint32_t)((uint64_t)n.hz * 65536 * 65536 / SR) : 0;
    while (done < total) {
      int frames = min(256, total - done);
      for (int i = 0; i < frames; i++) {
        int t = done + i, env = 3000;
        int edge = SR / 400;                                   // 2.5 ms fade in/out, no clicks
        if (t < edge) env = env * t / edge;
        if (total - t < edge) env = env * (total - t) / edge;
        int16_t v = n.hz ? ((phase & 0x80000000u) ? env : -env) : 0;
        phase += step;
        buf[2 * i] = buf[2 * i + 1] = v;
      }
      i2s.write((uint8_t *)buf, frames * 4);
      done += frames;
    }
    if (uxQueueMessagesWaiting(noteQ) == 0) {                 // trailing silence flushes the DMA
      memset(buf, 0, sizeof(buf));
      for (int k = 0; k < 4; k++) i2s.write((uint8_t *)buf, sizeof(buf));
    }
  }
}
void play(uint16_t hz, uint16_t ms) {
  if (!soundOn || !audioOk) return;
  Note n{hz, ms};
  xQueueSend(noteQ, &n, 0);
}
void sfxEat()   { play(880, 45); play(1320, 70); }
void sfxTurn()  { play(1500, 12); }
void sfxDie()   { play(440, 110); play(330, 110); play(220, 260); }
void sfxStart() { play(523, 80); play(659, 80); play(784, 120); }
void sfxMenu()  { play(1000, 25); }
void sfxBest()  { play(784, 90); play(988, 90); play(1175, 90); play(1568, 220); }

bool audioInit() {
  pinMode(SPK_EN, OUTPUT);
  digitalWrite(SPK_EN, HIGH);
  i2s.setPins(I2S_BCK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {false, false, true, SR * 256, SR};
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  es8311_voice_volume_set(es, 70, NULL);
  noteQ = xQueueCreate(16, sizeof(Note));
  xTaskCreatePinnedToCore(soundTask, "snd", 4096, nullptr, 2, nullptr, 0);
  return true;
}

// =====================================================================================
// Buttons: physical BOOT (GPIO0) and PWR (power chip), mapped to LEFT / RIGHT
// =====================================================================================
bool leftIsBoot = true;              // learned on first start, saved in flash

struct Buttons {
  bool bootDown = false, longFired = false;
  uint32_t bootSince = 0, lastPoll = 0;
  bool left = false, right = false, hold = false;     // events this loop
  void update() {
    bool bootPress = false, pwrPress = false;
    hold = false;
    bool d = digitalRead(PIN_BOOT) == LOW;
    uint32_t now = millis();
    if (d && !bootDown && now - bootSince > 30) { bootPress = true; bootSince = now; longFired = false; }
    if (!d && bootDown) bootSince = now;
    if (d && !longFired && now - bootSince > 1000) { hold = true; longFired = true; }
    bootDown = d;
    if (now - lastPoll >= 15) { lastPoll = now; pwrPress = axpPwrPressed(); }
    left = leftIsBoot ? bootPress : pwrPress;
    right = leftIsBoot ? pwrPress : bootPress;
  }
} btn;

// =====================================================================================
// Drawing (into the canvas; flushed once per loop when dirty)
// =====================================================================================
int cellX(int c) { return c * CELL; }
int cellY(int r) { return HUD + r * CELL; }

enum Tile { T_EMPTY, T_HEAD, T_BODY, T_BODY2, T_FOOD, T_DEAD, T_COUNT };
uint16_t tiles[T_COUNT][CELL * CELL];

void buildTiles() {
  const uint16_t fill[T_COUNT] = {C_BG, C_HEAD, C_BODY, C_BODY2, C_FOOD, C_FOOD};
  for (int t = 0; t < T_COUNT; t++)
    for (int y = 0; y < CELL; y++)
      for (int x = 0; x < CELL; x++) {
        uint16_t c = C_BG;
        if (t == T_EMPTY) {
          if ((x == 7 || x == 8) && (y == 7 || y == 8)) c = C_GRID;            // faint grid dot
        } else if (t == T_FOOD) {
          float dx = x - 7.5f, dy = y - 8.5f;
          if (dx * dx + dy * dy <= 36.f) c = C_FOOD;                         // apple
          if ((x == 8 || x == 9) && y >= 1 && y <= 3) c = C_BODY;              // stalk
        } else {                                                              // rounded square, 2 px margin
          int ix = x < 8 ? 6 - x : x - 9, iy = y < 8 ? 6 - y : y - 9;
          bool inside = x >= 2 && x <= 13 && y >= 2 && y <= 13;
          if (inside && !(ix >= 2 && iy >= 2 && ix + iy > 5)) c = fill[t];
        }
        tiles[t][y * CELL + x] = c;
      }
}
void drawTile(int c, int r, int t) {
  gfx->draw16bitRGBBitmap(cellX(c), cellY(r), tiles[t], CELL, CELL);
  dirty = true;
}
void centerText(const char *s, int y, int size, uint16_t col) {
  gfx->setTextSize(size);
  gfx->setTextColor(col);
  gfx->setCursor((W - (int)strlen(s) * 6 * size) / 2, y);
  gfx->print(s);
  dirty = true;
}
void present() {                     // send the frame to the panel if anything changed
  if (!dirty) return;
  gfx->flush();
  dirty = false;
}

// =====================================================================================
// Game
// =====================================================================================
enum Mode : uint8_t { MODE_BUTTONS, MODE_TILT };
enum Screen : uint8_t { SCR_SETUP, SCR_MENU, SCR_PLAY, SCR_PAUSE, SCR_OVER };
Screen scr = SCR_MENU;
Mode mode = MODE_BUTTONS;
int menuSel = 0;                       // 0 Play, 1 Mode, 2 Sound, 3 Buttons setup
constexpr int MENU_N = 4;
uint32_t best[2] = {0, 0};
SnakeState sn;
int8_t turnQ[2]; int turnN = 0;        // queued relative turns (-1 left, +1 right)
uint32_t lastStep = 0, lastHud = 0;
bool newBest = false;
int px = -1, py = -1;                  // last tilt-marker position in the menu

const char *holdName() { return leftIsBoot ? "LEFT" : "RIGHT"; }   // the button that can be held

void drawHud() {
  gfx->fillRect(0, 0, W, HUD, C_HUD);
  gfx->setTextSize(2);
  gfx->setTextColor(C_TEXT);
  gfx->setCursor(10, 9);
  gfx->printf("%lu", (unsigned long)sn.score);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(120, 9);
  gfx->printf("BEST %lu", (unsigned long)best[mode]);
  int b = axpBattery();
  gfx->setTextColor(axpCharging() ? C_ACC : C_DIM);
  gfx->setCursor(W - 80, 9);
  if (b >= 0) gfx->printf("%3d%%", b);
  gfx->fillRect(W - 20, 10, 10, 12, mode == MODE_TILT ? C_ACC : C_HEAD);   // mode badge
  dirty = true;
}

void drawBoard() {
  for (int r = 0; r < SN_ROWS; r++)
    for (int c = 0; c < SN_COLS; c++) drawTile(c, r, T_EMPTY);
  for (int i = sn.len - 1; i >= 0; i--) drawTile(sn.sx[i], sn.sy[i], i == 0 ? T_HEAD : (i & 1 ? T_BODY : T_BODY2));
  drawTile(sn.fx, sn.fy, T_FOOD);
  drawHud();
}

void drawSetup() {
  gfx->fillScreen(C_BG);
  centerText("BUTTON SETUP", 70, 4, C_HEAD);
  centerText("Hold the board with the", 150, 2, C_TEXT);
  centerText("two buttons on TOP.", 176, 2, C_TEXT);
  centerText("Press the LEFT button.", 230, 3, C_ACC);
  gfx->fillTriangle(40, 20, 70, 20, 55, 0, C_ACC);       // arrows pointing at both top corners
  gfx->fillTriangle(W - 70, 20, W - 40, 20, W - 55, 0, C_DIM);
}

void drawMenu() {
  gfx->fillScreen(C_BG);
  px = py = -1;
  centerText("SNAKE", 26, 6, C_HEAD);
  for (int i = 0; i < 11; i++) gfx->fillRoundRect(124 + i * 16, 82, 12, 12, 4, i == 10 ? C_HEAD : (i & 1 ? C_BODY : C_BODY2));
  gfx->fillCircle(124 + 12 * 16 + 6, 88, 6, C_FOOD);
  const char *items[MENU_N] = {"PLAY", mode == MODE_TILT ? "MODE: TILT" : "MODE: BUTTONS",
                               soundOn ? "SOUND: ON" : "SOUND: OFF", "BUTTON SETUP"};
  for (int i = 0; i < MENU_N; i++) {
    int y = 120 + i * 44;
    if (i == menuSel) gfx->fillRoundRect(64, y - 8, W - 128, 36, 10, C_HUD);
    centerText(items[i], y, 3, i == menuSel ? C_TEXT : C_DIM);
  }
  char buf[48];
  snprintf(buf, sizeof buf, "BEST  buttons %lu   tilt %lu", (unsigned long)best[0], (unsigned long)best[1]);
  centerText(buf, 308, 1, C_DIM);
  centerText("LEFT = next      RIGHT = select", 330, 2, C_DIM);
  if (mode == MODE_TILT && !imuOk) centerText("IMU not found!", 352, 2, C_FOOD);
}

void drawTiltMarker() {                // live tilt dot in the menu: check the direction before playing
  if (mode != MODE_TILT || !imuOk) return;
  int x = 405 + (int)constrain(tiltX * 30, -18.f, 18.f), y = 230 + (int)constrain(tiltY * 30, -18.f, 18.f);
  if (x == px && y == py) return;
  gfx->fillRect(381, 206, 49, 49, C_BG);
  gfx->drawRect(381, 206, 49, 49, C_GRID);
  gfx->fillRect(x - 4, y - 4, 8, 8, C_ACC);
  px = x; py = y;
  dirty = true;
}

void startGame() {
  snakeReset(sn, esp_random());
  sn.stepMs = START_MS;
  turnN = 0;
  newBest = false;
  drawBoard();
  for (int n = 3; n >= 1; n--) {        // 3-2-1 countdown; hold the board how you want to play
    char c[2] = {(char)('0' + n), 0};
    gfx->fillRoundRect(W / 2 - 40, H / 2 - 30, 80, 80, 16, C_HUD);
    centerText(c, H / 2 - 12, 6, C_ACC);
    present();
    play(660, 90);
    uint32_t t0 = millis();
    while (millis() - t0 < 700) { imuUpdate(); delay(10); }
  }
  imuZero();                            // "level" = how the board is held at GO
  drawBoard();
  present();
  sfxStart();
  scr = SCR_PLAY;
  lastStep = millis();
}

void gameOver() {
  scr = SCR_OVER;
  if (sn.score > best[mode]) {
    best[mode] = sn.score;
    newBest = true;
    prefs.putUInt(mode == MODE_TILT ? "bestT" : "bestB", sn.score);
    sfxBest();
  } else {
    sfxDie();
  }
  drawTile(sn.sx[0], sn.sy[0], T_DEAD);
  gfx->fillRoundRect(64, 110, W - 128, 150, 14, C_HUD);
  centerText(sn.alive ? "YOU WIN!" : "GAME OVER", 126, 4, C_TEXT);
  char buf[32];
  snprintf(buf, sizeof buf, newBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)sn.score);
  centerText(buf, 176, 3, newBest ? C_ACC : C_TEXT);
  centerText("RIGHT again   LEFT menu", 226, 2, C_DIM);
  drawHud();
}

void doStep() {
  if (mode == MODE_BUTTONS && turnN > 0) {
    int t = turnQ[0];
    turnQ[0] = turnQ[1];
    turnN--;
    int dx = sn.dx, dy = sn.dy;
    if (t < 0) snakeSetDir(sn, dy, -dx);     // left
    else snakeSetDir(sn, -dy, dx);           // right
  }
  int tx = sn.sx[sn.len - 1], ty = sn.sy[sn.len - 1];
  int oldHx = sn.sx[0], oldHy = sn.sy[0];
  SnakeResult r = snakeStep(sn);
  if (r == SN_DEAD || r == SN_WON) { gameOver(); return; }
  if (r == SN_MOVED) drawTile(tx, ty, T_EMPTY);
  drawTile(oldHx, oldHy, (sn.len & 1) ? T_BODY : T_BODY2);
  drawTile(sn.sx[0], sn.sy[0], T_HEAD);
  if (r == SN_ATE) {
    if (sn.stepMs < MIN_MS) sn.stepMs = MIN_MS;
    drawTile(sn.fx, sn.fy, T_FOOD);
    sfxEat();
    drawHud();
  }
}

// =====================================================================================
void setup() {
  USBSerial.setTxTimeoutMs(0);           // never block the game when no PC is reading
  USBSerial.begin(115200);
  buildTiles();
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!gfx->begin()) USBSerial.println("display init failed (PSRAM enabled?)");
  panel->setBrightness(200);
  gfx->fillScreen(C_BG);

  axpInit();
  imuOk = imuInit();
  audioOk = audioInit();

  prefs.begin("snake", false);
  best[0] = prefs.getUInt("bestB", 0);
  best[1] = prefs.getUInt("bestT", 0);
  soundOn = prefs.getBool("sound", true);
  mode = (Mode)prefs.getUChar("mode", MODE_BUTTONS);
  if (mode == MODE_TILT && !imuOk) mode = MODE_BUTTONS;
  leftIsBoot = prefs.getBool("leftBoot", true);

  for (int i = 0; i < 20; i++) { imuUpdate(); delay(5); }
  if (!prefs.isKey("leftBoot")) { scr = SCR_SETUP; drawSetup(); }
  else { scr = SCR_MENU; drawMenu(); }
  present();
  sfxStart();
}

void loop() {
  uint32_t now = millis();
  static uint32_t lastImu = 0, lastLog = 0, flushUs = 0;
  if (now - lastImu >= 10) { lastImu = now; imuUpdate(); }   // 100 Hz is plenty

  // Button setup screen: whichever physical button is pressed first becomes LEFT.
  if (scr == SCR_SETUP) {
    bool boot = digitalRead(PIN_BOOT) == LOW, pwr = false;
    static uint32_t lastPoll = 0;
    if (now - lastPoll >= 15) { lastPoll = now; pwr = axpPwrPressed(); }
    if (boot || pwr) {
      leftIsBoot = boot;
      prefs.putBool("leftBoot", leftIsBoot);
      sfxMenu();
      while (digitalRead(PIN_BOOT) == LOW) delay(5);      // wait for release
      btn.bootDown = false;
      scr = SCR_MENU;
      drawMenu();
      present();
    }
    delay(5);
    return;
  }

  btn.update();
  if (now - lastLog >= 1000) {          // status line for the serial monitor
    lastLog = now;
    USBSerial.printf("imu=%d audio=%d bat=%d%% tilt x=%+.2f y=%+.2f scr=%d score=%lu flush=%luus leftIsBoot=%d\n",
                     imuOk, audioOk, axpBattery(), tiltX - zeroX, tiltY - zeroY, scr, (unsigned long)sn.score,
                     (unsigned long)flushUs, leftIsBoot);
  }

  switch (scr) {
    case SCR_MENU:
      if (btn.left) { menuSel = (menuSel + 1) % MENU_N; sfxMenu(); drawMenu(); }
      if (btn.right) {
        if (menuSel == 0) startGame();
        else if (menuSel == 1) {
          mode = (mode == MODE_BUTTONS && imuOk) ? MODE_TILT : MODE_BUTTONS;
          prefs.putUChar("mode", mode); sfxMenu(); drawMenu();
        } else if (menuSel == 2) {
          soundOn = !soundOn; prefs.putBool("sound", soundOn); sfxMenu(); drawMenu();
        } else {
          scr = SCR_SETUP; drawSetup();
        }
      }
      drawTiltMarker();
      break;

    case SCR_PLAY:
      if (btn.hold) { scr = SCR_MENU; drawMenu(); break; }
      if (mode == MODE_BUTTONS) {
        if (btn.left && turnN < 2) { turnQ[turnN++] = -1; sfxTurn(); }
        if (btn.right && turnN < 2) { turnQ[turnN++] = +1; sfxTurn(); }
      } else {
        int dx, dy;
        if (tiltDir(dx, dy) && !(dx == sn.dx && dy == sn.dy) && !(dx == -sn.dx && dy == -sn.dy)) {
          if (dx != sn.ndx || dy != sn.ndy) { snakeSetDir(sn, dx, dy); sfxTurn(); }
        }
        if (btn.right) {
          scr = SCR_PAUSE;
          gfx->fillRoundRect(W / 2 - 100, H / 2 - 40, 200, 70, 14, C_HUD);
          centerText("PAUSED", H / 2 - 20, 4, C_TEXT);
          sfxMenu();
          break;
        }
      }
      if ((int32_t)(now - lastStep) >= sn.stepMs) { lastStep = now; doStep(); }
      if (now - lastHud > 10000) { lastHud = now; drawHud(); }   // battery refresh
      break;

    case SCR_PAUSE:
      if (btn.right) { imuZero(); drawBoard(); scr = SCR_PLAY; lastStep = now + 400; sfxMenu(); }
      if (btn.hold || btn.left) { scr = SCR_MENU; drawMenu(); }
      break;

    case SCR_OVER:
      if (btn.right) startGame();
      if (btn.left) { scr = SCR_MENU; drawMenu(); }
      break;

    default:
      break;
  }

  if (dirty) {
    uint32_t t0 = micros();
    present();
    flushUs = micros() - t0;
  }
  delay(1);
}
