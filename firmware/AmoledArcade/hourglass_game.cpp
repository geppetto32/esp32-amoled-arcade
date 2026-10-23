// hourglass_game.cpp - Hourglass: a falling-sand sandbox whose gravity follows the tilt of the board.
// Elements: SAND, WATER, OIL, FIRE, STONE, WOOD (+ steam, made when fire meets water).
//   TILT = gravity (8 directions, snapped with hysteresis; level = keeps the last one)
//   hold LEFT = pour the selected element at the nozzle      RIGHT (short press) = next element
//   hold RIGHT 1 s = clear the glass                         TAP = move the nozzle there and drop a blob
//                                                              (tap the swatch bar at the bottom to pick an element)
//   leave: tap BOOT three times quickly (home), or touch and hold the top-left corner 1.5 s
// MODE: GOALS (8 goals in four glasses: flood, hourglass flip, burn the wood, make steam, light an oil bowl ...)
//       or ZEN (free play; GLASS picks the shape).
//
// Simulation: half-resolution grid GW x GH = 224 x 184 cells (each drawn as 2x2 pixels, written straight into the
// frame buffer with 32-bit stores). One byte per cell: bits 0-2 type, bits 3-6 life (fire/steam), bit 7 "already moved
// this step". The scan runs from the gravity-leading edge, so a falling grain is never moved twice.
// 1 or 2 sim steps per frame (2 when the measured sim time is small). Set HG_DEBUG 1 for a frame-time print
// (sim ms / draw ms, once a second, over USB serial).
#include "engine.h"
#include <math.h>
#include "esp_heap_caps.h"

#define HG_DEBUG 0

static const char *const HG_MODES[] = {"GOALS", "ZEN"};
static const char *const HG_GLASS[] = {"BOX", "HOURGLASS", "SHELVES", "BOWL"};
static Option hgOpts[] = {{"MODE", HG_MODES, 2, "hgMode", 0}, {"GLASS", HG_GLASS, 4, "hgGlass", 1}};

enum HgSt : uint8_t { HG_INTRO, HG_PLAY, HG_DONE, HG_END };
enum : uint8_t { T_EMPTY, T_SAND, T_WATER, T_OIL, T_FIRE, T_STONE, T_WOOD, T_STEAM };
enum : uint8_t { L_BOX, L_GLASS, L_SHELVES, L_BOWL };

static constexpr int GW = 224, GH = 184;
static constexpr int HG_NELEM = 6;
static const uint8_t HG_ELEM[HG_NELEM] = {T_SAND, T_WATER, T_OIL, T_FIRE, T_STONE, T_WOOD};
static const char *const HG_ENAME[HG_NELEM] = {"SAND", "WATER", "OIL", "FIRE", "STONE", "WOOD"};
static constexpr int HG_NGOAL = 8;

struct HgGoal { const char *text; uint8_t layout; bool upperOnly; };
static const HgGoal HG_GOALS[HG_NGOAL] = {
  {"POUR 4000 WATER", L_BOX, false},
  {"FLOOD THE LOWER HALF 45% WITH WATER", L_BOX, false},
  {"FILL THE LOWER BULB WITH 3000 SAND", L_GLASS, true},
  {"FLIP IT: 2700 SAND BACK ON TOP", L_GLASS, true},
  {"BURN THE WOOD (FIRE + TAP)", L_SHELVES, false},
  {"MAKE 800 STEAM (WATER + FIRE)", L_SHELVES, false},
  {"FILL THE BOWL WITH OIL, LIGHT IT", L_BOWL, false},
  {"3000 SAND ON 3000 WATER", L_BOX, false},
};

static uint8_t *hgG = nullptr;
static uint16_t hgPal[8][8];
static HgSt hgSt;
static int hgGx, hgGy, hgSector, hgElem, hgNx, hgNy, hgMode, hgLayout, hgGoal, hgFlips, hgWood0, hgPts;
static uint32_t hgScore, hgBest, hgStAt, hgLastRender, hgGoalAt, hgRightAt, hgLastHiss, hgFrame, hgRng = 2463534242u;
static bool hgUpperOnly, hgRightDownPrev, hgCleared, hgNewBest, hgWasDown;
static uint32_t hgSteamMade;
static float hgSimMs = 3.0f;
struct HgCounts { int t[8], up[8], low[8]; };
static HgCounts hgC;
static float hgProg;
#if HG_DEBUG
static float hgDbgSim, hgDbgDraw;
static int hgDbgN;
static uint32_t hgDbgAt;
#endif

