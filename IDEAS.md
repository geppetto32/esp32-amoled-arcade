# AmoledArcade - ideas for what to build next

Date: 2026-10-23 (design notes; several items have since been built, see README). Board: Waveshare ESP32-S3-Touch-AMOLED-1.8 **V1** (SH8601 + FT3168). Research only: nothing here was built, flashed or installed.
Legend: **PROVEN** = used on this board by the AmoledArcade firmware; **EXAMPLE** = Waveshare official example exists, not yet run by the author; **UNVERIFIED** = no local evidence.
Effort: S = an evening, M = a few evenings, L = weeks.

Note: Waveshare's page says V1 is **discontinued** (V2 uses CO5300 + CST820, different drivers). Keep one working spare plan: V2 would need driver changes.

## 1. Capability inventory

| Feature | What it enables | Status / evidence |
|---|---|---|
| 368x448 AMOLED (SH8601, QSPI) | True blacks, vivid colour, always-on dim clock, images/GIFs | PROVEN: `AmoledArcade` (Arduino_GFX, canvas in PSRAM) and `WAVESHARE_AMOLED_V1_REFERENCE.md` |
| FT3168 touch | Menus, sliders, on-screen buttons, drawing, swipe gestures | PROVEN: tap detection in `hw.cpp`; hold-to-speak in an earlier prototype. Multi-touch not used (reads 1 point) |
| QMI8658 accel | Tilt control, level/angle gauge, shake, step count | PROVEN: accel +-4 g, 125 Hz in `hw.cpp` |
| QMI8658 gyro | Rotation games, gesture detect, heading (drifts) | EXAMPLE: `11_LVGL_QMI8658_ui`. Not enabled in the arcade |
| AXP2101 | Battery %, charge state, PWR short press | PROVEN: `battery()`, `charging()`, PWR as a button (`hw.cpp`). Power-off, wake, voltage, temperature, charge current: UNVERIFIED (example `12_LVGL_AXP2101_ADC_Data`) |
| ES8311 + speaker amp (GPIO46) | Tones, WAV/MP3 playback, mic recording | PROVEN: playback in arcade (`es8311.c`). Mic: EXAMPLE `15_ES8311` (record/loop); mic is UNVERIFIED here |
| PCF85063 RTC | Clock that survives reboot (needs backup cell on the pads for power-off) | EXAMPLE: `05_GFX_PCF85063_simpleTime`, `10_LVGL_PCF85063_simpleTime`. UNVERIFIED on the author's board |
| TCA9554 IO expander (I2C 0x20) | Extra pins; the official examples drive display/touch resets through it | EXAMPLE: used in `14_LVGL_SD_Test` init. Arcade does not touch it and works. Pin roles UNVERIFIED |
| micro-SD (SDMMC CLK=2 CMD=1 D0=3) | Saves, ROMs, music, photos, logs | EXAMPLE: `14_LVGL_SD_Test` (SD_MMC.setPins). UNVERIFIED on the author's board: no card has been tested |
| Wi-Fi 2.4 GHz | NTP, weather, OTA, robot status, ESP-NOW | PROVEN in an earlier prototype (scan/connect diagnostics); example `06_GFX_ESPWiFiAnalyzer`. Not in arcade |
| BLE 5 | HID gamepad/remote, phone link | UNVERIFIED (no local sketch) |
| 16 MB flash / 8 MB PSRAM | Big assets, double buffer, emulators, OTA slots | PROVEN: PSRAM canvas. Partition "16M Flash (3 MB APP / 9.9 MB FATFS)" per reference. A 3 MB app slot rules out OTA with two slots; needs a new partition table |
| BOOT (GPIO0) + PWR | The only two physical buttons | PROVEN: arcade maps them to LEFT/RIGHT, hold BOOT 1 s = back |
| USB-C (HWCDC / JTAG) | Serial debug, flashing | PROVEN. Flashing quirk: hold BOOT while replugging (reference doc) |
| 4 hardware limits | No magnetometer, no vibration motor, no dedicated D-pad, 7 free pads only | Known from Waveshare page |

## 2. More games (10 ideas)

Existing: snake, flap, breakout, astro, stacker, duck, maze, runner, reflex, pacman.

