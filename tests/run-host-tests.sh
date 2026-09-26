#!/bin/sh
# Original LUNA code: Danny Nunez (dnunezx) 2026
set -eu

build_dir="${TMPDIR:-/tmp}/luna-host-tests"
mkdir -p "$build_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c tests/test_navigation.c \
  -o "$build_dir/test_navigation"
"$build_dir/test_navigation"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Itests/stubs -Iinclude \
  src/ui/game_options.c tests/test_game_options.c \
  -o "$build_dir/test_game_options"
"$build_dir/test_game_options"