static inline uint32_t hgRnd() { hgRng ^= hgRng << 13; hgRng ^= hgRng >> 17; hgRng ^= hgRng << 5; return hgRng; }
static inline uint8_t hgAt(int x, int y) { return ((unsigned)x < (unsigned)GW && (unsigned)y < (unsigned)GH) ? (hgG[y * GW + x] & 7) : (uint8_t)T_STONE; }

// ---------------- palette ----------------
static void hgBuildPal() {
  for (int s = 0; s < 8; s++) {
    float f = s / 7.0f;
    hgPal[T_EMPTY][s] = C_BG;
    hgPal[T_SAND][s] = lerpColor(rgb(196, 146, 56), rgb(255, 226, 124), f);
    hgPal[T_WATER][s] = lerpColor(rgb(24, 96, 240), rgb(90, 190, 255), f);
    hgPal[T_OIL][s] = lerpColor(rgb(120, 66, 12), rgb(210, 140, 40), f);
    static const uint16_t fire[8] = {rgb(130, 12, 0), rgb(200, 40, 0), rgb(255, 80, 0), rgb(255, 125, 10),
                                     rgb(255, 170, 20), rgb(255, 210, 60), rgb(255, 240, 140), rgb(255, 255, 220)};
    hgPal[T_FIRE][s] = fire[s];
    hgPal[T_STONE][s] = lerpColor(rgb(86, 88, 104), rgb(150, 152, 172), f);
    hgPal[T_WOOD][s] = lerpColor(rgb(86, 48, 20), rgb(150, 90, 40), f);
    hgPal[T_STEAM][s] = lerpColor(rgb(40, 50, 64), rgb(170, 196, 220), f);
  }
}

// ---------------- simulation ----------------
static void hgSwap(int i, int j) { uint8_t a = hgG[i]; hgG[i] = hgG[j] & 0x7F; hgG[j] = a & 0x7F; }
static void hgMove(int i, int j, uint8_t flag) { hgG[j] = (hgG[i] & 0x7F) | flag; hgG[i] = 0; }

