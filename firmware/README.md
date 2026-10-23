# Firmware

| Folder | What |
|---|---|
| `AmoledArcade/` | **The current firmware** for the Waveshare ESP32-S3-Touch-AMOLED-1.8: launcher, games, apps, settings. Build and flash: see the top-level README (`flash.cmd [COMx]`) |
| `AmoledSnake/` | Early Snake-only version for the same board |
| `PlayDevice/` | Older version for a LilyGO T-Display-S3 with physical buttons (described below) |
| `tests/` | Host-side logic tests |

---

# PlayDevice firmware

Handheld game console on a LilyGO T-Display-S3 (ESP32-S3, 1.9" ST7789 over 8-bit parallel).
Games: Snake, Tetris, Breakout, Pong, Invaders. High scores and the mute setting persist in NVS.

## Open / build (Arduino IDE 2.x)
1. Open `firmware\PlayDevice\PlayDevice.ino` (folder name must stay `PlayDevice`).
2. Install the ESP32 core (tested 3.3.10) and the library **GFX Library for Arduino**
   (Arduino_GFX by moononournation, tested 1.6.4) via Library Manager.
3. Tools menu:
   - Board: **ESP32S3 Dev Module**
   - USB CDC On Boot: **Enabled**
   - USB Mode: **Hardware CDC and JTAG**
   - Flash Size: **16MB (128Mb)**
   - Partition Scheme: **16M Flash (3MB APP/9.9MB FATFS)**
   - PSRAM: **OPI PSRAM** (the frame buffer lives in PSRAM; init halts without it)
4. CLI equivalent FQBN:
   `esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc`

## Flash
Plug in USB-C and Upload. If no port appears: hold **BOOT** (GPIO0), plug in USB, release BOOT,
then pick the new COM port and upload; press RST afterwards.

## Config
All pins, colours and the display rotation are in `PlayDevice\config.h`.
If the picture is upside down (USB-C should be on the right), flip `DISPLAY_ROTATION` between 1 and 3.

## Controls
- Menu: UP/DOWN choose, A or START play, SELECT toggles sound.
- In game: START pause/resume, hold SELECT 1 s back to menu (bar appears at the bottom).
  While paused, a short SELECT press toggles sound.
- Onboard buttons also work: GPIO0 (BOOT) = SELECT, GPIO14 = START.
- Snake: D-pad steer. Tetris: LEFT/RIGHT move, A rotate CW, B rotate CCW, DOWN soft drop, UP hard drop.
- Breakout: LEFT/RIGHT paddle, A launch. Pong: UP/DOWN, first to 7. Invaders: LEFT/RIGHT, A shoot (hold to repeat).
- After game over, A restarts.

## Host tests (pure logic, no hardware)
`tests\build_and_run.cmd` compiles `tests\test_logic.cpp` (g++ if on PATH, else MSVC) and runs it.
Covers Tetris line clears, rotation/wall blocking, Snake self-hit and food placement.
