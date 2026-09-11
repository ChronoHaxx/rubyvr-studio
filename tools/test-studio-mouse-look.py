# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise remote-pointer deltas through the real SDL editor event loop.

Uses hidden windows and synthetic input, never the desktop pointer. The WSL
drag path must not request relative mode. This is not physical-device UAT.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
from studio_paths import executable, native_environment

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--out-dir', type=Path, default=ROOT / 'build/mouse-look')
    args = parser.parse_args()
    gui = (args.exe or executable('rubyvr_gui')).resolve()
    out = args.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    events, points = [], []

    def event(frame, kind, **fields):
        events.append(dict(frame=frame, type=kind, **fields))

    def click(frame, x, y):
        event(frame, 'motion', x=x, y=y)
        event(frame + 1, 'down', x=x, y=y)
        event(frame + 2, 'up', x=x, y=y)

    def point(frame, name):
        points.append(dict(frame=frame, name=name))

    click(8, 552, 116)
    click(20, 1180, 438)  # Fly in the existing 1600x950 layout.
    event(28, 'motion', x=800, y=620)
    event(30, 'down', x=800, y=620, button=3)
    point(35, 'start')
    # Window coordinates describe a small physical movement. Deliberately
    # hostile xrel/yrel must never turn it into repeated revolutions.
    event(40, 'motion', x=820, y=606, rx=32000, ry=-28000)
    point(44, 'moved')
    event(46, 'motion', x=820, y=606, rx=32000, ry=-28000)
    point(50, 'stationary')
    event(52, 'motion', x=810, y=600, rx=-1000000, ry=1000000)
    point(54, 'reverse')
    event(56, 'up', x=810, y=600, button=3)
    event(56, 'motion', x=1200, y=900, rx=99999, ry=99999)
    event(57, 'key-down', scan=26)
    event(60, 'key-up', scan=26)
    point(64, 'released')
    event(68, 'motion', x=900, y=650)
    event(70, 'down', x=900, y=650, button=3)
    point(74, 'regrab')
    event(76, 'motion', x=920, y=650, rx=32000, ry=-28000)
    point(80, 'regrab-moved')
    event(84, 'focus-lost')
    event(86, 'motion', x=940, y=670, rx=50000, ry=-50000)
    event(86, 'key-down', scan=26)
    event(88, 'key-up', scan=26)
    event(89, 'up', x=940, y=670, button=3)
    point(94, 'focus-lost')
    event(98, 'motion', x=800, y=620)
    event(100, 'down', x=800, y=620, button=3)
    point(104, 'edge-start')
    event(106, 'motion', x=65535, y=-3000, rx=65535, ry=-3000)
    event(107, 'motion', x=810, y=620, rx=5000, ry=5000)
    point(110, 'outside')
    event(112, 'up', x=810, y=620, button=3)
    event(116, 'motion', x=800, y=620)
    event(118, 'down', x=800, y=620, button=3)
    event(122, 'key-down', scan=41)
    event(123, 'key-up', scan=41)
    event(125, 'motion', x=820, y=620, rx=50000, ry=50000)
    point(130, 'escape')
    event(132, 'up', x=820, y=620, button=3)
    event(136, 'quit')
    scenario = ROOT / 'tools/gui-probe-scene.json'
    script = out / 'events.json'
    script.write_text(json.dumps(dict(frames=140, events=events, checkpoints=points,
                                     record=0, chapters=[]), indent=2), encoding='utf-8')
    prefix = out / 'mouse'
    env = native_environment()
    env['RUBYVR_MOUSE_LOOK'] = 'drag'
    with (out / 'editor.log').open('w') as log:
        result = subprocess.run([str(gui), '--probe', str(scenario), '--showcase', str(script),
            '--probe-out', str(prefix), '--out', str(out / 'personal.json')],
            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=False, timeout=180)
    if result.returncode:
        raise SystemExit(f'Editor failed ({result.returncode}); see {out / "editor.log"}')
    rendered = json.loads(prefix.with_suffix('.json').read_bytes())
    states = {p['name']: p for p in rendered['checkpoints']}
    checks = {}

    def pose(name):
        p = states[name]
        return p['camera'] + p['camera_target']

    def unchanged(a, b):
        return math.dist(pose(a), pose(b)) < 1e-4

    start, moved, reverse = (states[n]['camera'] for n in ('start', 'moved', 'reverse'))
    checks['all checkpoints completed'] = rendered['ok'] and len(states) == len(points)
    checks['20 pixel movement turns 0.12 radians without pitch clamp'] = (
        abs((moved[0] - start[0]) + .12) < 1e-5 and abs((moved[1] - start[1]) + .07) < 1e-5)
    checks['stationary cursor ignores repeated raw deltas'] = unchanged('moved', 'stationary')
    checks['reverse movement follows window coordinates'] = (
        abs((reverse[0] - moved[0]) - .06) < 1e-5 and abs((reverse[1] - moved[1]) + .03) < 1e-5)
    checks['release stops same-frame motion and keyboard flight'] = (
        unchanged('reverse', 'released') and not states['released']['fly_looking'])
    checks['regrab does not reuse the old cursor anchor'] = unchanged('released', 'regrab')
    checks['new drag retains the original sensitivity'] = (
        abs(states['regrab-moved']['camera'][0] - states['regrab']['camera'][0] + .12) < 1e-5)
    checks['focus loss stops look and movement'] = (
        unchanged('regrab-moved', 'focus-lost') and not states['focus-lost']['fly_looking'])
    checks['outside coordinates release look without a camera jump'] = (
        unchanged('edge-start', 'outside') and not states['outside']['fly_looking'])
    checks['Escape releases look'] = not states['escape']['fly_looking'] and unchanged('outside', 'escape')
    checks['drag never enables native relative mode'] = all(
        p.get('mouse_look') == 'drag' and p.get('native_mouse_relative') is False for p in states.values())
    baseline = Path(str(prefix) + '.png.start.draft.json').read_bytes()
    checks['geometry draft and history stay unchanged'] = all(
        p['room_hash'] == states['start']['room_hash'] and p['undo'] == states['start']['undo']
        and Path(str(prefix) + f'.png.{name}.draft.json').read_bytes() == baseline
        for name, p in states.items())
    report = dict(status='PASS' if all(checks.values()) else 'FAIL', checks=checks,
        executable_sha256=hashlib.sha256(gui.read_bytes()).hexdigest(),
        capture_file=str(prefix.with_suffix('.json')),
        scope='Real SDL event loop with synthetic remote-pointer faults; no physical-device UAT')
    (out / 'verification.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    for name, ok in checks.items():
        print(f'{"PASS" if ok else "FAIL"}: {name}')
    print(f'{report["status"]}: {len(checks)} mouse-look checks; {out / "verification.json"}')
    return int(report['status'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