static void hgStep() {
  const int gx = hgGx, gy = hgGy;
  int cx[2], cy[2];                                   // the two "slide" candidates next to straight-on
  if (gx != 0 && gy != 0) { cx[0] = gx; cy[0] = 0; cx[1] = 0; cy[1] = gy; }
  else { cx[0] = gx - gy; cy[0] = gy + gx; cx[1] = gx + gy; cy[1] = gy - gx; }    // g +- p with p = (-gy, gx)
  const int px = -gy, py = gx;                        // sideways direction (for diagonal g: the perpendicular diagonal)
  bool vertical = gy != 0;
  int a0, a1, aStep, b0, b1, bStep;                   // outer / inner loop ranges
  uint32_t par = hgFrame & 1;
  if (vertical) {
    a0 = gy > 0 ? GH - 1 : 0; a1 = gy > 0 ? -1 : GH; aStep = gy > 0 ? -1 : 1;
    bool asc = gx != 0 ? gx < 0 : par;
    b0 = asc ? 0 : GW - 1; b1 = asc ? GW : -1; bStep = asc ? 1 : -1;
  } else {
    a0 = gx > 0 ? GW - 1 : 0; a1 = gx > 0 ? -1 : GW; aStep = gx > 0 ? -1 : 1;
    b0 = par ? 0 : GH - 1; b1 = par ? GH : -1; bStep = par ? 1 : -1;
  }
  for (int a = a0; a != a1; a += aStep) {
    for (int b = b0; b != b1; b += bStep) {
      int x = vertical ? b : a, y = vertical ? a : b;
      int i = y * GW + x;
      uint8_t c = hgG[i];
      if (!c) continue;
      if (c & 0x80) { hgG[i] = c & 0x7F; continue; }
      uint8_t t = c & 7;
      switch (t) {
        case T_SAND: {
          int fx = x + gx, fy = y + gy;
          uint8_t ft = hgAt(fx, fy);
          if (ft == T_EMPTY) { hgMove(i, fy * GW + fx, 0); break; }
          if ((ft == T_WATER || ft == T_OIL) && (hgRnd() & 3) != 0) { hgSwap(i, fy * GW + fx); break; }
          int r = hgRnd() & 1;
          for (int k = 0; k < 2; k++) {
            int nx = x + cx[k ^ r], ny = y + cy[k ^ r];
            uint8_t nt = hgAt(nx, ny);
            if (nt == T_EMPTY) { hgMove(i, ny * GW + nx, 0); break; }
            if ((nt == T_WATER || nt == T_OIL) && (hgRnd() & 7) == 0) { hgSwap(i, ny * GW + nx); break; }
          }
          break;
        }
        case T_WATER:
        case T_OIL: {
          int fx = x + gx, fy = y + gy;
          uint8_t ft = hgAt(fx, fy);
          if (ft == T_EMPTY) { hgMove(i, fy * GW + fx, 0); break; }
          if (t == T_WATER && ft == T_OIL && (hgRnd() & 1)) { hgSwap(i, fy * GW + fx); break; }
          int r = hgRnd() & 1;
          bool moved = false;
          for (int k = 0; k < 2 && !moved; k++) {
            int nx = x + cx[k ^ r], ny = y + cy[k ^ r];
            if (hgAt(nx, ny) == T_EMPTY) { hgMove(i, ny * GW + nx, 0); moved = true; }
          }
          if (moved) break;
          int dir = (hgRnd() & 1) ? 1 : -1, reach = t == T_WATER ? 3 : 2;
          for (int s = 0; s < 2 && !moved; s++, dir = -dir) {
            int fxx = x, fyy = y;
            for (int k = 1; k <= reach; k++) {
              int nx = x + px * dir * k, ny = y + py * dir * k;
              if (hgAt(nx, ny) != T_EMPTY) break;
              fxx = nx; fyy = ny;
            }
            if (fxx != x || fyy != y) { hgMove(i, fyy * GW + fxx, 0x80); moved = true; }
          }
          break;
        }
        case T_FIRE: {
          int life = (c >> 3) & 15;
          if ((hgRnd() % 3) == 0) life--;
          if (life <= 0) { hgG[i] = 0; break; }
          c = (uint8_t)(T_FIRE | (life << 3));
          hgG[i] = c;
          static const int8_t nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
          const int8_t *d = nb[hgRnd() & 3];
          int nx = x + d[0], ny = y + d[1];
          uint8_t nt = hgAt(nx, ny);
          int j = ny * GW + nx;
          if (nt == T_OIL) { if ((hgRnd() % 3) == 0) hgG[j] = (uint8_t)(T_FIRE | (15 << 3)); }
          else if (nt == T_WOOD) { if ((hgRnd() & 15) == 0) hgG[j] = (uint8_t)(T_FIRE | (15 << 3)); }
          else if (nt == T_WATER) {
            hgG[i] = 0;
            if (hgRnd() & 1) { hgG[j] = (uint8_t)(T_STEAM | (12 << 3) | 0x80); hgSteamMade++; }
            break;
          }
          if ((hgRnd() & 1) == 0) break;
          int ux = x - gx, uy = y - gy;                      // up = against gravity
          int r = hgRnd() & 1;
          if (hgAt(ux, uy) == T_EMPTY) { hgMove(i, uy * GW + ux, 0x80); break; }
          int sx = ux + px * (r ? 1 : -1), sy = uy + py * (r ? 1 : -1);
          if (hgAt(sx, sy) == T_EMPTY) { hgMove(i, sy * GW + sx, 0x80); break; }
          sx = x + px * (r ? 1 : -1); sy = y + py * (r ? 1 : -1);
          if ((hgRnd() % 3) == 0 && hgAt(sx, sy) == T_EMPTY) hgMove(i, sy * GW + sx, 0x80);
          break;
        }
        case T_STEAM: {
          int life = (c >> 3) & 15;
          if (hgRnd() & 1) life--;
          if (life <= 0) { hgG[i] = 0; break; }
          hgG[i] = (uint8_t)(T_STEAM | (life << 3));
          int ux = x - gx, uy = y - gy, r = hgRnd() & 1;
          if (hgAt(ux, uy) == T_EMPTY && (hgRnd() & 3) != 0) { hgMove(i, uy * GW + ux, 0x80); break; }
          int sx = ux + px * (r ? 1 : -1), sy = uy + py * (r ? 1 : -1);
          if (hgAt(sx, sy) == T_EMPTY) { hgMove(i, sy * GW + sx, 0x80); break; }
          sx = x + px * (r ? 1 : -1); sy = y + py * (r ? 1 : -1);
          if (hgAt(sx, sy) == T_EMPTY) hgMove(i, sy * GW + sx, 0x80);
          break;
        }
        default: break;                                    // stone, wood: static
      }
    }
  }
  hgFrame++;
}

