#!/bin/sh
# Original LUNA code: Danny Nunez (dnunezx) 2026
set -eu

build_dir="${TMPDIR:-/tmp}/luna-host-tests"
mkdir -p "$build_dir"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/runtime-lock-stubs \
  src/runtime_locks.c tests/test_runtime_locks.c \
  -o "$build_dir/test_runtime_locks"
"$build_dir/test_runtime_locks"
"${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror \
  -Itests/worker-stubs -Iinclude src/ui/worker_lifecycle.c \
  tests/test_worker_lifecycle.c -o "$build_dir/test_worker_lifecycle"
"$build_dir/test_worker_lifecycle"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/texture-stubs -Iinclude \
  src/ui/texture_budget.c src/ui/collection_art.c src/ui/navigation.c \
  tests/test_texture_budget.c -o "$build_dir/test_texture_budget"
"$build_dir/test_texture_budget"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c tests/test_navigation.c \
  -o "$build_dir/test_navigation"
"$build_dir/test_navigation"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c src/ui/case_page.c tests/test_case_page.c \
  -o "$build_dir/test_case_page"
"$build_dir/test_case_page"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  src/ui/navigation.c src/ui/collection_art.c tests/test_collection_art.c \
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
  -Itests/storage-stubs -Iinclude src/storage.c src/target.c src/genres.c tests/test_storage.c \
  -o "$build_dir/test_storage"
"$build_dir/test_storage"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -DENABLE_PRINTF -D_DEFAULT_SOURCE -Wno-sign-compare -Wno-calloc-transposed-args \
  -ffunction-sections -fdata-sections -Itests/storage-stubs -Iinclude \
  src/storage.c src/genres.c src/options.c src/ui/game_options.c src/vmc_create.c \
  src/devices/mmce.c tests/test_vmc_storage.c -Wl,--gc-sections \
  -o "$build_dir/test_vmc_storage"
"$build_dir/test_vmc_storage"
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
  -Itests/storage-stubs -Iinclude src/genres.c tests/test_genres.c \
  -o "$build_dir/test_genres"
"$build_dir/test_genres"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Iinclude \
  tests/test_ps2_menu_scene.c -lm -o "$build_dir/test_ps2_menu_scene"
"$build_dir/test_ps2_menu_scene"
