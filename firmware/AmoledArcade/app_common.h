// app_common.h - small helpers shared by the APPS (level, clock, metronome).
#pragma once
#include "game.h"

static inline int textW(const char *s, int size) { return (int)strlen(s) * 6 * size; }

// Degree sign (the built-in font has none): a small ring just right of a number drawn at (x, y) with `size`.
static inline void degreeMark(int x, int y, int size, uint16_t col) {
  int r = size + 1;
  gfx->drawCircle(x + r + 2, y + r + 1, r, col);
  if (size >= 3) gfx->drawCircle(x + r + 2, y + r + 1, r - 1, col);
  if (size >= 6) gfx->drawCircle(x + r + 2, y + r + 1, r - 2, col);
  dirty = true;
}

// Hold-to-repeat for a button. Call every frame with the edge (pressed this frame) and the held state.
// Returns the step to apply now: 0 = nothing. With ramp the step grows 1 -> 3 -> 5 the longer it is held.
// (Holding a button never leaves an app: HOME is three quick BOOT taps.)
struct Repeat { uint32_t t0, next; };
static inline int repeatStep(Repeat &r, bool edge, bool down, uint32_t now, bool ramp) {
  if (edge) { r.t0 = now; r.next = now + 300; return 1; }
  if (!down || (int32_t)(now - r.next) < 0) return 0;
  uint32_t held = now - r.t0;
  r.next = now + (held < 550 ? 110 : 70);
  if (!ramp) return 1;
  return held > 750 ? 5 : held > 550 ? 3 : 1;
}

// Thin header strip used by the apps: title on the left, optional badge on the right.
static inline void appHeader(const char *title, const char *badge, uint16_t col) {
  gfx->fillRect(0, 0, W, 30, rgb(8, 10, 24));
  text(title, 10, 8, 2, C_TEXT);
  if (badge && badge[0]) text(badge, W - 10 - textW(badge, 2), 8, 2, col);
}
