// fourrow_game.cpp - Four Row (Connect Four) against a CPU, 7 x 6 board, glowing discs.
//   LEFT / RIGHT = move the drop column (hold to repeat)      BOTH buttons together = drop
//   tilt TOWARD YOU (flick) = drop;  flick left / right = move   TAP a column = drop there
// CPU: bitboard negamax with alpha-beta, centre-first move ordering, iterative deepening and a time limit.
// The search runs in its own FreeRTOS task on core 0, so the game loop (core 1) never blocks: the board keeps
// animating at full frame rate and only polls a result flag. Levels: EASY = depth 2 + random blunders,
// MEDIUM = depth <= 6 / 0.25 s, HARD = depth <= 14 / 1.1 s (usually solves the endgame).
// OPTIONS: LEVEL, FIRST (YOU / CPU / ALTERNATE). Score = win streak; record W-D-L per level is stored.
#include "engine.h"
#include <math.h>

static const char *const FR_LEVELS[] = {"EASY", "MEDIUM", "HARD"};
static const char *const FR_FIRSTS[] = {"YOU", "CPU", "ALTERNATE"};
static Option frOpts[] = {{"LEVEL", FR_LEVELS, 3, "frLevel", 1}, {"FIRST", FR_FIRSTS, 3, "frFirst", 0}};

enum FrSt : uint8_t { FR_PLAY, FR_THINK, FR_DROP, FR_END };

static constexpr int FR_CELL = 44, FR_BX = 70, FR_BY = 80, FR_COLS = 7, FR_ROWS = 6;

// ---------------- bitboard search (runs on core 0) ----------------
static uint64_t frWinMask[69];
static int frNWin = 0;
static bool frTablesReady = false;

static inline bool frAlign(uint64_t p) {
  uint64_t m = p & (p >> 7);  if (m & (m >> 14)) return true;
  m = p & (p >> 6);           if (m & (m >> 12)) return true;
  m = p & (p >> 8);           if (m & (m >> 16)) return true;
  m = p & (p >> 1);           if (m & (m >> 2)) return true;
  return false;
}
static inline bool frCan(uint64_t mask, int c) { return !(mask & (1ULL << (5 + 7 * c))); }
static inline uint64_t frBit(uint64_t mask, int c) { return (mask + (1ULL << (7 * c))) & (0x3FULL << (7 * c)); }

static void frBuildTables() {
  if (frTablesReady) return;
  frNWin = 0;
  auto add = [](int c, int r, int dc, int dr) {
    uint64_t m = 0;
    for (int i = 0; i < 4; i++) m |= 1ULL << ((c + dc * i) * 7 + (r + dr * i));
    frWinMask[frNWin++] = m;
  };
  for (int r = 0; r < 6; r++) for (int c = 0; c < 4; c++) add(c, r, 1, 0);
  for (int c = 0; c < 7; c++) for (int r = 0; r < 3; r++) add(c, r, 0, 1);
  for (int c = 0; c < 4; c++) for (int r = 0; r < 3; r++) add(c, r, 1, 1);
  for (int c = 0; c < 4; c++) for (int r = 3; r < 6; r++) add(c, r, 1, -1);
  frTablesReady = true;
}

static const int8_t FR_ORDER[7] = {3, 2, 4, 1, 5, 0, 6};
static uint32_t frNodes, frDeadline;
static volatile bool frAbort = false, frCancel = false;

static int frEval(uint64_t pos, uint64_t mask) {
  uint64_t opp = pos ^ mask;
  static const int8_t wgt[5] = {0, 1, 5, 28, 0};
  int s = 0;
  for (int i = 0; i < frNWin; i++) {
    uint64_t w = frWinMask[i];
    int a = __builtin_popcountll(pos & w), b = __builtin_popcountll(opp & w);
    if (b == 0) s += wgt[a]; else if (a == 0) s -= wgt[b];
  }
  uint64_t centre = 0x3FULL << 21;
  s += 3 * (__builtin_popcountll(pos & centre) - __builtin_popcountll(opp & centre));
  return s;
}

