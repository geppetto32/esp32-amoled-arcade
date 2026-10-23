// app_metro.cpp - METRONOME: 30-240 BPM, 2/3/4/6 beats per bar (option BEATS), accent on beat 1.
//   LEFT / RIGHT = -1 / +1 BPM (hold = fast).  Touch: pulse = start/stop, bottom boxes = -10 / TAP tempo / +10.
//   Tap BOOT 3 times quickly = leave (home), so quick repeated taps can leave the app by accident; use the -10 / +10 boxes for big jumps.
#include "app_common.h"

static const char *const MT_BEATS[] = {"2", "3", "4", "6"};
static const uint8_t MT_BEAT_N[4] = {2, 3, 4, 6};
static Option mtOpts[] = {{"BEATS", MT_BEATS, 4, "mt_beats", 2}};

static int mtBpm = 100;
static bool mtRun, mtPrefPending, mtNeedDraw;
static int mtBeatIdx, mtCurBeat;            // index of the next beat to sound / of the one that sounded last
static double mtNext;                       // time (ms) of the next beat
static uint32_t mtLastBeatT, mtPrefAt, mtLastDraw;
static uint32_t mtTaps[4];
static int mtNTaps;
static Repeat mtRepL, mtRepR;

static int mtBeats() { return MT_BEAT_N[mtOpts[0].value < 4 ? mtOpts[0].value : 2]; }
static double mtPeriod() { return 60000.0 / mtBpm; }

static void mtSetBpm(int v, uint32_t now) {
  v = constrain(v, 30, 240);
  if (v == mtBpm) return;
  mtBpm = v;
  mtPrefPending = true; mtPrefAt = now + 300;
  mtNeedDraw = true;
}

static void mtStart() {
  mtBpm = constrain((int)prefs.getUInt("mt_bpm", 100), 30, 240);
  mtRun = true;
  mtBeatIdx = 0; mtCurBeat = -1;
  mtNext = millis() + 400.0;
  mtLastBeatT = 0;
  mtNTaps = 0;
  mtPrefPending = false;
  mtNeedDraw = true;
  mtLastDraw = 0;
  gfx->fillScreen(C_BG);
  dirty = true;
}

static void mtDraw(uint32_t now) {
  const int cx = W / 2, cy = 148;
  gfx->fillScreen(C_BG);
  int beats = mtBeats();
  char b[24];
  snprintf(b, sizeof b, "METRONOME  %d beats", beats);
  appHeader(b, mtRun ? "RUNNING" : "STOPPED", mtRun ? C_GOOD : C_ACC);

  // pulse disc: swells on each beat and fades
  float decay = 0;
  if (mtRun && mtCurBeat >= 0) {
    float span = (float)min(300.0, mtPeriod() * 0.8);
    decay = constrain(1.0f - (now - mtLastBeatT) / span, 0.0f, 1.0f);
  }
  bool accent = mtCurBeat == 0;
  uint16_t pc = accent ? C_ACC : C_CYAN;
  int r = 88 + (int)(28 * decay);
  gfx->fillCircle(cx, cy, r, lerpColor(C_BG, pc, 0.10f + 0.55f * decay));
  gfx->drawCircle(cx, cy, r, lerpColor(C_BG, pc, 0.35f + 0.65f * decay));
  gfx->drawCircle(cx, cy, r + 1, lerpColor(C_BG, pc, 0.2f + 0.5f * decay));

  snprintf(b, sizeof b, "%d", mtBpm);
  text(b, cx - textW(b, 10) / 2, cy - 44, 10, C_TEXT);
  centerText("BPM", cy + 42, 3, mtRun ? pc : C_DIM);

  // beat dots
  int gap = 44, x0 = cx - (beats - 1) * gap / 2;
  for (int i = 0; i < beats; i++) {
    int x = x0 + i * gap, y = 292, rr = i == 0 ? 11 : 8;
    uint16_t c = i == 0 ? C_ACC : C_CYAN;
    if (mtRun && i == mtCurBeat) gfx->fillCircle(x, y, rr, c);
    else { gfx->drawCircle(x, y, rr, lerpColor(c, C_BG, 0.45f)); gfx->drawCircle(x, y, rr - 1, lerpColor(c, C_BG, 0.45f)); }
  }
  if (!soundOn) centerText("SOUND OFF (settings)", 44, 2, C_BAD);

  static const char *const lab[3] = {"-10", "TAP", "+10"};
  for (int i = 0; i < 3; i++) {
    int x = 8 + i * 148, y = H - 40;
    gfx->fillRoundRect(x, y, 140, 34, 8, C_PANEL);
    gfx->drawRoundRect(x, y, 140, 34, 8, i == 1 ? C_CYAN : 0x2945);
    text(lab[i], x + (140 - textW(lab[i], 3)) / 2, y + 7, 3, i == 1 ? C_CYAN : C_TEXT);
  }
  dirty = true;
}

static bool mtFrame(uint32_t now) {
  keepAwake = mtRun;

  // beat clock first, so drawing delays it as little as possible
  if (mtRun && (double)now >= mtNext) {
    bool acc = mtBeatIdx == 0;
    if (acc) play(1500, 45); else play(1000, 30);
    mtCurBeat = mtBeatIdx;
    mtBeatIdx = (mtBeatIdx + 1) % mtBeats();
    mtLastBeatT = now;
    mtNext += mtPeriod();
    if ((double)now > mtNext + mtPeriod()) mtNext = now + mtPeriod();   // we fell behind: resync
    mtNeedDraw = true;
  }

  int dl = repeatStep(mtRepL, in.left, in.leftDown, now, true);
  int dr = repeatStep(mtRepR, in.right, in.rightDown, now, true);
  if (dl) mtSetBpm(mtBpm - dl, now);
  if (dr) mtSetBpm(mtBpm + dr, now);

  if (in.tap) {
    if (in.tapY >= H - 44) {
      int zone = constrain((in.tapX - 8) / 148, 0, 2);
      if (zone == 0) mtSetBpm(mtBpm - 10, now);
      else if (zone == 2) mtSetBpm(mtBpm + 10, now);
      else {                                                  // tap tempo: average of the last taps
        if (mtNTaps > 0 && now - mtTaps[mtNTaps - 1] > 2000) mtNTaps = 0;
        if (mtNTaps == 4) { for (int i = 0; i < 3; i++) mtTaps[i] = mtTaps[i + 1]; mtNTaps = 3; }
        mtTaps[mtNTaps++] = now;
        if (mtNTaps >= 2) {
          mtSetBpm((int)(60000.0 * (mtNTaps - 1) / (mtTaps[mtNTaps - 1] - mtTaps[0]) + 0.5), now);
          mtRun = true; mtBeatIdx = 0; mtNext = now + mtPeriod();
        }
        play(1200, 20);
      }
    } else if (in.tapY > 30) {
      mtRun = !mtRun;
      if (mtRun) { mtBeatIdx = 0; mtNext = now + 150.0; }
      else mtCurBeat = -1;
    }
    mtNeedDraw = true;
  }

  if (mtPrefPending && now >= mtPrefAt) { prefs.putUInt("mt_bpm", (uint32_t)mtBpm); mtPrefPending = false; }

  bool animating = mtRun && mtCurBeat >= 0 && now - mtLastBeatT < 320;
  if ((mtNeedDraw || animating) && now - mtLastDraw >= 33) {
    mtLastDraw = now;
    mtNeedDraw = false;
    mtDraw(now);
  }
  return true;
}

Game APP_METRO = {"METRONOME", "30-240 BPM, accent beat 1", 0xFD20, mtOpts, 1, mtStart, mtFrame};