// ---------------- layouts ----------------
static void hgPut(int x, int y, uint8_t v) { if ((unsigned)x < (unsigned)GW && (unsigned)y < (unsigned)GH) hgG[y * GW + x] = v; }
static void hgLoadLayout(int id) {
  memset(hgG, 0, GW * GH);
  hgLayout = id;
  switch (id) {
    case L_GLASS:
      for (int y = 0; y < GH; y++) {
        int hw = (int)fminf(6.0f + 1.1f * fabsf(y - GH / 2.0f), 100.0f);
        for (int x = 0; x < GW; x++) if (abs(x - GW / 2) > hw) hgG[y * GW + x] = T_STONE;
      }
      break;
    case L_SHELVES:
      for (int x = 0; x < 150; x++) for (int t = 0; t < 3; t++) hgPut(x, 52 + x * 15 / 100 + t, T_STONE);
      for (int x = GW - 1; x > 74; x--) for (int t = 0; t < 3; t++) hgPut(x, 102 + (GW - 1 - x) * 15 / 100 + t, T_STONE);
      for (int x = 0; x < 150; x++) for (int t = 0; t < 3; t++) hgPut(x, 150 + x * 10 / 100 + t, T_STONE);
      for (int x = 96; x < 124; x++) for (int y = 52 + x * 15 / 100 - 9; y < 52 + x * 15 / 100; y++) hgPut(x, y, T_WOOD);
      for (int x = 100; x < 126; x++) for (int y = 150 + x * 10 / 100 - 9; y < 150 + x * 10 / 100; y++) hgPut(x, y, T_WOOD);
      for (int x = 150; x < 178; x++) for (int y = 102 + (GW - 1 - x) * 15 / 100 - 9; y < 102 + (GW - 1 - x) * 15 / 100; y++) hgPut(x, y, T_WOOD);
      break;
    case L_BOWL: {
      float cx = GW / 2.0f, cy = GH - 84.0f, R = 90.0f;
      for (int y = (int)cy; y < GH; y++)
        for (int x = 0; x < GW; x++) { float dx = x - cx, dy = y - cy; if (dx * dx + dy * dy > R * R) hgG[y * GW + x] = T_STONE; }
      break;
    }
    default: break;
  }
  hgWood0 = 0;
  for (int i = 0; i < GW * GH; i++) if ((hgG[i] & 7) == T_WOOD) hgWood0++;
  hgSteamMade = 0; hgFlips = 0; hgWasDown = hgGy > 0;
}
// clear everything except the glass (stone that belongs to the layout is rebuilt by reloading it)
static void hgClearGlass() { hgLoadLayout(hgLayout); }

