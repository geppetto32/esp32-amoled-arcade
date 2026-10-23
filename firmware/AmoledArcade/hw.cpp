// hw.cpp - see hw.h
#include "hw.h"
#include <Wire.h>
#include <HWCDC.h>
#include "ESP_I2S.h"
#include "es8311.h"
#include <SD_MMC.h>
#include <driver/gpio.h>

// ---------------- pins / addresses ----------------
static constexpr int LCD_SDIO0 = 4, LCD_SDIO1 = 5, LCD_SDIO2 = 6, LCD_SDIO3 = 7, LCD_SCLK = 11, LCD_CS = 12;
static constexpr int PANEL_W = 368, PANEL_H = 448;
static constexpr int I2C_SDA = 15, I2C_SCL = 14;
static constexpr int I2S_MCK = 16, I2S_BCK = 9, I2S_DIN = 10, I2S_WS = 45, I2S_DOUT = 8, SPK_EN = 46;
static constexpr int PIN_BOOT = 0;
static constexpr uint8_t AXP_ADDR = 0x34, QMI_ADDR = 0x6B;
static constexpr uint8_t ROTATION = 1;       // top of picture on the buttons' edge (3 = flipped)

// Tilt axes relative to the landscape picture. Flip if tilt steers the wrong way.
#define TILT_SWAP_XY 0
#define TILT_INVERT_X 0
#define TILT_INVERT_Y 0

HWCDC USBSerial;
#if REMOTE_ENABLED
static bool remTilt = false;                 // TILT x y override (remote screenshots)
static float remTX = 0, remTY = 0;
#endif
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
static Arduino_SH8601 *panel = new Arduino_SH8601(bus, GFX_NOT_DEFINED, 0, PANEL_W, PANEL_H);
Arduino_Canvas *gfx = new Arduino_Canvas(PANEL_W, PANEL_H, panel, 0, 0, ROTATION);
Preferences prefs;
bool dirty = false;
static I2SClass i2s;
uint8_t brightPct = 75;
uint8_t volumePct = 70;
static es8311_handle_t esH = nullptr;

// =====================================================================================
// I2C
// =====================================================================================
static int i2cRead(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return -1;
  return Wire.read();
}
static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

// =====================================================================================
// Screen
// =====================================================================================
void present() {
  if (!dirty) return;
  gfx->flush();
  dirty = false;
}
void applyBrightness(int pct) {
  pct = constrain(pct, 1, 100);
  panel->setBrightness((uint8_t)(pct * 255 / 100));
}
uint16_t *frameBuffer() { return gfx->getFramebuffer(); }
void text(const char *s, int x, int y, int size, uint16_t col) {
  gfx->setTextSize(size);
  gfx->setTextColor(col);
  gfx->setCursor(x, y);
  gfx->print(s);
  dirty = true;
}
void centerText(const char *s, int y, int size, uint16_t col) {
  text(s, (W - (int)strlen(s) * 6 * size) / 2, y, size, col);
}
uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31, br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// =====================================================================================
// Power chip: battery + PWR key (edge IRQs give press AND release -> instant response)
// =====================================================================================
int battery() { return i2cRead(AXP_ADDR, 0xA4); }
int batteryMv() {                            // AXP2101 0x34/0x35: 14-bit VBAT in mV (UNVERIFIED on this board)
  int h = i2cRead(AXP_ADDR, 0x34), l = i2cRead(AXP_ADDR, 0x35);
  return (h < 0 || l < 0) ? -1 : (((h & 0x3F) << 8) | l);
}
bool charging() {
  int s = i2cRead(AXP_ADDR, 0x01);
  return s >= 0 && ((s & 0x60) >> 5) == 1;
}
static void axpInit() {
  int en2 = i2cRead(AXP_ADDR, 0x41);   // INTEN2: bit0 PWRON rising, bit1 PWRON falling, bit3 short press
  if (en2 >= 0) i2cWrite(AXP_ADDR, 0x41, en2 | 0x0B);
  i2cWrite(AXP_ADDR, 0x49, 0xFF);      // clear pending
  int adc = i2cRead(AXP_ADDR, 0x30);   // ADC channel enable: bit0 = battery voltage (for the SYSTEM app)
  if (adc >= 0) i2cWrite(AXP_ADDR, 0x30, adc | 0x01);
}

// =====================================================================================
// Buttons
// =====================================================================================
Input in;
bool leftIsBoot = true;
const char *holdButtonName() { return leftIsBoot ? "LEFT" : "RIGHT"; }

