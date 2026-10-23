# External controller options - Waveshare ESP32-S3-Touch-AMOLED-1.8 V1 arcade

Researched 2026-10-23. Items marked **[UNVERIFIED]** come from memory or a single weak source.
Nothing was flashed or changed.

## 1. What the board actually exposes (decides the wired options)

Source: pinout image on https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8 (text says "7 GPIO,
1 I2C, 1 UART, 1 USB pad", the image names them). The repo has no schematic.

One 1.27 mm pitch pad row (needs soldered wires or a 1.27 mm header), in order:
`VBUS, GND, 3V3, GND, TXD, RXD, SCL(GPIO14), SDA(GPIO15), IO17, IO18, IO38, IO39, IO40, IO41, IO42`
plus two separate pads `USB_P (GPIO20)` and `USB_N (GPIO19)`.

| Resource | Status for us |
|---|---|
| I2C SCL14/SDA15 + 3V3 + GND on the pad row | Exposed. Same bus as touch/IMU/AXP/RTC/ES8311/TCA9554. |
| Free GPIOs: **17, 18, 38, 39, 40, 41, 42** | 7 free pins, none used by our firmware (our hw.h/hw.cpp use 0,1,2,3,4-7,8-12,14-16,45,46). 38-42 are only JTAG defaults, fine as GPIO. |
| TXD/RXD (GPIO43/44) | Exposed, but USBSerial is HWCDC so UART0 is free: 2 more GPIOs if wanted. |
| USB_P/USB_N (GPIO20/19) | Exposed as pads = a second route to the S3 USB PHY (the same lines as the USB-C). |
| VBUS pad | 5 V from the USB-C when plugged. Whether it is also 5 V on battery is **[UNVERIFIED]**; AXP2101 has no boost converter, so assume NO 5 V on battery. |
| TCA9554 (0x20) pins | NOT broken out on the pad row. Known uses: display reset, AMOLED power, touch reset, TF CD (EXIO0,1,2,7); EXIO3-6 unlisted. Our firmware never touches 0x20. Option 4 is effectively impossible. |

Practical catch: the pad row sits on the board edge; check the `case/` design leaves access, and plan a
short flying lead (4-7 wires) or a small connector. Soldering 1.27 mm pads is the main manual effort.

## 2. Comparison

Effort: S = hours, M = a day, L = several days. Costs EU-ish, **[UNVERIFIED]** unless stated.

| # | Option | Feasible here? | Parts / cost | Effort | Main risks |
|---|---|---|---|---|---|
| 1 | BLE gamepad (host) | Yes in principle | Pad: Xbox Series ~EUR 50-60, Switch 2 Pro ~EUR 80; most 8BitDo ~EUR 25-35 | L | See 2.1 |
| 2 | USB host, owned SNES pad | Yes via USB_P/N pads, not via USB-C | USB-A socket breakout ~EUR 2, 5 V boost ~EUR 3, SNES pad already owned | L | See 2.2 |
| 3 | I2C gamepad (Adafruit seesaw) | **Yes, cleanest** | Adafruit 5743 ~USD 7.5-10 at source, ~EUR 17 on eBay.de (seen), + STEMMA QT cable ~EUR 1-2 | S | Soldering the pads; resale in Greece unconfirmed |
| 4 | TCA9554 free pins | No (not exposed) | - | - | - |
| 4b | Direct wired buttons on GPIO 17/18/38-42 | Yes, simplest | 7 tact switches/arcade buttons, wire: < EUR 5 | S | Digital only (no analog stick) |
| 5 | ESP-NOW DIY pad | Yes | 2nd ESP32 board (C3/S3 supermini ~EUR 5-8) + buttons/joystick + LiPo ~EUR 15 | M-L | Two firmwares, battery on the pad, latency tuning |

### 2.1 Bluetooth LE gamepad
- ESP32-S3 = BLE only (confirmed by the Bluepad32 docs/FAQ). Bluepad32 on ESP32-S3 works "BLE gamepads only".
- Bluepad32's own compatibility table lists **all 8BitDo models as BR/EDR** (Zero 2, SN30 Pro, Pro 2, Lite 2, Ultimate...),
  so on the S3 they will NOT pair. Some newer 8BitDo pads may expose a BLE HID mode: **[UNVERIFIED]**, test one before buying.
- Pads that do work over BLE: Xbox Series (model 1914) with firmware >= v5.15, Xbox Adaptive Controller, Switch 2 Pro/Joy-Con,
  Steam Controller 2nd gen, many generic Android-style BLE pads (confirm "BLE HID/Bluetooth 5" on the box).
- Bluepad32 + Arduino: supported for ESP32-S3 via a separate "ESP32 + Bluepad32" board package (installed beside the
  esp32 package) - docs at https://bluepad32.readthedocs.io/en/latest/plat_arduino/. Docs do not state which arduino-esp32
  core version it tracks, and I found no 2026 release news (latest item seen: v4.0 beta). **[UNVERIFIED]** whether it builds
  with the core your `flash.cmd` uses (FQBN `esp32:esp32:waveshare_esp32_s3_touch_amoled_18`). Switching to a different
  board package means a board-definition mismatch (this board is not in the Bluepad32 package; you would pick a generic S3).
- Alternative: NimBLE-Arduino as a hand-written HID-over-GATT central. Works for pads with a simple HID report map,
  but you write the report parsing yourself. Xbox pads use a vendor report quirk; not a beginner task.
- Extras: pairing UI, reconnect handling, ~30-60 KB RAM and latency 10-30 ms; BLE+AMOLED+SD shares power budget (fine).
- Verdict: nice-to-have wireless, but the highest toolchain risk and you must buy a compatible pad.

### 2.2 USB host (your SNES pad)
- Hardware: the ESP32-S3 has one USB OTG PHY shared with USB-Serial-JTAG. Your sketch uses HWCDC (`USBMode=hwcdc`).
  Running USB Host takes the PHY away from HWCDC: **no serial log and no auto-flash via that port while hosting**; you re-flash with
  BOOT held and a plain USB-C plug (download mode still works since the ROM owns it before boot).
- The USB-C port itself is a device-side connector (CC resistors set it as a sink, and the AXP2101 does not source 5 V out of VBUS), so
  an OTG adapter on it will not power the pad. **[UNVERIFIED]** (no schematic). Do NOT rely on the USB-C for host.
- Better: wire a USB-A socket to the **USB_P/USB_N pads** (GPIO20/19) + GND, give the pad 5 V from a small boost converter
  fed from the battery/3V3 rail (SNES pads draw ~20-50 mA; boost at 5 V/500 mA ~EUR 3), D+/D- 15k pulldowns are inside the ESP32-S3.
  Keep the USB-C unplugged while the pad is attached (the two share D+/D-).
- Software: arduino-esp32 does not ship a ready host/HID gamepad API. Options: esp32beans/ESP32_USB_Host_HID
  (Arduino port of IDF host HID, keyboard-oriented, old toolchain) or the ESP-IDF `usb_host` + HID class driver
  (espp/usb_host component handles gamepads) - which means moving the project to ESP-IDF/arduino-as-component. **[UNVERIFIED]** on core 3.x.
- A cheap SNES-style pad: Grobotronics SNES grey, product 39-00011812, in stock, 1.2 m cable, "plug and play". The page shows "EUR 590", surely EUR 5.90 (parse
  glitch) **[UNVERIFIED]**. Chipset/HID descriptor is not given; typical clones are DragonRise-style (VID 0079:0011) with a
  fixed 8-byte report where the D-pad is two 8-bit axes (0x00/0x7F/0xFF) and buttons are bits. Plug into a PC first and dump the
  descriptor (`usbhid-dump` / Windows "USB Device Tree Viewer") before writing any code.
- Verdict: free pad, but weird cable/power/dual-PHY complexity and a real toolchain fork. Good "stretch" only.

### 2.3 I2C gamepad (recommended)
- Adafruit Mini I2C Gamepad with seesaw (5743): 2-axis analog thumbstick + 6 buttons (A,B,X,Y,Select,Start), 3.3 V, STEMMA QT
  connector, default I2C address 0x50 (jumpers give 4 addresses) - no conflict with 0x20/0x34/0x38/0x51/0x6B. Optional IRQ pin: use GPIO17.
  Libraries: Adafruit_seesaw (Arduino). Source: https://www.adafruit.com/product/5743 (STEMMA QT cable sold separately).
- Needs only 4 wires: 3V3, GND, SDA(14/15 pad), SCL. All other free GPIOs stay free.
- Alternatives (all **[UNVERIFIED]** for availability/price in Greece): SparkFun Qwiic Joystick, M5Stack Joystick2 unit (I2C 0x63),
  PCF8574 button board (digital only; 8 buttons, ~EUR 2, but also 8-bit I2C polling), generic 4-pin joystick modules (analog needs ADC: avoid
  ADC2 pins when Wi-Fi is on).
- Bus risk: SDA/SCL are shared with touch/IMU/AXP; a long (>30 cm) cable adds capacitance; keep the lead short, use 400 kHz max,
  and keep the poll small (one 4-byte bulk read of the button register + 2 ADC reads, ~0.5 ms).
- Resale in Greece: not confirmed on grobotronics/nettop; Adafruit lists Botland (PL/CZ/DE) as EU distributor.

### 2.4 Direct wired buttons (fallback)
- 7 free GPIOs = D-pad 4 + A + B + 1 spare. Use INPUT_PULLUP, switches to GND, 30 ms debounce (same as BOOT today).
- Zero libraries, instant, runs with no bus risk. Loses analog control; the SNES-pad "feel" needs a custom perfboard.

### 2.5 ESP-NOW DIY controller
- Second board (e.g. an ESP32-C3 Supermini) sends 4 bytes (buttons + stick) at 100-200 Hz via ESP-NOW broadcast; the arcade enables Wi-Fi STA +
  ESP-NOW receive (arduino-esp32 `esp_now_*`). No pairing UI needed (fixed MAC / channel).
- Cost ~EUR 15-20 with battery; effort two firmwares + power management for the pad; Wi-Fi radio on the arcade adds ~20-40 mA.
- Good if you want a *wireless* pad AND want toolchain safety (no board-package change) - the best wireless alternative to BLE.

## 3. Recommendation
- **Primary: Option 3, Adafruit seesaw I2C gamepad.** Cleanest on this board (exposed I2C pads), no firmware toolchain change, analog + 6 buttons,
  4 wires, ~EUR 20 total.
- **Fallback: Option 4b, 6-7 wired buttons on GPIO 17/18/38-42** (parts bin, same afternoon). Pick this if the seesaw board cannot be sourced in time.
- Later (nice-to-have): Option 5 ESP-NOW wireless pad; BLE/USB host only if you accept a toolchain fork.

## 4. Parts list (primary)
1. Adafruit Mini I2C Gamepad with seesaw, product 5743 (1x, ~EUR 17-20 delivered).
2. STEMMA QT / Qwiic 4-pin JST-SH cable, ~150-300 mm, or bare-end variant (1x, ~EUR 1-2).
3. 1.27 mm pin header strip (or just solder wires) + 4 pieces of thin silicone wire, heat-shrink.
4. Optional: 10 k pull-ups are already on the board and the gamepad; skip extras.
(Fallback: 7x tactile switches or Kailh/arcade micro switches, 0.14 mm2 wire, perfboard.)

## 5. Integration plan into `Input` (code in `hw.h` / `hw.cpp`; not applied)
1. Extend the struct (keep existing fields so games compile unchanged):
   ```
   struct Input { ...existing..., bool up, down, a, b, x, y, start, select;   // edges this frame
                  bool upDown, downDown, aDown, bDown;                         // held
                  float ax, ay;                                                 // analog -1..1, 0 when centred
                  bool extPresent; };
   ```
2. In `hwBegin()` after Wire init: probe 0x50 with a `Wire.beginTransmission`. If it answers, `ss.begin(0x50)`, set `ss.pinModeBulk(mask, INPUT_PULLUP)`; set `extPresent`.
   No answer = pad absent, everything below is skipped (device works exactly as today).
3. In `inputUpdate()` (rate limit 8-10 ms like the IMU): read `ss.digitalReadBulk(mask)` and two `analogRead`s (ADC channels 14/15 on the seesaw, per Adafruit's example; **[UNVERIFIED]** pin numbers, confirm on the learn page). Build edges by diffing against the previous mask.
4. Map: stick X < -0.5 or D-left -> `in.left`/`in.leftDown` (OR with BOOT/PWR), stick X > 0.5 -> `in.right`; `up/down` from stick Y; A/B buttons -> `in.a/in.b`. Select or Start long press maps to `in.hold` (back to menu).
5. Games opt in: menus use `in.up/down/a`; Snake/Tetris/Pac-Man/Maze read `in.up/down/left/right`; analog games (Breakout, Duck) read `in.ax`. Games that ignore the new fields keep working.
6. Add a setup-menu toggle "External pad: on/off/auto" stored in `prefs` (default auto = on when 0x50 answers).
7. Test: run a `build -> flash.cmd` cycle, check `USBSerial` logs the 0x50 probe + button bits (no flashing done by this research task). Verify tilt, touch and the two onboard buttons still work with the pad attached (bus sharing).

## 6. Open questions to settle before buying
- Does the `case/` design leave access to the pad row? If not, decide a connector location (side hole) first.
- Do you want wireless? If yes choose ESP-NOW (safe) or BLE (needs a Bluepad32-board-package decision).
- Confirm the VBUS pad behaviour on battery with a multimeter **[UNVERIFIED]** if you ever want 5 V from it.

## Sources
- Waveshare wiki + pinout image: https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8 ; repo https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 (no schematic)
- Bluepad32: https://bluepad32.readthedocs.io/en/latest/plat_arduino/ , https://bluepad32.readthedocs.io/en/latest/supported_gamepads/ , FAQ
- Adafruit 5743: https://www.adafruit.com/product/5743
- Grobotronics SNES pad: https://grobotronics.com/gamepad-usb-controller-retro-snes-grey.html
- ESP32 USB host: https://github.com/esp32beans/ESP32_USB_Host_HID , https://components.espressif.com/components/espp/usb_host
- Waveshare V2 pins (TCA9554 usage): https://lcdwizard.com/boards/waveshare-s3-touch-amoled-18-v2 (V2, not identical to V1)