// ---------------- counting / goals ----------------
static void hgCount() {
  memset(&hgC, 0, sizeof hgC);
  for (int y = 0; y < GH; y++) {
    int *dst = y < GH / 2 ? hgC.up : hgC.low;
    const uint8_t *row = hgG + y * GW;
    for (int x = 0; x < GW; x++) dst[row[x] & 7]++;
  }
  for (int k = 0; k < 8; k++) hgC.t[k] = hgC.up[k] + hgC.low[k];
}
static float hgProgress() {
  switch (hgGoal) {
    case 0: return hgC.t[T_WATER] / 4000.0f;
    case 1: return hgC.low[T_WATER] / (0.45f * GW * GH / 2);
    case 2: return hgC.low[T_SAND] / 3000.0f;
    case 3: return hgFlips >= 1 ? hgC.up[T_SAND] / 2700.0f : fminf(0.45f, hgC.low[T_SAND] / 6000.0f);
    case 4: return hgWood0 ? (hgWood0 - hgC.t[T_WOOD]) / (hgWood0 * 0.95f) : 0.0f;
    case 5: return hgSteamMade / 800.0f;
    case 6: return hgC.t[T_OIL] + hgC.t[T_FIRE] >= 2500 && hgC.t[T_FIRE] > 0 ? 0.5f + 0.5f * fminf(1.0f, hgC.t[T_FIRE] / 300.0f)
                                                                              : 0.5f * fminf(1.0f, (hgC.t[T_OIL] + hgC.t[T_FIRE]) / 2500.0f);
    case 7: return fminf(hgC.t[T_SAND], 3000) / 6000.0f + fminf(hgC.t[T_WATER], 3000) / 6000.0f;
  }
  return 0;
}

// ---------------- input -> world ----------------
static const int8_t HG_SEC[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
static void hgGravity() {
  float x = axisX(), y = axisY();
  float mag = sqrtf(x * x + y * y);
  if (!imuOk || mag < 0.12f) return;
  float ang = atan2f(y, x);                                  // 0 = right, +90 deg = toward you (screen down)
  float sec = ang / 0.7853982f;
  float cur = (float)hgSector;
  float d = sec - cur;
  while (d > 4) d -= 8;
  while (d < -4) d += 8;
  if (fabsf(d) > 0.5f + 0.2f) {                              // beyond the sector edge + hysteresis
    int ns = ((int)lroundf(sec) % 8 + 8) % 8;
    if (ns != hgSector) {
      hgSector = ns; hgGx = HG_SEC[ns][0]; hgGy = HG_SEC[ns][1];
      if (hgGy > 0) hgWasDown = true;
      else if (hgGy < 0 && hgWasDown) { hgWasDown = false; hgFlips++; }
    }
  }
}

static void hgPour(int nx, int ny, int radius, int tries, uint8_t t) {
  for (int k = 0; k < tries; k++) {
    int dx = (int)(hgRnd() % (uint32_t)(2 * radius + 1)) - radius, dy = (int)(hgRnd() % (uint32_t)(2 * radius + 1)) - radius;
    if (dx * dx + dy * dy > radius * radius) continue;
    int x = nx + dx, y = ny + dy;
    if ((unsigned)x >= (unsigned)GW || (unsigned)y >= (unsigned)GH) continue;
    if (hgG[y * GW + x] != T_EMPTY) continue;
    hgG[y * GW + x] = t == T_FIRE ? (uint8_t)(T_FIRE | ((11 + (int)(hgRnd() % 5)) << 3)) : t;
  }
}
static void hgClampNozzle() {
  hgNx = constrain(hgNx, 6, GW - 7);
  hgNy = constrain(hgNy, 6, hgUpperOnly ? GH / 2 - 14 : GH - 7);
}

// ---------------- render ----------------
static void hgRenderGrid() {
  uint16_t *fb = frameBuffer();
  static uint16_t fireX[400], fireY[400];
  int nf = 0;
  uint32_t shim = hgFrame >> 2;
  for (int cx = 0; cx < GW; cx++) {
    uint32_t *p0 = (uint32_t *)(fb + (2 * cx) * 368 + 366);
    uint32_t *p1 = (uint32_t *)(fb + (2 * cx + 1) * 368 + 366);
    const uint8_t *col = hgG + cx;
    for (int cy = 0; cy < GH; cy++, p0--, p1--) {
      uint8_t c = col[cy * GW];
      uint16_t color;
      if (!c) color = C_BG;
      else {
        uint8_t t = c & 7;
        if (t == T_FIRE || t == T_STEAM) {
          int life = (c >> 3) & 15;
          int s = t == T_FIRE ? min(7, 1 + life / 2 + (int)(hgRnd() & 1)) : min(7, life / 2);
          color = hgPal[t][s];
          if (t == T_FIRE && nf < 400) { fireX[nf] = (uint16_t)cx; fireY[nf] = (uint16_t)cy; nf++; }
        } else {
          int s = (cx * 7 + cy * 13 + ((cx >> 2) * cy)) & 7;
          if (t == T_WATER) s = (s + (int)shim + cy) & 7;
          else if (t == T_WOOD) s = ((cy >> 1) + (cx >> 3) * 3 + (s >> 2)) & 7;
          color = hgPal[t][s];
        }
      }
      uint32_t v = color | ((uint32_t)color << 16);
      *p0 = v; *p1 = v;
    }
  }
  for (int k = 0; k < nf; k++) {                              // fire glow on empty neighbours
    int cx = fireX[k], cy = fireY[k];
    static const int8_t nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto &d : nb) {
      int nx = cx + d[0], ny = cy + d[1];
      if ((unsigned)nx >= (unsigned)GW || (unsigned)ny >= (unsigned)GH || hgG[ny * GW + nx]) continue;
      uint32_t v = rgb(90, 28, 0) | ((uint32_t)rgb(90, 28, 0) << 16);
      *(uint32_t *)(fb + (2 * nx) * 368 + 366 - 2 * ny) = v;
      *(uint32_t *)(fb + (2 * nx + 1) * 368 + 366 - 2 * ny) = v;
    }
  }
}

