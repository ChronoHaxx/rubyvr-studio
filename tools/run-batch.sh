#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/run-batch.sh — run the native batch executable with an unchanged argv.
#
# Every argument is passed through verbatim, one array element per argument, so
# paths with spaces and flags with values behave exactly as if the executable
# had been invoked directly. The exit status is the executable's own.
#
# Resolution order for the executable:
#   $RUBYVR_BATCH                     explicit native batch binary
#   $RUBYVR_BUILD_DIR/rubyvr_studio   build directory override
#   build-linux/rubyvr_studio         tools/build.sh default
#
# Examples:
#   bash tools/run-batch.sh --test-connected
#   bash tools/run-batch.sh --test-terrain "build-linux/terrain test"
#   RUBYVR_REVIEW_BATCH="$PWD/build-linux/rubyvr_studio" python3 tools/test-coverage-review.py
set -euo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${RUBYVR_BUILD_DIR:-build-linux}"
if [[ "$build_dir" = /* ]]; then
    build_path="$build_dir"
else
    build_path="$repo/$build_dir"
fi
exe="${RUBYVR_BATCH:-$build_path/rubyvr_studio}"

if [[ ! -e "$exe" ]]; then
    echo "[run-batch] native batch executable not found: $exe" >&2
    echo "[run-batch] build it first: tools/build.sh --batch-only --build-dir $build_dir" >&2
    echo "[run-batch] or set RUBYVR_BATCH to the executable path." >&2
    exit 2
fi
if [[ ! -x "$exe" ]]; then
    echo "[run-batch] not executable: $exe" >&2
    exit 2
fi

exec "$exe" "$@"
