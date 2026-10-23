// app_clock.cpp - CLOCK: RTC time, stopwatch (0.01 s) and countdown timer.
//   Tabs along the bottom (tap them, or LEFT on CLOCK / on a zeroed STOPWATCH / "NEXT" in TIMER).
//   CLOCK:     RIGHT = set time.  Set: LEFT = next field, RIGHT = + (SAVE / CANCEL at the end).
//   STOPWATCH: RIGHT = start/stop, LEFT = lap (running) / reset (stopped) / next tab (zeroed).
//   TIMER:     LEFT = next field (MM, SS, START, NEXT), RIGHT = + / start / next. Running: RIGHT = pause, LEFT = cancel.
// Stopwatch and timer keep their state while you leave and re-enter the app; they only count and ring while
// the app is open (the screen never dims while one of them runs).
#include "app_common.h"

enum CkTab : uint8_t { T_TIME, T_STOP, T_TIMER };
enum TmState : uint8_t { TM_IDLE, TM_RUN, TM_PAUSE, TM_ALARM };

static CkTab ckTab;
static uint32_t ckLastDraw;
static char ckLastKey[40];                  // what the CLOCK tab last drew (redraw only on change)

// ---- time view / set ----
static RtcTime ckT;
static bool ckRtcAnswers, ckSet;
static int ckField;                         // 0 H, 1 M, 2 S, 3 SAVE, 4 CANCEL
static int ckEd[3];
static Repeat ckRep;

// ---- stopwatch ----
static constexpr int SW_LAPS = 8;
static bool swRun;
static uint32_t swBase, swStart;
static uint32_t swLap[SW_LAPS];
static int swNLaps;

// ---- timer ----
static TmState tmState;
static int tmField;                         // 0 MM, 1 SS, 2 START, 3 NEXT
static uint32_t tmSetSec = 300, tmEnd, tmRemain, tmAlarmAt;
static uint32_t tmPrefSaveAt;
static bool tmPrefPending;
static Repeat tmRep;
static uint32_t tmLastBeep;

static void ckStart() {
  ckTab = T_TIME;
  ckSet = false;
  ckLastKey[0] = 0;
  ckLastDraw = 0;
  tmSetSec = prefs.getUInt("tm_set", 300);
  if (tmSetSec > 99 * 60 + 59) tmSetSec = 300;
  if (tmState == TM_ALARM) tmState = TM_IDLE;       // an old alarm is not replayed
  if (tmField > 3) tmField = 0;
  gfx->fillScreen(C_BG);
  dirty = true;
}

// ---------------------------------------------------------------------------
static void fmtTime(char *b, size_t n, uint32_t ms, bool centis) {
  uint32_t cs = (ms / 10) % 100, s = (ms / 1000) % 60, m = (ms / 60000) % 100;
  if (centis) snprintf(b, n, "%02lu:%02lu.%02lu", (unsigned long)m, (unsigned long)s, (unsigned long)cs);
  else snprintf(b, n, "%02lu:%02lu", (unsigned long)m, (unsigned long)s);
}
static uint32_t swElapsed(uint32_t now) { return swBase + (swRun ? now - swStart : 0); }

static void drawTabs() {
  static const char *const names[3] = {"CLOCK", "STOPWATCH", "TIMER"};
  bool running[3] = {false, swRun, tmState == TM_RUN};
  for (int i = 0; i < 3; i++) {
    int x = 4 + i * 148, y = H - 38;
    bool on = i == (int)ckTab;
    gfx->fillRoundRect(x, y, 144, 34, 8, on ? C_PANEL : C_BG);
    gfx->drawRoundRect(x, y, 144, 34, 8, on ? C_CYAN : 0x2104);
    text(names[i], x + (144 - textW(names[i], 2)) / 2, y + 10, 2, on ? C_TEXT : C_DIM);
    if (running[i]) gfx->fillCircle(x + 130, y + 8, 4, C_GOOD);
  }
}

static void nextTab() { ckTab = (CkTab)((ckTab + 1) % 3); ckLastDraw = 0; ckLastKey[0] = 0; sfxClick(); }