static uint16_t hgElemColor(int e) { return hgPal[HG_ELEM[e]][5]; }

static void hgRenderUi(uint32_t now) {
  // nozzle
  int px = hgNx * 2, py = hgNy * 2;
  uint16_t ec = hgElemColor(hgElem);
  gfx->drawCircle(px, py, 11, ec);
  gfx->drawCircle(px, py, 12, lerpColor(ec, C_BG, 0.6f));
  gfx->drawFastHLine(px - 16, py, 7, ec); gfx->drawFastHLine(px + 10, py, 7, ec);
  gfx->drawFastVLine(px, py - 16, 7, ec); gfx->drawFastVLine(px, py + 10, 7, ec);
  // gravity arrow
  int ax = W - 30, ay = 74;
  gfx->fillCircle(ax, ay, 17, rgb(6, 8, 18));
  gfx->drawCircle(ax, ay, 17, 0x4208);
  int tx = ax + hgGx * 11, ty = ay + hgGy * 11;
  gfx->drawLine(ax - hgGx * 8, ay - hgGy * 8, tx, ty, C_ACC);
  gfx->fillCircle(tx, ty, 3, C_ACC);
  // palette bar
  gfx->fillRect(0, H - 26, W, 26, rgb(6, 8, 18));
  for (int e = 0; e < HG_NELEM; e++) {
    int x = 8 + e * 72;
    bool sel = e == hgElem;
    uint16_t c = hgElemColor(e);
    gfx->fillRoundRect(x, H - 23, 66, 20, 6, lerpColor(c, C_BG, sel ? 0.45f : 0.82f));
    gfx->drawRoundRect(x, H - 23, 66, 20, 6, sel ? C_TEXT : lerpColor(c, C_BG, 0.5f));
    text(HG_ENAME[e], x + 33 - (int)strlen(HG_ENAME[e]) * 3, H - 17, 1, sel ? C_TEXT : lerpColor(c, C_TEXT, 0.4f));
  }
  // top bar
  hudBar(hgScore, hgBest, rgb(8, 10, 26), C_TEXT);
  char b[48];
  if (hgMode == 0) {
    gfx->fillRect(0, 32, W, 30, rgb(6, 8, 20));
    snprintf(b, sizeof b, "GOAL %d/%d", hgGoal + 1, HG_NGOAL);
    text(b, 8, 36, 1, C_DIM);
    text(HG_GOALS[hgGoal].text, 8, 46, 1, C_TEXT);
    gfx->drawRect(W - 128, 38, 120, 10, 0x4208);
    gfx->fillRect(W - 126, 40, (int)(116 * constrain(hgProg, 0.0f, 1.0f)), 6, lerpColor(C_ACC, C_GOOD, constrain(hgProg, 0.0f, 1.0f)));
    snprintf(b, sizeof b, "%d s", (int)((now - hgGoalAt) / 1000));
    text(b, W - 128, 50, 1, C_DIM);
  } else {
    text("ZEN", 262, 9, 2, C_CYAN);
  }
  uint16_t acc = C_CYAN;
  if (hgSt == HG_INTRO) {
    gfx->fillRoundRect(30, 96, W - 60, 168, 16, C_PANEL);
    gfx->drawRoundRect(30, 96, W - 60, 168, 16, acc);
    centerText("HOURGLASS", 108, 3, acc);
    centerText("TILT = gravity", 148, 2, C_TEXT);
    centerText("hold LEFT = pour   RIGHT = next element", 176, 1, C_DIM);
    centerText("hold RIGHT 1 s = clear   TAP = drop here", 192, 1, C_DIM);
    centerText("home: tap BOOT 3x, or hold top-left corner", 208, 1, C_DIM);
    if (hgMode == 0) centerText(HG_GOALS[hgGoal].text, 230, 1, C_ACC);
  } else if (hgSt == HG_DONE) {
    char l2[32];
    snprintf(l2, sizeof l2, "+%d", hgPts);
    panelBox("GOAL DONE", l2, hgGoal + 1 < HG_NGOAL ? "next glass coming" : "last one!", C_GOOD);
  } else if (hgSt == HG_END) {
    char l2[32];
    snprintf(l2, sizeof l2, hgNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)hgScore);
    panelBox("ALL GOALS", l2, "RIGHT again   LEFT menu", C_GOOD);
  }
  dirty = true;
}

