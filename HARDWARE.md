# Hardware

## Current device: Waveshare ESP32-S3-Touch-AMOLED-1.8 (V1)
ESP32-S3, 16 MB flash, 8 MB PSRAM, 368x448 AMOLED (SH8601), FT3168 touch, QMI8658 IMU, AXP2101 power chip,
ES8311 audio codec + speaker amp, micro-SD slot. No extra parts are needed.

| Function | Pins / address |
|---|---|
| Display QSPI | SDIO0-3 = GPIO 4, 5, 6, 7; SCLK 11; CS 12 |
| I2C bus | SDA 15, SCL 14: touch FT3168 0x38, IMU QMI8658 0x6B, AXP2101 0x34, ES8311 codec, IO expander TCA9554 0x20 |
| I2S audio | MCLK 16, BCLK 9, DIN 10, WS 45, DOUT 8; speaker amp enable 46 |
| micro-SD (SDMMC 1-bit) | CLK 2, CMD 1, D0 3 (FAT32 card) |
| Buttons | BOOT = GPIO0; PWR via the AXP2101 (read over I2C) |

Notes: V1 is discontinued; V2 uses different display/touch chips (CO5300 + CST820) and needs driver changes.
Flashing quirk: hold BOOT while replugging USB.

---

## Original plan (not built): LilyGO T-Display-S3

**Platform:** LilyGO T-Display-S3 (ESP32-S3, 1.9" 320x170 colour screen, USB-C, built-in LiPo charger).
Chosen because it works in the Arduino IDE, already has the battery charger and connector, and has
plenty of free pins for our own buttons. Runner-up: Raspberry Pi Pico 2 + Waveshare Pico-LCD-1.3
(buttons already on the screen board, but needs an extra charger and fixes the button positions).

## Bill of materials (prices are estimates; check before ordering)
| # | Part | Where | ~EUR |
|---|---|---|---|
| 1 | LilyGO T-Display-S3, **non-touch, 16 MB / 8 MB PSRAM**, soldered or unsoldered headers | lilygo.cc (DE warehouse), AliExpress LilyGO store | 22-25 |
| 1 | LiPo 503450 3.7 V 1000 mAh with protection, **JST 1.25 mm 2-pin** plug | AliExpress / eBay.de | 5-6 |
| 8 | Tact switch 6x6 mm, **5 mm tall** (THT) | Grobotronics / BerryBase / AliExpress | 1-2 |
| 1 | Slide switch SS12D00 (power, cuts battery +) | same | 0.5 |
| 1 | Passive piezo buzzer 12 mm | same | 0.5-1 |
| 1 | Perfboard, cut to 60 x 42 mm | same | 1 |
| - | 28-30 AWG silicone wire, heat-shrink | same | 2 |
| 4 | M2 x 10 wood screws (back plate) + 4 M2 x 6 (perfboard to spacers) | hardware shop | 2 |
| - | Wood: 3 mm plywood or veneered ply (plates), 14 mm hardwood (frame), offcuts for caps | workshop | - |
| | **Total** | | **~35-40** |

Check the battery plug polarity against the board's silkscreen before plugging in. Many cells come wired the wrong way round.

## Pins
Buttons connect between the GPIO and GND, using the internal pull-up, so no resistors are needed.
| Function | GPIO | | Function | GPIO |
|---|---|---|---|---|
| Up | 1 | | A | 11 |
| Down | 2 | | B | 12 |
| Left | 3 | | Start | 16 (also onboard button 14) |
| Right | 10 | | Select | 21 (also onboard BOOT 0) |
| Buzzer + | 13 (other leg to GND) | | Battery sense | 4 (on board) |

Used by the board, do not touch: 5-9, 15, 38-42, 45-48 (screen), 17/18 (I2C), 43/44 (serial).
**Before soldering, check that 1, 2, 3, 10-13, 16 and 21 are on the headers of your board.** Use the
PinMap image in the LilyGO GitHub repo.

## Wiring
```
 Perfboard (under front plate)                 T-Display-S3 header
 [UP]---------------------------------------- GPIO1
 [DOWN]-------------------------------------- GPIO2
 [LEFT]-------------------------------------- GPIO3
 [RIGHT]------------------------------------- GPIO10
 [A]----------------------------------------- GPIO11
 [B]----------------------------------------- GPIO12
 [START]------------------------------------- GPIO16
 [SELECT]------------------------------------ GPIO21
 other leg of every switch -> common GND bus  GND
 piezo (+) ---------------------------------- GPIO13, piezo (-) -> GND
 LiPo (+) -> slide switch -> board battery JST (+);  LiPo (-) -> JST (-)
```
- Tact switches have 4 legs in two connected pairs. Use two diagonal legs so you always get the switch contact.
- D-pad switch centres sit 7.5 mm from the D-pad centre. The case script places all switch centres; print the front plate DXF 1:1 and use it as a drilling template for the perfboard.
- The battery only charges while the slide switch is ON, because the switch is in the battery lead. That is fine. Leave it on while charging.

## Assembly order
1. Flash the firmware over USB and test with the two onboard buttons before building anything.
2. Wire the perfboard and test all 8 buttons on the bench.
3. Cut the case (see `case/README.md`), glue the front plate to the frame and the spacers under the front.
4. Fit the caps, then screw the perfboard to the spacers. Seat the board against the screen window
   and fix it with a strip of foam or a dab of hot glue. Fit the battery and switch, then close the back.
