#!/bin/sh
# Original LUNA code: Danny Nunez (dnunezx) 2026
set -eu

# ps2max/dev keeps the shared CMake toolchain one directory above PS2SDK,
# while NHDDL includes it from PS2SDK for compatibility with the upstream
# container. The container is disposable, so add the expected link at run time.
if [ ! -e "$PS2SDK/ps2dev.cmake" ]; then
  ln -s ../share/ps2dev.cmake "$PS2SDK/ps2dev.cmake"
fi

# The local ps2max image omits libtiff, while the installed upstream image
# provides it. The wrapper stages that archive in the generated build folder.
if [ -f /src/build-emulator/libtiff.a ] && [ ! -f "$PS2SDK/ports/lib/libtiff.a" ]; then
  cp /src/build-emulator/libtiff.a "$PS2SDK/ports/lib/libtiff.a"
fi

# Defaults remain the canonical emulator build; overrides also build hardware.
build_dir="${LUNA_BUILD_DIR:-/src/build-emulator}"
emulator_build="${LUNA_EMULATOR_OPTION:-ON}"
cmake -S /src -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLUNA_EMULATOR_BUILD="$emulator_build"

cmake --build "$build_dir" --parallel 2
