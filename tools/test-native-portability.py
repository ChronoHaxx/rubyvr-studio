# SPDX-License-Identifier: GPL-3.0-or-later
"""Native Linux portability checks with original fixtures.

Runs the real rubyvr_studio --test-portability hook and independently checks
what it claims: the ELF executable, the PASS/FAIL transcript, the persisted
v7 document, the caller stderr log and the absence of leftover temporaries.

This proves the native file/capture boundary only. It needs no display, source
art, ROM or network and runs with DISPLAY removed from the environment.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

# Check names the native fixture must report as PASS. The unwritable-directory
# case is environment-dependent (root), so it is asserted separately.
REQUIRED = [
    'v5 save/readback round trip',
    'v6 save/readback round trip',
    'v7 save/readback round trip',
    'paths with spaces persist and read back',
    'replace publishes the new document over the old bytes',
    'written document starts without a byte order mark',
    'rejected version keeps the previous destination bytes',
    'rejected pattern keeps the previous destination bytes',
    'failed publication leaves the destination untouched',
    'capture still runs the callback',
    'capture returns the redirected stderr text',
    'writing after capture reaches the caller\'s redirected stderr',
    'captured lines do not leak into the caller\'s log',
    'capture removes its scratch file',
    'capture failure loses the log but keeps the work',
    'no temporary file survives an attempted write',
    'relative and absolute names of one file compare equal',
    'a path with a . segment compares equal',
    'different paths do not compare equal',
    'Linux case variants are different files',
    'a backslash is a filename character, not a separator',
    'a symlink alias compares equal',
]
POSIX_ONLY = {'a symlink alias compares equal', 'Linux case variants are different files'}


def fail(message):
    print(f'FAIL: {message}')
    return 1


def remove_tree(root):
    """Remove a previous fixture, including a permission-denied leftover."""
    if not root.exists():
        return
    for path in root.rglob('*'):
        if path.is_dir():
            try:
                path.chmod(0o755)
            except OSError:
                pass
    shutil.rmtree(root, ignore_errors=True)


def leftovers(root):
    return sorted(
        str(path.relative_to(root))
        for path in root.rglob('*')
        if path.is_file() and ('.tmp.' in path.name or path.name.startswith('rubyvr_capture_'))
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', default='build-linux', help='native build directory')
    parser.add_argument('--exe', default=None, help='explicit rubyvr_studio path')
    args = parser.parse_args()

    exe = Path(args.exe).resolve() if args.exe else (ROOT / args.build_dir / 'rubyvr_studio')
    if not exe.is_file():
        return fail(f'native batch executable not found: {exe}; run tools/build.sh --batch-only first')

    out_dir = ROOT / args.build_dir / 'portability-test'
    remove_tree(out_dir)
    fixture = out_dir / 'fixture with spaces'
    run_cwd = out_dir / 'run cwd'
    fixture.mkdir(parents=True)
    run_cwd.mkdir(parents=True)

    env = os.environ.copy()
    env.pop('DISPLAY', None)
    proc = subprocess.run(
        [str(exe), '--test-portability', str(fixture)],
        cwd=run_cwd, env=env, capture_output=True, text=True, timeout=120,
    )
    transcript = proc.stdout + proc.stderr
    passed = set(re.findall(r'^\[portability\] PASS (.*)$', proc.stdout, re.M))
    skipped = set(re.findall(r'^\[portability\] SKIP (.*?):', proc.stdout, re.M))
    failed = re.findall(r'^\[portability\] FAIL (.*)$', transcript, re.M)

    checks = {}
    stream = exe.open('rb')
    magic = stream.read(4)
    stream.close()
    checks['native ELF executable'] = magic == b'\x7fELF'
    if sys.platform.startswith('linux'):
        if not checks['native ELF executable']:
            return fail(f'{exe} is not a native ELF binary (magic {magic!r})')
    checks['exit status'] = proc.returncode == 0
    checks['no FAIL lines'] = not failed
    summary = re.search(r'^\[portability\] PASS: (\d+) checks, (\d+) skipped', proc.stdout, re.M)
    checks['summary present'] = summary is not None
    if summary:
        checks['at least 25 checks'] = int(summary.group(1)) >= 25
    required = [name for name in REQUIRED if name not in POSIX_ONLY or os.name == 'posix']
    missing = [name for name in required if name not in passed and name not in skipped]
    checks['required cases passed'] = not missing
    if missing:
        print('missing cases: ' + '; '.join(missing))
    if 'unwritable directory keeps the previous bytes' not in skipped:
        checks['unwritable destination case ran'] = (
            'unwritable directory keeps the previous bytes' in passed
        )

    destination = fixture / 'patterns with spaces' / 'personal override.json'
    checks['destination document exists'] = destination.is_file()
    if destination.is_file():
        document = json.loads(destination.read_text())
        checks['destination is v7 with a pattern'] = (
            document.get('version') == 7 and bool(document.get('patterns'))
        )

    log = fixture / 'caller stderr.log'
    checks['caller stderr log exists'] = log.is_file()
    if log.is_file():
        checks['caller log kept the failing capture output'] = (
            'captured mesher-style line' in log.read_text()
        )

    stray = leftovers(out_dir)
    checks['no temporary leftovers'] = not stray
    if stray:
        print('leftover files: ' + '; '.join(stray))
    checks['no capture scratch in repository root'] = not list(ROOT.glob('rubyvr_capture_*.tmp'))
    checks['subprocess cwd untouched'] = not any(run_cwd.iterdir())
    checks['ran with DISPLAY removed'] = 'DISPLAY' not in env

    for name, ok in checks.items():
        print(f"{'PASS' if ok else 'FAIL'}: {name}")
    status = 'PASS' if all(checks.values()) else 'FAIL'
    evidence = {
        'status': status,
        'checked_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'executable': str(exe),
        'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
        'native_checks': int(summary.group(1)) if summary else 0,
        'skipped': sorted(skipped),
        'required_cases': required,
        'display_unset': True,
        'limits': 'Native file/capture boundary only; no source art, GUI journey, rendering or OpenXR runtime.',
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / 'verification.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f"{status}: native portability fixtures; {evidence['native_checks']} native checks; "
          f"evidence={out_dir / 'verification.json'}")
    return 0 if status == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