// ---------------- flow ----------------
static void hgSetupGoal(uint32_t now) {
  hgLoadLayout(HG_GOALS[hgGoal].layout);
  hgUpperOnly = HG_GOALS[hgGoal].upperOnly;
  hgNx = GW / 2; hgNy = hgUpperOnly ? 30 : 14;
  hgGoalAt = now; hgProg = 0;
  hgClampNozzle();
}

static void hgStart() {
  if (!hgG) {
    hgG = (uint8_t *)heap_caps_malloc(GW * GH, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!hgG) hgG = (uint8_t *)ps_malloc(GW * GH);
  }
  if (!hgG) return;
  hgBuildPal();
  hgMode = hgOpts[0].value;
  hgBest = loadBest("hgBest");
  hgScore = 0; hgGoal = 0; hgElem = 0; hgNewBest = false;
  hgGx = 0; hgGy = 1; hgSector = 2;
  hgRightDownPrev = false; hgCleared = false; hgRightAt = 0; hgLastHiss = 0; hgFrame = 0;
  hgRng ^= esp_random();
  if (hgMode == 0) hgSetupGoal(millis());
  else {
    hgLoadLayout(hgOpts[1].value);
    hgUpperOnly = false; hgNx = GW / 2; hgNy = 14; hgGoalAt = millis();
  }
  hgCount();
  hgSt = HG_INTRO; hgStAt = millis(); hgLastRender = 0;
  sfxStart();
}