static int frNegamax(uint64_t pos, uint64_t mask, int depth, int alpha, int beta, int moves) {
  if ((++frNodes & 2047) == 0 && (millis() > frDeadline || frCancel)) frAbort = true;
  if (frAbort) return 0;
  if (moves >= 42) return 0;
  for (int i = 0; i < 7; i++) {                              // win in one?
    int c = FR_ORDER[i];
    if (frCan(mask, c) && frAlign(pos | frBit(mask, c))) return 1000 + depth;
  }
  if (depth <= 0) return frEval(pos, mask);
  int best = -100000;
  for (int i = 0; i < 7; i++) {
    int c = FR_ORDER[i];
    if (!frCan(mask, c)) continue;
    uint64_t bit = frBit(mask, c);
    int sc = -frNegamax(pos ^ mask, mask | bit, depth - 1, -beta, -alpha, moves + 1);
    if (frAbort) return 0;
    if (sc > best) best = sc;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;
  }
  return best;
}

// Task interface: main sets the request fields, then frTS = 1; the task does 1 -> 2 -> 3; main resets 3 -> 0.
static volatile uint8_t frTS = 0;
static volatile int frRes = 3, frDepthDone = 0;
static uint64_t frReqPos, frReqMask;
static int frReqLevel, frReqMoves;
static TaskHandle_t frTask = nullptr;

static int frSearchRoot(uint64_t pos, uint64_t mask, int moves, int level) {
  static const int maxD[3] = {2, 6, 14};
  static const uint32_t limMs[3] = {40, 250, 1100};
  frNodes = 0; frAbort = false; frDeadline = millis() + limMs[level];
  int bestMove = -1, bestScore = -100000;
  for (int i = 0; i < 7; i++) if (frCan(mask, FR_ORDER[i])) { bestMove = FR_ORDER[i]; break; }
  frDepthDone = 0;
  for (int c = 0; c < 7; c++)                                   // immediate win
    if (frCan(mask, c) && frAlign(pos | frBit(mask, c))) return c;
  for (int d = 1; d <= maxD[level]; d++) {
    int iterBest = -1, iterScore = -100000, alpha = -100000;
    int order[7], n = 0;
    if (bestMove >= 0) order[n++] = bestMove;                   // previous best first
    for (int i = 0; i < 7; i++) if (FR_ORDER[i] != bestMove && frCan(mask, FR_ORDER[i])) order[n++] = FR_ORDER[i];
    for (int i = 0; i < n; i++) {
      int c = order[i];
      int sc = -frNegamax(pos ^ mask, mask | frBit(mask, c), d - 1, -100000, -alpha, moves + 1);
      if (frAbort) break;
      if (sc > iterScore) { iterScore = sc; iterBest = c; }
      if (sc > alpha) alpha = sc;
    }
    if (frAbort) break;
    bestMove = iterBest; bestScore = iterScore; frDepthDone = d;
    if (bestScore >= 1000 || bestScore <= -1000) break;         // forced result found
  }
  if (level == 0 && (esp_random() % 100) < 35) {                // easy: blunders
    int n = 0, cols[7];
    for (int c = 0; c < 7; c++) if (frCan(mask, c)) cols[n++] = c;
    if (n) bestMove = cols[esp_random() % (uint32_t)n];
  }
  return bestMove;
}

static void frTaskFn(void *) {
  for (;;) {
    if (frTS == 1) {
      frTS = 2;
      frRes = frSearchRoot(frReqPos, frReqMask, frReqMoves, frReqLevel);
      frTS = 3;
    } else vTaskDelay(pdMS_TO_TICKS(4));
  }
}