// ---------------------------------------------------------------------------
static void drawTimeView(uint32_t now) {
  char key[40];
  bool colon = (now % 1000) < 500;
  if (ckSet) snprintf(key, sizeof key, "S%d:%d:%d:%d", ckField, ckEd[0], ckEd[1], ckEd[2]);
  else snprintf(key, sizeof key, "V%d%d%d%d:%d:%d", ckRtcAnswers, ckT.ok, colon, 0, ckT.minute * 60 + ckT.hour, ckT.second);
  if (strcmp(key, ckLastKey) == 0) return;
  strcpy(ckLastKey, key);
  gfx->fillRect(0, 30, W, H - 30 - 40, C_BG);
  if (ckSet) {
    text("SET TIME", 10, 8, 2, C_ACC);
    char b[16];
    snprintf(b, sizeof b, "%02d:%02d:%02d", ckEd[0], ckEd[1], ckEd[2]);
    int x = (W - textW(b, 8)) / 2, y = 80;
    for (int i = 0; i < 3; i++) {
      char f[4];
      snprintf(f, sizeof f, "%02d", ckEd[i]);
      int fx = x + i * 3 * 48;
      bool on = ckField == i;
      text(f, fx, y, 8, on ? C_ACC : C_TEXT);
      if (on) gfx->fillRect(fx, y + 72, 96, 6, C_ACC);
      if (i < 2) text(":", fx + 96, y, 8, C_DIM);
    }
    const char *btn[2] = {"SAVE", "CANCEL"};
    for (int i = 0; i < 2; i++) {
      int bx = 44 + i * 190, by = 200;
      bool on = ckField == 3 + i;
      gfx->fillRoundRect(bx, by, 170, 48, 10, on ? C_PANEL : C_BG);
      gfx->drawRoundRect(bx, by, 170, 48, 10, on ? (i ? C_BAD : C_GOOD) : 0x2104);
      text(btn[i], bx + (170 - textW(btn[i], 3)) / 2, by + 14, 3, on ? (i ? C_BAD : C_GOOD) : C_DIM);
    }
    centerText("L next field    R + / go", 270, 2, C_DIM);
  } else {
    text("CLOCK", 10, 8, 2, C_TEXT);
    if (!ckRtcAnswers) {
      centerText("RTC NOT FOUND", 120, 4, C_BAD);
      centerText("(PCF85063 does not answer)", 180, 2, C_DIM);
    } else {
      char hm[8], ss[4];
      if (ckT.ok) {
        snprintf(hm, sizeof hm, "%02d:%02d", ckT.hour, ckT.minute);
        snprintf(ss, sizeof ss, "%02d", ckT.second);
      } else { strcpy(hm, "--:--"); strcpy(ss, "--"); }
      int total = textW(hm, 10) + 14 + textW(ss, 5), x = (W - total) / 2, y = 78;
      char h2[3] = {hm[0], hm[1], 0}, m2[3] = {hm[3], hm[4], 0};
      uint16_t col = ckT.ok ? C_TEXT : C_DIM;
      text(h2, x, y, 10, col);
      text(m2, x + 3 * 60, y, 10, col);
      if (colon || !ckT.ok) text(":", x + 2 * 60, y, 10, ckT.ok ? C_ACC : C_DIM);
      text(ss, x + textW(hm, 10) + 14, y + 45, 5, ckT.ok ? C_ACC : C_DIM);
      if (!ckT.ok) {
        centerText("TIME NOT SET", 190, 3, C_ACC);
        centerText("RTC lost power or was never set", 226, 2, C_DIM);
      } else {
        char d[24];
        snprintf(d, sizeof d, "%04d-%02d-%02d", ckT.year, ckT.month, ckT.day);
        centerText(d, 190, 2, 0x6B4D);
      }
      centerText("L next tab    R set time", 270, 2, C_DIM);
    }
  }
  drawTabs();
  dirty = true;
}

static bool frameTime(uint32_t now) {
  static uint32_t lastRead = 0;
  if (!ckSet && now - lastRead >= 100) {
    lastRead = now;
    ckRtcAnswers = rtcRead(ckT);
    if (!ckRtcAnswers) ckT.ok = false;
  }
  if (!ckSet) {
    if (in.left) { nextTab(); return true; }
    if (in.right && ckRtcAnswers) {
      ckSet = true; ckField = 0;
      ckEd[0] = ckT.ok ? ckT.hour : 12; ckEd[1] = ckT.ok ? ckT.minute : 0; ckEd[2] = ckT.ok ? ckT.second : 0;
      ckLastKey[0] = 0; sfxClick();
    }
  } else {
    if (in.left) { ckField = (ckField + 1) % 5; sfxClick(); ckLastKey[0] = 0; }
    int st = ckField < 3 ? repeatStep(ckRep, in.right, in.rightDown, now, false) : 0;
    if (ckField < 3 && st) {
      static const int mod[3] = {24, 60, 60};
      ckEd[ckField] = (ckEd[ckField] + 1) % mod[ckField];
      sfxClick(); ckLastKey[0] = 0;
    } else if (ckField >= 3 && in.right) {
      if (ckField == 3) { rtcSet(ckEd[0], ckEd[1], ckEd[2]); sfxGood(); }
      else sfxClick();
      ckSet = false; ckLastKey[0] = 0; lastRead = 0;
    }
  }
  if (now - ckLastDraw >= 60) { ckLastDraw = now; drawTimeView(now); }
  return true;
}

