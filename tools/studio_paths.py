# SPDX-License-Identifier: GPL-3.0-or-later
"""Resolve one native Studio session and portable executable paths.

tools/run-studio.sh uses this for fresh/resume, separate input and personal
output files, and generated terrain presets.

The plan is written to stdout as NUL-delimited ``key=value`` records. That keeps
paths containing spaces (or newlines) intact without word splitting or eval.

    python3 tools/studio_paths.py plan --repo . --fresh
    python3 tools/studio_paths.py plan --repo . --terrain-regions
"""
import argparse
import os
from pathlib import Path
import secrets
import shutil
import sys
import time

DEFAULT_STARTER = 'mod-assets/voxel-world-v6.json'
DEFAULT_REVIEW_INDEX = 'build/coverage/studio/index.json'
TERRAIN_REGIONS = 'build/terrain-regions/regions.json'
TERRAIN_EXAMPLE = 'build/terrain-review/seam-fixture.json'


def executable(name):
    """Resolve the native tool independently of where verification output lives."""
    repo = Path(__file__).resolve().parents[1]
    override = os.environ.get('RUBYVR_GUI' if name == 'rubyvr_gui' else 'RUBYVR_BATCH')
    if override:
        return Path(override).resolve()
    directory = Path(os.environ.get('RUBYVR_BUILD_DIR', 'build' if os.name == 'nt' else 'build-linux'))
    if not directory.is_absolute():
        directory = repo / directory
    return directory / (name + ('.exe' if os.name == 'nt' else ''))


def native_environment():
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = env.get('RUBYVR_MINGW_BIN', r'C:\msys64\mingw64\bin') + os.pathsep + env.get('PATH', '')
    return env


def fail(message):
    print(f'[run-studio] {message}', file=sys.stderr)
    raise SystemExit(1)


def absolute(path):
    """Absolute, lexically normalised, symlinks left alone (matches PowerShell)."""
    return os.path.abspath(path)


def same_path(a, b):
    """One file for the launcher's purposes: case-insensitive on Windows only."""
    return os.path.normcase(absolute(a)) == os.path.normcase(absolute(b))


def default_output_name(fresh):
    if not fresh:
        return 'my-scenery.json'
    stamp = time.strftime('%Y%m%d-%H%M%S')
    return f'my-scenery-{stamp}-{secrets.token_hex(4)}.json'


def build_plan(args):
    repo = Path(absolute(args.repo))
    if not (repo / 'tools').is_dir():
        fail(f'{repo} does not look like the RubyVR Studio repository')
    if args.terrain_regions and (args.terrain_example or args.overrides):
        fail('--terrain-regions takes no --terrain-example or --overrides')
    if args.terrain_example and args.overrides:
        fail('use either --terrain-example or --overrides')

    fresh = args.fresh
    map_id = args.map_id if args.map_id else 'MAP_OLDALE_TOWN'
    overrides = args.overrides
    generator = ''
    generator_out = ''

    if args.terrain_regions:
        fresh = True
        overrides = str(repo / TERRAIN_REGIONS)
        generator = str(repo / 'tools/build-terrain-region-example.py')
        generator_out = overrides
    if args.terrain_example:
        fresh = True
        if args.map_id is None:
            map_id = 'MAP_ROUTE101'
        overrides = str(repo / TERRAIN_EXAMPLE)
        generator = str(repo / 'tools/terrain-seam-fixture.py')
        generator_out = overrides

    out = absolute(args.out) if args.out else absolute(str(repo / 'build' / default_output_name(fresh)))

    # The editor protects its input template. When resuming the same personal
    # file, take a read-only session baseline so the personal output stays
    # writable.
    session_input = ''
    if overrides:
        overrides = absolute(overrides)
        resume = False
    else:
        resume = (not fresh) and os.path.exists(out)
        overrides = out if resume else str(repo / DEFAULT_STARTER)

    # A preset generator creates its overrides file after this plan is printed,
    # so only an existing input has to be present here.
    if not generator and not os.path.exists(overrides):
        fail('Generate the starter pack with tools/prepare-assets.py first, or pass --overrides.')

    if resume and same_path(overrides, out):
        session_input = absolute(str(repo / 'build' / f'session-input-{secrets.token_hex(16)}.json'))
        try:
            (repo / 'build').mkdir(parents=True, exist_ok=True)
            shutil.copyfile(out, session_input)
        except OSError as exc:
            fail(f'could not create the read-only session baseline: {exc}')

    # Create output directories before opening the editor.
    try:
        Path(out).parent.mkdir(parents=True, exist_ok=True)
        if generator_out:
            Path(generator_out).parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        fail(f'could not create the output directory: {exc}')

    export_review = (repo / 'build/coverage/ledger.sqlite').exists()
    return [
        ('repo', str(repo)),
        ('map', map_id),
        ('overrides', overrides),
        ('out', out),
        ('review_index', args.review_index),
        ('connected', '1' if args.connected else '0'),
        ('generator', generator),
        ('generator_out', generator_out),
        ('session_input', session_input),
        ('export_review', '1' if export_review else '0'),
    ]


def emit(plan):
    stream = sys.stdout.buffer
    for key, value in plan:
        stream.write(f'{key}={value}'.encode('utf-8') + b'\0')
    stream.flush()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    plan = sub.add_parser('plan', help='print one session plan as NUL-delimited records')
    plan.add_argument('--repo', required=True)
    plan.add_argument('--map', dest='map_id', default=None)
    plan.add_argument('--overrides', default=None)
    plan.add_argument('--out', default=None)
    plan.add_argument('--fresh', action='store_true')
    plan.add_argument('--terrain-example', action='store_true')
    plan.add_argument('--terrain-regions', action='store_true')
    plan.add_argument('--connected', action='store_true')
    plan.add_argument('--review-index', default=DEFAULT_REVIEW_INDEX)
    args = parser.parse_args(argv)
    emit(build_plan(args))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
