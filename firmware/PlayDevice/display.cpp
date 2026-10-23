#include "display.h"

Arduino_Canvas* canvas = nullptr;
static Arduino_DataBus* bus = nullptr;
static Arduino_GFX* panel = nullptr;

void displayInit() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);          // peripheral power first
  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, LOW);             // backlight off until first frame

  bus = new Arduino_ESP32PAR8Q(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_WR, PIN_LCD_RD,
                               PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3,
                               PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);
  panel = new Arduino_ST7789(bus, PIN_LCD_RST, DISPLAY_ROTATION, true /*IPS*/, PANEL_W, PANEL_H,
                             PANEL_COL_OFFSET, 0, PANEL_COL_OFFSET, 0);
  canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, panel, 0, 0);
  if (!canvas->begin()) {
    Serial.println("display/canvas init failed (PSRAM set to OPI?)");
    while (true) delay(1000);
  }
  canvas->fillScreen(C_BLACK);
  canvas->flush();
  digitalWrite(PIN_LCD_BL, HIGH);
}

void displayFlush() { canvas->flush(); }

int uiTextWidth(const char* s, uint8_t size) { return (int)strlen(s) * 6 * size; }

void uiText(int x, int y, const char* s, uint16_t color, uint8_t size) {
  canvas->setTextSize(size);
  canvas->setTextColor(color);
  canvas->setCursor(x, y);
  canvas->print(s);
}
void uiTextC(int y, const char* s, uint16_t color, uint8_t size) {
  uiText((SCREEN_W - uiTextWidth(s, size)) / 2, y, s, color, size);
}
void uiTextR(int xRight, int y, const char* s, uint16_t color, uint8_t size) {
  uiText(xRight - uiTextWidth(s, size), y, s, color, size);
}
void uiNum(int x, int y, uint32_t v, uint16_t color, uint8_t size) {
  char b[16];
  snprintf(b, sizeof b, "%lu", (unsigned long)v);
  uiText(x, y, b, color, size);
}

void uiOverlay(const char* title, const char* line1, const char* line2) {
  // Cheap dim: checkerboard of black pixels over the frame.
  for (int y = 0; y < SCREEN_H; y++)
    for (int x = (y & 1); x < SCREEN_W; x += 2) canvas->drawPixel(x, y, C_BLACK);
  canvas->fillRect(50, 40, 220, 90, C_BG);
  canvas->drawRect(50, 40, 220, 90, C_WHITE);
  uiTextC(52, title, C_WHITE, 3);
  if (line1) uiTextC(88, line1, C_YELLOW, 1);
  if (line2) uiTextC(106, line2, C_DIM, 1);
}

void uiGameOver(uint32_t score, uint32_t best, bool newRecord) {
  char a[40];
  snprintf(a, sizeof a, "Score %lu   Best %lu", (unsigned long)score, (unsigned long)best);
  uiOverlay("GAME OVER", a, newRecord ? "NEW RECORD!   A = retry" : "A = retry");
}