// ---------------------------------------------------------------------------
static void frameStop(uint32_t now) {
  if (swRun) homeLock = true;                        // laps are quick taps: no triple-tap home while the stopwatch runs
  if (in.right) {
    if (swRun) { swBase += now - swStart; swRun = false; }
    else { swStart = now; swRun = true; }
    sfxClick();
  }
  if (in.left) {
    if (swRun) {
      swLap[swNLaps % SW_LAPS] = swElapsed(now);
      swNLaps++;
      sfxClick();
    } else if (swBase > 0) { swBase = 0; swNLaps = 0; sfxClick(); }
    else { nextTab(); return; }
  }
  if (now - ckLastDraw < 33) return;
  ckLastDraw = now;
  gfx->fillRect(0, 0, W, H - 40, C_BG);
  appHeader("STOPWATCH", swRun ? "RUNNING" : (swBase ? "STOPPED" : "READY"), swRun ? C_GOOD : C_ACC);
  char b[48];
  fmtTime(b, sizeof b, swElapsed(now), true);
  text(b, (W - textW(b, 8)) / 2, 44, 8, swRun ? C_TEXT : (swBase ? C_ACC : C_DIM));
  int shown = min(swNLaps, 5);
  for (int i = 0; i < shown; i++) {
    int n = swNLaps - 1 - i;
    uint32_t t = swLap[n % SW_LAPS];
    uint32_t prev = n > 0 && n - 1 >= swNLaps - SW_LAPS ? swLap[(n - 1) % SW_LAPS] : 0;
    char tt[16], sp[16];
    fmtTime(tt, sizeof tt, t, true);
    fmtTime(sp, sizeof sp, t - prev, true);
    snprintf(b, sizeof b, "#%-2d %s +%s", n + 1, tt, sp);
    text(b, (W - textW(b, 2)) / 2, 124 + i * 28, 2, i == 0 ? C_TEXT : C_DIM);
  }
  centerText(swRun ? "L lap    R stop" : (swBase ? "L reset    R start" : "L next tab    R start"), 280, 2, C_DIM);
  drawTabs();
  dirty = true;
}

// ---------------------------------------------------------------------------
static void tmSavePref(uint32_t now) {
  if (tmPrefPending && now >= tmPrefSaveAt) { prefs.putUInt("tm_set", tmSetSec); tmPrefPending = false; }
}

