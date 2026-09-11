# SPDX-License-Identifier: GPL-3.0-or-later
"""Three paired actual SDL views of the local Route 104 bridge fixture."""
import hashlib
import importlib.util
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/terrain-bridge'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    compiler = module('bridge_example', 'build-terrain-bridge-example.py')
    base, _ = compiler.regions.build(json.loads((ROOT/'recipes/terrain-regions.json').read_bytes()),
                                     json.loads((ROOT/'mod-assets/voxel-world-v6.json').read_bytes()))
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'before.json').write_text(json.dumps(base), encoding='utf-8')
    ui = module('bridge_view', 'test-connected-studio.py')
    ui.OUT = OUT/'visual'
    views = [
        ('overview', 'Route 104: original bent boardwalk', dict(yaw=.35, pitch=.9, dist=22, tx=32, ty=1, tz=22)),
        ('clearance', 'Water continues below a solid deck', dict(yaw=0, pitch=.12, dist=5.5, tx=34, ty=.8, tz=22.5)),
        ('bank', 'Entrance stays level with its bank', dict(yaw=3.14, pitch=.4, dist=6, tx=32, ty=1, tz=17)),
    ]
    try:
        font = ImageFont.truetype('DejaVuSans.ttf', 19)
        small = ImageFont.truetype('DejaVuSans.ttf', 15)
    except OSError:
        font = small = ImageFont.load_default()
    frames, captures = [], []
    for name, title, camera in views:
        sheet = Image.new('RGB', (1124, 354), '#131720')
        draw = ImageDraw.Draw(sheet)
        draw.text((12, 5), title, font=font, fill='#eef2ff')
        for column, version in enumerate(('before', 'after')):
            source = OUT/('before.json' if version == 'before' else 'regions.json')
            state = ui.run(name+'-'+version, [dict(frame=26, type='quit')], [(20, 'view')],
                           frames=30, map_id='MAP_ROUTE104', source=source, camera=camera)['view']
            x, y, w, h = map(int, state['view'])
            capture = ui.OUT/f'{name}-{version}.png.view.png'
            im = Image.open(capture).convert('RGB').crop((x, y, x+w, y+h))
            sheet.paste(im.resize((562, 256), Image.Resampling.NEAREST), (column*562, 64))
            draw.text((column*562+12, 35), ['Before: flat source floor', 'After: layered water and deck'][column], font=small, fill='#eef2ff')
            captures.append(dict(view=name, version=version, room_hash=state['room_hash'],
                                 source_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
        draw.text((12, 328), 'Actual SDL captures, same camera/art | bank/deck 16 px, water 8 px, deck underside 12 px',
                  font=small, fill='#b9c5d5')
        sheet.save(OUT/f'{name}-comparison.png')
        frames.append(sheet)
    target = ROOT/'docs/media/terrain-bridge.gif'
    frames[0].save(target, save_all=True, append_images=frames[1:], duration=4000, loop=0, disposal=2)
    (OUT/'visual.json').write_text(json.dumps(dict(duration_seconds=12, frames=3, captures=captures,
        gif_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
        scope='Paired single-map production renders; sampled checkpoints, not real-time or human/headset evidence'), indent=2)+'\n')
    print(f'PASS: six production captures; {target}')


if __name__ == '__main__':
    main()
