#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/free-walk-test
for variant in release sanitizer; do
  flags=(-O2)
  if [[ "$variant" == sanitizer ]]; then flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); fi
  "${CXX:-g++}" -std=c++20 -Wall -Wextra -Werror -pedantic "${flags[@]}" -Isrc/vr \
    src/vr/free_walk.cpp tools/free-walk-test.cpp -o "build/free-walk-test/$variant"
  "build/free-walk-test/$variant"
done
