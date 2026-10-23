@echo off
rem Builds and runs the host tests. Uses g++ if on PATH, else MSVC from Visual Studio 2022.
setlocal
set OUT=%TEMP%\playdevice_tests
if not exist "%OUT%" mkdir "%OUT%"
where g++ >nul 2>nul
if %errorlevel%==0 (
  g++ -std=c++17 -Wall -Wextra -o "%OUT%\test_logic.exe" "%~dp0test_logic.cpp" || exit /b 1
) else (
  call "E:\VisualStudio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
  cl /nologo /std:c++17 /EHsc /W4 /Fo"%OUT%\\" /Fe"%OUT%\test_logic.exe" "%~dp0test_logic.cpp" || exit /b 1
)
"%OUT%\test_logic.exe"
