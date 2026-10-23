// game.h - common game interface. Games must not use START / hold-SELECT (menu owns them).
#pragma once
#include <stdint.h>

struct Game {
  const char* name;
  const char* key;      // short NVS key for the high score
  void (*init)();       // called when the game is started / restarted from the menu
  void (*update)();     // one fixed timestep (TICK_MS); inputUpdate() already ran
  void (*draw)();       // draw into the canvas (no flush)
};

extern const Game gameSnake;
extern const Game gameTetris;
extern const Game gameBreakout;
extern const Game gamePong;
extern const Game gameInvaders;
