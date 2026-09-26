#!/bin/sh
# Original LUNA code: Danny Nunez (dnunezx) 2026
set -eu

build_dir="${TMPDIR:-/tmp}/luna-host-tests"
mkdir -p "$build_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c tests/test_navigation.c \
  -o "$build_dir/test_navigation"
"$build_dir/test_navigation"