// ---------------- game state ----------------
static uint8_t frGrid[FR_COLS][FR_ROWS];                         // [col][row from the bottom], 0 empty 1 you 2 cpu
static uint8_t frH[FR_COLS];
static FrSt frSt;
static int frLevel, frCol, frMoves, frTurn, frWinLen, frLastC, frLastR, frDropC, frDropR, frDropP, frBounce;
static int8_t frWinCells[4][2];
static bool frDraw, frAltCpuFirst, frWaiting, frHumanWon, frNewBest;
static uint32_t frStreak, frBest, frLast, frLastRender, frStAt, frThinkAt, frHintUntil;
static float frDropY, frDropV;
static uint32_t frW, frD, frL;
static Flick frFlick;
static int8_t frPend;
static uint32_t frPendAt, frRep;
static bool frGuard;

static void frRecordLoad() {
  char k[8];
  snprintf(k, sizeof k, "frW%d", frLevel); frW = prefs.getUInt(k, 0);
  snprintf(k, sizeof k, "frD%d", frLevel); frD = prefs.getUInt(k, 0);
  snprintf(k, sizeof k, "frL%d", frLevel); frL = prefs.getUInt(k, 0);
}
static void frRecordSave() {
  char k[8];
  snprintf(k, sizeof k, "frW%d", frLevel); prefs.putUInt(k, frW);
  snprintf(k, sizeof k, "frD%d", frLevel); prefs.putUInt(k, frD);
  snprintf(k, sizeof k, "frL%d", frLevel); prefs.putUInt(k, frL);
}

static void frEnsureTask() {
  frBuildTables();
  if (!frTask) xTaskCreatePinnedToCore(frTaskFn, "fourrow", 8192, nullptr, 1, &frTask, 0);
}

static void frNewRound(uint32_t now) {
  memset(frGrid, 0, sizeof frGrid); memset(frH, 0, sizeof frH);
  frMoves = 0; frWinLen = 0; frDraw = false; frCol = 3; frLastC = -1; frLastR = -1;
  frPend = 0; frRep = 0; frGuard = true;
  bool cpuFirst = frOpts[1].value == 1 || (frOpts[1].value == 2 && frAltCpuFirst);
  if (frOpts[1].value == 2) frAltCpuFirst = !frAltCpuFirst;
  frTurn = cpuFirst ? 2 : 1;
  frSt = cpuFirst ? FR_THINK : FR_PLAY;
  frWaiting = false; frThinkAt = now;
  imuZero(); frFlick.reset(now);
}

static void frStart() {
  frEnsureTask();
  frCancel = true;                                              // drop any search still running from a previous game
  frLevel = frOpts[0].value;
  frBest = loadBest("frBest");
  frStreak = 0; frNewBest = false;
  frRecordLoad();
  frAltCpuFirst = false;
  frLast = millis(); frLastRender = 0; frHintUntil = frLast + 6000;
  frNewRound(frLast);
  sfxStart();
}

static bool frCheckWin(int c, int r, int p) {
  static const int8_t dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
  auto in2 = [](int cc, int rr) { return cc >= 0 && cc < FR_COLS && rr >= 0 && rr < FR_ROWS; };
  for (auto &d : dir) {
    int sc = c, sr = r;                                              // walk back to the start of the run
    while (in2(sc - d[0], sr - d[1]) && frGrid[sc - d[0]][sr - d[1]] == p) { sc -= d[0]; sr -= d[1]; }
    int n = 0, cc = sc, rr = sr;
    while (in2(cc, rr) && frGrid[cc][rr] == p) {
      if (n < 4) { frWinCells[n][0] = (int8_t)cc; frWinCells[n][1] = (int8_t)rr; }
      n++; cc += d[0]; rr += d[1];
    }
    if (n >= 4) { frWinLen = 4; return true; }
  }
  return false;
}

static bool frTryDrop(int c, uint32_t now);

static void frBeginDrop(int c, int p, uint32_t now) {
  frDropC = c; frDropR = frH[c]; frDropP = p;
  frDropY = (float)(FR_BY - FR_CELL / 2); frDropV = 0; frBounce = 0;
  frSt = FR_DROP; frStAt = now;
}