static bool hgFrameFn(uint32_t now) {
  if (!hgG) return false;
  tuneUpdate(now);
  switch (hgSt) {
    case HG_INTRO:
      if (now - hgStAt >= 2200) { hgSt = HG_PLAY; imuZero(); hgGoalAt = now; }
      break;
    case HG_PLAY: {
      hgGravity();
      // pouring
      bool both = in.leftDown && in.rightDown;
      if (in.leftDown && !both) {
        hgPour(hgNx, hgNy, 5, 44, HG_ELEM[hgElem]);
        if (now - hgLastHiss > 50) { hgLastHiss = now; play((uint16_t)(2200 + (hgRnd() % 1800)), 6); }
      }
      // RIGHT: short = next element, long = clear
      if (in.rightDown && !both) {
        if (!hgRightDownPrev) { hgRightAt = now; hgCleared = false; }
        if (!hgCleared && now - hgRightAt > 1000) { hgCleared = true; hgClearGlass(); sfxBad(); }
      }
      if (!in.rightDown && hgRightDownPrev && !hgCleared && now - hgRightAt < 600) {
        hgElem = (hgElem + 1) % HG_NELEM; sfxClick();
      }
      hgRightDownPrev = in.rightDown;
      if (in.tap) {
        if (in.tapY >= H - 26) { int e = constrain((in.tapX - 8) / 72, 0, HG_NELEM - 1); hgElem = e; sfxClick(); }
        else {
          hgNx = in.tapX / 2; hgNy = in.tapY / 2; hgClampNozzle();
          hgPour(hgNx, hgNy, 8, 220, HG_ELEM[hgElem]);
          play(1800, 14);
        }
      }
      if (in.dragging && in.touchY < H - 26) {            // dragging a finger pours along its path (a drag is never a tap)
        hgNx = in.touchX / 2; hgNy = in.touchY / 2; hgClampNozzle();
        hgPour(hgNx, hgNy, 5, 44, HG_ELEM[hgElem]);
      }
      uint32_t t0 = micros();
      int steps = hgSimMs < 7.0f ? 2 : 1;
      for (int s = 0; s < steps; s++) hgStep();
      float ms = (micros() - t0) / 1000.0f / steps;
      hgSimMs += (ms * steps - hgSimMs) * 0.1f;
#if HG_DEBUG
      hgDbgSim += ms * steps;
#endif
      if ((hgFrame & 7) < (uint32_t)steps) {
        hgCount();
        if (hgMode == 0) {
          hgProg = hgProgress();
          if (hgProg >= 1.0f) {
            int secs = (int)((now - hgGoalAt) / 1000);
            hgPts = 100 + max(0, 120 - secs);
            hgScore += hgPts;
            if (hgScore > hgBest) hgBest = hgScore;
            hgSt = HG_DONE; hgStAt = now;
            tunePlay(TUNE_LEVEL);
          }
        }
      }
      break;
    }
    case HG_DONE:
      if (now - hgStAt > 2300) {
        hgGoal++;
        if (hgGoal >= HG_NGOAL) {
          hgSt = HG_END; hgStAt = now;
          hgNewBest = saveBest("hgBest", hgScore);
          if (hgNewBest) hgBest = hgScore;
          tunePlay(TUNE_WIN);
        } else { hgSetupGoal(now); hgSt = HG_PLAY; imuZero(); hgGoalAt = now; hgGx = 0; hgGy = 1; hgSector = 2; }
      }
      break;
    case HG_END:
      if (now - hgStAt > 900) {
        if (in.right) { hgStart(); return true; }
        if (in.left) return false;
      }
      break;
  }
  if (hgSt != HG_PLAY && hgSt != HG_DONE) hgStep();           // keep the sand alive on banners too (cheap, 1 step)
  if (now - hgLastRender >= 16) {
    hgLastRender = now;
#if HG_DEBUG
    uint32_t d0 = micros();
#endif
    hgRenderGrid();
    hgRenderUi(now);
#if HG_DEBUG
    hgDbgDraw += (micros() - d0) / 1000.0f;
    hgDbgN++;
    if (now - hgDbgAt > 1000) {
      USBSerial.printf("hg: sim %.1f ms draw %.1f ms (avg of %d frames) simEma %.1f\n", hgDbgSim / max(1, hgDbgN), hgDbgDraw / max(1, hgDbgN), hgDbgN, hgSimMs);
      hgDbgSim = hgDbgDraw = 0; hgDbgN = 0; hgDbgAt = now;
    }
#endif
  }
  return true;
}

Game GAME_HOURGLASS = {"HOURGLASS", "tilt = gravity, pour sand/water", 0x5D7F, hgOpts, 2, hgStart, hgFrameFn};
