// app_wood.cpp - WOOD CALC: three woodworking tools in one app.
//   INCH<>MM   inch fraction (1/64 steps) <-> millimetres, both directions.
//   BOARD VOL  board feet (imperial) / cubic metres (metric) from length x width x thickness x quantity.
//   CUT LIST   stock length, saw kerf and up to 6 part lengths with quantities -> boards needed, waste (first-fit decreasing).
// Controls (every number is a "field"; the first field of each tool is TOOL):
//   LEFT = next field    RIGHT = +  (hold = repeat, speeds up)    tilt the board up / down = + / - (hold = repeat)
//   Gamepad: stick up/down = + / -, A = +.   Touch: tap a row to select it, tap its [-] / [+] boxes to change it.
//   Everything is saved (one blob) shortly after a change. Tap BOOT 3 times = back (home).
#include "app_common.h"
#include <math.h>

struct WoodState {
  uint8_t ver, mode;          // mode 0 INCH<>MM, 1 BOARD VOL, 2 CUT LIST
  uint8_t bUnit, cUnit;       // board: 0 imperial 1 metric; cut list: 0 inch (1/16 units) 1 mm
  int32_t fr64;               // inch tool: value in 1/64 inch
  int32_t mm10;               // inch tool: value in 0.1 mm
  int16_t bfL2, bfW4, bfT4;   // imperial: length in half feet, width and thickness in quarter inches
  int16_t mL10, mW, mT;       // metric: length in 0.1 m, width mm, thickness mm
  int16_t qty;
  int32_t stock, kerf;        // cut list, in cUnit ticks (1/16 inch or mm)
  int32_t pLen[6];
  uint8_t pQty[6];
};
static WoodState ws;
static constexpr uint8_t WS_VER = 1;
static constexpr int NPARTS = 6;

static int wField;
static bool wNeed, wSavePending;
static uint32_t wSaveAt, wLastDraw;
static Repeat wRepR, wRepU, wRepD;
static bool wTiltOn, wTiltFirst;
static uint32_t wTiltNext;
static constexpr float W_TILT_ON = 0.30f;   // g of tilt (relative to the position when the app started)
static constexpr float W_TILT_SIGN = 1.0f;  // flip to -1 if tilting up should decrease (UNVERIFIED on hardware)