static void frEndRound(uint32_t now, int result) {              // result 1 you win, 2 cpu win, 0 draw
  frSt = FR_END; frStAt = now;
  if (result == 1) {
    frW++; frStreak++; frHumanWon = true;
    frNewBest = saveBest("frBest", frStreak);
    if (frNewBest) frBest = frStreak;
    tunePlay(TUNE_WIN);
  } else if (result == 2) {
    frL++; frStreak = 0; frHumanWon = false; tunePlay(TUNE_LOSE);
  } else {
    frD++; frHumanWon = false; tunePlay(TUNE_LEVEL);
  }
  frRecordSave();
}

static bool frTryDrop(int c, uint32_t now) {
  if (c < 0 || c >= FR_COLS || frH[c] >= FR_ROWS) { play(180, 60); return false; }
  frBeginDrop(c, 1, now);
  return true;
}

static void frRequestSearch() {
  uint64_t pos = 0, mask = 0;
  for (int c = 0; c < FR_COLS; c++)
    for (int r = 0; r < FR_ROWS; r++) {
      if (!frGrid[c][r]) continue;
      uint64_t b = 1ULL << (c * 7 + r);
      mask |= b;
      if (frGrid[c][r] == 2) pos |= b;                          // CPU to move: pos = CPU discs
    }
  frReqPos = pos; frReqMask = mask; frReqLevel = frLevel; frReqMoves = frMoves;
  frCancel = false;
  __sync_synchronize();
  frTS = 1;
  frWaiting = true;
}

// ---------------- render ----------------
static void frDisc(int cx, int cy, uint16_t c, float glow) {
  gfx->drawCircle(cx, cy, 20, lerpColor(c, C_BG, 0.78f - 0.3f * glow));
  gfx->fillCircle(cx, cy, 18, lerpColor(c, C_BG, 0.45f - 0.3f * glow));
  gfx->fillCircle(cx, cy, 15, c);
  gfx->drawCircle(cx, cy, 11, lerpColor(c, C_TEXT, 0.35f));
  gfx->fillCircle(cx - 5, cy - 5, 3, lerpColor(c, C_TEXT, 0.7f));
}
static inline int frCX(int c) { return FR_BX + c * FR_CELL + FR_CELL / 2; }
static inline int frCY(int r) { return FR_BY + (FR_ROWS - 1 - r) * FR_CELL + FR_CELL / 2; }
static uint16_t frColP(int p) { return p == 1 ? rgb(255, 50, 80) : rgb(255, 210, 40); }