// =====================================================================================
// Gamepad: Adafruit Mini I2C Gamepad (product 5743, seesaw firmware, ATtiny817) at I2C 0x50.
// Register map (source: Adafruit_Seesaw library, Adafruit_seesaw.h / .cpp, and the Mini_I2C_Gamepad_QT example):
//   every transaction = write {base, reg [, data...]}, STOP, wait >= 250 us (ADC: 500 us), read.
//   STATUS base 0x00: HW_ID reg 0x01 (1 byte; ATtiny817 = 0x87, SAMD09 = 0x55), VERSION reg 0x02 (4 bytes big endian,
//                     product id = upper 16 bits, 5743 expected)
//   GPIO base 0x01:   DIRCLR_BULK 0x03, BULK 0x04 (read 4 bytes), BULK_SET 0x05, PULLENSET 0x0B (each 4-byte mask)
//   ADC base 0x09:    channel n result at reg 0x07 + n, 2 bytes big endian (10 bit)
// Pins (Mini_I2C_Gamepad_QT example): SELECT 0, B 1, Y 2, A 5, X 6, START 16 (active low, internal pull-up);
//   stick X = ADC channel 14, stick Y = ADC channel 15, example uses 1023 - reading.
// UNVERIFIED on hardware: stick direction signs (PAD_INVERT_X / PAD_INVERT_Y below), centre value, that the 5743
//   reports HW_ID 0x87, and the real timing on this bus. Nothing is polled when the probe fails at boot.
// =====================================================================================
static constexpr uint8_t PAD_ADDR = 0x50;
static constexpr int PAD_B_SELECT = 0, PAD_B_B = 1, PAD_B_Y = 2, PAD_B_A = 5, PAD_B_X = 6, PAD_B_START = 16;
static constexpr uint32_t PAD_MASK = (1UL << PAD_B_SELECT) | (1UL << PAD_B_B) | (1UL << PAD_B_Y) |
                                     (1UL << PAD_B_A) | (1UL << PAD_B_X) | (1UL << PAD_B_START);
static constexpr uint8_t PAD_ADC_X = 14, PAD_ADC_Y = 15;
#define PAD_INVERT_X 0       // set 1 if pushing the stick right moves the cursor left
#define PAD_INVERT_Y 0       // set 1 if pushing the stick up moves the cursor down

static bool padOk = false;           // answering and being polled
static bool padSeen = false;         // answered at least once (only then do we ever re-probe after a loss)
static uint32_t padProd = 0;
static uint8_t padFails = 0;
static uint32_t padNextPoll = 0, padRetryAt = 0;
static uint32_t padBtns = 0;         // bit set = pressed
static int padCx = 512, padCy = 512;
static float padAx = 0, padAy = 0;

bool gamepadConnected() { return padOk; }
uint32_t gamepadProduct() { return padProd; }

