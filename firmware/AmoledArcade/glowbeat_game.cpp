// glowbeat_game.cpp - Glow Beat. Three-lane rhythm game; the speaker plays the melody THROUGH YOUR HITS.
//   LEFT = left lane,  RIGHT = right lane,  BOTH buttons together = centre lane
//   flick the tilt up/down = centre lane too;  TAP a third of the screen = that lane
// Notes slide to the hit line. PERFECT +-45 ms, GOOD +-105 ms, BAD +-150 ms (breaks the combo), later = MISS.
// A hit plays the note of the melody with play(); a miss leaves a hole in the song and plays a dull thud.
// The chart is generated from the melody: lane follows the pitch contour (low = left, mid = centre, high = right);
// LEVEL EASY keeps every 2nd note, NORMAL keeps every note, HARD keeps all and shortens the approach time.
// MODE CALIBRATE: tap along with 12 ticks (visual pulse + beep); the median offset is saved ("gbLat") and used to
// shift the judging time in PLAY mode. SONGS: Ode to Joy, Fur Elise, Greensleeves (public domain) and an
// original arpeggio, "Neon Run". Scores: unique best per song and level.
#include "engine.h"
#include <math.h>

static const char *const GB_SONGS[] = {"ODE TO JOY", "FUR ELISE", "GREENSLEEVES", "NEON RUN"};
static const char *const GB_LEVELS[] = {"EASY", "NORMAL", "HARD"};
static const char *const GB_MODES[] = {"PLAY", "CALIBRATE"};
static Option gbOpts[] = {{"SONG", GB_SONGS, 4, "gbSong", 0}, {"LEVEL", GB_LEVELS, 3, "gbLevel", 1}, {"MODE", GB_MODES, 2, "gbMode", 0}};

// ---------------- songs (public domain tunes, original arpeggio) ----------------
enum : uint16_t { N_G3 = 196, N_C4 = 262, N_E4 = 330, N_FS4 = 370, N_GS4 = 415, N_A4 = 440, N_B4 = 494, N_C5 = 523, N_D5 = 587, N_DS5 = 622,
                  N_E5 = 659, N_F5 = 698, N_G5 = 784, N_A5 = 880, N_AS4 = 466, N_CS5 = 554, N_D6 = 1175, N_D4 = 294, N_F4 = 349, N_G4 = 392 };
struct GbNote { uint16_t hz, ms; };                           // hz 0 = rest

static const GbNote SONG_ODE[] = {
  {N_E5,300},{N_E5,300},{N_F5,300},{N_G5,300},{N_G5,300},{N_F5,300},{N_E5,300},{N_D5,300},{N_C5,300},{N_C5,300},{N_D5,300},{N_E5,300},{N_E5,450},{N_D5,150},{N_D5,600},
  {N_E5,300},{N_E5,300},{N_F5,300},{N_G5,300},{N_G5,300},{N_F5,300},{N_E5,300},{N_D5,300},{N_C5,300},{N_C5,300},{N_D5,300},{N_E5,300},{N_D5,450},{N_C5,150},{N_C5,600},
  {N_D5,300},{N_D5,300},{N_E5,300},{N_C5,300},{N_D5,300},{N_E5,150},{N_F5,150},{N_E5,300},{N_C5,300},{N_D5,300},{N_E5,150},{N_F5,150},{N_E5,300},{N_D5,300},{N_C5,300},{N_D5,300},{N_G4,600},
  {N_E5,300},{N_E5,300},{N_F5,300},{N_G5,300},{N_G5,300},{N_F5,300},{N_E5,300},{N_D5,300},{N_C5,300},{N_C5,300},{N_D5,300},{N_E5,300},{N_D5,450},{N_C5,150},{N_C5,900}};
