"""Click visible house surfaces through SDL; retain geometry, saves and camera ownership."""
import hashlib
import json
import os
from pathlib import Path
from studio_paths import executable as native_executable, native_environment
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / 'mod-assets/voxel-world-v6.json'
    original = source.read_bytes()
    env = native_environment()
    report = dict(status='RUNNING', layouts=[],
                  gui_sha256=hashlib.sha256((native_executable('rubyvr_gui')).read_bytes()).hexdigest())
    try:
        for size, scenario, center, height in [('wide', 'scene', (770, 665.5), 425),
                                              ('small', 'small', (610, 502), 292)]:
            prefix = 'build/part-selection-' + size
            events, points = [], []

            def event(frame, kind, **values):
                events.append(dict(frame=frame, type=kind, **values))

            def click(frame, x, y):
                event(frame, 'motion', x=x, y=y)
                event(frame+1, 'down', x=x, y=y)
                event(frame+2, 'up', x=x, y=y)

            def key(frame, scan, mod=0):
                event(frame, 'key-down', scan=scan, mod=mod)
                event(frame+1, 'key-up', scan=scan)

            def pixel(x, y):
                # Fixed visual landmarks from the framed starter house. The
                # narrower layout uses the same camera and scales by height.
                return (round(center[0] + (x-770)*height/425),
                        round(center[1] + (y-665.5)*height/425))

            def point(frame, name):
                points.append(dict(frame=frame, name=name))

            click(12, 80, 252)
            click(26, 180, 48)
            event(33, 'motion', x=round(center[0]), y=round(center[1]))
            key(35, 9)  # F: frame whole model.
            point(42, 'baseline')
            click(45, *pixel(740, 620))
            point(53, 'roof')
            key(55, 9, 1)  # Shift+F: frame selected part.
            point(61, 'focused')
            key(64, 9)
            point(69, 'restored')
            click(72, *pixel(720, 740))
            point(80, 'door')
            click(83, *pixel(780, 718))
            point(91, 'window')
            click(94, *pixel(450, 820))
            point(102, 'miss')
            x, y = pixel(780, 718)
            event(105, 'motion', x=x, y=y)
            event(106, 'down', x=x, y=y)
            event(109, 'motion', x=x+50, y=y-20, rx=50, ry=-20)
            event(112, 'up', x=x+50, y=y-20)
            point(117, 'orbit')
            key(121, 9)
            key(127, 30)  # 1: front orthographic view.
            point(135, 'front')
            click(138, *pixel(770, 615))
            point(146, 'ortho-roof')
            click(149, *pixel(736, 711))
            point(157, 'ortho-door')
            click(160, *pixel(802, 692))
            point(168, 'ortho-frame')  # The opaque divider between window panes.
            click(171, *pixel(790, 682))
            point(179, 'ortho-window')
            key(183, 22, 64)  # Ctrl+S.
            point(191, 'saved')
            event(196, 'quit')
            script = dict(frames=201, events=events, checkpoints=points, chapters=[], record=0)
            (ROOT/(prefix+'-events.json')).write_text(json.dumps(script), encoding='utf-8')
            command = [str(native_executable('rubyvr_gui')), '--map', 'MAP_OLDALE_TOWN',
                       '--mode', 'diorama', '--overrides', str(source), '--out', prefix+'-saved.json',
                       '--probe', f'tools/gui-probe-{scenario}.json', '--showcase', prefix+'-events.json',
                       '--probe-out', prefix]
            with (ROOT/(prefix+'.log')).open('w', encoding='utf-8') as log:
                subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                               check=True, timeout=90, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            result = json.loads((ROOT/(prefix+'.json')).read_text())
            states = {row['name']:row for row in result['checkpoints']}
            assert result['ok'] and result['closed'], f'{size}: replay did not close cleanly'
            for name, suffix in [('roof', '-front-roof'), ('door', '-door-leaf'), ('window', '-window-panes'),
                                 ('ortho-roof', '-front-roof'), ('ortho-door', '-door-leaf'),
                                 ('ortho-frame', '-wall-and-frames'),
                                 ('ortho-window', '-window-panes')]:
                assert states[name]['selected_part'].endswith(suffix), f'{size}/{name}: selected {states[name]["selected_part"]}'
            base = states['baseline']
            assert states['focused']['camera_target'] != base['camera_target'], f'{size}: part focus did not move target'
            assert states['restored']['camera'] == base['camera'], f'{size}: F did not restore model framing'
            assert states['restored']['camera_target'] == base['camera_target'], f'{size}: F did not restore target'
            assert states['miss']['selected_part'] == states['window']['selected_part'], f'{size}: empty space selected a part'
            assert states['orbit']['selected_part'] == states['window']['selected_part'], f'{size}: orbit changed selection'
            assert states['orbit']['camera'] != states['window']['camera'], f'{size}: drag failed to orbit'
            assert states['front']['orthographic'], f'{size}: front view not orthographic'
            baseline = json.loads((ROOT/(prefix+'.png.baseline.draft.json')).read_text())
            for name, state in states.items():
                assert state['preview_hash'] == base['preview_hash'], f'{size}/{name}: picking changed rendered geometry'
                assert state['mesh_uploads'] == base['mesh_uploads'], f'{size}/{name}: picking uploaded a mesh'
                assert state['undo'] == base['undo'], f'{size}/{name}: selection created an undo step'
                assert not state['draft_dirty'] and not state['unsaved'], f'{size}/{name}: browsing dirtied the document'
                assert json.loads((ROOT/(prefix+'.png.'+name+'.draft.json')).read_text()) == baseline, f'{size}/{name}: model changed'
            working = json.loads((ROOT/(prefix+'.png.baseline.working.json')).read_text())
            assert json.loads((ROOT/(prefix+'-saved.json')).read_text()) == working, f'{size}: save changed pack'
            assert source.read_bytes() == original, 'starter asset changed'
            report['layouts'].append(dict(size=size, status='PASS', checkpoints=len(states), prefix=prefix))
            print(f'PASS direct part selection: {size}', flush=True)
        report['status'] = 'PASS'
    except Exception as exc:
        report['status'], report['error'] = 'FAIL', str(exc)
        raise
    finally:
        (ROOT/'build/part-selection-verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
