// engine.cpp - see engine.h
#include "engine.h"
#include <math.h>

// ---------------- particles ----------------
struct Particle { float x, y, vx, vy, age, life, grav, size; uint16_t c0, c1; bool on; };
static Particle pts[PT_MAX];
static int ptNext = 0;

static float frand() { return (esp_random() & 0xFFFF) / 65535.0f; }

void ptClear() { for (auto &p : pts) p.on = false; }
static Particle &ptAlloc() {
  for (int i = 0; i < PT_MAX; i++) {                 // first free slot after the last one used, else recycle
    int k = (ptNext + i) % PT_MAX;
    if (!pts[k].on) { ptNext = (k + 1) % PT_MAX; return pts[k]; }
  }
  Particle &p = pts[ptNext];
  ptNext = (ptNext + 1) % PT_MAX;
  return p;
}
void ptSpray(float x, float y, float angle, float spread, int n, float speed, uint16_t c0, uint16_t c1, float life,
             float grav, float size) {
  for (int i = 0; i < n; i++) {
    Particle &p = ptAlloc();
    float a = angle + (frand() * 2 - 1) * spread, s = speed * (0.3f + 0.7f * frand());
    p.x = x; p.y = y; p.vx = cosf(a) * s; p.vy = sinf(a) * s;
    p.age = 0; p.life = life * (0.6f + 0.4f * frand());
    p.grav = grav; p.size = size; p.c0 = c0; p.c1 = c1; p.on = true;
  }
}
void ptBurst(float x, float y, int n, float speed, uint16_t c0, uint16_t c1, float life, float grav, float size) {
  ptSpray(x, y, 0, (float)M_PI, n, speed, c0, c1, life, grav, size);
}
void ptUpdate(float dt) {
  for (auto &p : pts) {
    if (!p.on) continue;
    p.age += dt;
    if (p.age >= p.life) { p.on = false; continue; }
    p.vy += p.grav * dt;
    p.x += p.vx * dt; p.y += p.vy * dt;
  }
}
void ptDraw() {
  for (auto &p : pts) {
    if (!p.on) continue;
    float t = p.age / p.life;
    int xi = (int)p.x, yi = (int)p.y;
    if (xi < -4 || xi >= W + 4 || yi < -4 || yi >= H + 4) continue;
    uint16_t c = lerpColor(p.c0, p.c1, t);
    float s = p.size * (1.0f - 0.6f * t);
    if (s < 1.5f) gfx->drawPixel(xi, yi, c);
    else { int si = (int)(s + 0.5f); gfx->fillRect(xi - si / 2, yi - si / 2, si, si, c); }
  }
  dirty = true;
}
int ptCount() { int n = 0; for (auto &p : pts) if (p.on) n++; return n; }

// ---------------- tunes ----------------
const TuneNote TUNE_LEVEL[6] = {{523, 70}, {659, 70}, {784, 70}, {1047, 140}, {0, 30}, {1319, 190}};
const TuneNote TUNE_WIN[8] = {{784, 90}, {988, 90}, {1175, 90}, {1568, 120}, {0, 40}, {1175, 90}, {1568, 90}, {2093, 320}};
const TuneNote TUNE_LOSE[5] = {{392, 140}, {370, 140}, {349, 140}, {330, 300}, {262, 380}};

static const TuneNote *tuneNotes = nullptr;
static uint8_t tuneN = 0, tuneI = 0;
static uint32_t tuneNext = 0;
static bool tuneRun = false;

void tunePlayRaw(const TuneNote *notes, uint8_t n) {
  tuneNotes = notes; tuneN = n; tuneI = 0; tuneNext = millis(); tuneRun = n > 0;
}
void tuneStop() { tuneRun = false; }
bool tuneBusy() { return tuneRun; }
void tuneUpdate(uint32_t now) {
  if (!tuneRun) return;
  while (tuneRun && (int32_t)(now - tuneNext) >= 0) {
    if (tuneI >= tuneN) { tuneRun = false; break; }
    const TuneNote &nt = tuneNotes[tuneI++];
    play(nt.hz, nt.ms);
    tuneNext += nt.ms;
    if ((int32_t)(now - tuneNext) > 200) tuneNext = now;   // fell far behind (long frame): do not burst
  }
}

// ---------------- tilt helpers ----------------
void Flick::reset(uint32_t now) { bx = axisX(); by = axisY(); armed = false; last = now; lastFire = now; }
uint8_t Flick::update(uint32_t now, float th, uint32_t minGapMs) {
  float dt = fminf((now - last) / 1000.0f, 0.1f);
  last = now;
  float x = axisX(), y = axisY();
  float k = 1.0f - expf(-dt / 0.9f);
  bx += (x - bx) * k; by += (y - by) * k;
  float dx = x - bx, dy = y - by;
  if (!armed) {
    if (fabsf(dx) < th * 0.45f && fabsf(dy) < th * 0.45f && now - lastFire >= minGapMs) armed = true;
    return DIR_NONE;
  }
  if (fabsf(dx) < th && fabsf(dy) < th) return DIR_NONE;
  armed = false; lastFire = now;
  if (fabsf(dx) >= fabsf(dy)) return dx > 0 ? DIR_R : DIR_L;
  return dy > 0 ? DIR_D : DIR_U;
}

uint8_t SnapDir::update(float x, float y, float on, float off, float bias) {
  float ax = fabsf(x), ay = fabsf(y);
  if (cur != DIR_NONE) {
    bool horiz = cur == DIR_L || cur == DIR_R;
    float mainV = horiz ? x : y, mag = horiz ? ax : ay, other = horiz ? ay : ax;
    bool sign = horiz ? cur == DIR_R : cur == DIR_D;
    bool still = mag > off && (mainV > 0) == sign;
    if (still && other < mag * bias) return cur;       // stay unless the other axis clearly wins
    if (!still && other <= off) { cur = DIR_NONE; return cur; }
  }
  if (ax < on && ay < on) { cur = DIR_NONE; return cur; }
  if (ax >= ay) cur = x > 0 ? DIR_R : DIR_L;
  else cur = y > 0 ? DIR_D : DIR_U;
  return cur;
}

void TiltCursor::update(float dt, float cx, float cy, float gainX, float gainY, float dead, float k,
                        float minX, float maxX, float minY, float maxY) {
  auto shape = [dead](float t) { return fabsf(t) < dead ? 0.0f : (t > 0 ? t - dead : t + dead); };
  float tx = cx + shape(axisX()) * gainX, ty = cy + shape(axisY()) * gainY;
  float a = 1.0f - expf(-dt * k);
  x += (constrain(tx, minX, maxX) - x) * a;
  y += (constrain(ty, minY, maxY) - y) * a;
}
