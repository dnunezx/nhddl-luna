#!/bin/sh
# Original LUNA code: Danny Nunez (dnunezx) 2026
set -eu

build_dir="${TMPDIR:-/tmp}/luna-host-tests"
mkdir -p "$build_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c tests/test_navigation.c \
  -o "$build_dir/test_navigation"
"$build_dir/test_navigation"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/collection_art.c tests/test_collection_art.c \
  -o "$build_dir/test_collection_art"
"$build_dir/test_collection_art"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Itests/stubs -Iinclude \
  src/ui/game_options.c tests/test_game_options.c \
  -o "$build_dir/test_game_options"
"$build_dir/test_game_options"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Iinclude src/vmc_create.c tests/test_vmc_create.c \
  -o "$build_dir/test_vmc_create"
"$build_dir/test_vmc_create"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Itests/storage-stubs -Iinclude src/storage.c src/target.c tests/test_storage.c \
  -o "$build_dir/test_storage"
"$build_dir/test_storage"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -DENABLE_PRINTF -D_DEFAULT_SOURCE -Wno-sign-compare -Wno-calloc-transposed-args \
  -ffunction-sections -fdata-sections -Itests/storage-stubs -Iinclude \
  src/storage.c src/options.c src/ui/game_options.c src/vmc_create.c \
  src/devices/mmce.c tests/test_vmc_storage.c -Wl,--gc-sections \
  -o "$build_dir/test_vmc_storage"
"$build_dir/test_vmc_storage"
