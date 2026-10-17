@echo off
rem Build with MinGW-w64 (e.g. MSYS2 "mingw-w64-ucrt-x86_64-gcc") on PATH.
cd /d "%~dp0"
if not exist build mkdir build
windres -I res res\app.rc -O coff -o build\app.res || exit /b 1
g++ -O2 -std=c++17 -DUNICODE -D_UNICODE -municode -mwindows -static -Wl,-S -Wl,--wrap=__cxa_throw src\main.cpp src\app_state.cpp src\engine.cpp src\gens.cpp src\gens2.cpp src\palette.cpp src\presets.cpp src\worker.cpp src\colorbrowser.cpp src\themes.cpp src\ui.cpp src\crash.cpp build\app.res -o build\IndigoMagic.exe -luser32 -lgdi32 -ldwmapi -lshell32 -ladvapi32 -lcomdlg32 -lole32 -lwindowscodecs -luuid || exit /b 1
echo built build\IndigoMagic.exe
