#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p build
compiler="${CXX:-g++}"
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Isrc/vr \
    tools/actor-frame-test.cpp src/vr/actor_frame.cpp -o build/actor-frame-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/actor-frame-test
"$compiler" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/vr \
    tools/actor-frame-test.cpp src/vr/actor_frame.cpp -o build/actor-frame-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/actor-frame-test-sanitized
