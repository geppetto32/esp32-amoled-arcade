// hw.h - hardware layer for the Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1) arcade.
// Screen (landscape canvas), two buttons as LEFT/RIGHT, tilt (QMI8658), sound (ES8311), battery (AXP2101).
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>
#include <HWCDC.h>
extern HWCDC USBSerial;                      // debug output over USB

// ---------------- screen ----------------
constexpr int W = 448, H = 368;              // landscape, buttons on the top edge
extern Arduino_Canvas *gfx;                  // draw here, then present()
extern Preferences prefs;
extern bool dirty;                           // set by anything that draws
void hwBegin();
void present();                              // send the frame if dirty; returns quickly otherwise
uint16_t *frameBuffer();                     // raw canvas memory (physical portrait 368x448, RGB565)
constexpr size_t FB_PIXELS = 368 * 448;
void centerText(const char *s, int y, int size, uint16_t col);
void text(const char *s, int x, int y, int size, uint16_t col);
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint16_t lerpColor(uint16_t a, uint16_t b, float t);   // t 0..1
extern uint8_t brightPct;                    // user brightness 25/50/75/100 (applied at boot)
void applyBrightness(int pct);               // 1..100 %, does not touch brightPct (idle dim uses it)

// Common palette
constexpr uint16_t C_BG = 0x0000, C_PANEL = 0x10A2, C_TEXT = 0xFFFF, C_DIM = 0x8410, C_ACC = 0xFEA0,
                   C_GOOD = 0x87F0, C_BAD = 0xF8A0, C_CYAN = 0x07FF;

// ---------------- buttons ----------------
// LEFT / RIGHT are the two top buttons, whichever physical one (BOOT or PWR) the user chose in setup.
struct Input {
  bool left, right;          // pressed this frame (edge): hardware button OR gamepad stick left/right
  bool leftDown, rightDown;  // currently held (same merge)
  bool home;                 // HOME gesture this frame: BOOT pressed 3 times within 0.8 s, or gamepad START+SELECT (the launcher leaves the game; games never see it)
  bool tap;                  // screen tapped this frame (short touch, finger barely moved; never after a drag)
  int tapX, tapY;            // tap position in landscape picture coordinates
  bool touching;             // finger on the screen now
  // ---- touch drag ----
  bool dragging;             // finger is down and has moved past the tap slop (DRAG_SLOP px)
  int dragDX, dragDY;        // finger movement since the previous frame (landscape px; +y = down)
  int dragTX, dragTY;        // total finger travel since the touch began (valid while dragging and on dragEnd: swipes)
  bool dragEnd;              // finger lifted after a drag, this frame only
  float flingVX, flingVY;    // px/s at the lift (valid on dragEnd; 0 when the finger rested before lifting)
  int touchX, touchY;        // current finger position while touching
  // ---- gamepad (Adafruit seesaw mini I2C gamepad); all false / 0 when absent ----
  bool btnLeft, btnRight;    // hardware buttons only (edges), without the gamepad stick
  bool up, down, a, b, x, y, start, select;                  // edges this frame (up/down = stick)
  bool upDown, downDown, aDown, bDown, xDown, yDown, startDown, selectDown;   // held
  float ax, ay;              // analog stick -1..1 (+x = right, +y = down), 0 inside the dead zone
  bool padAny;               // any pad activity right now (for idle-dim wake)
};
constexpr int DRAG_SLOP = 18;                // px of finger travel before a touch becomes a drag
extern Input in;
extern bool leftIsBoot;
void inputUpdate();                          // call once per loop
const char *holdButtonName();                // "LEFT" or "RIGHT" (the BOOT side)
bool gamepadConnected();                     // seesaw I2C gamepad answered (boot or later) and is being polled
bool stickPresent();                         // analog thumbstick on GPIO17/18 detected at boot
void stickRaw(int &rx, int &ry);             // last raw ADC counts (0..4095) of the analog stick, for the test screen
bool stickPressed();                         // analog stick push switch (GPIO38) held now
uint32_t gamepadProduct();                   // seesaw product id read at probe (5743 expected), 0 = not read

// ---------------- tilt ----------------
extern bool imuOk;
void imuUpdate();                            // call often (rate-limited inside)
void imuZero();                              // current position = level
float tiltX();                               // g, + = tilted to the right (relative to zero)
float tiltY();                               // g, + = tilted toward you / screen bottom
bool tiltDir(int &dx, int &dy, float threshold = 0.22f);   // dominant axis of axisX/axisY, false when level
// Steering axes for games: the analog stick / gamepad stick (as g: full deflection = 0.7 g) while it is deflected,
// otherwise the tilt. Same signs as tiltX / tiltY. While a game reads these, stick left/right do NOT also count as the
// LEFT / RIGHT buttons (they would pause / re-zero tilt games). Only axisX() claims the stick for steering.
float axisX();
float axisY();
// Absolute (never zeroed) filtered accelerometer vector in g, same axes as tiltX/tiltY; z = out of the screen.
// Lying flat face-up: x,y ~ 0, |z| ~ 1. Returns false when there is no IMU / no sample yet.
bool imuRaw(float &x, float &y, float &z);

// ---------------- RTC (PCF85063 at I2C 0x51, plain register access) ----------------
struct RtcTime {
  uint8_t hour, minute, second, day, month;
  uint16_t year;
  bool ok;                   // false = oscillator-stop flag set (never set / power was lost) or invalid fields
};
bool rtcRead(RtcTime &t);    // false = chip does not answer
bool rtcSet(uint8_t h, uint8_t m, uint8_t s);   // sets the time, clears the invalid flag; keeps the date (or 2026-01-01)

// ---------------- sound ----------------
extern bool soundOn, audioOk;
void play(uint16_t hz, uint16_t ms);         // queued, non-blocking; hz 0 = rest
void sfxClick();
void sfxGood();
void sfxBad();
void sfxStart();
void sfxBest();
extern uint8_t volumePct;                    // 0..100 (ES8311 voice volume, applied at boot)
void applyVolume(int pct);                   // sets the codec volume and volumePct

// ---------------- SD card (SDMMC 1-bit: CLK=2 CMD=1 D0=3) ----------------
struct SdStatus {
  bool present;              // card mounted and readable
  const char *type;          // "SDSC", "SDHC", "MMC", "?"
  uint32_t totalMB, usedMB, freeMB;
  int files;                 // entries in the root folder (max 999)
};
bool sdBegin();              // mount (safe with no card; returns false). Creates /arcade when missing.
bool sdPresent();
SdStatus sdInfo();

// ---------------- power ----------------
int battery();                               // %, -1 on error
bool charging();
int batteryMv();                             // battery voltage in mV, -1 on error

// ---------------- USB-serial remote control + screenshot (see tools/capture.py) ----------------
#define REMOTE_ENABLED 1                     // 0 = feature compiled out completely
#if REMOTE_ENABLED
void remotePoll();                           // call once per loop: reads command lines from USBSerial (non-blocking)
bool remoteSeen();                           // true once any command byte arrived (the 1 Hz debug log then stays quiet)
void remoteInject();                         // called at the end of inputUpdate(): applies injected keys / taps
// Launcher hooks, implemented in AmoledArcade.ino. op: 'H' home, 'G' open game idx, 'A' open app idx, 'S' start,
// 'T' settings, 'P' apps list, 'L' select row idx in the current list. Returns nullptr = ok, else an error text.
const char *remoteNav(char op, int idx);
void remoteListEntries(Print &out);          // prints the "G i name" / "A i name" lines (in AmoledArcade.ino)
#endif
