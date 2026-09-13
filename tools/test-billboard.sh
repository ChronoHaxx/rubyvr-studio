#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p build
compiler="${CXX:-g++}"
sources=(tools/billboard-test.cpp src/vr/billboard.cpp)
"$compiler" -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -Isrc/vr "${sources[@]}" -o build/billboard-test
env -u DISPLAY -u WAYLAND_DISPLAY ./build/billboard-test
"$compiler" -std=c++20 -O1 -g -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc/vr "${sources[@]}" -o build/billboard-test-sanitized
env -u DISPLAY -u WAYLAND_DISPLAY ./build/billboard-test-sanitized