1. **Marble Labyrinth (tilt)** - tilt rolls a ball through holes, between walls, to goals. Controls: tilt only, buttons = restart/hint. Fun: IMU feel + AMOLED glow. Different from `maze` if physics (momentum, friction) is real. Effort S-M.
2. **Pinball** - tilt = nudge, LEFT/RIGHT = flippers (perfect fit for two buttons). Fun: two buttons are literally flippers; shake = tilt penalty. Effort M (ball physics, one table).
3. **Tetris** - touch/tilt left-right, buttons: LEFT = rotate, RIGHT = drop (tilt moves). Already exists in the old `PlayDevice` firmware (logic tested on PC), so porting is cheap. Effort S-M.
4. **Pong / Air hockey vs AI** - tilt moves paddle, buttons = serve / power. Touch lets a second person play on the glass (2-player on one device). Effort S.
5. **Space Invaders** - tilt to move, either button fires. Old `PlayDevice` firmware had it (untested). Effort S.
6. **Rhythm tapper** - four lanes by touch, beat map synced to ES8311 tones/WAV. Fun: bright AMOLED notes, touch is precise. Effort M (chart format, latency calibration).
7. **Sand / water toy ("falling sand")** - tilt tilts gravity for a pixel simulation. Relaxing, shows off IMU and AMOLED. Effort M, CPU-light with tricks.
8. **Memory / Simon** - four colours and tones, touch to repeat. Effort S. Good first SD-asset user.
9. **2048 / sliding puzzle** - swipe on touch, or tilt direction. Effort S.
10. **Minesweeper-lite / Sudoku** - touch grid, buttons toggle flag/mode. Effort M. Doubles as a "quiet" game.
11. **Duel (2 devices, ESP-NOW)** - tank or pong over ESP-NOW between two boards. Needs two boards (UNVERIFIED, user has one). Effort L.
12. **Pocket pet (Tamagotchi-style)** - RTC-driven needs, touch petting, shake to wake; runs for days. Effort M-L. Good deep-sleep showcase.