static bool ssRead(uint8_t base, uint8_t reg, uint8_t *buf, uint8_t n, uint16_t waitUs) {
  Wire.beginTransmission(PAD_ADDR);
  Wire.write(base);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return false;
  delayMicroseconds(waitUs);
  if (Wire.requestFrom(PAD_ADDR, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}
static bool ssWrite32(uint8_t base, uint8_t reg, uint32_t v) {
  Wire.beginTransmission(PAD_ADDR);
  Wire.write(base);
  Wire.write(reg);
  Wire.write((uint8_t)(v >> 24));
  Wire.write((uint8_t)(v >> 16));
  Wire.write((uint8_t)(v >> 8));
  Wire.write((uint8_t)v);
  return Wire.endTransmission() == 0;
}
static bool ssAdc(uint8_t ch, int &out) {
  uint8_t b[2];
  if (!ssRead(0x09, (uint8_t)(0x07 + ch), b, 2, 500)) return false;
  out = ((b[0] << 8) | b[1]) & 0x3FF;
  return true;
}
static bool padProbe() {                          // quick when nothing answers: one address ACK test
  Wire.beginTransmission(PAD_ADDR);
  if (Wire.endTransmission() != 0) return false;
  uint8_t id = 0;
  if (!ssRead(0x00, 0x01, &id, 1, 250)) return false;
  bool known = id == 0x55 || id == 0x46 || (id >= 0x84 && id <= 0x89) || (id >= 0x90 && id <= 0x93);
  if (!known) { USBSerial.printf("pad: 0x50 answered with unknown HW_ID 0x%02X, ignored\n", id); return false; }
  uint8_t v[4];
  padProd = 0;
  if (ssRead(0x00, 0x02, v, 4, 250)) padProd = ((uint32_t)v[0] << 8) | v[1];
  if (padProd != 0 && padProd != 5743) {
    USBSerial.printf("pad: seesaw product %lu is not 5743, ignored\n", (unsigned long)padProd);
    return false;
  }
  if (!ssWrite32(0x01, 0x03, PAD_MASK) || !ssWrite32(0x01, 0x0B, PAD_MASK) || !ssWrite32(0x01, 0x05, PAD_MASK))
    return false;                                 // inputs with pull-ups
  long sx = 0, sy = 0;
  int n = 0, vx, vy;
  for (int i = 0; i < 4; i++) if (ssAdc(PAD_ADC_X, vx) && ssAdc(PAD_ADC_Y, vy)) { sx += 1023 - vx; sy += 1023 - vy; n++; }
  padCx = n ? (int)(sx / n) : 512;
  padCy = n ? (int)(sy / n) : 512;
  USBSerial.printf("pad: seesaw gamepad found, hw=0x%02X product=%lu centre=%d,%d\n", id, (unsigned long)padProd, padCx, padCy);
  return true;
}
static void padDrop() {
  padOk = false; padFails = 0; padBtns = 0;
  padAx = padAy = 0;
  padRetryAt = millis() + 2000;
  USBSerial.println("pad: lost");
}
static float padShape(int raw, int centre, bool invert) {   // -1..1 with dead zone
  float d = (raw - centre) / 480.0f;
  if (invert) d = -d;
  float m = fabsf(d);
  m = m < 0.12f ? 0.0f : (m - 0.12f) / 0.88f;
  if (m > 1.0f) m = 1.0f;
  return d < 0 ? -m : m;
}
static void padPoll(uint32_t now) {
  if (!padSeen) return;                                      // never present: costs nothing
  if (!padOk) {
    if ((int32_t)(now - padRetryAt) < 0) return;
    if (padProbe()) { padOk = true; padFails = 0; } else padRetryAt = now + 2000;
    return;
  }
  if ((int32_t)(now - padNextPoll) < 0) return;
  padNextPoll = now + 10;                                    // <= 100 Hz
  uint8_t b[4];
  if (!ssRead(0x01, 0x04, b, 4, 250)) { if (++padFails >= 8) padDrop(); return; }
  padFails = 0;
  uint32_t raw = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  padBtns = ~raw & PAD_MASK;
  static bool alt = false;
  alt = !alt;
  if (alt) {                                                 // stick at 50 Hz (two ADC reads cost ~1.3 ms)
    int vx, vy;
    if (ssAdc(PAD_ADC_X, vx) && ssAdc(PAD_ADC_Y, vy)) {
      padAx = padShape(1023 - vx, padCx, PAD_INVERT_X);
      padAy = padShape(1023 - vy, padCy, PAD_INVERT_Y);
    } else if (++padFails >= 8) padDrop();
  }
}

// =====================================================================================
// Analog thumbstick (Xbox-style replacement module: two potentiometers + push switch) on the board's edge pads:
//   X = GPIO17 (ADC2_CH6), Y = GPIO18 (ADC2_CH7), push switch = GPIO38 (to GND, internal pull-up). Pots across 3V3/GND.
//   ADC2 is fine as long as Wi-Fi is off (it is). 12 bit, 11 dB attenuation.
// Detection at boot, so unconnected pins never produce phantom input: for each axis the ADC reads the pin with the
//   internal pull-up on, then with the pull-down on. A floating pin simply follows the pull (high, then ~0). A pot holds
//   the pin at a level that hardly changes. Present = both axes: |high - low| < 500 counts and both reads in 500..3600.
//   Centre = average of 16 reads straight after detection (keep the stick untouched while booting).
// UNVERIFIED on hardware: that the pad pulls still act while the pad is in ADC mode (set STK_FORCE 1 to skip the
//   detection), the stick directions (STK_INVERT_X / _Y / STK_SWAP_XY) and the usable travel (it adapts upward).
// =====================================================================================
static constexpr int STK_X = 17, STK_Y = 18, STK_SW = 38;
#define STK_FORCE 0          // 1 = assume the stick is connected, skip detection
#define STK_INVERT_X 0       // 1 if pushing the stick right moves left
#define STK_INVERT_Y 0       // 1 if pushing the stick up moves down
#define STK_SWAP_XY 0        // 1 if the two pots are wired the other way round
static constexpr float STK_DEAD = 0.08f;
static bool stkOk = false, stkSw = false;
static int stkCx = 2048, stkCy = 2048, stkRx = 0, stkRy = 0;
static float stkSpanPx = 900, stkSpanNx = 900, stkSpanPy = 900, stkSpanNy = 900;   // adaptive full-travel (counts)
static float stkFx = 0, stkFy = 0, stkAx = 0, stkAy = 0;
static uint32_t stkNext = 0;
static uint8_t stkSwCnt = 0;

bool stickPresent() { return stkOk; }
void stickRaw(int &rx, int &ry) { rx = stkRx; ry = stkRy; }
bool stickPressed() { return stkOk && stkSw; }

static int adcAvg(int pin, int n) {
  long s = 0;
  for (int i = 0; i < n; i++) s += analogRead(pin);
  return (int)(s / n);
}
static bool stkProbe() {
  analogSetPinAttenuation(STK_X, ADC_11db);
  analogSetPinAttenuation(STK_Y, ADC_11db);
  const int pins[2] = {STK_X, STK_Y};
  bool ok = true;
  for (int i = 0; i < 2; i++) {
    (void)analogRead(pins[i]);                         // initialise the ADC channel first (it clears pulls), then pull
    gpio_pulldown_dis((gpio_num_t)pins[i]);
    gpio_pullup_en((gpio_num_t)pins[i]);
    delay(4);
    int hi = adcAvg(pins[i], 8);
    gpio_pullup_dis((gpio_num_t)pins[i]);
    gpio_pulldown_en((gpio_num_t)pins[i]);
    delay(4);
    int lo = adcAvg(pins[i], 8);
    gpio_pulldown_dis((gpio_num_t)pins[i]);
    USBSerial.printf("stick: GPIO%d pull-up read %d, pull-down read %d\n", pins[i], hi, lo);
    if (abs(hi - lo) >= 500 || hi < 500 || hi > 3600 || lo < 500 || lo > 3600) ok = false;
  }
#if STK_FORCE
  ok = true;
#endif
  if (!ok) return false;
  delay(4);
  long sx = 0, sy = 0;
  for (int i = 0; i < 16; i++) { sx += analogRead(STK_X); sy += analogRead(STK_Y); delay(2); }
  stkCx = (int)(sx / 16); stkCy = (int)(sy / 16);
  stkFx = (float)stkCx; stkFy = (float)stkCy;
  pinMode(STK_SW, INPUT_PULLUP);
  USBSerial.printf("stick: analog thumbstick found, centre %d,%d\n", stkCx, stkCy);
  return true;
}
static float stkShape(float raw, int c, float &spanP, float &spanN, bool invert) {
  float d = raw - c;
  if (d > spanP) spanP = d;
  if (-d > spanN) spanN = -d;
  float v = d > 0 ? d / spanP : d / spanN;
  if (invert) v = -v;
  float m = fabsf(v);
  m = m < STK_DEAD ? 0.0f : (m - STK_DEAD) / (1.0f - STK_DEAD);
  if (m > 1.0f) m = 1.0f;
  return v < 0 ? -m : m;
}
static void stkPoll(uint32_t now) {
  if (!stkOk || (int32_t)(now - stkNext) < 0) return;
  stkNext = now + 10;                                    // 100 Hz
  int rx = adcAvg(STK_X, 3), ry = adcAvg(STK_Y, 3);
#if STK_SWAP_XY
  int t = rx; rx = ry; ry = t;
#endif
  stkRx = rx; stkRy = ry;
  stkFx += 0.5f * (rx - stkFx);
  stkFy += 0.5f * (ry - stkFy);
  stkAx = stkShape(stkFx, stkCx, stkSpanPx, stkSpanNx, STK_INVERT_X);
  stkAy = stkShape(stkFy, stkCy, stkSpanPy, stkSpanNy, STK_INVERT_Y);
  bool low = digitalRead(STK_SW) == LOW;                 // debounce: two reads in a row
  if (low != stkSw) { if (++stkSwCnt >= 2) { stkSw = low; stkSwCnt = 0; } } else stkSwCnt = 0;
}

// merged stick (analog stick wins when both exist and it is deflected more) + axis helpers for games
static float sAx = 0, sAy = 0;
static bool sL = false, sR = false, sU = false, sD = false;
static uint32_t axisUsedAt = 0;
static constexpr float AXIS_G = 0.7f;                    // full stick deflection counts as this much tilt (g)
float axisX() { axisUsedAt = millis(); return fabsf(sAx) > 0 ? sAx * AXIS_G : tiltX(); }
float axisY() { return fabsf(sAy) > 0 ? sAy * AXIS_G : tiltY(); }

// =====================================================================================
void inputUpdate() {
  static bool bootPrev = false, pwrHeld = false, edgesWork = false;
  static uint32_t bootT = 0, lastPoll = 0;
  static uint32_t tapT[3] = {0, 0, 0};                   // times of the last BOOT presses (home gesture)
  uint32_t now = millis();
  bool bootPress = false, pwrPress = false;
  in.home = false;

  bool d = digitalRead(PIN_BOOT) == LOW;
  if (d && !bootPrev && now - bootT > 30) {
    bootPress = true; bootT = now;
    tapT[0] = tapT[1]; tapT[1] = tapT[2]; tapT[2] = now;
    if (tapT[0] && now - tapT[0] <= 800) { in.home = true; tapT[0] = tapT[1] = tapT[2] = 0; }   // triple tap = home
  }
  if (!d && bootPrev) bootT = now;
  bootPrev = d;

  if (now - lastPoll >= 10) {
    lastPoll = now;
    int st = i2cRead(AXP_ADDR, 0x49);
    if (st > 0 && (st & 0x0B)) {
      i2cWrite(AXP_ADDR, 0x49, st & 0x0B);       // write-1-to-clear what we saw
      bool fall = st & 0x02, rise = st & 0x01, shortPress = st & 0x08;
      if (fall) { pwrPress = true; pwrHeld = true; edgesWork = true; }
      if (rise) pwrHeld = false;
      if (shortPress && !edgesWork) pwrPress = true;   // fallback if edge IRQs never arrive
    }
  }

  // Touch (FT3168 at 0x38). A touch ends as a TAP (short, finger barely moved) or, once the finger travelled
  // DRAG_SLOP px, as a DRAG: in.dragging / in.dragDX/DY while down, in.dragEnd + in.flingVX/VY at the lift.
  static bool fingerPrev = false, dragged = false;
  static int downX = 0, downY = 0, lastX = 0, lastY = 0;
  static uint32_t lastTouchPoll = 0, downT = 0, lastMoveT = 0;
  static float velX = 0, velY = 0;
  in.tap = false;
  in.dragEnd = false;
  in.dragDX = in.dragDY = 0;
  if (now - lastTouchPoll >= 12) {
    uint32_t dtms = now - lastTouchPoll;
    if (dtms > 100) dtms = 100;
    lastTouchPoll = now;
    Wire.beginTransmission(0x38);
    Wire.write(0x02);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom((uint8_t)0x38, (uint8_t)5) == 5) {
      uint8_t fingers = Wire.read();
      int px = ((Wire.read() & 0x0F) << 8); px |= Wire.read();
      int py = ((Wire.read() & 0x0F) << 8); py |= Wire.read();
      bool finger = (fingers & 0x0F) > 0;
      // panel is portrait 368x448; picture is rotated: x = panel y, y = 367 - panel x
      int lx = py, ly = 367 - px;
      if (finger) {
        if (!fingerPrev) {
          downX = lastX = lx; downY = lastY = ly;
          downT = lastMoveT = now;
          dragged = false;
          velX = velY = 0;
        } else {
          int dx = lx - lastX, dy = ly - lastY;
          if (!dragged && (abs(lx - downX) >= DRAG_SLOP || abs(ly - downY) >= DRAG_SLOP)) {
            dragged = true;
            dx = lx - downX; dy = ly - downY;        // hand over everything travelled so far: no jump
          }
          if (dragged) { in.dragDX += dx; in.dragDY += dy; }
          velX = 0.6f * velX + 0.4f * (dx * 1000.0f / dtms);
          velY = 0.6f * velY + 0.4f * (dy * 1000.0f / dtms);
          if (dx || dy) lastMoveT = now;
          lastX = lx; lastY = ly;
        }
        in.touchX = lx; in.touchY = ly;
        in.dragTX = lx - downX; in.dragTY = ly - downY;
      } else if (fingerPrev) {
        if (dragged) {
          in.dragEnd = true;
          bool moving = now - lastMoveT < 90;
          in.flingVX = moving ? velX : 0.0f;
          in.flingVY = moving ? velY : 0.0f;
          in.dragTX = lastX - downX; in.dragTY = lastY - downY;
        } else if (now - downT < 900 && abs(lastX - downX) < 40 && abs(lastY - downY) < 40) {
          in.tap = true; in.tapX = downX; in.tapY = downY;
        }
        dragged = false;
      }
      fingerPrev = finger;
      in.touching = finger;
      in.dragging = finger && dragged;
    }
  }

  // Gamepad + analog stick (nothing happens when absent)
  padPoll(now);
  stkPoll(now);
  float sx = padAx, sy = padAy;
  if (stkOk) {
    if (fabsf(stkAx) >= fabsf(sx)) sx = stkAx;
    if (fabsf(stkAy) >= fabsf(sy)) sy = stkAy;
  }
  sAx = sx; sAy = sy;
  sR = sx > (sR ? 0.35f : 0.55f);
  sL = sx < -(sL ? 0.35f : 0.55f);
  sD = sy > (sD ? 0.35f : 0.55f);
  sU = sy < -(sU ? 0.35f : 0.55f);
  uint32_t btns = padBtns | (stkSw ? (1UL << PAD_B_A) : 0);   // stick push = A / confirm
  static uint32_t prevBtns = 0;
  static bool prevL = false, prevR = false, prevU = false, prevD = false;
  uint32_t edge = btns & ~prevBtns;
  in.up = sU && !prevU;     in.upDown = sU;
  in.down = sD && !prevD;   in.downDown = sD;
  in.a = edge & (1UL << PAD_B_A);            in.aDown = btns & (1UL << PAD_B_A);
  in.b = edge & (1UL << PAD_B_B);            in.bDown = btns & (1UL << PAD_B_B);
  in.x = edge & (1UL << PAD_B_X);            in.xDown = btns & (1UL << PAD_B_X);
  in.y = edge & (1UL << PAD_B_Y);            in.yDown = btns & (1UL << PAD_B_Y);
  in.start = edge & (1UL << PAD_B_START);    in.startDown = btns & (1UL << PAD_B_START);
  in.select = edge & (1UL << PAD_B_SELECT);  in.selectDown = btns & (1UL << PAD_B_SELECT);
  // While a game reads axisX the stick steers: it must not also press LEFT / RIGHT (pause, re-zero, pour ...)
  bool steer = (uint32_t)(now - axisUsedAt) < 250;
  bool padLeftEdge = sL && !prevL && !steer, padRightEdge = sR && !prevR && !steer;
  bool padLeftHeld = sL && !steer, padRightHeld = sR && !steer;
  prevBtns = btns; prevL = sL; prevR = sR; prevU = sU; prevD = sD;
  if ((in.start && in.selectDown) || (in.select && in.startDown)) in.home = true;   // gamepad home: START + SELECT
  in.ax = sx; in.ay = sy;
  in.padAny = (padOk || stkOk) && (btns || sL || sR || sU || sD || fabsf(sx) > 0.25f || fabsf(sy) > 0.25f);

  in.btnLeft = leftIsBoot ? bootPress : pwrPress;
  in.btnRight = leftIsBoot ? pwrPress : bootPress;
  in.left = in.btnLeft || padLeftEdge;
  in.right = in.btnRight || padRightEdge;
  in.leftDown = (leftIsBoot ? d : pwrHeld) || padLeftHeld;
  in.rightDown = (leftIsBoot ? pwrHeld : d) || padRightHeld;
#if REMOTE_ENABLED
  remoteInject();
#endif
}

// =====================================================================================
// IMU (QMI8658): accelerometer, +-4 g, 125 Hz
// =====================================================================================
bool imuOk = false;
static float tX = 0, tY = 0, zX = 0, zY = 0;
static float rawX = 0, rawY = 0, rawZ = 0;
static bool rawOk = false;

static bool imuInit() {
  if (i2cRead(QMI_ADDR, 0x00) != 0x05) return false;
  i2cWrite(QMI_ADDR, 0x60, 0xB0);                  // soft reset
  delay(20);
  i2cWrite(QMI_ADDR, 0x02, 0x40);                  // CTRL1: auto-increment
  i2cWrite(QMI_ADDR, 0x03, (1 << 4) | 6);          // CTRL2: 4 g, 125 Hz
  i2cWrite(QMI_ADDR, 0x08, 0x01);                  // CTRL7: accel on
  return true;
}
void imuUpdate() {
  static uint32_t last = 0;
  if (!imuOk || millis() - last < 10) return;
  last = millis();
  Wire.beginTransmission(QMI_ADDR);
  Wire.write(0x35);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(QMI_ADDR, (uint8_t)6) != 6) return;
  int16_t ax = Wire.read() | (Wire.read() << 8);
  int16_t ay = Wire.read() | (Wire.read() << 8);
  int16_t az = Wire.read();
  az |= (int16_t)(Wire.read() << 8);
  float x = ax * (4.0f / 32768.0f), y = ay * (4.0f / 32768.0f);
#if TILT_SWAP_XY
  float t = x; x = y; y = t;
#endif
#if TILT_INVERT_X
  x = -x;
#endif
#if TILT_INVERT_Y
  y = -y;
#endif
  float z = az * (4.0f / 32768.0f);
  if (!rawOk) { rawX = x; rawY = y; rawZ = z; rawOk = true; }
  rawX += 0.35f * (x - rawX);
  rawY += 0.35f * (y - rawY);
  rawZ += 0.35f * (z - rawZ);
  tX += 0.35f * (x - tX);
  tY += 0.35f * (y - tY);
}
bool imuRaw(float &x, float &y, float &z) {
#if REMOTE_ENABLED
  if (remTilt) { x = remTX; y = remTY; z = 1.0f; return true; }
#endif
  if (!imuOk || !rawOk) return false;
  x = rawX; y = rawY; z = rawZ;
  return true;
}
void imuZero() { zX = tX; zY = tY; }
float tiltX() {
#if REMOTE_ENABLED
  if (remTilt) return remTX;
#endif
  return tX - zX;
}
float tiltY() {
#if REMOTE_ENABLED
  if (remTilt) return remTY;
#endif
  return tY - zY;
}
bool tiltDir(int &dx, int &dy, float th) {
  float x = axisX(), y = axisY();
  if (fabsf(x) < th && fabsf(y) < th) return false;
  if (fabsf(x) > fabsf(y)) { dx = x > 0 ? 1 : -1; dy = 0; }
  else { dx = 0; dy = y > 0 ? 1 : -1; }
  return true;
}