static void frRender(uint32_t now) {
  gfx->fillScreen(C_BG);
  for (int y = 32; y < H; y += 2) gfx->drawFastHLine(0, y, W, lerpColor(rgb(4, 6, 24), C_BG, (y - 32) / 336.0f));
  uint16_t frame = rgb(40, 110, 255);
  gfx->fillRoundRect(FR_BX - 6, FR_BY - 4, FR_COLS * FR_CELL + 12, FR_ROWS * FR_CELL + 10, 10, rgb(6, 18, 64));
  gfx->drawRoundRect(FR_BX - 6, FR_BY - 4, FR_COLS * FR_CELL + 12, FR_ROWS * FR_CELL + 10, 10, frame);
  gfx->drawRoundRect(FR_BX - 7, FR_BY - 5, FR_COLS * FR_CELL + 14, FR_ROWS * FR_CELL + 12, 11, lerpColor(frame, C_BG, 0.7f));
  float pulse = 0.5f + 0.5f * sinf(now * 0.012f);
  for (int c = 0; c < FR_COLS; c++)
    for (int r = 0; r < FR_ROWS; r++) {
      int cx = frCX(c), cy = frCY(r);
      int p = frGrid[c][r];
      if (frSt == FR_DROP && c == frDropC && r == frDropR) p = 0;
      if (p) {
        bool win = false;
        if (frSt == FR_END && frWinLen)
          for (int i = 0; i < frWinLen; i++) if (frWinCells[i][0] == c && frWinCells[i][1] == r) win = true;
        frDisc(cx, cy, win ? lerpColor(frColP(p), C_TEXT, 0.5f * pulse) : frColP(p), win ? 1.0f : 0.0f);
        if (c == frLastC && r == frLastR && !win) gfx->fillCircle(cx, cy, 3, C_TEXT);
      } else {
        gfx->fillCircle(cx, cy, 18, rgb(2, 4, 16));
        gfx->drawCircle(cx, cy, 19, rgb(20, 60, 160));
      }
    }
  if (frSt == FR_END && frWinLen) {                                  // win line glow
    int x1 = frCX(frWinCells[0][0]), y1 = frCY(frWinCells[0][1]), x2 = frCX(frWinCells[3][0]), y2 = frCY(frWinCells[3][1]);
    uint16_t lc = lerpColor(C_TEXT, frColP(frGrid[frWinCells[0][0]][frWinCells[0][1]]), 0.3f);
    for (int o = -2; o <= 2; o++) gfx->drawLine(x1 + o, y1, x2 + o, y2, lerpColor(lc, C_BG, 0.4f + 0.2f * abs(o)));
    gfx->drawLine(x1, y1, x2, y2, C_TEXT);
  }
  if (frSt == FR_DROP) frDisc(frCX(frDropC), (int)frDropY, frColP(frDropP), 0.4f);
  if (frSt == FR_PLAY) {                                              // cursor ghost + arrow
    frDisc(frCX(frCol), FR_BY - FR_CELL / 2, lerpColor(frColP(1), C_BG, 0.35f + 0.3f * pulse), 0.0f);
    gfx->fillTriangle(frCX(frCol) - 7, FR_BY - 2, frCX(frCol) + 7, FR_BY - 2, frCX(frCol), FR_BY + 7, C_TEXT);
  }
  // side panels
  char b[24];
  frDisc(36, 120, frColP(1), 0.0f);  text("YOU", 20, 150, 2, C_TEXT);
  frDisc(412, 120, frColP(2), 0.0f); text("CPU", 396, 150, 2, C_TEXT);
  snprintf(b, sizeof b, "W%lu", (unsigned long)frW); text(b, 10, 190, 2, C_GOOD);
  snprintf(b, sizeof b, "D%lu", (unsigned long)frD); text(b, 10, 214, 2, C_DIM);
  snprintf(b, sizeof b, "L%lu", (unsigned long)frL); text(b, 10, 238, 2, C_BAD);
  text(FR_LEVELS[frLevel], 380, 190, 2, C_ACC);
  if (frSt == FR_THINK) {
    int dots = (now / 250) % 4;
    snprintf(b, sizeof b, "THINK%.*s", dots, "...");
    text(b, 380, 222, 1, C_CYAN);
    if (frTS == 3 || frDepthDone) { snprintf(b, sizeof b, "depth %d", (int)frDepthDone); text(b, 380, 238, 1, C_DIM); }
  }
  hudBar(frStreak, frBest, rgb(8, 8, 22), C_TEXT);
  text("STREAK", 262, 9, 1, C_DIM);
  centerText("LEFT/RIGHT move   BOTH / tilt-to-you / tap = drop", 352, 1, now < frHintUntil ? C_TEXT : C_DIM);
  if (frSt == FR_END) {
    const char *t = frWinLen ? (frHumanWon ? "YOU WIN" : "CPU WINS") : "DRAW";
    gfx->fillRoundRect(120, 14, 208, 56, 12, C_PANEL);
    gfx->drawRoundRect(120, 14, 208, 56, 12, frWinLen ? (frHumanWon ? C_GOOD : C_BAD) : C_ACC);
    centerText(t, 22, 3, C_TEXT);
    centerText("RIGHT again   LEFT menu", 54, 1, C_DIM);
  }
  dirty = true;
}

// ---------------- frame ----------------
static void frMoveCol(int d) { frCol = constrain(frCol + d, 0, FR_COLS - 1); play(500, 6); }