### Retro emulators: is it realistic?
- **Retro-Go** (https://github.com/ducalex/retro-go, GPLv2): NES, Game Boy / Color, Master System, Game Gear, SG-1000, Genesis, PC Engine, Lynx, ColecoVision, Game & Watch, DOOM; SNES is "slow". ROMs come from SD, one per zip. Officially supported targets are ODROID-GO and MRGC-G32 (page checked 2026-10-23); other ESP32-S3 targets are community ports, so S3 support must be checked in `components/retro-go/README.md` and PORTING.md. **No existing port for this board was found.** A port needs ESP-IDF (not Arduino) plus an SH8601 QSPI driver, SD_MMC, and an input shim. Effort L.
- **NES:** ESP32 NES projects exist for S3 (Nofrendo fork needing PSRAM; "Anemoia-ESP32" claims ~50-60 fps for ~79% of the library via six mappers; both from search snippets, unverified on this screen). 256x240 NES scales to 368x448 portrait or ~448x368 landscape with letterbox. Performance is plausible; audio via ES8311 plausible.
- **Game Boy (160x144)** and **Game Gear** are the easiest and fit the screen nicely at 2x. Best first emulator.
- **Input is the real problem:** a NES/GB needs D-pad + A/B + Start/Select. Options: touch overlay (no tactile feedback, poor), tilt as D-pad plus the two buttons as A/B, or a BLE gamepad. Honest take: playable for slow games, frustrating for platformers. Soldering extra buttons to the 7 pads is the real fix and fits the wooden-case idea (the AMOLED board is small; check what the case plan says).
- **Legal ROMs only:** homebrew (NESdev Compo / itch.io homebrew, GB Compo, "Zelda-like" homebrew), public-domain/CC titles, and your own dumps of cartridges they own. Do not download commercial ROMs.
- **Verdict:** Not a quick win. Do first: SD works + a simple file browser. Then try a GB emulator (e.g. a minimal peanut-gb style core; UNVERIFIED) inside the Arduino sketch, which is much smaller than porting Retro-Go. Retro-Go port is a stretch goal.

## 3. Non-game functions (14 ideas)

| # | Idea | Needs | Effort | Risk / notes |
|---|---|---|---|---|
| 1 | **Watch face / clock** (analog + digital, always-dim AMOLED) | RTC (EXAMPLE), AXP | S-M | Set time via NTP or touch. Backup cell on RTC pads, else time lost on full power-off. AMOLED burn-in: shift the picture, keep it dim |
| 2 | **NTP time sync** | Wi-Fi (PROVEN), RTC | S | Credentials in `secrets.h` style file, never on SD in plain text without thinking |
| 3 | **Alarm / timer / pomodoro / stopwatch** | RTC, ES8311, touch | S-M | Alarm needs the device awake or RTC-alarm wake (UNVERIFIED: RTC INT pin routing) |
| 4 | **Tilt level / angle gauge** (woodworker) | QMI8658 accel (PROVEN) | S | Calibrate per axis, show degrees, hold/freeze, zero button. Accuracy +-0.5 deg after calibration (estimate). Great first useful tool |
| 5 | **Step counter / activity** | accel; chip has maybe a hardware pedometer (UNVERIFIED) | M | Software peak detection OK when worn at the hip; unreliable on a desk |
| 6 | **Compass** | NO magnetometer | - | Cannot give north. Possible: gyro-integrated relative heading (drifts in minutes); or phone-fed heading over BLE. Not recommended |
| 7 | **Voice memo recorder to SD** | mic (EXAMPLE), SD, WAV writer | M | Needs SD first; 16 kHz mono WAV = 32 KB/s. Mic path UNVERIFIED |
| 8 | **Music player (WAV/MP3)** | SD, I2S playback (PROVEN), MP3 lib (e.g. ESP8266Audio, UNVERIFIED) | M | Speaker is tiny, quality limited. Optional BT is not possible (BLE only, no A2DP on S3) |
| 9 | **Photo / GIF viewer** | SD, JPEG decoder (JPEGDEC / TJpgDec), AnimatedGIF | M | 368x448 JPEGs; good showcase for AMOLED. Pre-resize on PC to save time |
| 10 | **BLE gamepad / media remote for PC** | BLE HID (e.g. NimBLE-Arduino, UNVERIFIED) | M | Tilt = stick, buttons = A/B; or slide-show/volume remote. Pairing + HID descriptor fiddly |
| 11 | **Wi-Fi weather / info display** | Wi-Fi, HTTPS, JSON (ArduinoJson) | S-M | Public API (e.g. Open-Meteo, no key). TLS RAM is fine with PSRAM. Needs 2.4 GHz network |
| 12 | **ESP-NOW walkie-talkie / 2-device chat** | second board, mic+speaker, ESP-NOW | L | Needs two boards. Voice over ESP-NOW is low-quality at best (UNVERIFIED) |
| 13 | **Workshop tools**: metronome, unit converter (mm/inch/fractions), board-feet and sheet-cut calculators, fraction-of-inch ruler on screen | touch + sound | S each | Pure software, zero risk. Metronome uses `play()`. Add "cut list" scratchpad saved to SD |
| 14 | **Status screen for a home project** (e.g. a robot or server) | Wi-Fi, HTTP GET of a status JSON | S-M | Just a poller plus 3-4 gauges. Keep it read-only, no commands from the handheld |
| 15 | **Notification display** (phone/PC via BLE or Wi-Fi HTTP push) | BLE/Wi-Fi | M | iOS/Android notification sharing is not easy; HTTP push from PC is simple |

## 4. SD card plan

- **Hardware:** SD_MMC in **1-bit mode** with `setPins(CLK=2, CMD=1, D0=3)`; format FAT32 (also fine up to 32 GB cards; avoid exFAT). 1-bit gives ~1-2 MB/s typical (estimate), plenty for saves/photos/audio; a GB emulator is fine, NES maybe.
- **Folder layout:** `/arcade/saves/` (scores, per-game files), `/arcade/settings.json`, `/roms/{gb,nes,gg}/`, `/music/`, `/photos/`, `/memos/`, `/logs/`, `/assets/{game}/` (sprites, levels).
- **No card present:** `sdOk` flag at boot (try mount, 1 s timeout). Everything falls back to NVS/Preferences (already exists), SD features hidden or greyed with "NO CARD". Never block startup or a game on SD. Mount lazily on first use, handle removal by catching write errors and unmounting.
- **Data policy:** NVS stays the source for scores and settings (reliable, fast); SD is a mirror/backup plus the home for big data. Write score files atomically (write temp, rename). Wear: write on game-over only, not per frame.
- **Caveats (UNVERIFIED):** The SD card pins overlap nothing in `hw.cpp` (display 4-7,11,12; touch 14,15,21; I2S 8,9,10,16,45,46) so no conflict on paper. GPIO3 is a strapping pin: leave as is, it worked in the official example. 1-bit mode must work with the TCA9554 setup the example performs; check whether the example needs the expander to power the card before relying on it.
- **First small step:** Settings > "SD STATUS" screen (card present, size, free space) read-only. Then: write scores to `/arcade/scores.txt` if present. See roadmap step 1.

## 5. Platform improvements

1. **"APPS" list next to "GAMES"** - reuse the `Game` struct with a `kind` field (game/app); the launcher shows two tabs. The apps (clock, level, timer) use the same `start/frame` hooks. S.
2. **Brightness setting** - `setBrightness` already used (200). Add a 3-5 step setting in NVS. S, also saves battery on AMOLED.
3. **Volume setting** - ES8311 volume register or scale samples; add to Settings. S.
4. **Idle dim and deep sleep** - dim after 30 s, light sleep / deep sleep after N minutes; wake with PWR (AXP IRQ) or BOOT (GPIO0). Verify that waking from PWR works (AXP2101 pin-level behaviour UNVERIFIED). M.
5. **Battery saver** - lower CPU to 80-160 MHz, lower brightness, Wi-Fi off. S-M. Show voltage/% in Settings.
6. **Wi-Fi manager** - one-time setup via captive portal or `secrets.h`; store SSID in NVS. S-M. Needed by clock/weather/robot ideas.
7. **OTA update over Wi-Fi** - needs a new partition table with two app slots (3 MB x2 plus data); the shipped one is 3 MB APP / 9.9 MB FATFS. Test carefully; keep a USB recovery path. M-L.
8. **USB mass-storage for the SD card** - ESP32-S3 USB OTG can do MSC (TinyUSB). BUT the board currently uses HWCDC/JTAG; switching to TinyUSB changes serial/flash behaviour (UNVERIFIED). Safer: Wi-Fi file upload page ("SD web browser") or just pull the card. L, low priority.
9. **Serial log to SD** for field debugging. S once SD works.
10. **Rotate/portrait support** - hold the board either way; the IMU auto-rotates menus. M.

## 6. Recommended roadmap (top 5)

1. **SD status in Settings** - mount via SD_MMC 1-bit, show card size/free and "NO CARD" gracefully. *Done when:* a card shows its size in Settings, and the arcade still boots and plays with the card removed.
2. **Brightness + volume settings, saved in NVS** (plus idle dim). *Done when:* both persist across a reboot and the screen dims after 30 s idle and wakes on touch/button.
3. **APPS tab with a Level gauge and a Workshop clock/stopwatch** - first real non-game apps; clock needs RTC test (set time by touch). *Done when:* the angle gauge reads 0.0 on a flat bench, a 45 deg piece reads about 45 deg, and the clock keeps time across a USB reset.
4. **Save high scores to SD (mirror) + a Wi-Fi NTP time sync** - uses roadmap 1 and the Wi-Fi support proven in an earlier prototype. *Done when:* scores appear in `/arcade/scores.txt`, and the RTC sets itself from NTP over the home Wi-Fi.
5. **Two more games: Marble Labyrinth (tilt) and Pinball (two flippers)** - best fit for the hardware. Decide emulator later after SD works. *Done when:* both are in the launcher with scores saved, and each plays at a steady 30+ fps (estimate, measure with a frame counter).

Stretch list after these: status screen, photo viewer, voice memo, then a Game Boy emulator experiment from SD.

Sources: Waveshare docs page; https://github.com/ducalex/retro-go; Hackaday "60 FPS NES Emulator On ESP32"; Hackster "How I built a working ESP32-based NES emulator in 2026"; local reference files named above.