static const GbNote SONG_ELISE[] = {
  {N_E5,200},{N_DS5,200},{N_E5,200},{N_DS5,200},{N_E5,200},{N_B4,200},{N_D5,200},{N_C5,200},{N_A4,500},{0,100},{N_C4,200},{N_E4,200},{N_A4,200},{N_B4,500},{0,100},
  {N_E4,200},{N_GS4,200},{N_B4,200},{N_C5,500},{0,100},{N_E4,200},{N_E5,200},{N_DS5,200},{N_E5,200},{N_DS5,200},{N_E5,200},{N_B4,200},{N_D5,200},{N_C5,200},{N_A4,500},{0,100},
  {N_C4,200},{N_E4,200},{N_A4,200},{N_B4,500},{0,100},{N_E4,200},{N_C5,200},{N_B4,200},{N_A4,800},
  {N_B4,200},{N_C5,200},{N_D5,200},{N_E5,500},{N_G4,200},{N_F5,200},{N_E5,200},{N_D5,500},{N_F4,200},{N_E5,200},{N_D5,200},{N_C5,500},{N_E4,200},{N_D5,200},{N_C5,200},{N_B4,700}};
static const GbNote SONG_GREEN[] = {
  {N_A4,300},{N_C5,600},{N_D5,300},{N_E5,450},{N_F5,150},{N_E5,300},{N_D5,600},{N_B4,300},{N_G4,450},{N_A4,150},{N_B4,300},{N_C5,600},{N_A4,300},{N_A4,450},{N_GS4,150},{N_A4,300},{N_B4,600},{N_GS4,300},{N_E4,600},
  {N_A4,300},{N_C5,600},{N_D5,300},{N_E5,450},{N_F5,150},{N_E5,300},{N_D5,600},{N_B4,300},{N_G4,450},{N_A4,150},{N_B4,300},{N_C5,450},{N_B4,150},{N_A4,300},{N_GS4,450},{N_FS4,150},{N_GS4,300},{N_A4,900}};
static const GbNote SONG_NEON[] = {
  {N_D5,150},{N_F5,150},{N_A5,150},{N_F5,150},{N_D5,150},{N_F5,150},{N_A5,150},{N_F5,150},{N_C5,150},{N_E5,150},{N_G5,150},{N_E5,150},{N_C5,150},{N_E5,150},{N_G5,150},{N_E5,150},
  {N_AS4,150},{N_D5,150},{N_F5,150},{N_D5,150},{N_AS4,150},{N_D5,150},{N_F5,150},{N_D5,150},{N_A4,150},{N_CS5,150},{N_E5,150},{N_CS5,150},{N_A4,150},{N_CS5,150},{N_E5,150},{N_CS5,150},
  {N_D5,150},{N_A5,150},{N_D6,150},{N_A5,150},{N_D5,150},{N_A5,150},{N_D6,150},{N_A5,150},{N_C5,150},{N_G5,150},{N_C5,150},{N_G5,150},{N_E5,150},{N_G5,150},{N_C5,300},
  {N_D5,150},{N_E5,150},{N_F5,150},{N_G5,150},{N_A5,150},{N_G5,150},{N_F5,150},{N_E5,150},{N_D5,600}};
struct GbSong { const GbNote *n; uint16_t count; };
static const GbSong GB_DATA[4] = {{SONG_ODE, sizeof SONG_ODE / sizeof SONG_ODE[0]}, {SONG_ELISE, sizeof SONG_ELISE / sizeof SONG_ELISE[0]},
                                  {SONG_GREEN, sizeof SONG_GREEN / sizeof SONG_GREEN[0]}, {SONG_NEON, sizeof SONG_NEON / sizeof SONG_NEON[0]}};

// ---------------- state ----------------
enum GbSt : uint8_t { GB_COUNT, GB_PLAY, GB_DONE, GB_CAL, GB_CALDONE };
struct GbChart { uint32_t t; uint16_t hz, ms; uint8_t lane, st; };   // st 0 pending, 1 hit, 2 missed
static constexpr int GB_MAXN = 150, GB_LANES = 3;
static constexpr int GB_HITY = 296, GB_TOPY = 40;
static GbChart gbN[GB_MAXN];
static int gbCount;
static GbSt gbSt;
static int gbSong, gbLevel, gbMode, gbCombo, gbMaxCombo, gbPerfect, gbGood, gbBad, gbMiss, gbNext0;
static int32_t gbLat;                                              // ms: press - visual beat (positive = player is late)
static uint32_t gbScore, gbBest, gbStartAt, gbLast, gbLastRender, gbEndAt, gbStAt;
static uint32_t gbLaneFlash[GB_LANES];
static uint32_t gbJudgeAt;
static const char *gbJudgeTxt = "";
static uint16_t gbJudgeCol;
static bool gbNewBest;
static float gbApproach;                                           // ms from spawn to hit line
static Flick gbFlick;
static int8_t gbPend;                                              // pending side press (0 left, 2 right) or -1
static uint32_t gbPendAt;
// calibration
static int gbCalN, gbCalTick;
static int32_t gbCalOff[12];
static uint32_t gbCalStart;

