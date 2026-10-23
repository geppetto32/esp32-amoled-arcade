// display.h - panel init, off-screen canvas and small UI helpers.
#pragma once
#include <Arduino_GFX_Library.h>
#include "config.h"

extern Arduino_Canvas* canvas;   // draw here; call displayFlush() once per frame

void displayInit();
void displayFlush();

// Text helpers (built-in 6x8 font scaled by `size`).
int  uiTextWidth(const char* s, uint8_t size = 1);
void uiText(int x, int y, const char* s, uint16_t color, uint8_t size = 1);
void uiTextC(int y, const char* s, uint16_t color, uint8_t size = 1);        // centred
void uiTextR(int xRight, int y, const char* s, uint16_t color, uint8_t size = 1);
void uiNum(int x, int y, uint32_t v, uint16_t color, uint8_t size = 1);
// Dimmed overlay with a title and up to two lines (nullptr to skip).
void uiOverlay(const char* title, const char* line1, const char* line2);
// Standard game-over overlay: score, best, new-record flag, restart hint.
void uiGameOver(uint32_t score, uint32_t best, bool newRecord);