// =====================================================================================
// RTC (PCF85063): 0x04..0x0A = sec, min, hour, day, weekday, month, year (BCD); bit 7 of seconds = OS flag
// =====================================================================================
static constexpr uint8_t RTC_ADDR = 0x51;
static uint8_t bcdToDec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 15)); }
static uint8_t decToBcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool rtcRead(RtcTime &t) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)7) != 7) return false;
  uint8_t b[7];
  for (int i = 0; i < 7; i++) b[i] = Wire.read();
  t.second = bcdToDec(b[0] & 0x7F);
  t.minute = bcdToDec(b[1] & 0x7F);
  t.hour = bcdToDec(b[2] & 0x3F);
  t.day = bcdToDec(b[3] & 0x3F);
  t.month = bcdToDec(b[5] & 0x1F);
  t.year = (uint16_t)(2000 + bcdToDec(b[6]));
  bool osFlag = (b[0] & 0x80) != 0;
  t.ok = !osFlag && t.second < 60 && t.minute < 60 && t.hour < 24 && t.day >= 1 && t.day <= 31 &&
         t.month >= 1 && t.month <= 12;
  return true;
}
bool rtcSet(uint8_t h, uint8_t m, uint8_t s) {
  int c1 = i2cRead(RTC_ADDR, 0x00);
  if (c1 < 0) return false;
  i2cWrite(RTC_ADDR, 0x00, (uint8_t)(c1 & ~0x22));      // clock running (STOP=0), 24 h mode
  RtcTime cur;
  bool haveDate = rtcRead(cur) && cur.ok;
  uint8_t day = haveDate ? cur.day : 1, month = haveDate ? cur.month : 1;
  uint16_t year = haveDate ? cur.year : 2026;
  static const uint8_t mt[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int y = year - (month < 3 ? 1 : 0);
  uint8_t wd = (uint8_t)((y + y / 4 - y / 100 + y / 400 + mt[month - 1] + day) % 7);
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x04);
  Wire.write((uint8_t)(decToBcd(s) & 0x7F));             // OS flag cleared by writing seconds with bit 7 = 0
  Wire.write(decToBcd(m));
  Wire.write(decToBcd(h));
  Wire.write(decToBcd(day));
  Wire.write(wd);
  Wire.write(decToBcd(month));
  Wire.write(decToBcd((uint8_t)(year % 100)));
  return Wire.endTransmission() == 0;
}