static int32_t clampI(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static void woodDefaults() {
  memset(&ws, 0, sizeof ws);
  ws.ver = WS_VER; ws.mode = 0; ws.bUnit = 0; ws.cUnit = 1;
  ws.fr64 = 96; ws.mm10 = 381;
  ws.bfL2 = 16; ws.bfW4 = 22; ws.bfT4 = 4;
  ws.mL10 = 24; ws.mW = 140; ws.mT = 25;
  ws.qty = 1;
  ws.stock = 2400; ws.kerf = 3;
  ws.pLen[0] = 600; ws.pLen[1] = 450; ws.pLen[2] = 300;
  ws.pQty[0] = 4; ws.pQty[1] = 2; ws.pQty[2] = 3;
}

// ---- limits ----
static int32_t cutMax() { return ws.cUnit ? 6000 : 3840; }            // longest stock / part in ticks
static int32_t kerfMax() { return ws.cUnit ? 20 : 16; }

static void woodValidate() {
  if (ws.ver != WS_VER || ws.mode > 2 || ws.bUnit > 1 || ws.cUnit > 1) woodDefaults();
  ws.fr64 = clampI(ws.fr64, 0, 64 * 999);
  ws.mm10 = clampI(ws.mm10, 0, 99990);
  ws.bfL2 = (int16_t)clampI(ws.bfL2, 1, 80);
  ws.bfW4 = (int16_t)clampI(ws.bfW4, 1, 192);
  ws.bfT4 = (int16_t)clampI(ws.bfT4, 1, 48);
  ws.mL10 = (int16_t)clampI(ws.mL10, 1, 100);
  ws.mW = (int16_t)clampI(ws.mW, 5, 1000);
  ws.mT = (int16_t)clampI(ws.mT, 1, 300);
  ws.qty = (int16_t)clampI(ws.qty, 1, 999);
  ws.stock = clampI(ws.stock, 10, cutMax());
  ws.kerf = clampI(ws.kerf, 0, kerfMax());
  for (int i = 0; i < NPARTS; i++) { ws.pLen[i] = clampI(ws.pLen[i], 0, cutMax()); if (ws.pQty[i] > 99) ws.pQty[i] = 99; }
}

static void woodSave() {
  prefs.putBytes("wd_state", &ws, sizeof ws);
  wSavePending = false;
}
static void woodChanged(uint32_t now) { wNeed = true; wSavePending = true; wSaveAt = now + 250; }

static int woodNFields() { return ws.mode == 0 ? 4 : ws.mode == 1 ? 6 : 4 + 2 * NPARTS; }
static bool woodToggle(int f) { return f == 0 || (f == 1 && ws.mode != 0); }   // TOOL / UNIT fields flip, no repeat
static int woodFieldRow(int f) { return ws.mode == 2 && f >= 4 ? 4 + (f - 4) / 2 : f; }
static int woodRows() { return ws.mode == 0 ? 4 : ws.mode == 1 ? 6 : 4 + NPARTS; }
static void woodRowGeom(int row, int &y, int &h) {
  if (ws.mode == 0) { y = 38 + row * 40; h = 36; }
  else if (ws.mode == 1) { y = 36 + row * 32; h = 28; }
  else { y = 34 + row * 22; h = 20; }
}

// ---- unit helpers ----
static int32_t toMm(int32_t t16) { return (int32_t)lroundf(t16 * 1.5875f); }
static int32_t toTicks16(int32_t mm) { return (int32_t)lroundf(mm / 1.5875f); }
static int gcdI(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static void fmtFrac(char *out, size_t n, int32_t units, int denom) {      // inches from units/denom
  int whole = units / denom, num = units % denom;
  if (num == 0) { snprintf(out, n, "%d\"", whole); return; }
  int g = gcdI(num, denom);
  if (whole == 0) snprintf(out, n, "%d/%d\"", num / g, denom / g);
  else snprintf(out, n, "%d %d/%d\"", whole, num / g, denom / g);
}
static void fmtCut(char *out, size_t n, int32_t ticks) {
  if (ws.cUnit) snprintf(out, n, "%ld mm", (long)ticks);
  else fmtFrac(out, n, ticks, 16);
}

// ---- adjusting a field ----
static void woodAdjust(int f, int dir, int ramp, uint32_t now) {
  if (ramp < 1) ramp = 1;
  if (f == 0) {
    ws.mode = (uint8_t)((ws.mode + (dir > 0 ? 1 : 2)) % 3);
    wField = 0;
    woodChanged(now);
    return;
  }
  switch (ws.mode) {
    case 0:
      if (f == 1) { ws.fr64 += dir * 64 * ramp; ws.fr64 = clampI(ws.fr64, 0, 64 * 999); ws.mm10 = (int32_t)lroundf(ws.fr64 * 3.96875f); }
      else if (f == 2) { ws.fr64 += dir * ramp; ws.fr64 = clampI(ws.fr64, 0, 64 * 999); ws.mm10 = (int32_t)lroundf(ws.fr64 * 3.96875f); }
      else { ws.mm10 += dir * 10 * ramp; ws.mm10 = clampI(ws.mm10, 0, 99990); ws.fr64 = (int32_t)lroundf(ws.mm10 * 0.2519685f); }
      break;
    case 1:
      if (f == 1) ws.bUnit ^= 1;
      else if (f == 2) { if (ws.bUnit) ws.mL10 = (int16_t)clampI(ws.mL10 + dir * ramp, 1, 100); else ws.bfL2 = (int16_t)clampI(ws.bfL2 + dir * ramp, 1, 80); }
      else if (f == 3) { if (ws.bUnit) ws.mW = (int16_t)clampI(ws.mW + dir * 5 * ramp, 5, 1000); else ws.bfW4 = (int16_t)clampI(ws.bfW4 + dir * ramp, 1, 192); }
      else if (f == 4) { if (ws.bUnit) ws.mT = (int16_t)clampI(ws.mT + dir * ramp, 1, 300); else ws.bfT4 = (int16_t)clampI(ws.bfT4 + dir * ramp, 1, 48); }
      else ws.qty = (int16_t)clampI(ws.qty + dir * ramp, 1, 999);
      break;
    default: {
      int32_t coarse = ws.cUnit ? 10 : 8;
      int32_t step = ramp == 1 ? 1 : ramp == 3 ? coarse : coarse * 4;
      if (f == 1) {                                  // unit switch converts every length
        if (ws.cUnit) { ws.cUnit = 0; ws.stock = toTicks16(ws.stock); ws.kerf = toTicks16(ws.kerf); for (int i = 0; i < NPARTS; i++) ws.pLen[i] = toTicks16(ws.pLen[i]); }
        else { ws.cUnit = 1; ws.stock = toMm(ws.stock); ws.kerf = toMm(ws.kerf); for (int i = 0; i < NPARTS; i++) ws.pLen[i] = toMm(ws.pLen[i]); }
        woodValidate();
      } else if (f == 2) ws.stock = clampI(ws.stock + dir * step, 10, cutMax());
      else if (f == 3) ws.kerf = clampI(ws.kerf + dir * ramp, 0, kerfMax());
      else {
        int p = (f - 4) / 2;
        if (((f - 4) & 1) == 0) ws.pLen[p] = clampI(ws.pLen[p] + dir * step, 0, cutMax());
        else ws.pQty[p] = (uint8_t)clampI((int32_t)ws.pQty[p] + dir * (ramp > 1 ? 2 : 1), 0, 99);
      }
      break;
    }
  }
  woodChanged(now);
}

// ---- cut list (first-fit decreasing); pieces sorted longest first, kerf between neighbouring pieces on a board ----
struct CutRes { int boards, pieces, tooLongPart; int32_t used, lastLeft; bool tooMany; };
static CutRes woodCut() {
  static int32_t pcs[200], bsum[200];
  static uint8_t bn[200];
  CutRes r = {0, 0, 0, 0, 0, false};
  int np = 0;
  for (int p = 0; p < NPARTS && !r.tooMany; p++) {
    if (ws.pLen[p] <= 0) continue;
    for (int q = 0; q < ws.pQty[p]; q++) {
      if (ws.pLen[p] > ws.stock) { if (!r.tooLongPart) r.tooLongPart = p + 1; break; }
      if (np >= 200) { r.tooMany = true; break; }
      pcs[np++] = ws.pLen[p];
    }
  }
  for (int i = 1; i < np; i++) {                     // insertion sort, descending
    int32_t v = pcs[i];
    int j = i - 1;
    while (j >= 0 && pcs[j] < v) { pcs[j + 1] = pcs[j]; j--; }
    pcs[j + 1] = v;
  }
  for (int i = 0; i < np; i++) {
    int b = 0;
    for (; b < r.boards; b++) if (bsum[b] + ws.kerf + pcs[i] <= ws.stock) break;
    if (b == r.boards) { bsum[b] = pcs[i]; bn[b] = 1; r.boards++; }
    else { bsum[b] += ws.kerf + pcs[i]; bn[b]++; }
    r.used += pcs[i];
  }
  r.pieces = np;
  if (r.boards) r.lastLeft = ws.stock - bsum[r.boards - 1];
  return r;
}

// ---- drawing ----
static void woodRow(int row, const char *label, const char *value, bool sel) {
  int y, h;
  woodRowGeom(row, y, h);
  gfx->fillRoundRect(6, y, W - 12, h, 8, sel ? C_PANEL : 0x0841);
  if (sel) gfx->drawRoundRect(6, y, W - 12, h, 8, C_ACC);
  int ty = y + (h - 16) / 2;
  text(label, 14, ty, 2, sel ? C_ACC : C_DIM);
  text(value, W - 100 - textW(value, 2), ty, 2, sel ? C_TEXT : 0xC618);
  if (sel) {
    int by = y + 2, bh = h - 4;
    gfx->drawRoundRect(W - 92, by, 40, bh, 6, C_DIM);
    gfx->drawRoundRect(W - 46, by, 36, bh, 6, C_DIM);
    text("-", W - 76, ty, 2, C_TEXT);
    text("+", W - 32, ty, 2, C_TEXT);
  }
}

static const char *const W_MODE[3] = {"INCH<>MM", "BOARD VOL", "CUT LIST"};

static void woodDraw() {
  gfx->fillScreen(C_BG);
  appHeader("WOOD CALC", W_MODE[ws.mode], C_ACC);
  char a[40], b[64];
  int nf = woodNFields();
  if (wField >= nf) wField = 0;

  if (ws.mode == 0) {
    woodRow(0, "TOOL", W_MODE[0], wField == 0);
    snprintf(a, sizeof a, "%ld\"", (long)(ws.fr64 / 64));
    woodRow(1, "INCHES", a, wField == 1);
    snprintf(a, sizeof a, "%ld/64", (long)(ws.fr64 % 64));
    woodRow(2, "1/64 STEPS", a, wField == 2);
    snprintf(a, sizeof a, "%ld.%ld mm", (long)(ws.mm10 / 10), (long)(ws.mm10 % 10));
    woodRow(3, "MILLIMETRES", a, wField == 3);
    fmtFrac(a, sizeof a, ws.fr64, 64);
    centerText(a, 206, 4, C_ACC);
    snprintf(b, sizeof b, "%ld.%ld mm", (long)(ws.mm10 / 10), (long)(ws.mm10 % 10));
    centerText(b, 250, 4, C_GOOD);
    snprintf(b, sizeof b, "%.4f in", ws.fr64 / 64.0f);
    centerText(b, 300, 2, C_TEXT);
    int32_t n64 = (int32_t)lroundf(ws.mm10 * 0.2519685f);
    float err = n64 * 0.396875f - ws.mm10 / 10.0f;
    char f2[24];
    fmtFrac(f2, sizeof f2, n64, 64);
    snprintf(b, sizeof b, "%.1f mm = %s (%+.2f mm)", ws.mm10 / 10.0f, f2, err);
    centerText(b, 324, 1, C_DIM);
  } else if (ws.mode == 1) {
    bool m = ws.bUnit;
    woodRow(0, "TOOL", W_MODE[1], wField == 0);
    woodRow(1, "UNITS", m ? "METRIC" : "IMPERIAL", wField == 1);
    if (m) {
      snprintf(a, sizeof a, "%d.%d m", ws.mL10 / 10, ws.mL10 % 10);
      woodRow(2, "LENGTH", a, wField == 2);
      snprintf(a, sizeof a, "%d mm", ws.mW);
      woodRow(3, "WIDTH", a, wField == 3);
      snprintf(a, sizeof a, "%d mm", ws.mT);
      woodRow(4, "THICKNESS", a, wField == 4);
    } else {
      snprintf(a, sizeof a, "%d.%d ft", ws.bfL2 / 2, (ws.bfL2 % 2) * 5);
      woodRow(2, "LENGTH", a, wField == 2);
      fmtFrac(a, sizeof a, ws.bfW4, 4);
      woodRow(3, "WIDTH", a, wField == 3);
      fmtFrac(a, sizeof a, ws.bfT4, 4);
      woodRow(4, "THICKNESS", a, wField == 4);
    }
    snprintf(a, sizeof a, "%d", ws.qty);
    woodRow(5, "QUANTITY", a, wField == 5);
    float perM3, perBF;
    if (m) { perM3 = (ws.mL10 / 10.0f) * (ws.mW / 1000.0f) * (ws.mT / 1000.0f); perBF = perM3 / 0.00235974f; }
    else {
      perBF = (ws.bfT4 / 4.0f) * (ws.bfW4 / 4.0f) * (ws.bfL2 / 2.0f) / 12.0f;
      perM3 = perBF * 0.00235974f;
    }
    float totM3 = perM3 * ws.qty, totBF = perBF * ws.qty;
    if (m) snprintf(b, sizeof b, "%.4f m3", totM3); else snprintf(b, sizeof b, "%.2f BF", totBF);
    centerText(b, 244, 4, C_GOOD);
    if (m) snprintf(b, sizeof b, "= %.2f board feet", totBF); else snprintf(b, sizeof b, "= %.4f m3", totM3);
    centerText(b, 288, 2, C_TEXT);
    if (m) snprintf(b, sizeof b, "per board %.4f m3", perM3); else snprintf(b, sizeof b, "per board %.2f BF", perBF);
    centerText(b, 312, 2, C_DIM);
  } else {
    woodRow(0, "TOOL", W_MODE[2], wField == 0);
    woodRow(1, "UNIT", ws.cUnit ? "MM" : "INCH", wField == 1);
    fmtCut(a, sizeof a, ws.stock);
    woodRow(2, "STOCK LENGTH", a, wField == 2);
    fmtCut(a, sizeof a, ws.kerf);
    woodRow(3, "SAW KERF", a, wField == 3);
    for (int p = 0; p < NPARTS; p++) {
      int row = 4 + p, y, h;
      woodRowGeom(row, y, h);
      bool sel = woodFieldRow(wField) == row;
      bool selLen = sel && ((wField - 4) & 1) == 0, selQty = sel && ((wField - 4) & 1) == 1;
      gfx->fillRoundRect(6, y, W - 12, h, 8, sel ? C_PANEL : 0x0841);
      if (sel) gfx->drawRoundRect(6, y, W - 12, h, 8, C_ACC);
      int ty = y + (h - 16) / 2;
      snprintf(a, sizeof a, "P%d", p + 1);
      text(a, 14, ty, 2, sel ? C_ACC : C_DIM);
      if (ws.pLen[p] > 0) fmtCut(a, sizeof a, ws.pLen[p]); else snprintf(a, sizeof a, "-");
      text(a, 54, ty, 2, selLen ? C_TEXT : (sel ? 0xC618 : 0x9CD3));
      snprintf(a, sizeof a, "x %d", ws.pQty[p]);
      text(a, 236, ty, 2, selQty ? C_TEXT : (sel ? 0xC618 : 0x9CD3));
      if (selLen) gfx->drawRoundRect(48, y + 1, 168, h - 2, 4, C_CYAN);
      if (selQty) gfx->drawRoundRect(230, y + 1, 90, h - 2, 4, C_CYAN);
      if (sel) {
        gfx->drawRoundRect(W - 92, y + 2, 40, h - 4, 6, C_DIM);
        gfx->drawRoundRect(W - 46, y + 2, 36, h - 4, 6, C_DIM);
        text("-", W - 76, ty, 2, C_TEXT);
        text("+", W - 32, ty, 2, C_TEXT);
      }
    }
    CutRes r = woodCut();
    if (r.tooLongPart) {
      snprintf(b, sizeof b, "PART P%d IS LONGER THAN STOCK", r.tooLongPart);
      centerText(b, 262, 2, C_BAD);
    }
    if (r.tooMany) centerText("TOO MANY PIECES (max 200)", 284, 2, C_BAD);
    if (r.pieces == 0) {
      if (!r.tooLongPart) centerText("set part lengths and quantities", 290, 2, C_DIM);
    } else {
      snprintf(b, sizeof b, "BOARDS %d   PIECES %d", r.boards, r.pieces);
      centerText(b, r.tooLongPart ? 286 : 262, 3, C_GOOD);
      float tot = (float)r.boards * ws.stock;
      snprintf(b, sizeof b, "WASTE %.1f %%", tot > 0 ? 100.0f * (tot - r.used) / tot : 0.0f);
      centerText(b, r.tooLongPart ? 316 : 296, 2, C_ACC);
      char f2[24];
      fmtCut(f2, sizeof f2, r.lastLeft);
      snprintf(b, sizeof b, "last board: %s left", f2);
      centerText(b, r.tooLongPart ? 336 : 318, 2, C_DIM);
    }
  }
  centerText("LEFT next  RIGHT +  tilt/stick -/+  tap boxes", H - 10, 1, C_DIM);
}

// ---- app entry points ----
static void woodStart() {
  if (prefs.getBytesLength("wd_state") == sizeof ws) prefs.getBytes("wd_state", &ws, sizeof ws);
  else woodDefaults();
  woodValidate();
  wField = 0;
  wNeed = true; wSavePending = false; wLastDraw = 0; wTiltOn = false;
  imuZero();                                         // tilt is relative to how you hold the board now
}

static void woodTap(uint32_t now) {
  int rows = woodRows();
  for (int row = 0; row < rows; row++) {
    int y, h;
    woodRowGeom(row, y, h);
    if (in.tapY < y - 2 || in.tapY >= y + h + 2) continue;
    int f;
    if (ws.mode == 2 && row >= 4) f = 4 + (row - 4) * 2 + (in.tapX >= 226 ? 1 : 0);
    else f = row;
    if (f == wField && in.tapX >= W - 92) woodAdjust(f, in.tapX < W - 48 ? -1 : 1, 1, now);
    else if (f != wField) { wField = f; wNeed = true; }
    else if (in.tapX < W - 92 && woodToggle(f)) woodAdjust(f, 1, 1, now);   // tap the TOOL / UNIT row itself = flip
    return;
  }
}

static bool woodFrame(uint32_t now) {
  if (in.b) { if (wSavePending) woodSave(); return false; }
  int nf = woodNFields();
  if (in.left) { wField = (wField + 1) % nf; sfxClick(); wNeed = true; }

  if (woodToggle(wField)) {
    if (in.right || in.a || in.up) woodAdjust(wField, 1, 1, now);
    if (in.down) woodAdjust(wField, -1, 1, now);
  } else {
    int sr = repeatStep(wRepR, in.right, in.rightDown, now, true);
    int su = repeatStep(wRepU, in.up, in.upDown, now, true);
    int sd = repeatStep(wRepD, in.down, in.downDown, now, true);
    if (sr) woodAdjust(wField, 1, sr, now);
    else if (in.a) woodAdjust(wField, 1, 1, now);
    if (su) woodAdjust(wField, 1, su, now);
    if (sd) woodAdjust(wField, -1, sd, now);
    float ty = tiltY() * W_TILT_SIGN;
    float aty = fabsf(ty);
    if (aty > W_TILT_ON) {
      if (!wTiltOn) { wTiltOn = true; wTiltFirst = true; wTiltNext = now; }
      if ((int32_t)(now - wTiltNext) >= 0) {
        int ramp = aty > 0.65f ? 5 : aty > 0.48f ? 3 : 1;
        woodAdjust(wField, ty < 0 ? 1 : -1, ramp, now);
        wTiltNext = now + (wTiltFirst ? 380 : aty > 0.65f ? 60 : 130);
        wTiltFirst = false;
      }
    } else wTiltOn = false;
  }
  if (in.tap) woodTap(now);

  if (wSavePending && now >= wSaveAt) woodSave();
  if (wNeed && now - wLastDraw >= 30) { wLastDraw = now; wNeed = false; woodDraw(); }
  return true;
}

Game APP_WOOD = {"WOOD CALC", "inch/mm, board feet, cut list", 0xFD60, nullptr, 0, woodStart, woodFrame};