static uint16_t gbLaneCol(int l) { return l == 0 ? rgb(60, 220, 255) : (l == 1 ? rgb(255, 70, 200) : rgb(255, 190, 40)); }

static void gbBuildChart() {
  const GbSong &s = GB_DATA[gbSong];
  uint16_t lo = 65535, hi = 0;
  for (int i = 0; i < s.count; i++) if (s.n[i].hz) { lo = min(lo, s.n[i].hz); hi = max(hi, s.n[i].hz); }
  gbCount = 0;
  uint32_t t = 0;
  int idx = 0;
  for (int i = 0; i < s.count && gbCount < GB_MAXN; i++) {
    const GbNote &n = s.n[i];
    if (n.hz) {
      idx++;
      bool keep = gbLevel != 0 || (idx & 1);
      if (keep) {
        float p = (n.hz - lo) / (float)(hi - lo + 1);
        GbChart &c = gbN[gbCount++];
        c.t = t; c.hz = n.hz; c.lane = p < 0.34f ? 0 : (p < 0.67f ? 1 : 2); c.st = 0;
        c.ms = n.ms;
      }
    }
    t += n.ms;
  }
  for (int i = 0; i < gbCount; i++) {                              // note length = gap to the next onset (never overlaps)
    uint32_t gap = i + 1 < gbCount ? gbN[i + 1].t - gbN[i].t : 600;
    gbN[i].ms = (uint16_t)min<uint32_t>(gbN[i].ms, gap > 20 ? gap - 8 : gap);
    if (gbN[i].ms < 40) gbN[i].ms = 40;
  }
}


static void gbStart() {
  gbSong = gbOpts[0].value; gbLevel = gbOpts[1].value; gbMode = gbOpts[2].value;
  gbLat = prefs.getInt("gbLat", 0);
  gbScore = 0; gbCombo = gbMaxCombo = gbPerfect = gbGood = gbBad = gbMiss = 0; gbNext0 = 0;
  gbNewBest = false; gbPend = -1; gbJudgeAt = 0;
  for (auto &f : gbLaneFlash) f = 0;
  ptClear(); tuneStop();
  gbApproach = gbLevel == 2 ? 1250.0f : 1650.0f;
  gbLast = millis(); gbLastRender = 0;
  imuZero(); gbFlick.reset(gbLast);
  if (gbMode == 1) {
    gbSt = GB_CAL; gbCalN = 0; gbCalTick = 0; gbCalStart = gbLast + 1500; gbStAt = gbLast;
  } else {
    gbBuildChart();
    char k[12]; snprintf(k, sizeof k, "gb%d_%d", gbSong, gbLevel);
    gbBest = loadBest(k);
    gbSt = GB_COUNT; gbStAt = gbLast; gbStartAt = gbLast + 3000;
  }
  sfxStart();
}

// ---------------- judging ----------------
static void gbJudge(const char *t, uint16_t c, uint32_t now) { gbJudgeTxt = t; gbJudgeCol = c; gbJudgeAt = now; }

