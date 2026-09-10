# SPDX-License-Identifier: GPL-3.0-or-later
"""Bash launcher argument and failure checks with an original recording stub.

The real tools/run-batch.sh and tools/run-studio.sh are copied into a fixture
repository whose executable is a stub that records its argv NUL-separated. That
tests the launcher boundary — exact arguments, spaces, fresh/resume, presets,
session cleanup and exit propagation — with no game data, display or renderer.
It does not prove the GUI renders; docs/native-wsl.md records that separately.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

RECORDER = """#!/usr/bin/env bash
# Original argv recorder for tools/test-bash-launcher.py.
set -u
: > "${RUBYVR_LAUNCHER_ARGV:?RUBYVR_LAUNCHER_ARGV must name a record file}"
for argument in "$@"; do
    printf '%s\\0' "$argument" >> "$RUBYVR_LAUNCHER_ARGV"
done
# Optional: snapshot the file named after RUBYVR_STUB_SNAPSHOT_ARG while the
# launcher's temporary session input still exists.
if [[ -n "${RUBYVR_STUB_SNAPSHOT_DIR:-}" && -n "${RUBYVR_STUB_SNAPSHOT_ARG:-}" ]]; then
    previous=""
    for argument in "$@"; do
        if [[ "$previous" == "$RUBYVR_STUB_SNAPSHOT_ARG" ]]; then
            cp -- "$argument" "$RUBYVR_STUB_SNAPSHOT_DIR/input.copy"
            break
        fi
        previous="$argument"
    done
