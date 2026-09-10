#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/build.sh — native Linux/WSL build for RubyVR Studio.
#
# Builds the batch executable and GUI into build-linux so an existing
# Windows build/ directory is preserved. The batch target needs SDL2, OpenGL
# development files, zlib and the OpenXR headers, but its test entry points
# never open a window: every accepted check runs with DISPLAY unset.
#
#   tools/build.sh                       # batch + GUI into build-linux
#   tools/build.sh --batch-only          # supported headless path
#   tools/build.sh --build-dir out -j 8
#   tools/build.sh --gui                 # include the native GUI target
#
# No powershell.exe, cmd.exe, Wine or Windows compiler is involved.
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="build-linux"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
gui=1

usage() {
    cat <<'EOF'
usage: tools/build.sh [--batch-only] [--gui] [--build-dir DIR] [--jobs N]

  --batch-only     build rubyvr_studio only; needs no display
  --gui            build rubyvr_gui too (the default)
  --build-dir DIR  CMake build directory (default: build-linux)
  --jobs N, -j N   parallel build jobs (default: number of processors)
  -h, --help       show this help
EOF
}

need_value() {
    if [[ $# -lt 2 ]]; then
        echo "[build] $1 needs a value" >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --batch-only) gui=0; shift ;;
        --gui)        gui=1; shift ;;
        --build-dir)  need_value "$@"; build_dir="$2"; shift 2 ;;
        --jobs|-j)    need_value "$@"; jobs="$2"; shift 2 ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "[build] unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if ! [[ "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "[build] --jobs must be a positive integer, got '$jobs'" >&2
    exit 2
fi

# Report the exact Ubuntu packages rather than a configure error the caller has
# to decode. Nothing here installs anything.
missing=()
command -v cmake >/dev/null 2>&1 || missing+=(cmake)
command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || missing+=(build-essential)
command -v pkg-config >/dev/null 2>&1 || missing+=(pkg-config)
if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists sdl2 || missing+=(libsdl2-dev)
    pkg-config --exists zlib || missing+=(zlib1g-dev)
    pkg-config --exists gl || missing+=(libgl1-mesa-dev)
fi
if [[ ! -f /usr/include/openxr/openxr.h ]] && ! pkg-config --exists openxr 2>/dev/null; then
    missing+=(libopenxr-dev)
fi
if (( ${#missing[@]} )); then
    echo "[build] missing native build dependencies: ${missing[*]}" >&2
    echo "[build] on Ubuntu/WSL install them with:" >&2
    echo "[build]   sudo apt-get install build-essential cmake ninja-build pkg-config libsdl2-dev zlib1g-dev libgl1-mesa-dev libopenxr-dev" >&2
    echo "[build] this script never installs packages itself." >&2
    exit 2
fi

if [[ "$build_dir" != /* ]]; then
    build_dir="$repo/$build_dir"
fi
cmake_args=(-S "$repo" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
            "-DRUBYVR_BUILD_GUI=$([[ $gui == 1 ]] && echo ON || echo OFF)")
if command -v ninja >/dev/null 2>&1; then
    cmake_args+=(-G Ninja)
fi

echo "[build] configuring $build_dir (GUI=$([[ $gui == 1 ]] && echo ON || echo OFF))"
cmake "${cmake_args[@]}"

targets=(rubyvr_studio)
[[ $gui == 1 ]] && targets+=(rubyvr_gui)
echo "[build] building ${targets[*]} with $jobs job(s)"
cmake --build "$build_dir" --target "${targets[@]}" --parallel "$jobs"

# A relative build directory is resolved against the repository; an absolute
# --build-dir is used as given.
if [[ "$build_dir" = /* ]]; then
    build_path="$build_dir"
else
    build_path="$repo/$build_dir"
fi
studio="$build_path/rubyvr_studio"
if [[ ! -x "$studio" ]]; then
    echo "[build] expected executable is missing: $studio" >&2
    exit 1
fi
echo "[build] rubyvr_studio: $studio"
if [[ $gui == 1 ]]; then
    echo "[build] rubyvr_gui: $build_path/rubyvr_gui"
fi