static bool frFrame(uint32_t now) {
  homeLock = true;
  float dt = fminf((now - frLast) / 1000.0f, 0.05f);
  frLast = now;
  tuneUpdate(now);
  uint8_t fl = frFlick.update(now, 0.40f, 380);
  if (frTS == 3) {                                                    // search finished
    if (frWaiting && now - frThinkAt >= 450) {
      frWaiting = false;
      int c = frRes;
      frTS = 0;
      if (c < 0 || c >= FR_COLS || frH[c] >= FR_ROWS) { c = 0; while (c < FR_COLS - 1 && frH[c] >= FR_ROWS) c++; }
      frBeginDrop(c, 2, now);
    } else if (!frWaiting) frTS = 0;
  }
  switch (frSt) {
    case FR_PLAY: {
      if (frGuard) {                                                  // ignore buttons still held from the last move
        if (!in.leftDown && !in.rightDown) frGuard = false;
        break;
      }
      if (in.left) {
        if (frPend == 1) { frPend = 0; frTryDrop(frCol, now); break; }
        frPend = -1; frPendAt = now + 70; frRep = now + 380;
      }
      if (in.right) {
        if (frPend == -1) { frPend = 0; frTryDrop(frCol, now); break; }
        frPend = 1; frPendAt = now + 70; frRep = now + 380;
      }
      if (frPend && now >= frPendAt) { frMoveCol(frPend); frPend = 0; }
      if (in.leftDown && in.rightDown) {                              // both held (also after the first press landed)
        frPend = 0; frTryDrop(frCol, now); break;
      }
      if (!frPend && now >= frRep && frRep && (in.leftDown != in.rightDown)) { frMoveCol(in.leftDown ? -1 : 1); frRep = now + 130; }
      if (fl == DIR_L) frMoveCol(-1);
      else if (fl == DIR_R) frMoveCol(1);
      else if (fl == DIR_D) { frTryDrop(frCol, now); break; }
      if (in.tap && in.tapX >= FR_BX && in.tapX < FR_BX + FR_COLS * FR_CELL && in.tapY > 36) {
        frCol = (in.tapX - FR_BX) / FR_CELL;
        frTryDrop(frCol, now); break;
      }
      break;
    }
    case FR_THINK:
      if (!frWaiting && frTS == 0) { frThinkAt = now; frRequestSearch(); }
      break;
    case FR_DROP: {
      frDropV += 2600.0f * dt;
      frDropY += frDropV * dt;
      float ty = (float)frCY(frDropR);
      if (frDropY >= ty) {
        if (frBounce == 0 && frDropV > 300) { frDropY = ty; frDropV = -frDropV * 0.28f; frBounce = 1; play(220, 12); }
        else {
          frDropY = ty;
          frGrid[frDropC][frDropR] = (uint8_t)frDropP; frH[frDropC]++; frMoves++;
          frLastC = frDropC; frLastR = frDropR;
          play(frDropP == 1 ? 330 : 262, 22); play(120, 30);
          ptClear();
          if (frCheckWin(frDropC, frDropR, frDropP)) frEndRound(now, frDropP);
          else if (frMoves >= FR_COLS * FR_ROWS) frEndRound(now, 0);
          else if (frDropP == 1) { frSt = FR_THINK; frWaiting = false; frThinkAt = now; }
          else { frSt = FR_PLAY; frPend = 0; frRep = 0; frGuard = true; frFlick.reset(now); }
        }
      }
      break;
    }
    case FR_END:
      if (now - frStAt > 800) {
        if (in.right) { frNewRound(now); return true; }
        if (in.left) { frCancel = true; return false; }
      }
      break;
  }
  if (now - frLastRender >= 16) { frLastRender = now; frRender(now); }
  return true;
}

Game GAME_FOURROW = {"FOUR ROW", "connect four vs the CPU", 0xFD20, frOpts, 2, frStart, frFrame};