static void gbPress(int lane, uint32_t pressMs, uint32_t now) {
  gbLaneFlash[lane] = now;
  int32_t st = (int32_t)(pressMs - gbStartAt) - gbLat;             // song time of the press
  int best = -1; int32_t bd = 100000;
  for (int i = gbNext0; i < gbCount; i++) {
    if (gbN[i].t > (uint32_t)(st + 400)) break;
    if (gbN[i].st || gbN[i].lane != lane) continue;
    int32_t d = (int32_t)gbN[i].t - st;
    if (abs(d) < abs(bd)) { bd = d; best = i; }
  }
  if (best < 0 || abs(bd) > 150) { play(150, 10); return; }          // nothing there: ignore (no penalty)
  GbChart &n = gbN[best];
  if (abs(bd) <= 45) { gbPerfect++; gbCombo++; n.st = 1; gbScore += 300 * (1 + min(gbCombo / 10, 3)); gbJudge("PERFECT", C_GOOD, now);
    ptBurst(224 + (lane - 1) * 96, GB_HITY, 14, 170, C_TEXT, gbLaneCol(lane), 0.45f, 0, 2); }
  else if (abs(bd) <= 105) { gbGood++; gbCombo++; n.st = 1; gbScore += 100 * (1 + min(gbCombo / 10, 3)); gbJudge("GOOD", C_CYAN, now);
    ptBurst(224 + (lane - 1) * 96, GB_HITY, 8, 120, C_TEXT, gbLaneCol(lane), 0.35f, 0, 2); }
  else { gbBad++; gbCombo = 0; n.st = 2; gbJudge("BAD", C_ACC, now); play(130, 40); return; }
  if (gbCombo > gbMaxCombo) gbMaxCombo = gbCombo;
  play(n.hz, n.ms);                                                // the melody plays through your hits
}

// ---------------- render ----------------
static void gbDrawLanes(uint32_t now) {
  for (int l = 0; l < GB_LANES; l++) {
    int cx = 224 + (l - 1) * 96, x0 = cx - 46;
    uint16_t c = gbLaneCol(l);
    gfx->fillRect(x0, GB_TOPY, 92, H - GB_TOPY, lerpColor(c, C_BG, 0.95f));
    gfx->drawFastVLine(x0, GB_TOPY, H - GB_TOPY, lerpColor(c, C_BG, 0.8f));
    gfx->drawFastVLine(x0 + 91, GB_TOPY, H - GB_TOPY, lerpColor(c, C_BG, 0.8f));
    float fl = constrain(1.0f - (now - gbLaneFlash[l]) / 180.0f, 0.0f, 1.0f);
    if (fl > 0) gfx->fillRect(x0 + 1, GB_HITY - 60, 90, 70, lerpColor(c, C_BG, 0.9f - 0.45f * fl));
    gfx->fillRoundRect(cx - 38, GB_HITY - 4, 76, 8, 4, lerpColor(c, C_TEXT, 0.3f * fl));
    gfx->drawRoundRect(cx - 38, GB_HITY - 4, 76, 8, 4, c);
    gfx->drawRoundRect(cx - 40, GB_HITY - 6, 80, 12, 6, lerpColor(c, C_BG, 0.6f));
    const char *lab = l == 0 ? "LEFT" : (l == 1 ? "BOTH" : "RIGHT");
    centerText("", 0, 1, C_BG);
    text(lab, cx - (int)strlen(lab) * 3, 336, 1, lerpColor(c, C_BG, 0.35f));
  }
}

static void gbDrawNote(int lane, float y, uint16_t col, bool dim) {
  int cx = 224 + (lane - 1) * 96, yi = (int)y;
  uint16_t c = dim ? lerpColor(col, C_BG, 0.75f) : col;
  gfx->fillRoundRect(cx - 34, yi - 8, 68, 17, 8, lerpColor(c, C_BG, 0.55f));
  gfx->fillRoundRect(cx - 30, yi - 5, 60, 11, 5, c);
  gfx->drawRoundRect(cx - 36, yi - 10, 72, 21, 10, lerpColor(c, C_BG, 0.65f));
  if (!dim) gfx->fillRect(cx - 22, yi - 3, 44, 2, lerpColor(c, C_TEXT, 0.6f));
}

