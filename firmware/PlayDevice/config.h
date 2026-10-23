// config.h - all pins, display geometry, colours and timing for the DIY play device.
#pragma once
#include <Arduino.h>

// ---- Power -------------------------------------------------------------
#define PIN_POWER_ON   15   // must be OUTPUT HIGH before the LCD works
#define PIN_BAT_ADC     4   // battery via x2 divider (valid on battery only)

// ---- Display (8-bit parallel ST7789) ------------------------------------
#define PIN_LCD_BL     38
#define PIN_LCD_RST     5
#define PIN_LCD_CS      6
#define PIN_LCD_DC      7
#define PIN_LCD_WR      8
#define PIN_LCD_RD      9
#define PIN_LCD_D0     39
#define PIN_LCD_D1     40
#define PIN_LCD_D2     41
#define PIN_LCD_D3     42
#define PIN_LCD_D4     45
#define PIN_LCD_D5     46
#define PIN_LCD_D6     47
#define PIN_LCD_D7     48

// Arduino_GFX rotation. Landscape options: 1 or 3 (they are 180 deg apart).
// Intended: USB-C on the RIGHT of the screen. If the picture is upside down
// (USB-C on the left), change 1 -> 3 (or 3 -> 1).
#define DISPLAY_ROTATION 1

#define SCREEN_W 320
#define SCREEN_H 170
#define PANEL_W  170   // physical panel size, portrait
#define PANEL_H  320
#define PANEL_COL_OFFSET 35   // 170-wide panel inside a 240-wide controller

// ---- Buttons (active LOW, INPUT_PULLUP) ----------------------------------
#define PIN_BTN_UP      1
#define PIN_BTN_DOWN    2
#define PIN_BTN_LEFT    3
#define PIN_BTN_RIGHT  10
#define PIN_BTN_A      11
#define PIN_BTN_B      12
#define PIN_BTN_START  16
#define PIN_BTN_SELECT 21
#define PIN_BOOT_BTN    0   // onboard, acts as SELECT
#define PIN_USER_BTN   14   // onboard, acts as START

// ---- Sound ----------------------------------------------------------------
#define PIN_BUZZER     13

// ---- Timing -----------------------------------------------------------------
#define TICK_MS          17       // fixed timestep, ~59 Hz
#define BACK_HOLD_MS   1000       // hold SELECT this long to return to menu

// ---- Colours (RGB565) ---------------------------------------------------------
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_BG      RGB(11, 18, 32)
#define C_GRID    RGB(24, 34, 52)
#define C_DIM     RGB(120, 130, 150)
#define C_RED     RGB(239, 68, 68)
#define C_GREEN   RGB(22, 163, 74)
#define C_CYAN    RGB(34, 211, 238)
#define C_YELLOW  RGB(250, 204, 21)
#define C_ORANGE  RGB(249, 115, 22)
#define C_BLUE    RGB(59, 130, 246)
#define C_PURPLE  RGB(168, 85, 247)
#define C_PINK    RGB(236, 72, 153)
