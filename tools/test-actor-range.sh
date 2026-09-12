#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p build
compiler="${CXX:-g++}"
sources=(tools/actor-range-test.cpp src/vr/actor_range.cpp src/vr/actor_frame.cpp integration/runtime/actor_rules.cpp integration/runtime/live_scene.cpp)
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Isrc/vr -Iintegration/runtime "${sources[@]}" -o build/actor-range-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/actor-range-test
"$compiler" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/vr -Iintegration/runtime "${sources[@]}" -o build/actor-range-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/actor-range-test-sanitized
