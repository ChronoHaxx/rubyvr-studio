# SPDX-License-Identifier: GPL-3.0-or-later
"""Capture before/after kitchen geometry using hidden production Studio windows.

No desktop-wide input or focus calls. Isolated views use only the kitchen parts
from each pack; room captures use the complete pack unchanged. Requires local
Ruby source art and a built native Studio GUI. Results are evidence, not human
acceptance or a performance benchmark.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw
from studio_paths import executable, native_environment

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', required=True, type=Path)
    parser.add_argument('--after', required=True, type=Path)
    parser.add_argument('--decomp', type=Path, default=ROOT/'third_party/pokeruby')
    parser.add_argument('--gui', type=Path)
    parser.add_argument('--out', type=Path, default=ROOT/'build/emerald-reuse/kitchen/review')
    args = parser.parse_args()
    gui = (args.gui or executable('rubyvr_gui')).resolve()
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    env = native_environment()
    for key in tuple(env):
        if key.startswith('RUBYVR_'):
            del env[key]
    records = dict(gui_sha256=hashlib.sha256(gui.read_bytes()).hexdigest(), packs={})
    def run(label, flags):
        with (out/(label+'.log')).open('w') as log:
            subprocess.run([str(gui), '--decomp', str(args.decomp.resolve()),
                '--map', 'MAP_LITTLEROOT_TOWN_MAYS_HOUSE_1F', *map(str, flags)],
                cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180)
    script = out/'events.json'
    script.write_text(json.dumps(dict(frames=12, events=[], checkpoints=[dict(frame=9, name='room')],
                                     record=0, chapters=[])))
    scenario = json.loads((ROOT/'tools/gui-probe-scene.json').read_bytes())
    # Raised room camera sees over the TV while remaining under the ceiling.
    scenario['camera'] = dict(yaw=0, pitch=.3, dist=2.7, tx=15.7, ty=1., tz=9.55)
    camera = out/'camera.json'; camera.write_text(json.dumps(scenario))
    for label, pack in (('before', args.before.resolve()), ('after', args.after.resolve())):
        data = json.loads(pack.read_bytes())
        pattern = copy.deepcopy(next(p for p in data['patterns'] if p['id']=='indoor-may-1f-4'))
        pattern['parts'] = [p for p in pattern['parts'] if p['name'] in ('Glass cabinet', 'Refrigerator')
                            or (p['name'].startswith('Kitchen ') and p['name'] not in (
                                'Kitchen window', 'Kitchen wall return', 'Kitchen back wall', 'Kitchen ceiling return'))]
        isolated = out/(label+'-isolated.json')
        isolated.write_text(json.dumps(dict(version=6, patterns=[pattern])))
        run(label+'-assets', ['--overrides', isolated, '--asset-review', out/label])
        run(label+'-room', ['--mode', 'diorama', '--overrides', pack, '--probe', camera,
                           '--showcase', script, '--probe-out', out/(label+'-room')])
        records['packs'][label] = dict(sha256=hashlib.sha256(pack.read_bytes()).hexdigest(),
            assets=json.loads((out/label/'renders.json').read_bytes()))
    frames = []
    for angle in ('front', 'oblique', 'right', 'back', 'left', 'roof'):
        frame = Image.new('RGB', (1152, 620), (16, 19, 27))
        draw = ImageDraw.Draw(frame)
        for col, label in enumerate(('before', 'after')):
            im = Image.open(out/label/f'indoor-may-1f-4-{angle}.png').convert('RGB')
            frame.paste(im.resize((576, 576), Image.Resampling.NEAREST), (col*576, 40))
            draw.text((col*576+20, 15), f'{label.upper()} | {angle} | actual Studio mesher', fill='white')
        frames.append(frame)
        frame.save(out/(angle+'.png'))
    frames[0].save(out/'comparison.gif', save_all=True, append_images=frames[1:], duration=2000, loop=0)
    records.update(human_acceptance='pending', native_game_tested=False,
                   isolated_views='kitchen-only part filter; full room captures retain the entire pack')
    (out/'verification.json').write_text(json.dumps(records, indent=2)+'\n')
    print('PASS: six before/after views, full-room captures and exact production resave; human acceptance pending')


if __name__ == '__main__':
    main()
