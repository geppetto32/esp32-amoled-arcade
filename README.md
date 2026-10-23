<div align="center">

# esp32-amoled-arcade

**A pocket arcade: 30+ tilt-and-touch games and workshop tools on a 1.8" AMOLED ESP32-S3.**

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-Waveshare%20AMOLED%201.8-red)
![Arduino](https://img.shields.io/badge/Arduino-esp32%20core%203.3.10-00979D)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

<img src="docs/screenshots/launcher_2x.png" width="360" alt="Launcher">

</div>

**English** · [Ελληνικά](README.el.md)

One small board, no soldering needed: 368x448 AMOLED with touch, tilt sensor, speaker, battery
charger, RTC and micro-SD slot. Hold it like a handheld, tilt to steer, use the two buttons on top
and the touch screen. Games, a set of woodworking tools and a settings page all live in one
firmware.

## Contents
[Features](#features) | [Hardware](#hardware--what-to-buy) | [Games](#games-gallery) | [Apps](#apps) | [Controls](#controls) | [Build and flash](#build-and-flash) | [Layout](#project-layout) | [Case](#wooden-case) | [Roadmap](#roadmap) | [Credits](#credits)

## Features
- **32 games**: classics (Snake, Tetris, Pacman, Breakout, Pinball), tilt toys (Lander, Hourglass, Tank, Murmur), puzzles (2048, Sokoban, Minefield, Mahjong), board and rhythm games (Four Row, Glow Beat), and a workshop game (Cut Length).
- **8 apps**: Level, Clock (stopwatch and timer), Metronome, Wood Calc, Dice, Counter, Torch, System.
- **Tilt control** with the QMI8658 IMU, plus touch, plus an optional analog stick or I2C gamepad.
- **Touch scrolling** lists (drag, flick, rubber-band), tap a card to open it.
- **High scores in flash**, and every game keeps its own options (theme, control, difficulty).
- **Settings**: sound on/off, volume, brightness, idle dim (30 s / 60 s / 3 min / off), SD card status, joystick test, button setup, tilt check.
- Works with **no SD card**. The card is optional.

<p align="center"><img src="docs/screenshots/settings_2x.png" width="280" alt="Settings"></p>

## Hardware / what to buy
| Part | Notes |
|---|---|
| **Waveshare ESP32-S3-Touch-AMOLED-1.8** (V1) | Buy at [Grobotronics (Greece)](https://grobotronics.com/waveshare-esp32-s3-touch-amoled-display-1-8-368448.html). Docs: [docs.waveshare.com](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8) |
| LiPo battery (optional) | Use the board's battery connector. Check the plug polarity against the board's silkscreen before connecting: many cells come wired the wrong way round |
| micro-SD card (optional) | FAT32. **Buy a genuine card from a reputable shop.** Fake high-capacity cards exist: test a new one with [H2testw](https://www.heise.de/download/product/h2testw-50539) (Windows) or [F3](https://github.com/AltraMayor/f3) (Linux/macOS) |
| Thumbstick (optional) | Xbox One/Series replacement stick module (two potentiometers + push switch), wired to the edge pad row (table below) |
| I2C gamepad (optional) | [Adafruit Mini I2C Gamepad (5743)](https://www.adafruit.com/product/5743) on the I2C pads (3V3, GND, SDA, SCL). Not tested on hardware |

The board has a V1 and a V2. This firmware is for **V1** (SH8601 display + FT3168 touch); Waveshare lists V1 as
discontinued and V2 uses different chips (CO5300 + CST820) and would need driver changes.

**Thumbstick wiring** (detected at boot, centre calibrated at boot, so do not touch the stick while it starts):

| Stick module pin | Board pad |
|---|---|
| 3V3 (outer pin of both pots) | 3V3 |
| GND (other outer pin, and switch) | GND |
| X wiper | IO17 |
| Y wiper | IO18 |
| Push switch (other side to GND) | IO38 |

If a direction is wrong, flip `STK_INVERT_X`, `STK_INVERT_Y` or `STK_SWAP_XY` at the top of the stick block in
`firmware/AmoledArcade/hw.cpp`. SETTINGS -> JOYSTICK shows a live test. The stick and the gamepad are untested on hardware.
More options (BLE pad, USB host, ESP-NOW) are researched in [CONTROLLER_OPTIONS.md](CONTROLLER_OPTIONS.md). Pins and I2C addresses: [HARDWARE.md](HARDWARE.md).

## Games gallery
Screenshots are 2x captures of the screen (landscape). Controls are summarised under each game; every game also has an options page.

| | | |
|:--:|:--:|:--:|
| <img src="docs/screenshots/snake_2x.png" width="280" alt="Snake"><br>**Snake**<br>Classic snake with classic, realistic and neon themes.<br><sub>Tilt or buttons</sub> | <img src="docs/screenshots/flap_2x.png" width="280" alt="Flap"><br>**Flap**<br>Tap through the pipe gaps.<br><sub>Tap / either button</sub> | <img src="docs/screenshots/breakout_2x.png" width="280" alt="Breakout"><br>**Breakout**<br>Bounce the ball, clear the bricks.<br><sub>Tilt or buttons</sub> |
| <img src="docs/screenshots/astro_2x.png" width="280" alt="Astro"><br>**Astro**<br>Dodge and blast asteroids.<br><sub>Tilt to move, buttons fire</sub> | <img src="docs/screenshots/stacker_2x.png" width="280" alt="Stacker"><br>**Stacker**<br>Drop blocks and build a tower.<br><sub>Any button / tap</sub> | <img src="docs/screenshots/duck_2x.png" width="280" alt="Duck"><br>**Duck**<br>Duck hunt: aim and shoot the ducks.<br><sub>Tilt or buttons aim, tap shoots</sub> |
| <img src="docs/screenshots/maze_2x.png" width="280" alt="Maze"><br>**Maze**<br>Roll the ball to the goal.<br><sub>Tilt</sub> | <img src="docs/screenshots/runner_2x.png" width="280" alt="Runner"><br>**Runner**<br>Jump the cacti and run forever.<br><sub>Tap / button</sub> | <img src="docs/screenshots/reflex_2x.png" width="280" alt="Reflex"><br>**Reflex**<br>Press the side that lights up.<br><sub>LEFT / RIGHT</sub> |
| <img src="docs/screenshots/pacman_2x.png" width="280" alt="Pacman"><br>**Pacman**<br>Eat the dots, dodge the ghosts.<br><sub>Tilt or buttons</sub> | <img src="docs/screenshots/tetris_2x.png" width="280" alt="Tetris"><br>**Tetris**<br>Hold, ghost, hard drop, levels.<br><sub>Tilt or buttons: move, rotate, drop</sub> | <img src="docs/screenshots/mahjong_2x.png" width="280" alt="Mahjong"><br>**Mahjong**<br>Solitaire, 82 tiles, always solvable.<br><sub>Tilt or LEFT/RIGHT; hint, undo, shuffle</sub> |
| <img src="docs/screenshots/cut_length_2x.png" width="280" alt="Cut Length"><br>**Cut Length**<br>Workshop timing: cut the board at the target length.<br><sub>Hold LEFT/RIGHT slides the saw, both = cut</sub> | <img src="docs/screenshots/missile_2x.png" width="280" alt="Missile"><br>**Missile**<br>Missile Command: defend the cities.<br><sub>Tilt aims, LEFT/RIGHT fire the silos</sub> | <img src="docs/screenshots/lander_2x.png" width="280" alt="Lander"><br>**Lander**<br>Land softly on a pad: moon, Mars or Io.<br><sub>Tilt turns, hold LEFT/RIGHT = thrust</sub> |
| <img src="docs/screenshots/2048_2x.png" width="280" alt="2048"><br>**2048**<br>Slide and merge tiles (3x3 to 5x5).<br><sub>Flick or lean; LEFT undo, RIGHT restart</sub> | <img src="docs/screenshots/hourglass_2x.png" width="280" alt="Hourglass"><br>**Hourglass**<br>Falling-sand toy: sand, water, oil, fire, stone, wood.<br><sub>Tilt = gravity, hold LEFT pours, RIGHT next element</sub> | <img src="docs/screenshots/duo_pong_2x.png" width="280" alt="Duo Pong"><br>**Duo Pong**<br>Two players, one board, first to 7 (or vs CPU).<br><sub>Hold your button: paddle rises</sub> |
| <img src="docs/screenshots/echo_2x.png" width="280" alt="Echo"><br>**Echo**<br>Tilt Simon: repeat the growing light sequence.<br><sub>Tilt to a pad, press a button</sub> | <img src="docs/screenshots/four_row_2x.png" width="280" alt="Four Row"><br>**Four Row**<br>Connect Four against a CPU with three levels.<br><sub>LEFT/RIGHT move, both drop</sub> | <img src="docs/screenshots/glow_beat_2x.png" width="280" alt="Glow Beat"><br>**Glow Beat**<br>Three-lane rhythm game, you play the tune.<br><sub>LEFT / RIGHT / both = centre lane</sub> |
| <img src="docs/screenshots/tilt_golf_2x.png" width="280" alt="Tilt Golf"><br>**Tilt Golf**<br>Nine mini-golf holes with sand, water and slopes.<br><sub>Tilt aims, hold LEFT = power, release = shoot</sub> | <img src="docs/screenshots/hop_2x.png" width="280" alt="Hop"><br>**Hop**<br>Frogger on a neon road and river.<br><sub>Flick the board to hop</sub> | <img src="docs/screenshots/lights_out_2x.png" width="280" alt="Lights Out"><br>**Lights Out**<br>Flip lamps until all are off, 4x4 to 6x6.<br><sub>Tilt cursor, RIGHT toggles, LEFT hint</sub> |
| <img src="docs/screenshots/minefield_2x.png" width="280" alt="Minefield"><br>**Minefield**<br>Minesweeper with a tilt cursor.<br><sub>Tilt, RIGHT reveals, LEFT flags</sub> | <img src="docs/screenshots/murmur_2x.png" width="280" alt="Murmur"><br>**Murmur**<br>Zen flock of 300 starlings over a sunset.<br><sub>Tilt = light, hold LEFT gather, RIGHT scatter</sub> | <img src="docs/screenshots/pinball_2x.png" width="280" alt="Pinball"><br>**Pinball**<br>One full table: bumpers, ramp, multiball.<br><sub>LEFT/RIGHT flippers, shake = nudge</sub> |
| <img src="docs/screenshots/ring_breaker_2x.png" width="280" alt="Ring Breaker"><br>**Ring Breaker**<br>Circular Arkanoid with a gravity well.<br><sub>Tilt orbits the paddle, both = launch</sub> | <img src="docs/screenshots/sokoban_2x.png" width="280" alt="Sokoban"><br>**Sokoban**<br>Push the crates, 24 levels.<br><sub>Tilt steps, LEFT undo, RIGHT level select</sub> | <img src="docs/screenshots/swarm_2x.png" width="280" alt="Swarm"><br>**Swarm**<br>Galaga-style formations and dive attacks.<br><sub>Tilt moves, hold a button = fire</sub> |
| <img src="docs/screenshots/tank_2x.png" width="280" alt="Tank"><br>**Tank**<br>Aquarium toy: feed, pet, tilt the current.<br><sub>Tilt, tap to feed, LEFT food</sub> | <img src="docs/screenshots/wobble_2x.png" width="280" alt="Wobble"><br>**Wobble**<br>Balance a tightrope walker across a canyon.<br><sub>Tilt balances, LEFT/RIGHT step</sub> |  |

## Apps
The APPS card in the main list opens these. The screen never idle-dims while Level runs or a stopwatch, timer or metronome is running.

| | | |
|:--:|:--:|:--:|
| <img src="docs/screenshots/level_2x.png" width="280" alt="Level"><br>**Level**<br>Bubble level and angle gauge (0.1 deg).<br><sub>LEFT hold, RIGHT zero</sub> | <img src="docs/screenshots/clock_2x.png" width="280" alt="Clock"><br>**Clock**<br>Time, stopwatch and timer.<br><sub>LEFT/RIGHT, tap tabs</sub> | <img src="docs/screenshots/metronome_2x.png" width="280" alt="Metronome"><br>**Metronome**<br>30-240 BPM with accent beat.<br><sub>LEFT/RIGHT -/+, tap = start/stop</sub> |
| <img src="docs/screenshots/wood_calc_2x.png" width="280" alt="Wood Calc"><br>**Wood Calc**<br>Inch/mm fractions, board volume, cut list.<br><sub>LEFT field, RIGHT +, tilt +/-</sub> | <img src="docs/screenshots/dice_2x.png" width="280" alt="Dice"><br>**Dice**<br>1-6 dice from d4 to d20, shake to roll.<br><sub>Shake, RIGHT or tap</sub> | <img src="docs/screenshots/counter_2x.png" width="280" alt="Counter"><br>**Counter**<br>Four saved tally counters.<br><sub>RIGHT +1, LEFT -1</sub> |
| <img src="docs/screenshots/torch_2x.png" width="280" alt="Torch"><br>**Torch**<br>White, red or warm full-screen light.<br><sub>LEFT dimmer, RIGHT brighter</sub> | <img src="docs/screenshots/system_2x.png" width="280" alt="System"><br>**System**<br>Battery, memory, SD, RTC, live IMU, gamepad.<br><sub>RIGHT rescans SD</sub> |  |

## Controls
Hold the board **landscape with the two buttons on top**.

| Action | How |
|---|---|
| LEFT / RIGHT | the two top buttons (BOOT and PWR). The first start asks you to press the LEFT one; redo it in SETTINGS |
| Launcher | LEFT = next, RIGHT = open / choose, or tap a card; drag to scroll |
| **Home** (back to the launcher) | **triple-tap BOOT** within 0.8 s, **or hold the top-left corner** of the screen for 1.5 s, or START+SELECT on the I2C gamepad |
| Tilt | steers most games; the stick counts as an equal alternative (full deflection = 0.7 g) |

Holding a button never leaves a game, so long holds are free for gameplay. In games where fast repeated taps are normal
(Flap, Snake, Breakout, Astro, Pacman, Tetris, Duck, Missile, Counter, a running stopwatch) the triple-tap is ignored:
use the pause menu / game-over screen (LEFT), the touch corner or the gamepad instead.

## Build and flash
**Windows, Arduino IDE 2.x.**

1. Install the **esp32 core 3.3.10** (Boards Manager, add `https://espressif.github.io/arduino-esp32/package_esp32_index.json`).
2. Install the library **GFX Library for Arduino 1.6.4** (Arduino_GFX by moononournation).
3. **Patch for core 3.3.10:** the core changed the signature of `spiFrequencyToClockDiv` (it now takes the SPI bus as first argument). In the library's `src/databus/Arduino_ESP32SPI.cpp` and `src/databus/Arduino_ESP32SPIDMA.cpp`, guard the calls:
   ```cpp
   #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
     _div = spiFrequencyToClockDiv(&_spi_bus_array[_spi_num], _speed);   // new signature
   #else
     _div = spiFrequencyToClockDiv(_speed);
   #endif
   ```
   (the same applies to the other `spiFrequencyToClockDiv` call in `Arduino_ESP32SPI.cpp`, which passes `_spi`). In `Arduino_ESP32SPIDMA.cpp` the ESP-IDF path sets the clock itself, so there `_div = 1` is enough on core 3.3+.
4. Open `firmware\AmoledArcade\AmoledArcade.ino` and set: board **Waveshare ESP32-S3-Touch-AMOLED-1.8**, **PSRAM: Enabled**, **USB CDC On Boot: Disabled**, USB Mode: Hardware CDC and JTAG.

**Command line:** `firmware\AmoledArcade\flash.cmd [COMx]` (default COM6) compiles with arduino-cli and flashes in two steps:
first it puts the chip into the bootloader (`chip-id`), then it writes bootloader, partitions and app. A plain upload sometimes
fails halfway while a game is running; the two steps avoid that.

```
arduino-cli compile -b esp32:esp32:waveshare_esp32_s3_touch_amoled_18:PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc firmware\AmoledArcade
```

**Cannot connect?** Unplug USB, **hold BOOT while plugging in**, release, then flash again. After a manual download-mode flash,
unplug and replug normally (without BOOT) to run the sketch. A blank serial monitor does not mean failure: the display is the status output.

## Project layout
| Path | What |
|---|---|
| `firmware/AmoledArcade/` | The arcade firmware: launcher, games (`*_game.cpp`), apps (`app_*.cpp`), `engine.h` helpers, `hw.cpp` hardware layer, `flash.cmd` |
| `firmware/AmoledSnake/` | Early single-game version (Snake only) for the same board |
| `firmware/PlayDevice/` | Older version for a LilyGO T-Display-S3 with physical buttons (Snake, Tetris, Breakout, Pong, Invaders) |
| `firmware/tests/` | Host-side logic tests (no hardware) |
| `games/html_prototypes/` | Browser prototype for trying gameplay on a PC |
| `case/` | Fusion script for a wooden case (see below) |
| `HARDWARE.md`, `CONTROLLER_OPTIONS.md` | Pins and parts; external controller research |
| `IDEAS.md`, `GAME_IDEAS_30.md` | Design notes and ideas |
| `tools/capture.py` | Takes the screenshots over USB serial: `uv run --with pyserial --with pillow tools/capture.py --port COM6 --out docs/screenshots` |
| `docs/screenshots/` | Screenshots used in this README |

## Wooden case
`case/fusion/PlayDeviceCase/` is a Fusion script that builds a wooden handheld (front plate, frame, back plate, button caps, spacers)
and writes DXF outlines for laser or CNC into `case/dxf/` (generated, not stored in the repo). **Note:** that first version targets the
older LilyGO plan (portrait, eight buttons), not this AMOLED board, and has not been run in Fusion yet.

### One-hand grip (v1, in design)
> **Not finished:** this is an early design. It will still change, and no working grip has been built or tested yet.

A wooden grip for the bare AMOLED board plus an Xbox-style thumbstick, built in Fusion by `case/fusion/hh_grip.py`
(design notes: `case/fusion/grip_design.md`). Right hand: thumb on the stick, index finger on the two top buttons, battery in the
palm handle. Four laminated layers (laser + CNC), about 92 x 95 x 21 mm. The stick and battery are still placeholders: measure yours
before cutting.

<p><img src="docs/grip/grip_iso.png" width="420" alt="one-hand grip, isometric"> <img src="docs/grip/grip_top.png" width="360" alt="one-hand grip, top view"></p>

Waveshare's STEP and schematic are not republished here:
get them from the [official documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8).

## Roadmap
- More games from [GAME_IDEAS_30.md](GAME_IDEAS_30.md): Marble Run, Sudoku, Nonogram, Checkers, Othello, Chess, Klondike, Artillery, Sink, Sand the Plank. Two-player over ESP-NOW.
- SD content: extra Sokoban levels, scores mirrored to the card, photo and music viewers.
- Wi-Fi: NTP time sync, weather and status screens; OTA updates (needs a new partition table).
- Hardware: the one-hand grip with thumbstick, battery-backed RTC, a Game Boy emulator experiment.
- More in [IDEAS.md](IDEAS.md) (design notes, written early: some items are already built).

## Credits
- Board and examples: [Waveshare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8) (hardware reference and official examples).
- `es8311.c`, `es8311.h`, `es8311_reg.h`: ES8311 audio codec driver, Copyright Espressif Systems (Shanghai) CO LTD, Apache-2.0. See [THIRD_PARTY.md](THIRD_PARTY.md).
- [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) by moononournation: display driver library (not included).
- Built with help from [Claude](https://claude.com/claude-code).

## License
MIT for the code in this repository, see [LICENSE](LICENSE). Bundled third-party files keep their own licenses: [THIRD_PARTY.md](THIRD_PARTY.md).