static void gbRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  gbDrawLanes(now);
  int32_t st = (int32_t)(now - gbStartAt);
  if (gbSt == GB_COUNT || gbSt == GB_PLAY || gbSt == GB_DONE) {
    for (int i = gbNext0; i < gbCount; i++) {
      float dtm = (float)((int32_t)gbN[i].t - st + gbLat);        // ms until the hit
      if (dtm > gbApproach) break;
      if (gbN[i].st == 1) continue;
      float y = GB_HITY - dtm / gbApproach * (GB_HITY - GB_TOPY);
      if (y < GB_TOPY - 10 || y > H) continue;
      gbDrawNote(gbN[i].lane, y, gbLaneCol(gbN[i].lane), gbN[i].st == 2);
    }
  }
  ptDraw();
  hudBar(gbScore, gbBest, rgb(8, 8, 22), C_TEXT);
  char b[40];
  if (gbSt == GB_PLAY && gbCount) {                                // progress line
    uint32_t endT = gbN[gbCount - 1].t + 800;
    int w = (int)(W * constrain(st / (float)endT, 0.0f, 1.0f));
    gfx->fillRect(0, 32, w, 2, C_ACC);
  }
  if (gbCombo >= 3 && gbSt != GB_CAL) {
    snprintf(b, sizeof b, "%d", gbCombo);
    centerText(b, 70, 4, lerpColor(C_TEXT, C_ACC, 0.4f));
    centerText("COMBO", 106, 1, C_DIM);
  }
  if (now - gbJudgeAt < 450 && gbJudgeAt) {
    float f = (now - gbJudgeAt) / 450.0f;
    centerText(gbJudgeTxt, 150 - (int)(14 * f), 3, lerpColor(gbJudgeCol, C_BG, f * f));
  }
  if (gbSt == GB_COUNT) {
    int left = (int)((gbStartAt - now) / 1000) + 1;
    snprintf(b, sizeof b, "%d", left);
    centerText(b, 190, 6, C_TEXT);
    centerText("LEFT / RIGHT = side lanes", 252, 1, C_DIM);
    centerText("BOTH buttons (or flick tilt) = centre", 266, 1, C_DIM);
  } else if (gbSt == GB_DONE) {
    int total = gbPerfect + gbGood + gbBad + gbMiss;
    float acc = total ? (gbPerfect + 0.6f * gbGood) / total : 0;
    const char *rank = acc > 0.95f ? "S" : acc > 0.85f ? "A" : acc > 0.7f ? "B" : acc > 0.5f ? "C" : "D";
    char l2[40], l3[48];
    snprintf(l2, sizeof l2, gbNewBest ? "NEW BEST %lu" : "SCORE %lu", (unsigned long)gbScore);
    snprintf(l3, sizeof l3, "%s  x%d  P%d G%d M%d", rank, gbMaxCombo, gbPerfect, gbGood, gbBad + gbMiss);
    panelBox(GB_SONGS[gbSong], l2, l3, C_ACC);
    centerText("RIGHT again   LEFT menu", 244, 1, C_DIM);
  } else if (gbSt == GB_CAL || gbSt == GB_CALDONE) {
    // pulse on every tick
    uint32_t el = now - gbCalStart;
    float ph = (el % 700) / 700.0f;
    int r = 18 + (int)(16 * (1.0f - ph));
    if (gbSt == GB_CAL && now >= gbCalStart) gfx->fillCircle(224, 170, r, lerpColor(C_TEXT, C_BG, ph * 0.9f));
    gfx->drawCircle(224, 170, 40, C_DIM);
    centerText("CALIBRATION", 44, 3, C_ACC);
    snprintf(b, sizeof b, "tap with the beat  %d / 12", gbCalN);
    centerText(b, 240, 2, C_TEXT);
    centerText("any button or tap  (BOTH counts too)", 268, 1, C_DIM);
    if (gbSt == GB_CALDONE) {
      char l2[40];
      snprintf(l2, sizeof l2, "OFFSET %+ld ms", (long)gbLat);
      panelBox("SAVED", l2, nullptr, C_GOOD);
      centerText("RIGHT retry   LEFT menu", 244, 1, C_DIM);
    }
  }
  dirty = true;
}

// ---------------- frame ----------------
static void gbFinish(uint32_t now) {
  gbSt = GB_DONE; gbEndAt = now;
  char k[12]; snprintf(k, sizeof k, "gb%d_%d", gbSong, gbLevel);
  gbNewBest = saveBest(k, gbScore);
  if (gbNewBest) { gbBest = gbScore; tunePlay(TUNE_WIN); } else tunePlay(TUNE_LEVEL);
}

