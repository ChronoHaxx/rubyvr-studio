"""Exercise visible lighting controls and compare real SDL/OpenGL frames.

Requires tools/prepare-assets.py and the built GUI. No game or headset runs.
Optionally pass --baseline-exe for a pre-change neutral-render comparison.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
PHASES = ['Neutral', 'Dawn', 'Noon', 'Dusk', 'Night', 'Neutral']
NAMES = ['neutral', 'dawn', 'noon', 'dusk', 'night', 'reset']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-exe', type=Path)
    args = parser.parse_args()
    build = ROOT / 'build/environment-review'
    build.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'mod-assets/voxel-world-v6.json'
    before = source.read_bytes()
    gui = ROOT / 'build/rubyvr_gui.exe'
    env = os.environ.copy()
    env['PATH'] = env.get('RUBYVR_MINGW_BIN', r'C:\msys64\mingw64\bin') + os.pathsep + env['PATH']
    report = dict(status='RUNNING', started_utc=datetime.now(timezone.utc).isoformat(),
                  gui_sha256=digest(gui), pack_sha256=digest(source), layouts=[])
    if args.baseline_exe:
        args.baseline_exe = args.baseline_exe.resolve()
        report['baseline_gui_sha256'] = digest(args.baseline_exe)

    def run(exe, prefix, scenario, script):
        events_path = Path(str(prefix)+'-events.json')
        events_path.write_text(json.dumps(script), encoding='utf-8')
        cmd = [str(exe), '--map', 'MAP_OLDALE_TOWN', '--mode', 'diorama',
               '--overrides', str(source), '--out', str(prefix)+'-saved.json',
               '--probe', str(scenario), '--showcase', str(events_path), '--probe-out', str(prefix)]
        with Path(str(prefix)+'.log').open('w', encoding='utf-8') as log:
            subprocess.run(cmd, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=180, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        result = json.loads(Path(str(prefix)+'.json').read_text())
        assert result['ok'] and result['closed'], 'SDL replay did not finish cleanly'
        return result

    try:
        for layout in ['scene', 'small', 'scaled']:
            scenario = json.loads((ROOT/f'tools/gui-probe-{layout}.json').read_text())
            # A lower, fixed inspection angle gives the sky a visible horizon.
            scenario['camera'].update(pitch=.36, dist=37.0)
            scenario_path = build / f'{layout}-scenario.json'
            scenario_path.write_text(json.dumps(scenario), encoding='utf-8')
            width, height = scenario['viewport']['w'], scenario['viewport']['h']
            scale = scenario['viewport'].get('framebuffer_scale', 1)
            body = height-118
            map_h = int(body*.42)
            view_y = 76+map_h+28
            events = []

            def click(frame, x, y):
                events.extend([dict(frame=frame, type='motion', x=x, y=y),
                               dict(frame=frame+1, type='down', x=x, y=y),
                               dict(frame=frame+2, type='up', x=x, y=y)])

            for i, phase in enumerate([1, 2, 3, 4, 0]):
                frame = 24+i*40
                click(frame, 470, view_y-15)
                click(frame+6, 460, view_y-24+34+phase*19)
                events.append(dict(frame=frame+10, type='motion', x=80, y=18))
            # Save through the public shortcut, then quit through normal SDL.
            events.extend([dict(frame=222, type='key-down', scan=22, mod=64),
                           dict(frame=223, type='key-up', scan=22), dict(frame=226, type='quit')])
            script = dict(frames=230, events=events, record=0, measure_environment=1, chapters=[],
                          checkpoints=[dict(frame=20+i*40, name=n) for i, n in enumerate(NAMES)])
            prefix = build / layout
            result = run(gui, prefix, scenario_path, script)
            states = result['checkpoints']
            assert [s['lighting'] for s in states] == PHASES, f'{layout}: visible lighting choices failed'
            base = states[0]
            for state in states:
                assert state['sky_ok'], f'{layout}: sky shader unavailable'
                for key in ['mode', 'undo', 'room_hash', 'preview_hash', 'mesh_uploads', 'camera',
                            'camera_target', 'draft_dirty', 'unsaved', 'raised', 'accepted_placements']:
                    assert state[key] == base[key], f'{layout}: lighting changed {key}'
                assert state['preview_draw_samples'] == 16 and state['preview_draw_ms'] > 0
            assert base['mode'] == 3 and base['raised'] > 0, 'test needs actual authored scenery'
            working = [Path(str(prefix)+f'.png.{n}.working.json').read_bytes() for n in NAMES]
            assert all(w == working[0] for w in working), 'lighting changed serialized model data'
            assert json.loads(Path(str(prefix)+'-saved.json').read_bytes()) == json.loads(working[0])
            assert source.read_bytes() == before, 'input pack was modified'

            images = [Image.open(str(prefix)+f'.png.{n}.png').convert('RGB') for n in NAMES]
            x, y, w, h = base['view']
            crop = tuple(round(v*scale) for v in [x+3, y+3, x+w-3, y+h-3])
            scenes = [im.crop(crop) for im in images]
            assert ImageChops.difference(scenes[0], scenes[-1]).getbbox() is None, 'Neutral reset changed scene pixels'
            assert len({hashlib.sha256(im.tobytes()).digest() for im in scenes[:5]}) == 5, 'missing/distinct sky phases'
            map_box = tuple(round(v*scale) for v in [208, 108, width-268, 108+map_h-40])
            for im in images[1:]:
                assert ImageChops.difference(images[0].crop(map_box), im.crop(map_box)).getbbox() is None, 'source map was tinted'

            # Independent pixel check: render tint must multiply existing art,
            # leave silhouettes in place, and preserve the dark outline pixels.
            clear = (18, 20, 28)
            night_tint = [(.35+.65*c/255) for c in (120, 136, 192)]
            world_pixels = dark_pixels = 0
            sky_pixels = [set() for _ in range(4)]
            phase_pixels = [list(im.getdata()) for im in scenes[1:5]]
            for pixel, (neutral, night) in enumerate(zip(scenes[0].getdata(), scenes[4].getdata())):
                # The SDL recording banner is an ImGui overlay, not scene art.
                if pixel % scenes[0].width < 300*scale and pixel // scenes[0].width < 50*scale:
                    continue
                if neutral == clear:
                    for phase in range(4):
                        sky_pixels[phase].add(phase_pixels[phase][pixel])
                    continue
                world_pixels += 1
                assert max(abs(night[c]-round(neutral[c]*night_tint[c])) for c in range(3)) <= 2, 'night pass changed/occluded scene art'
                if max(neutral) <= 80:
                    dark_pixels += 1
            assert world_pixels > 10000 and dark_pixels > 100, 'insufficient scene/outline coverage'
            assert all(len(colors) == 6 and (0, 0, 0) not in colors for colors in sky_pixels), 'sky bands missing or rendered black'

            baseline_equal = None
            if args.baseline_exe:
                old_prefix = build / f'{layout}-before'
                old_script = dict(frames=30, events=[dict(frame=26, type='quit')], record=0,
                                  chapters=[], checkpoints=[dict(frame=20, name='neutral')])
                run(args.baseline_exe, old_prefix, scenario_path, old_script)
                old = Image.open(str(old_prefix)+'.png.neutral.png').convert('RGB').crop(crop)
                baseline_equal = ImageChops.difference(old, scenes[0]).getbbox() is None
                assert baseline_equal, 'default neutral differs from pre-change renderer'

            report['layouts'].append(dict(layout=layout, framebuffer=[result['width'], result['height']],
                viewport=[round(w*scale), round(h*scale)],
                checked_crop=[scenes[0].width, scenes[0].height], room_hash=base['room_hash'],
                mesh_upload_delta=states[-1]['mesh_uploads']-base['mesh_uploads'],
                world_pixels_checked=world_pixels, dark_pixels_checked=dark_pixels,
                baseline_neutral_equal=baseline_equal, synchronized_draw_ms={s['name']:s['preview_draw_ms'] for s in states},
                samples_per_phase=16, status='PASS'))
            print(f'PASS {layout}: lighting UI, pixels, persistence, zero mesh uploads', flush=True)
        report['status'] = 'PASS'
    except Exception as exc:
        report['status'], report['error'] = 'FAIL', str(exc)
        raise
    finally:
        report['finished_utc'] = datetime.now(timezone.utc).isoformat()
        (build/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
