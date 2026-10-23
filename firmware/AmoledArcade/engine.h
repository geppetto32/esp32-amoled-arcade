// engine.h - shared helpers for the newer games (Cut to Length, Missile Command, Lander, 2048, Hourglass).
// Existing games do not use these.
//
//  Particles   : fixed pool of PT_MAX. ptBurst()/ptSpray() spawn, ptUpdate(dt) moves, ptDraw() draws with a colour fade
//                (c0 -> c1 over the lifetime, size shrinks). RGB565 has no alpha: fading to black = additive glow on AMOLED.
//  Tunes       : tunePlay(notes) queues a note sequence through play() in time (non-blocking). Call tuneUpdate(now)
//                once per frame. A new tunePlay() replaces the running one. play() has a 24-slot queue, so a tune
//                feeds one note at a time when it is due.
//  Tilt        : Flick    - flick detection (fires on crossing a threshold, debounced, slow high-pass baseline so a held
//                           tilt does not fire). update() returns DIR_*.
//                SnapDir  - 4-way snap of the tilt vector with hysteresis (enter/leave thresholds, axis bias).
//                TiltCursor - proportional cursor (deadzone, gain, smoothing, clamped).
//  Framebuffer : fbIndex(x,y) maps a landscape pixel to raw canvas memory (the canvas is portrait, rotation 1):
//                idx = x*368 + (367 - y). A logical column is contiguous; (y+1,y) pairs are 32-bit aligned for even y.
#pragma once
#include "game.h"


// ---------------- framebuffer ----------------
static inline int fbIndex(int x, int y) { return x * 368 + (367 - y); }

// ---------------- particles ----------------
constexpr int PT_MAX = 256;
void ptClear();
// n particles flying out in all directions, speed px/s (randomised 30..100 %), life seconds (randomised 60..100 %).
void ptBurst(float x, float y, int n, float speed, uint16_t c0, uint16_t c1, float life, float grav = 0, float size = 2);
// n particles in a cone: angle in radians (0 = right, PI/2 = down), spread = half-angle.
void ptSpray(float x, float y, float angle, float spread, int n, float speed, uint16_t c0, uint16_t c1, float life,
             float grav = 0, float size = 2);
void ptUpdate(float dt);
void ptDraw();
int ptCount();

// ---------------- tunes ----------------
struct TuneNote { uint16_t hz, ms; };            // hz 0 = rest
void tunePlayRaw(const TuneNote *notes, uint8_t n);
template <size_t N> static inline void tunePlay(const TuneNote (&notes)[N]) { tunePlayRaw(notes, (uint8_t)N); }
void tuneUpdate(uint32_t now);
bool tuneBusy();
void tuneStop();
extern const TuneNote TUNE_LEVEL[6], TUNE_WIN[8], TUNE_LOSE[5];

// ---------------- tilt helpers ----------------
enum : uint8_t { DIR_NONE = 0, DIR_L, DIR_R, DIR_U, DIR_D };   // R = tilted right, D = tilted toward you / screen bottom

struct Flick {
  float bx = 0, by = 0;          // slow baseline (follows a held tilt)
  bool armed = false;
  uint32_t last = 0, lastFire = 0;
  void reset(uint32_t now);      // call when play starts (after imuZero)
  // th: change in g (relative to the baseline) that counts as a flick. Returns DIR_*.
  uint8_t update(uint32_t now, float th = 0.38f, uint32_t minGapMs = 260);
};

struct SnapDir {
  uint8_t cur = DIR_NONE;
  // on/off: enter / leave threshold in g; bias: the winning axis must beat the other by this factor to switch axis.
  uint8_t update(float x, float y, float on = 0.24f, float off = 0.13f, float bias = 1.3f);
};

struct TiltCursor {
  float x = 0, y = 0;
  void set(float px, float py) { x = px; y = py; }
  // target = centre + shaped(tilt) * gain, smoothed with rate k (1/s), clamped to the box.
  void update(float dt, float cx, float cy, float gainX, float gainY, float dead, float k,
              float minX, float maxX, float minY, float maxY);
};