// =====================================================================================
// Sound: ES8311 + amp, square-wave notes from a background task
// =====================================================================================
static constexpr int SR = 16000;
struct Note { uint16_t hz, ms; };
static QueueHandle_t noteQ;
bool soundOn = true, audioOk = false;

static void soundTask(void *) {
  static int16_t buf[2 * 256];
  Note n;
  for (;;) {
    if (xQueueReceive(noteQ, &n, portMAX_DELAY) != pdTRUE) continue;
    int total = SR * n.ms / 1000, done = 0;
    uint32_t phase = 0, step = n.hz ? (uint32_t)((uint64_t)n.hz * 65536 * 65536 / SR) : 0;
    while (done < total) {
      int frames = min(256, total - done);
      for (int i = 0; i < frames; i++) {
        int t = done + i, env = 3000, edge = SR / 400;
        if (t < edge) env = env * t / edge;
        if (total - t < edge) env = env * (total - t) / edge;
        int16_t v = n.hz ? ((phase & 0x80000000u) ? env : -env) : 0;
        phase += step;
        buf[2 * i] = buf[2 * i + 1] = v;
      }
      i2s.write((uint8_t *)buf, frames * 4);
      done += frames;
    }
    if (uxQueueMessagesWaiting(noteQ) == 0) {
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
void sfxClick() { play(1000, 25); }
void sfxGood()  { play(880, 45); play(1320, 70); }
void sfxBad()   { play(440, 110); play(330, 110); play(220, 260); }
void sfxStart() { play(523, 80); play(659, 80); play(784, 120); }
void sfxBest()  { play(784, 90); play(988, 90); play(1175, 90); play(1568, 220); }

void applyVolume(int pct) {
  volumePct = (uint8_t)constrain(pct, 0, 100);
  if (esH) es8311_voice_volume_set(esH, volumePct, NULL);
}

static bool audioInit() {
  pinMode(SPK_EN, OUTPUT);
  digitalWrite(SPK_EN, HIGH);
  i2s.setPins(I2S_BCK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCK);
  if (!i2s.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) return false;
  es8311_handle_t es = es8311_create(0, ES8311_ADDRRES_0);
  if (!es) return false;
  const es8311_clock_config_t clk = {false, false, true, SR * 256, SR};
  if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) return false;
  es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
  esH = es;
  es8311_voice_volume_set(es, volumePct, NULL);
  noteQ = xQueueCreate(24, sizeof(Note));
  xTaskCreatePinnedToCore(soundTask, "snd", 4096, nullptr, 2, nullptr, 0);
  return true;
}

// =====================================================================================
// SD card (never required; every call is safe with no card inserted)
// =====================================================================================
static bool sdMounted = false;
static bool sdPinsSet = false;

bool sdBegin() {
  if (sdMounted) return true;
  if (!sdPinsSet) sdPinsSet = SD_MMC.setPins(2, 1, 3);
  if (!sdPinsSet || !SD_MMC.begin("/sdcard", true) || SD_MMC.cardType() == CARD_NONE) {
    USBSerial.println("sd: no card");
    return false;
  }
  sdMounted = true;
  if (!SD_MMC.exists("/arcade")) SD_MMC.mkdir("/arcade");
  USBSerial.printf("sd: mounted, %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  return true;
}
bool sdPresent() { return sdMounted; }
SdStatus sdInfo() {
  SdStatus s{false, "?", 0, 0, 0, 0};
  if (!sdMounted) return s;
  File root = SD_MMC.open("/");
  if (!root) { SD_MMC.end(); sdMounted = false; return s; }   // card pulled
  s.present = true;
  uint8_t t = SD_MMC.cardType();
  s.type = t == CARD_MMC ? "MMC" : t == CARD_SD ? "SDSC" : t == CARD_SDHC ? "SDHC" : "?";
  s.totalMB = (uint32_t)(SD_MMC.totalBytes() / (1024 * 1024));
  s.usedMB = (uint32_t)(SD_MMC.usedBytes() / (1024 * 1024));
  s.freeMB = s.totalMB > s.usedMB ? s.totalMB - s.usedMB : 0;
  File f = root.openNextFile();
  while (f && s.files < 999) { s.files++; f.close(); f = root.openNextFile(); }
  if (f) f.close();
  root.close();
  return s;
}

// =====================================================================================
void hwBegin() {
  USBSerial.setTxTimeoutMs(0);         // never block when no PC is reading
  USBSerial.begin(115200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  prefs.begin("arcade", false);
  uint8_t bi = prefs.getUChar("bright", 2);
  brightPct = (bi < 4) ? (uint8_t)(25 * (bi + 1)) : 75;
  volumePct = (uint8_t)min(100, (int)prefs.getUChar("vol", 70));
  if (!gfx->begin()) USBSerial.println("display init failed (PSRAM enabled?)");
  applyBrightness(brightPct);
  gfx->fillScreen(C_BG);
  axpInit();
  imuOk = imuInit();
  padSeen = padOk = padProbe();        // seesaw gamepad at 0x50; absent = nothing is ever polled
  stkOk = stkProbe();                  // analog thumbstick on GPIO17/18/38; absent = nothing is ever polled
  audioOk = audioInit();
  sdBegin();
  soundOn = prefs.getBool("sound", true);
  leftIsBoot = prefs.getBool("leftBoot", true);
  for (int i = 0; i < 30; i++) { imuUpdate(); delay(11); }
  imuZero();
}

// =====================================================================================
// USB-serial remote control + screenshot (REMOTE_ENABLED). Line commands in, replies "OK <cmd>" / "ERR <reason>" out.
// Never blocks: bytes are read only when available; SHOT is the one command that streams (with a temporary TX timeout).
// =====================================================================================
#if REMOTE_ENABLED
static bool remSeen = false;
static bool remPendL = false, remPendR = false, remPendTap = false;     // one-frame presses / tap
static uint32_t remHoldL = 0, remHoldR = 0;                             // millis() deadline of a hold, 0 = none
static int remTapX = 0, remTapY = 0;

bool remoteSeen() { return remSeen; }

void remoteInject() {
  uint32_t now = millis();
  if (remHoldL && (int32_t)(now - remHoldL) >= 0) remHoldL = 0;
  if (remHoldR && (int32_t)(now - remHoldR) >= 0) remHoldR = 0;
  if (remPendL) { in.btnLeft = in.left = true; }
  if (remPendR) { in.btnRight = in.right = true; }
  if (remPendL || remHoldL) in.leftDown = true;
  if (remPendR || remHoldR) in.rightDown = true;
  remPendL = remPendR = false;
  if (remPendTap) { in.tap = true; in.tapX = remTapX; in.tapY = remTapY; remPendTap = false; }
}

static void remShot() {
  const uint8_t *fb = (const uint8_t *)frameBuffer();
  if (!fb) { USBSerial.println("ERR no framebuffer"); return; }
  const size_t n = FB_PIXELS * 2;
  USBSerial.setTxTimeoutMs(2000);
  USBSerial.printf("SHOT 368 448 RGB565LE %u\n", (unsigned)n);
  size_t sent = 0;
  bool ok = true;
  while (sent < n && ok) {
    size_t chunk = n - sent < 4096 ? n - sent : 4096;
    size_t w = USBSerial.write(fb + sent, chunk);
    if (w == 0) { ok = false; break; }                  // timed out: PC stopped reading
    sent += w;
    USBSerial.flush();
  }
  if (ok) USBSerial.print("\nEND\n");
  USBSerial.setTxTimeoutMs(0);
  if (ok) USBSerial.println("OK SHOT");
}

static void remExec(char *line) {
  char *save = nullptr;
  char *cmd = strtok_r(line, " \t", &save);
  if (!cmd) return;
  for (char *c = cmd; *c; c++) *c = toupper((unsigned char)*c);
  char *a1 = strtok_r(nullptr, " \t", &save);
  char *a2 = strtok_r(nullptr, " \t", &save);
  if (a1) for (char *c = a1; *c; c++) *c = toupper((unsigned char)*c);
  const char *err = nullptr;
  if (!strcmp(cmd, "SHOT")) { remShot(); return; }
  else if (!strcmp(cmd, "LIST")) { remoteListEntries(USBSerial); }
  else if (!strcmp(cmd, "OPEN")) {
    if (!a1 || !a2 || (a1[0] != 'G' && a1[0] != 'A')) err = "usage OPEN G|A <index>";
    else err = remoteNav(a1[0], atoi(a2));
  }
  else if (!strcmp(cmd, "START")) err = remoteNav('S', 0);
  else if (!strcmp(cmd, "HOME")) err = remoteNav('H', 0);
  else if (!strcmp(cmd, "SETTINGS")) err = remoteNav('T', 0);
  else if (!strcmp(cmd, "APPS")) err = remoteNav('P', 0);
  else if (!strcmp(cmd, "SEL")) err = a1 ? remoteNav('L', atoi(a1)) : "usage SEL <row>";
  else if (!strcmp(cmd, "KEY")) {
    if (!a1) err = "usage KEY L|R|B|LH|RH [ms]";
    else {
      uint32_t ms = a2 ? (uint32_t)constrain(atoi(a2), 1, 10000) : 0;
      uint32_t until = millis() + ms;
      if (!strcmp(a1, "L")) remPendL = true;
      else if (!strcmp(a1, "R")) remPendR = true;
      else if (!strcmp(a1, "B")) remPendL = remPendR = true;
      else if (!strcmp(a1, "LH") && ms) { remPendL = true; remHoldL = until ? until : 1; }
      else if (!strcmp(a1, "RH") && ms) { remPendR = true; remHoldR = until ? until : 1; }
      else err = "usage KEY L|R|B|LH <ms>|RH <ms>";
    }
  }
  else if (!strcmp(cmd, "TAP")) {
    if (!a1 || !a2) err = "usage TAP x y";
    else { remTapX = atoi(a1); remTapY = atoi(a2); remPendTap = true; }
  }
  else if (!strcmp(cmd, "TILT")) {
    if (a1 && !strcmp(a1, "OFF")) remTilt = false;
    else if (a1 && a2) { remTX = atof(a1); remTY = atof(a2); remTilt = true; }
    else err = "usage TILT x y | TILT OFF";
  }
  else if (!strcmp(cmd, "SEED")) err = "no central rng";
  else err = "unknown command";
  if (err) USBSerial.printf("ERR %s\n", err);
  else USBSerial.printf("OK %s\n", cmd);
}

void remotePoll() {
  static char buf[65];
  static uint8_t len = 0;
  static bool overflow = false;
  int avail = USBSerial.available();
  while (avail-- > 0) {
    int c = USBSerial.read();
    if (c < 0) break;
    remSeen = true;
    if (c == '\r') continue;
    if (c == '\n') {
      if (overflow) USBSerial.println("ERR line too long");
      else if (len) { buf[len] = 0; remExec(buf); }
      len = 0; overflow = false;
      continue;
    }
    if (len < 64) buf[len++] = (char)c; else overflow = true;
  }
}
#endif