fi
exit "${RUBYVR_STUB_EXIT:-0}"
"""

GENERATOR = """import argparse
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument('--out', required=True)
args = parser.parse_args()
target = Path(args.out)
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('{"version":7,"patterns":[],"terrain":{"maps":[]}}\\n')
print(f'Generated synthetic preset {target}')
"""

LEDGER_STUB = """import sys
print('Synthetic stale coverage index', file=sys.stderr)
sys.exit(1)
"""


def read_argv(path):
    if not path.exists():
        return None
    return [part.decode() for part in path.read_bytes().split(b'\0') if part]


class Cases:
    def __init__(self, out):
        self.out = out
        self.results = []
        self.failures = []

    def check(self, name, ok, detail=''):
        self.results.append({'case': name, 'status': 'PASS' if ok else 'FAIL'})
        print(f"{'PASS' if ok else 'FAIL'}: {name}" + (f' — {detail}' if detail and not ok else ''))
        if not ok:
            self.failures.append(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build-linux', help='fixture location')
    args = parser.parse_args()

    out = ROOT / args.build_dir / 'launcher-test'
    if out.exists():
        shutil.rmtree(out)
    fixture = out / 'repo with spaces'
    (fixture / 'tools').mkdir(parents=True)
    (fixture / 'mod-assets').mkdir()
    (fixture / 'build').mkdir()

    for name in ('build.sh', 'run-batch.sh', 'run-studio.sh', 'studio_paths.py'):
        shutil.copyfile(ROOT / 'tools' / name, fixture / 'tools' / name)
    (fixture / 'tools' / 'build-terrain-region-example.py').write_text(GENERATOR)
    (fixture / 'tools' / 'terrain-seam-fixture.py').write_text(GENERATOR)
    (fixture / 'tools' / 'coverage-ledger.py').write_text(LEDGER_STUB)
    (fixture / 'mod-assets' / 'voxel-world-v6.json').write_text('{"version":6,"patterns":[]}\n')

    binary_dir = fixture / 'build-linux'
    binary_dir.mkdir()
    for name in ('rubyvr_studio', 'rubyvr_gui'):
        stub = binary_dir / name
        stub.write_text(RECORDER)
        stub.chmod(0o755)

    cases = Cases(out)
    (fixture / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(LauncherFixture LANGUAGES CXX)\n'
        'add_executable(rubyvr_studio stub.cpp)\n')
    (fixture / 'stub.cpp').write_text('int main() { return 0; }\n')
    built = subprocess.run(['bash', str(fixture / 'tools/build.sh'), '--batch-only',
                            '--build-dir', 'other build', '--jobs', '2'],
                           cwd=out, capture_output=True, text=True, timeout=120)
    cases.check('build.sh resolves a relative output from outside the repository',
                built.returncode == 0 and (fixture / 'other build/rubyvr_studio').is_file()
                and not (out / 'other build').exists(), built.stdout + built.stderr)
    for name in ('build.sh', 'run-batch.sh', 'run-studio.sh'):
        proc = subprocess.run(['bash', '-n', str(ROOT / 'tools' / name)], capture_output=True, text=True)
        cases.check(f'bash -n tools/{name}', proc.returncode == 0, proc.stderr)

    def run(script, script_args, extra_env=None, cwd=None, record='argv.bin'):
        record_path = out / record
        record_path.unlink(missing_ok=True)
        env = os.environ.copy()
        env['PYTHON'] = sys.executable
        env['RUBYVR_LAUNCHER_ARGV'] = str(record_path)
        env.pop('RUBYVR_GUI', None)
        env.pop('RUBYVR_BATCH', None)
        env.pop('RUBYVR_BUILD_DIR', None)
        env.pop('RUBYVR_STUB_EXIT', None)
        env.update(extra_env or {})
        proc = subprocess.run(['bash', str(fixture / 'tools' / script), *script_args],
                              cwd=cwd or out, env=env, capture_output=True, text=True, timeout=90)
        return proc, record_path

    # ── run-batch.sh passes argv unchanged ───────────────────────────────────
    batch_args = ['--test-connected', '--out', 'a b.json', '--decomp', 'third party/pokeruby',
                  '--emit-pattern', '7,7', '--emit-name', 'name with spaces']
    proc, record = run('run-batch.sh', batch_args)
    cases.check('run-batch exits with the executable status', proc.returncode == 0, proc.stderr)
    cases.check('run-batch passes the argument array unchanged', read_argv(record) == batch_args,
                f'{read_argv(record)!r}')

    proc, _ = run('run-batch.sh', ['--test-connected'], {'RUBYVR_STUB_EXIT': '7'})
    cases.check('run-batch propagates a failing exit status', proc.returncode == 7, str(proc.returncode))

    saved = binary_dir / 'rubyvr_studio.saved'
    (binary_dir / 'rubyvr_studio').rename(saved)
    proc, _ = run('run-batch.sh', ['--test-connected'])
    (saved).rename(binary_dir / 'rubyvr_studio')
    cases.check('run-batch reports a missing executable',
                proc.returncode != 0 and 'tools/build.sh' in proc.stderr, proc.stderr)

    alt = fixture / 'alt build'
    alt.mkdir()
    shutil.copyfile(binary_dir / 'rubyvr_studio', alt / 'rubyvr_studio')
    (alt / 'rubyvr_studio').chmod(0o755)
    proc, record = run('run-batch.sh', ['--test-foundation', 'out dir'],
                       {'RUBYVR_BATCH': str(alt / 'rubyvr_studio')})
    cases.check('run-batch honours RUBYVR_BATCH',
                proc.returncode == 0 and read_argv(record) == ['--test-foundation', 'out dir'],
                proc.stderr or f'{read_argv(record)!r}')

    # ── run-studio.sh session planning ───────────────────────────────────────
    personal = fixture / 'build' / 'personal with spaces.json'
    proc, record = run('run-studio.sh', ['--fresh', '--out', str(personal)],
                       {})
    expected = ['--map', 'MAP_OLDALE_TOWN', '--mode', 'diorama',
                '--overrides', str(fixture / 'mod-assets/voxel-world-v6.json'),
                '--out', str(personal),
                '--review-index', 'build/coverage/studio/index.json']
    cases.check('run-studio fresh launch builds the exact native arguments',
                proc.returncode == 0 and read_argv(record) == expected,
                f'{read_argv(record)!r} {proc.stderr}')

    proc, record = run('run-studio.sh', ['--fresh', '--connected', '--out', str(personal)],
                       {})
    cases.check('run-studio --connected appends the flag once',
                proc.returncode == 0 and read_argv(record) == expected + ['--connected'],
                f'{read_argv(record)!r}')

    personal.write_text('{"version":6,"patterns":[{"name":"resume me"}]}\n')
    snapshot_dir = out / 'resume snapshot'
    snapshot_dir.mkdir(exist_ok=True)
    proc, record = run('run-studio.sh', ['--out', str(personal)], {
        'RUBYVR_STUB_SNAPSHOT_DIR': str(snapshot_dir),
        'RUBYVR_STUB_SNAPSHOT_ARG': '--overrides',
    })
    resumed = read_argv(record) or []
    overrides = resumed[resumed.index('--overrides') + 1] if '--overrides' in resumed else ''
    session = Path(overrides)
    cases.check('run-studio resume keeps input and output separate',
                proc.returncode == 0 and session.name.startswith('session-input-') and
                session != personal and personal.read_text().startswith('{"version":6'),
                f'{resumed!r}')
    copied = snapshot_dir / 'input.copy'
    cases.check('run-studio resume baseline holds the personal bytes',
                copied.is_file() and copied.read_text() == personal.read_text(),
                copied.read_text() if copied.is_file() else 'no snapshot')
    cases.check('run-studio removes the temporary session input',
                not session.exists(), str(session))

    proc, record = run('run-studio.sh', ['--terrain-regions'], {})
    regions = fixture / 'build' / 'terrain-regions' / 'regions.json'
    regions_argv = read_argv(record) or []
    cases.check('run-studio --terrain-regions generates and opens the preset',
                proc.returncode == 0 and regions.is_file() and
                len(regions_argv) > 5 and regions_argv[4] == '--overrides' and
                regions_argv[5] == str(regions), f'{regions_argv!r} {proc.stderr}')

    proc, record = run('run-studio.sh', ['--terrain-example'], {})
    seam = fixture / 'build' / 'terrain-review' / 'seam-fixture.json'
    seam_argv = read_argv(record) or []
    cases.check('run-studio --terrain-example uses Route 101 and the seam preset',
                proc.returncode == 0 and seam.is_file() and
                len(seam_argv) > 5 and seam_argv[1] == 'MAP_ROUTE101' and
                seam_argv[4] == '--overrides' and seam_argv[5] == str(seam),
                f'{seam_argv!r} {proc.stderr}')

    (fixture / 'build' / 'coverage').mkdir(parents=True, exist_ok=True)
    (fixture / 'build' / 'coverage' / 'ledger.sqlite').write_bytes(b'synthetic')
    proc, record = run('run-studio.sh', ['--fresh', '--out', str(personal)],
                       {})
    cases.check('stale review export warns without failing the launch',
                proc.returncode == 0 and 'Review export failed' in (proc.stdout + proc.stderr),
                proc.stdout + proc.stderr)

    proc, _ = run('run-studio.sh', ['--fresh'], {'RUBYVR_STUB_EXIT': '5', 'RUBYVR_UNVERIFIED_GUI': '1'})
    cases.check('run-studio propagates a failing editor status', proc.returncode == 5,
                str(proc.returncode))

    proc, record = run('run-studio.sh', ['--fresh'], {})
    fresh_argv = read_argv(record) or []
    cases.check('run-studio explicit fresh output is timestamped under build/',
                proc.returncode == 0 and len(fresh_argv) > 7 and
                re.fullmatch(r'my-scenery-\d{8}-\d{6}-[0-9a-f]{8}\.json',
                             Path(fresh_argv[7]).name) is not None,
                f'{fresh_argv!r} {proc.stderr}')

    gui = binary_dir / 'rubyvr_gui'
    gui_saved = binary_dir / 'rubyvr_gui.saved'
    gui.rename(gui_saved)
    proc, _ = run('run-studio.sh', ['--fresh'], {})
    gui_saved.rename(gui)
    cases.check('run-studio reports a missing GUI with the build command',
                proc.returncode == 2 and 'tools/build.sh --gui' in proc.stderr, proc.stderr)

    proc, _ = run('run-studio.sh', ['--fresh'])
    cases.check('run-studio launches without an experimental acknowledgement',
                proc.returncode == 0, proc.stderr)
    proc, record = run('run-studio.sh', ['--fresh', '--', '--room-review', 'capture with spaces.png'])
    cases.check('run-studio passes native capture options after the separator',
                proc.returncode == 0 and read_argv(record)[-2:] ==
                ['--room-review', 'capture with spaces.png'], proc.stderr)

    report = {
        'status': 'PASS' if not cases.failures else 'FAIL',
        'cases': cases.results,
        'note': 'Launcher boundary only; the GUI runtime is not exercised.',
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"{report['status']}: {len(cases.results)} launcher checks; "
          f"evidence={out / 'verification.json'}")
    return 0 if not cases.failures else 1


if __name__ == '__main__':
    raise SystemExit(main())