static void gbCalPress(uint32_t now) {
  if (gbSt != GB_CAL || now < gbCalStart - 300) return;
  int32_t el = (int32_t)(now - gbCalStart);
  int32_t off = ((el + 350) % 700 + 700) % 700 - 350;               // offset to the nearest tick
  if (gbCalN < 12) gbCalOff[gbCalN++] = off;
  play(900, 8);
  if (gbCalN >= 12) {
    int32_t a[12];
    memcpy(a, gbCalOff, sizeof a);
    for (int i = 0; i < 12; i++) for (int j = i + 1; j < 12; j++) if (a[j] < a[i]) { int32_t t = a[i]; a[i] = a[j]; a[j] = t; }
    gbLat = (a[5] + a[6]) / 2;
    gbLat = constrain(gbLat, -250, 250);
    prefs.putInt("gbLat", gbLat);
    gbSt = GB_CALDONE; gbEndAt = now;
    tunePlay(TUNE_LEVEL);
  }
}

static bool gbFrame(uint32_t now) {
  homeLock = true;                                                  // quick BOOT taps are the notes
  gbLast = now;
  tuneUpdate(now); ptUpdate(0.016f);
  uint8_t fl = gbFlick.update(now, 0.34f, 120);

  if (gbSt == GB_CAL || gbSt == GB_CALDONE) {
    if (gbSt == GB_CAL) {
      if (now >= gbCalStart && (int)((now - gbCalStart) / 700) >= gbCalTick) { gbCalTick = (int)((now - gbCalStart) / 700) + 1; play(1000, 20); }
      if (in.left || in.right || (in.tap && in.tapY > 40) || fl == DIR_U || fl == DIR_D) gbCalPress(now);
    } else if (now - gbEndAt > 700) {
      if (in.right) { gbStart(); return true; }
      if (in.left) return false;
    }
    if (now - gbLastRender >= 16) { gbLastRender = now; gbRender(now); }
    return true;
  }
  if (gbSt == GB_DONE) {
    if (now - gbEndAt > 900) {
      if (in.right) { gbStart(); return true; }
      if (in.left) return false;
    }
    if (now - gbLastRender >= 16) { gbLastRender = now; gbRender(now); }
    return true;
  }

  // input -> lane presses (left/right with a 55 ms window to merge into "both" = centre)
  if (in.left) {
    if (gbPend == 2) { gbPress(1, (gbPendAt + now) / 2, now); gbPend = -1; }
    else if (gbPend < 0) { gbPend = 0; gbPendAt = now; }
  }
  if (in.right) {
    if (gbPend == 0) { gbPress(1, (gbPendAt + now) / 2, now); gbPend = -1; }
    else if (gbPend < 0) { gbPend = 2; gbPendAt = now; }
  }
  if (gbPend >= 0 && now - gbPendAt >= 55) { gbPress(gbPend, gbPendAt, now); gbPend = -1; }
  if (fl == DIR_U || fl == DIR_D) gbPress(1, now, now);
  if (in.tap && in.tapY > 40) gbPress(in.tapX < 176 ? 0 : (in.tapX < 272 ? 1 : 2), now, now);

  if (gbSt == GB_COUNT && now >= gbStartAt) gbSt = GB_PLAY;
  // misses: notes the player let slide past the window
  int32_t st = (int32_t)(now - gbStartAt) - gbLat;
  while (gbNext0 < gbCount && gbN[gbNext0].st) gbNext0++;
  for (int i = gbNext0; i < gbCount; i++) {
    if ((int32_t)gbN[i].t + 150 >= st) break;
    if (gbN[i].st == 0) {
      gbN[i].st = 2; gbMiss++; gbCombo = 0; gbJudge("MISS", C_BAD, now); play(95, 45);
    }
  }
  while (gbNext0 < gbCount && gbN[gbNext0].st) gbNext0++;
  if (gbSt == GB_PLAY && gbCount && st > (int32_t)gbN[gbCount - 1].t + 1200) gbFinish(now);
  if (now - gbLastRender >= 16) { gbLastRender = now; gbRender(now); }
  return true;
}

Game GAME_GLOWBEAT = {"GLOW BEAT", "3-lane rhythm, you play the tune", 0x07FF, gbOpts, 3, gbStart, gbFrame};
