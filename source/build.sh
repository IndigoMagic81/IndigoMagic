#!/bin/sh
# Cross-compile from Linux (or build in MSYS2/MinGW on Windows): needs mingw-w64.
#   ./build.sh            release build -> build/IndigoMagic.exe
#   ./build.sh tests      also builds the engine test and fuzz test (build/enginetest.exe, build/fuzztest.exe)
set -e
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
FLAGS="-O2 -std=c++17 -DUNICODE -D_UNICODE -Wall -Wno-unused-function -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable"
SRCS="main app_state engine gens gens2 palette presets worker colorbrowser themes ui crash"
LIBS="-luser32 -lgdi32 -ldwmapi -lshell32 -ladvapi32 -lcomdlg32 -lole32 -lwindowscodecs -luuid"
mkdir -p build/obj
$WINDRES -I res res/app.rc -O coff -o build/app.res
pids=""
for s in $SRCS; do
  if [ ! -f build/obj/$s.o ] || [ src/$s.cpp -nt build/obj/$s.o ] || [ -n "$(find src -name '*.h' -newer build/obj/$s.o 2>/dev/null)" ] || [ -n "$(find src -name '*.inc' -newer build/obj/$s.o 2>/dev/null)" ]; then
    $CXX $FLAGS -c src/$s.cpp -o build/obj/$s.o &
    pids="$pids $!"
  fi
done
fail=0
for p in $pids; do wait $p || fail=1; done
[ $fail -eq 0 ] || { echo "compile failed"; exit 1; }
OBJS=""
for s in $SRCS; do OBJS="$OBJS build/obj/$s.o"; done
# -Wl,-S drops debug info but keeps the symbol table, so crash.log can name functions;
# --wrap=__cxa_throw lets the crash reporter record where every exception was thrown.
LDFLAGS="-static -Wl,-S -Wl,--wrap=__cxa_throw"
$CXX -municode -mwindows $LDFLAGS $OBJS build/app.res -o build/IndigoMagic.exe $LIBS
echo "built build/IndigoMagic.exe"
if [ "$1" = "tests" ]; then
  $CXX $FLAGS $LDFLAGS tests/enginetest.cpp build/obj/engine.o build/obj/gens.o build/obj/gens2.o build/obj/crash.o -o build/enginetest.exe $LIBS
  $CXX $FLAGS $LDFLAGS tests/fuzztest.cpp build/obj/engine.o build/obj/gens.o build/obj/gens2.o build/obj/crash.o -o build/fuzztest.exe $LIBS
  echo "built build/enginetest.exe build/fuzztest.exe"
fi
