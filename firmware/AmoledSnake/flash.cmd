@echo off
rem Build and flash AmoledSnake to the Waveshare ESP32-S3-Touch-AMOLED-1.8.
rem Two-step flash (enter bootloader, then write) - a plain Arduino upload sometimes fails
rem halfway while the game is running. Usage: flash.cmd [COMport]   (default COM6)
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM6
set CLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
set FQBN=esp32:esp32:waveshare_esp32_s3_touch_amoled_18:PSRAM=enabled,CDCOnBoot=default,USBMode=hwcdc
for /r "%LOCALAPPDATA%\Arduino15\packages\esp32\tools" %%f in (esptool.exe) do if exist "%%f" set ET=%%f
set B0=%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\3.3.10\tools\partitions\boot_app0.bin
cd /d "%~dp0"
"%CLI%" compile -b %FQBN% --output-dir build . || exit /b 1
"%ET%" --chip esp32s3 -p %PORT% --before default-reset --after no-reset chip-id || exit /b 1
"%ET%" --chip esp32s3 -p %PORT% -b 921600 --before no-reset --after hard-reset write-flash -z ^
  0x0 build\AmoledSnake.ino.bootloader.bin 0x8000 build\AmoledSnake.ino.partitions.bin ^
  0xe000 "%B0%" 0x10000 build\AmoledSnake.ino.bin