static void frameTimer(uint32_t now) {
  if (tmState == TM_ALARM) {
    if (in.left || in.right || in.tap) { tmState = TM_IDLE; sfxClick(); ckLastDraw = 0; gfx->fillScreen(C_BG); return; }
    if (now - tmAlarmAt < 120000 && now - tmLastBeep >= 600) {
      tmLastBeep = now;
      play(2200, 110); play(0, 70); play(2200, 110); play(0, 70); play(2200, 110);
    }
    if (now - ckLastDraw >= 120) {
      ckLastDraw = now;
      bool on = ((now - tmAlarmAt) / 300) % 2 == 0;
      gfx->fillScreen(on ? C_BAD : C_BG);
      centerText("TIME UP", 130, 6, on ? C_BG : C_BAD);
      centerText("press any button or tap", 250, 2, on ? C_BG : C_TEXT);
      dirty = true;
    }
    return;
  }
  uint32_t mm = tmSetSec / 60, ss = tmSetSec % 60;
  if (tmState == TM_IDLE) {
    if (in.left) { tmField = (tmField + 1) % 4; sfxClick(); ckLastDraw = 0; }
    bool edit = tmField < 2;
    int st = edit ? repeatStep(tmRep, in.right, in.rightDown, now, false) : 0;
    if (edit && st) {
      if (tmField == 0) mm = (mm + 1) % 100; else ss = (ss + 1) % 60;
      tmSetSec = mm * 60 + ss;
      tmPrefPending = true; tmPrefSaveAt = now + 400;
      sfxClick(); ckLastDraw = 0;
    } else if (!edit && in.right) {
      if (tmField == 3) { nextTab(); return; }
      if (tmSetSec > 0) { tmState = TM_RUN; tmEnd = now + tmSetSec * 1000; sfxGood(); ckLastDraw = 0; }
      else sfxBad();
    }
  } else {
    if (in.left) { tmState = TM_IDLE; sfxClick(); ckLastDraw = 0; }
    else if (in.right) {
      if (tmState == TM_RUN) { tmRemain = tmEnd > now ? tmEnd - now : 0; tmState = TM_PAUSE; }
      else { tmEnd = now + tmRemain; tmState = TM_RUN; }
      sfxClick(); ckLastDraw = 0;
    }
  }
  tmSavePref(now);
  if (now - ckLastDraw < 50) return;
  ckLastDraw = now;
  uint32_t shownMs = tmState == TM_IDLE ? tmSetSec * 1000 : tmState == TM_RUN ? (tmEnd > now ? tmEnd - now : 0) : tmRemain;
  uint32_t shownSec = (shownMs + 999) / 1000;           // count down: 0.1 s left still reads 00:01
  gfx->fillRect(0, 0, W, H - 40, C_BG);
  const char *badge = tmState == TM_RUN ? "RUNNING" : tmState == TM_PAUSE ? "PAUSED" : "SET";
  appHeader("TIMER", badge, tmState == TM_RUN ? C_GOOD : C_ACC);
  char b[16];
  snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(shownSec / 60), (unsigned long)(shownSec % 60));
  int x = (W - textW(b, 10)) / 2, y = 62;
  bool editing = tmState == TM_IDLE;
  char m2[3] = {b[0], b[1], 0}, s2[3] = {b[3], b[4], 0};
  uint16_t base = tmState == TM_PAUSE ? C_ACC : C_TEXT;
  text(m2, x, y, 10, editing && tmField == 0 ? C_ACC : base);
  text(":", x + 120, y, 10, C_DIM);
  text(s2, x + 180, y, 10, editing && tmField == 1 ? C_ACC : base);
  if (editing && tmField < 2) gfx->fillRect(x + tmField * 180, y + 88, 120, 6, C_ACC);
  if (!editing && tmSetSec > 0) {                          // progress bar
    int bw = W - 80;
    float f = constrain(shownMs / (tmSetSec * 1000.0f), 0.0f, 1.0f);
    gfx->drawRoundRect(40, 176, bw, 14, 5, C_DIM);
    gfx->fillRoundRect(42, 178, (int)((bw - 4) * f), 10, 4, tmState == TM_RUN ? C_GOOD : C_ACC);
  }
  if (editing) {
    const char *btn[2] = {"START", "NEXT >"};
    for (int i = 0; i < 2; i++) {
      int bx = 44 + i * 190, by = 190;
      bool on = tmField == 2 + i;
      gfx->fillRoundRect(bx, by, 170, 44, 10, on ? C_PANEL : C_BG);
      gfx->drawRoundRect(bx, by, 170, 44, 10, on ? (i ? C_CYAN : C_GOOD) : 0x2104);
      text(btn[i], bx + (170 - textW(btn[i], 3)) / 2, by + 12, 3, on ? (i ? C_CYAN : C_GOOD) : C_DIM);
    }
    centerText(tmField < 2 ? "L next field    R +" : "L next field    R go", 250, 2, C_DIM);
  } else {
    centerText(tmState == TM_RUN ? "L cancel    R pause" : "L cancel    R resume", 220, 2, C_DIM);
  }
  drawTabs();
  dirty = true;
}

// ---------------------------------------------------------------------------
static bool ckFrame(uint32_t now) {
  // the timer keeps counting on every tab
  if (tmState == TM_RUN && now >= tmEnd) {
    tmState = TM_ALARM; tmAlarmAt = now; tmLastBeep = 0;
    ckTab = T_TIMER; ckSet = false; ckLastDraw = 0;
    wakeScreen();
    sfxStart();
  }
  keepAwake = swRun || tmState == TM_RUN || tmState == TM_ALARM;

  if (in.tap && tmState != TM_ALARM && !(ckTab == T_TIME && ckSet) && in.tapY >= H - 40) {
    int t = constrain((in.tapX - 4) / 148, 0, 2);
    if (t != (int)ckTab) { ckTab = (CkTab)t; ckLastDraw = 0; ckLastKey[0] = 0; gfx->fillScreen(C_BG); sfxClick(); in.left = in.right = false; }
  }
  if (ckTab == T_TIME && !ckSet && ckLastKey[0] == 0 && ckLastDraw == 0) gfx->fillScreen(C_BG);

  CkTab before = ckTab;
  switch (ckTab) {
    case T_TIME: frameTime(now); break;
    case T_STOP: frameStop(now); break;
    default: frameTimer(now); break;
  }
  if (ckTab != before) gfx->fillScreen(C_BG);
  return true;
}

Game APP_CLOCK = {"CLOCK", "time, stopwatch, timer", 0x07FF, nullptr, 0, ckStart, ckFrame};
