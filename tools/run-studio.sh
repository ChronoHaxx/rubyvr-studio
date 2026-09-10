#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# tools/run-studio.sh — native Linux/WSL launcher for the studio editor.
#
# Requires local source assets and a working SDL/OpenGL display (WSLg works).
# Personal outputs stay separate from the input template, including on resume.
set -uo pipefail

repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python_bin="${PYTHON:-python3}"
build_dir="${RUBYVR_BUILD_DIR:-build-linux}"
if [[ "$build_dir" = /* ]]; then
    build_path="$build_dir"
else
    build_path="$repo/$build_dir"
fi
gui="${RUBYVR_GUI:-$build_path/rubyvr_gui}"
map_id=""
overrides=""
out=""
review_index="build/coverage/studio/index.json"
fresh=0
terrain_example=0
terrain_regions=0
connected=0
extra_args=()

usage() {
    cat <<'EOF'
usage: tools/run-studio.sh [--map MAP] [--overrides FILE] [--out FILE] [--fresh]
                           [--terrain-example] [--terrain-regions] [--connected]
                           [--review-index FILE] [-- GUI_OPTIONS...]

  --map MAP            source map to open (default MAP_OLDALE_TOWN)
  --overrides FILE     existing override document to open read-only
  --out FILE           where Save writes (default build/my-scenery*.json)
  --fresh              start a new personal output instead of resuming
  --terrain-example    generate and open the Route 101 seam preset
  --terrain-regions    generate and open the six-map regional preset
  --connected          start in the connected explorer
  --review-index FILE  coverage review index exported before launch
  -- GUI_OPTIONS...    pass additional native GUI options, including captures
  -h, --help           show this help
EOF
}

need_value() {
    if [[ $# -lt 2 ]]; then
        echo "[run-studio] $1 needs a value" >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --map)           need_value "$@"; map_id="$2"; shift 2 ;;
        --overrides)     need_value "$@"; overrides="$2"; shift 2 ;;
        --out)           need_value "$@"; out="$2"; shift 2 ;;
        --fresh)         fresh=1; shift ;;
        --terrain-example) terrain_example=1; shift ;;
        --terrain-regions) terrain_regions=1; shift ;;
        --connected)     connected=1; shift ;;
        --review-index)  need_value "$@"; review_index="$2"; shift 2 ;;
        --)              shift; extra_args=("$@"); break ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "[run-studio] unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! -e "$gui" ]]; then
    echo "[run-studio] native GUI executable not found: $gui" >&2
    echo "[run-studio] build it first: tools/build.sh --gui --build-dir $build_dir" >&2
    echo "[run-studio] or point RUBYVR_GUI at an existing executable." >&2
    exit 2
fi
if [[ ! -x "$gui" ]]; then
    echo "[run-studio] not executable: $gui" >&2
    exit 2
fi

if ! command -v "$python_bin" >/dev/null 2>&1; then
    echo "[run-studio] '$python_bin' is required to prepare the session; set PYTHON to an interpreter." >&2
    exit 2
fi

plan_file="$(mktemp "${TMPDIR:-/tmp}/rubyvr-studio-plan.XXXXXX")" || exit 1
session_input=""
cleanup() {
    rm -f -- "$plan_file"
    if [[ -n "$session_input" ]]; then
        rm -f -- "$session_input"
    fi
}
trap cleanup EXIT

helper_args=(plan --repo "$repo" --review-index "$review_index")
[[ -n "$map_id" ]] && helper_args+=(--map "$map_id")
[[ -n "$overrides" ]] && helper_args+=(--overrides "$overrides")
[[ -n "$out" ]] && helper_args+=(--out "$out")
(( fresh )) && helper_args+=(--fresh)
(( terrain_example )) && helper_args+=(--terrain-example)
(( terrain_regions )) && helper_args+=(--terrain-regions)
(( connected )) && helper_args+=(--connected)

if ! "$python_bin" "$repo/tools/studio_paths.py" "${helper_args[@]}" >"$plan_file"; then
    echo "[run-studio] could not prepare the session." >&2
    exit 1
fi

declare -A plan=()
while IFS= read -r -d '' record; do
    plan["${record%%=*}"]="${record#*=}"
done <"$plan_file"
session_input="${plan[session_input]:-}"
# When resuming, the personal output is also the input template: the helper
# copied it to a read-only session baseline, and that copy is what the editor
# must open.
if [[ -n "$session_input" ]]; then
    editor_input="$session_input"
else
    editor_input="${plan[overrides]}"
fi

cd "$repo" || exit 1

if [[ -n "${plan[generator]:-}" ]]; then
    if ! "$python_bin" "${plan[generator]}" --out "${plan[generator_out]}"; then
        echo "[run-studio] preset generation failed; prepare local assets first (see docs/building.md)." >&2
        exit 1
    fi
fi

# Read the audited starter ledger; never write reviews from the launcher.
if [[ "${plan[export_review]:-0}" == "1" ]]; then
    if ! "$python_bin" "$repo/tools/coverage-ledger.py" export-studio --out "${plan[review_index]}"; then
        echo "[run-studio] WARNING: Review export failed; any previous snapshot remains available. Re-sync coverage to refresh it." >&2
    fi
fi

studio_args=(--map "${plan[map]}" --mode diorama --overrides "$editor_input"
             --out "${plan[out]}" --review-index "${plan[review_index]}")
if [[ "${plan[connected]:-0}" == "1" ]]; then
    studio_args+=(--connected)
fi

"$gui" "${studio_args[@]}" "${extra_args[@]}"
status=$?
if (( status != 0 )); then
    echo "[run-studio] Studio exited with code $status" >&2
fi
exit "$status"
