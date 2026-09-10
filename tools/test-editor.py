"""Standalone editor regression checks; requires local source assets and OpenGL.

All SDL windows are hidden. No game runner, ROM, BIOS or live snapshot is used.
Frozen geometry is checked explicitly: batch --scenes alone only prints hashes.
"""
import hashlib
import json
import os
from pathlib import Path
from studio_paths import executable as native_executable, native_environment
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
FROZEN = {
    'route101': 'f3a1bd5a56a98d41', 'house': '92d76bb11e408a83',
    'pokecenter': '94dc8b44ccd9cb97', 'tree': 'cac8b0cd5ca32253',
    'trees-stacked': '683ce96a70b24e56', 'forest': 'd6b432b23316aaf3',
    'ledge': '29c7362056392a43', 'littleroot-clipped': '9b283dac1177116b',
}


def main():
    BUILD.mkdir(exist_ok=True)
    env = native_environment()
    report = {
        'status': 'RUNNING', 'started_utc': datetime.now(timezone.utc).isoformat(),
        'scope': 'standalone editor; disk source; no live/headset validation', 'checks': [],
    }
    output = BUILD / 'editor-verification.json'

    def save():
        output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')

    def run(name, command, pattern=None):
        result = subprocess.run([str(a) for a in command], cwd=ROOT, env=env,
                                capture_output=True, text=True, encoding='utf-8', errors='replace',
                                timeout=300, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        log = result.stdout + '\n' + result.stderr
        (BUILD / (name + '.log')).write_text(log, encoding='utf-8')
        good = result.returncode == 0 and (pattern is None or re.search(pattern, log, re.M) is not None)
        report['checks'].append({'name': name, 'exit_code': result.returncode, 'passed': good})
        save()
        if not good:
            raise RuntimeError(f'{name} failed; see build/{name}.log\n{log[-3000:]}')
        print(f'PASS {name}', flush=True)
        return log

    save()
    try:
        for name in ('rubyvr_gui', 'rubyvr_studio'):
            report[name + '_sha256'] = hashlib.sha256(native_executable(name).read_bytes()).hexdigest()
        gui, batch = native_executable('rubyvr_gui'), native_executable('rubyvr_studio')
        if not (ROOT / 'mod-assets/voxel-house-v6.json').is_file():
            raise RuntimeError('Run tools/prepare-assets.py first.')
        run('editor-selftest', [gui, '--selftest', '--out', BUILD / 'editor-selftest.json'], r'^\[gui-selftest\] PASS ')
        # Object checks warm the mesher cache. Use a fresh process for scenes so
        # the full-room scene emits its diagnostic line on its first build.
        run('editor-objects', [batch, '--objects'])
        log = run('editor-scenes', [batch, '--scenes', ROOT / 'tools/uat-scenes.json'])
        actual = {}
        for line in log.splitlines():
            match = re.match(r'^\s*(\S+)\s+.*\bgeom=([0-9a-f]+)', line)
            if match and match[1] in FROZEN:
                actual[match[1]] = match[2]
        if actual != FROZEN:
            raise RuntimeError(f'Frozen inference geometry changed or is missing: {actual}')
        report['checks'].append({'name': 'eight-frozen-geometry-hashes', 'passed': True, 'hashes': actual})
        print('PASS eight frozen geometry hashes (disk only)', flush=True)
        for variant in ('scene', 'view', 'small', 'scaled'):
            run('editor-layout-' + variant,
                [gui, '--probe', ROOT / f'tools/gui-probe-{variant}.json',
                 '--probe-out', BUILD / ('editor-probe-' + variant)], r'^\[visual-probe\] PASS ')
        run('editor-camera', [sys.executable, ROOT / 'tools/test-studio-camera.py'])
        run('editor-voxel-ui', [sys.executable, ROOT / 'tools/test-studio-voxel-ui.py'])
        run('editor-part-selection', [sys.executable, ROOT / 'tools/test-studio-part-selection.py'])
        report['status'] = 'PASS'
    except Exception as exc:
        report['status'], report['error'] = 'FAIL', str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        save()
    print(f"{report['status']}: {output}")
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
