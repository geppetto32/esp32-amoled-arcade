// app_system.cpp - SYSTEM: read-only info page (battery, uptime, memory, flash, SD, RTC, build, IMU, gamepad).
//   RIGHT or tap = rescan the SD card. Tap BOOT 3 times = back (home).
#include "app_common.h"

static SdStatus sySd;
static bool syHaveSd;
static uint32_t syLast;

static void syScanSd() {
  if (!sdPresent()) sdBegin();
  sySd = sdInfo();
  syHaveSd = true;
}

static void syStart() {
  syLast = 0;
  syHaveSd = false;
  syScanSd();
}

static int syY;
static void syRow(const char *label, const char *val, uint16_t col = C_TEXT) {
  text(label, 10, syY, 2, C_DIM);
  text(val, 110, syY, 2, col);
  syY += 22;
}

static void syDraw(uint32_t now) {
  gfx->fillScreen(C_BG);
  appHeader("SYSTEM", "read only", C_DIM);
  syY = 38;
  char b[40];

  int bat = battery(), mv = batteryMv();
  if (bat < 0) snprintf(b, sizeof b, "?");
  else if (mv > 0) snprintf(b, sizeof b, "%d %%   %d.%02d V", bat, mv / 1000, (mv % 1000) / 10);
  else snprintf(b, sizeof b, "%d %%", bat);
  syRow("BATTERY", b, bat >= 0 && bat < 15 ? C_BAD : C_TEXT);
  bool ch = charging();
  syRow("POWER", ch ? "CHARGING" : "ON BATTERY / FULL", ch ? C_ACC : C_TEXT);

  uint32_t s = now / 1000;
  if (s >= 86400) snprintf(b, sizeof b, "%lud %02lu:%02lu:%02lu", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24),
                           (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(b, sizeof b, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  syRow("UPTIME", b);

  snprintf(b, sizeof b, "%u / %u KB free", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getHeapSize() / 1024));
  syRow("HEAP", b);
  snprintf(b, sizeof b, "%u / %u KB free", (unsigned)(ESP.getFreePsram() / 1024), (unsigned)(ESP.getPsramSize() / 1024));
  syRow("PSRAM", b);
  snprintf(b, sizeof b, "%u MB", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  syRow("FLASH", b);
  snprintf(b, sizeof b, "%s r%d  %u MHz", ESP.getChipModel(), (int)ESP.getChipRevision(), (unsigned)ESP.getCpuFreqMHz());
  syRow("CHIP", b);

  if (!sdPresent() || !sySd.present) syRow("SD", "NO CARD", C_DIM);
  else {
    if (sySd.totalMB >= 1024) snprintf(b, sizeof b, "%s %.1f GB, %.1f free", sySd.type, sySd.totalMB / 1024.0f, sySd.freeMB / 1024.0f);
    else snprintf(b, sizeof b, "%s %lu MB, %lu free", sySd.type, (unsigned long)sySd.totalMB, (unsigned long)sySd.freeMB);
    syRow("SD", b);
  }

  RtcTime t;
  if (rtcRead(t) && t.ok) {
    snprintf(b, sizeof b, "%02d:%02d:%02d  %02d.%02d.%04d", t.hour, t.minute, t.second, t.day, t.month, t.year);
    syRow("RTC", b);
  } else syRow("RTC", rtcRead(t) ? "NOT SET" : "NO ANSWER", C_ACC);

  syRow("BUILD", __DATE__ " " __TIME__);

  float x, y, z;
  if (imuRaw(x, y, z)) {
    snprintf(b, sizeof b, "%+.2f %+.2f %+.2f g", x, y, z);
    syRow("IMU", b);
    snprintf(b, sizeof b, "%+.2f  %+.2f", tiltX(), tiltY());
    syRow("TILT", b);
  } else {
    syRow("IMU", "NOT FOUND", C_BAD);
    syRow("TILT", "-", C_DIM);
  }
  if (stickPresent() || gamepadConnected()) {
    snprintf(b, sizeof b, "%s %+.2f %+.2f", stickPresent() && gamepadConnected() ? "BOTH" : stickPresent() ? "ANALOG" : "I2C PAD", in.ax, in.ay);
    syRow("JOYSTICK", b, C_GOOD);
  } else syRow("JOYSTICK", "none", C_DIM);
}

static bool syFrame(uint32_t now) {
  if (in.b) return false;
  if (in.right || in.tap || in.a) { syScanSd(); syLast = 0; }
  if (now - syLast >= 250) {
    syLast = now;
    syDraw(now);
  }
  return true;
}

Game APP_SYSTEM = {"SYSTEM", "battery, memory, SD, IMU info", 0x8410, nullptr, 0, syStart, syFrame};
