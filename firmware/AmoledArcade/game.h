// game.h - how a game plugs into the arcade launcher.
//
// The launcher shows the game list, then the game's options page (THEME, CONTROL, ...) and START.
// While a game runs, the launcher calls inputUpdate() + imuUpdate() every loop, handles
// the HOME gesture itself (tap BOOT 3 times within 0.8 s, gamepad START+SELECT, or touch-and-hold the top-left corner
// for 1.5 s = back to the launcher), calls game.frame(now), then present(). Holding a button never leaves.
// A game draws into `gfx` (see hw.h) and sets `dirty` when it changed the picture.
#pragma once
#include "hw.h"

struct Option {
  const char *label;          // e.g. "THEME"
  const char *const *values;  // e.g. {"CLASSIC", "REALISTIC", "NEON"}
  uint8_t count;
  const char *key;            // Preferences key (<= 15 chars), value is remembered
  uint8_t value;              // current choice (loaded by the launcher before start())
};

struct Game {
  const char *name;           // shown in the launcher, short (<= 12 chars)
  const char *blurb;          // one line, <= 34 chars
  uint16_t accent;            // launcher card colour
  Option *opts;               // may be nullptr
  uint8_t nopts;
  void (*start)();            // new game (options already loaded)
  bool (*frame)(uint32_t now);// one loop step: read `in`, update, draw. Return false = back to launcher.
};

// Shared helpers for games (in AmoledArcade.ino)
void hudBar(uint32_t score, uint32_t best, uint16_t bg, uint16_t fg);   // top 32 px status bar
void panelBox(const char *title, const char *line2, const char *line3, uint16_t accent);  // centred overlay
uint32_t loadBest(const char *key);
bool saveBest(const char *key, uint32_t score);                         // true = new best

// Register games here (defined in their own .cpp files)
extern Game GAME_SNAKE;
extern Game GAME_FLAP;
extern Game GAME_BREAKOUT;
extern Game GAME_ASTRO;
extern Game GAME_STACKER;
extern Game GAME_DUCK;
extern Game GAME_MAZE;
extern Game GAME_RUNNER;
extern Game GAME_REFLEX;
extern Game GAME_PACMAN;
extern Game GAME_TETRIS;
extern Game GAME_MAHJONG;
extern Game GAME_CUT;
extern Game GAME_MISSILE;
extern Game GAME_LANDER;
extern Game GAME_2048;
extern Game GAME_HOURGLASS;
extern Game GAME_PINBALL;
extern Game GAME_FOURROW;
extern Game GAME_SOKOBAN;
extern Game GAME_MURMUR;
extern Game GAME_GLOWBEAT;
extern Game GAME_ECHO;
extern Game GAME_LIGHTSOUT;
extern Game GAME_DUOPONG;
extern Game GAME_TANK;
extern Game GAME_WOBBLE;
extern Game GAME_HOP;
extern Game GAME_SWARM;
extern Game GAME_RINGBREAKER;
extern Game GAME_GOLF;
extern Game GAME_MINEFIELD;

// Apps (same Game struct, shown in the APPS list; defined in app_*.cpp)
extern Game APP_LEVEL;
extern Game APP_CLOCK;
extern Game APP_METRO;
extern Game APP_WOOD;
extern Game APP_DICE;
extern Game APP_COUNTER;
extern Game APP_TORCH;
extern Game APP_SYSTEM;

// Apps set this every frame while the screen must not idle-dim (the launcher clears it before each frame).
extern bool keepAwake;
// A game or app sets this every frame while quick repeated BOOT taps are normal play (flap, shoot, count ...): the
// triple-tap HOME gesture is then ignored (touch corner / pause menu / game-over screen still leave).
extern bool homeLock;
void wakeScreen();                // undim now (e.g. a timer alarm)
