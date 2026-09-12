#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Compile and exercise the shared query without graphics libraries or assets.
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
compiler="${CXX:-g++}"
mkdir -p build
sources=(tools/terrain-region-query-test.cpp src/vr/terrain.cpp src/vr/json_scan.cpp)

echo '[terrain-region-query] Optimized build and headless checks'
"$compiler" -std=c++20 -O2 -Wall -Wextra -pedantic -Isrc/vr \
    "${sources[@]}" -o build/terrain-region-query-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/terrain-region-query-test

echo '[terrain-region-query] ASan/UBSan build and headless checks'
"$compiler" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Isrc/vr "${sources[@]}" -o build/terrain-region-query-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/terrain-region-query-test-sanitized
