#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"
compiler="${CXX:-g++}"
mkdir -p build
sources=(tools/live-scene-test.cpp integration/runtime/live_scene.cpp)
echo '[live-scene] Optimized contract checks'
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Isrc/vr -Iintegration/runtime \
    "${sources[@]}" -o build/live-scene-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/live-scene-test
echo '[live-scene] ASan/UBSan contract checks'
"$compiler" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Isrc/vr -Iintegration/runtime "${sources[@]}" -o build/live-scene-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/live-scene-test-sanitized
echo '[live-region] Optimized and sanitized neighbourhood checks'
sources=(tools/live-region-test.cpp integration/runtime/live_region.cpp)
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Isrc/vr -Iintegration/runtime \
    "${sources[@]}" -o build/live-region-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/live-region-test
"$compiler" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Isrc/vr -Iintegration/runtime "${sources[@]}" -o build/live-region-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/live-region-test-sanitized
