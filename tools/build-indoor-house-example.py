# SPDX-License-Identifier: GPL-3.0-or-later
"""Author the two May's-house rooms using locally supplied, pinned Ruby art.

No art is embedded in this recipe. Geometry uses the ordinary v6 Studio parts;
v7 terrain preserves the floor and source collision layers without turning
indoor sprite-priority values into physical elevations.
"""
import argparse
import copy
import importlib.util
import json
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('voxel_world', ROOT / 'tools/build-voxel-world.py')
world = importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)


def words(path):
    data = path.read_bytes()
    return list(struct.unpack('<' + 'H' * (len(data) // 2), data))


def source(root, floor):
    name = f'LittlerootTown_MaysHouse_{floor}F'
    info = json.loads((root / 'data/maps' / name / 'map.json').read_bytes())
    groups = json.loads((root / 'data/maps/map_groups.json').read_bytes())
    if groups[groups['group_order'][1]][floor+1] != name:
        raise ValueError('House map identity changed; review the native support profile')
    layouts = json.loads((root / 'data/layouts/layouts.json').read_bytes())['layouts']
    layout = next(item for item in layouts if item['id'] == info['layout'])
    w, h = layout['width'], layout['height']
    if (w, h) != ((11, 9) if floor == 1 else (9, 8)) or (
        layout['primary_tileset'], layout['secondary_tileset']) != (
            'gTileset_Building', 'gTileset_BrendansMaysHouse'):
        raise ValueError('House layout changed; review the recipe against its source')
    blocks = words(root / layout['blockdata_filepath'])
    if len(blocks) != w * h:
        raise ValueError('Incomplete room map')
    definitions, sheets, palettes = {}, [], []
    for offset, folder in ((0, 'primary/building'), (512, 'secondary/brendans_mays_house')):
        base = root / 'data/tilesets' / folder
        entries = words(base / 'metatiles.bin')
        for i, attr in enumerate(words(base / 'metatile_attributes.bin')):
            definitions[i + offset] = dict(attr=attr, entries=entries[i*8:i*8+8])
        sheets.append(Image.open(base / 'tiles.png'))
        palettes.append([list(map(int, line.split())) for p in range(16)
                         for line in (base / 'palettes' / f'{p:02}.pal').read_text().splitlines()[3:19]])
    if any(sheet.mode != 'P' for sheet in sheets):
        raise ValueError('Indexed source art required')
    meta, rgb = [], []
    for y in range(h * 16):
        for x in range(w * 16):
            entries = definitions[blocks[y//16*w+x//16] & 1023]['entries']
            winner = None
            for layer in (0, 4):
                e = entries[layer + y % 16 // 8 * 2 + x % 16 // 8]
                tile = e & 1023
                sheet = sheets[tile >= 512]
                tile %= 512
                tx, ty = x % 8, y % 8
                if e & 1024: tx = 7-tx
                if e & 2048: ty = 7-ty
                sx, sy = tile % (sheet.width//8)*8+tx, tile // (sheet.width//8)*8+ty
                index = sheet.getpixel((sx, sy)) & 15 if sy < sheet.height else 0
                if index: winner = (e >> 12, index)
            meta.append(winner)
            rgb.append(tuple(palettes[winner[0] >= 6][winner[0]*16+winner[1]]) if winner else (0, 0, 0))
    art = Image.new('RGB', (w*16, h*16)); art.putdata(rgb)
    return name, w, h, blocks, definitions, meta, art


def stripe(data, floor, x0, width, output):
    name, w, h, blocks, definitions, meta, art = data
    s = world.Source.__new__(world.Source)
    s.w, s.h = width*16, h*16
    cells = [y*w+x for y in range(h) for x in range(x0, x0+width)]
    ids = [blocks[i] & 1023 for i in cells]
    s.p = dict(id=f'indoor-may-{floor}f-{x0}', name=f"May's house {floor}F {'west' if not x0 else 'east'}",
               source=dict(room='MAP_LITTLEROOT_TOWN_MAYS_HOUSE_'+str(floor)+'F', x=x0+7, y=7,
                           coordinates='backup-map'), w=width, extent=h, ids=ids, mask=[1]*len(ids),
               tiles={str(i): definitions[i] for i in sorted(set(ids))}, apply={'class': 'prop'},
               model_seeded=True, parts=[])
    s.meta = [meta[y*w*16+x] for y in range(h*16) for x in range(x0*16, (x0+width)*16)]
    s.art = art.crop((x0*16, 0, (x0+width)*16, h*16))
    s.obj, s.shadow = [False]*(s.w*s.h), [False]*(s.w*s.h)
    north = 48 if floor == 1 else 32
    # Projected furniture fronts are removed from the floor as well as the
    # blocked source cell. Carpets and ordinary wooden floor remain native art.
    regions = ([[0, 0, w*16, north], [96, 64, 48, 32], [78, 96, 68, 32]] if floor == 1 else
               [[0, 0, w*16, north], [64, 32, 80, 16], [96, 48, 48, 48]])
    s.mark(lambda x, y, m: any(world.in_rect(x+x0*16, y, r) for r in regions))

    def mat(rect):
        x, y, rw, rh = rect
        left, right = max(x, x0*16), min(x+rw, (x0+width)*16)
        return s.material([left-x0*16, y, right-left, rh])

    wall = mat([4, 24, 1, 1] if x0 == 0 and floor == 1 else
               [98, 9, 1, 1] if floor == 1 else [36 if x0 == 0 else 96, 5, 1, 1])

    def box(label, x, z, rw, depth, height, material=None, y=0, front=None, top=None):
        material = material or wall
        materials = [front or material, material, material, material, top or material, material]
        s.solid(label, [x-x0*16, y, z+depth/2-(h*16-8)], [rw, height, depth], materials)

    # Furniture uses its own front, side and top texels, never a door/screen
    # repeated around every side. Pixel scale is unchanged by the reconstruction.
    if x0 and floor == 1:
        wood = mat([101, 125, 2, 2])
        box('Glass cabinet', 106, 32, 22, 16, 28, wood, front=mat([106, 20, 22, 28]))
        box('Kitchen counter', 128, 32, 32, 16, 24, wood,
            front=mat([128, 36, 32, 12]), top=mat([128, 28, 32, 8]))
        box('Refrigerator', 160, 32, 16, 16, 32, mat([164, 24, 6, 20]),
            front=mat([160, 16, 16, 32]))
        box('Video cabinet', 96, 64, 16, 16, 26, mat([98, 73, 1, 1]), front=mat([96, 62, 16, 26]))
        box('Television', 112, 64, 32, 16, 23, mat([112, 70, 1, 1]), front=mat([112, 65, 32, 23]))
        box('Table top', 96, 96, 32, 32, 3, wood, y=15, top=mat([96, 96, 32, 32]))
        for lx in (98, 124):
            for lz in (98, 124): box(f'Table leg {lx} {lz}', lx, lz, 3, 3, 15, wood)
        # Narrow seats preserve the source's passable approach to the table.
        for cx in (83, 132):
            for cz in (99, 115):
                chair = mat([86, 103, 1, 1])
                box(f'Chair {cx} {cz}', cx, cz, 9, 9, 9, chair)
                box(f'Chair back {cx} {cz}', cx if cx < 96 else cx+7, cz, 2, 9, 17, chair)
        box('Twin windows',64,48,32,1,14,wall,y=22,front=mat([64,6,32,14]))
        box('Kitchen window',144,48,16,1,14,wall,y=22,front=mat([144,6,16,14]))
    elif x0:
        desk = mat([110, 26, 4, 10])
        box('Television', 64, 16, 16, 16, 26, mat([64, 13, 3, 12]), front=mat([64, 8, 16, 32]))
        box('GameCube', 80, 20, 16, 12, 10, mat([82, 21, 8, 8]), front=mat([80, 16, 16, 16]))
        box('Desk', 96, 16, 48, 16, 18, desk, front=mat([96, 32, 48, 16]), top=mat([96, 16, 48, 8]))
        box('Computer', 128, 18, 16, 12, 17, mat([130, 18, 4, 12]), y=18, front=mat([128, 0, 16, 32]))
        bed = mat([110, 67, 1, 1])
        box('Bed frame', 109, 64, 22, 30, 7, bed)
        box('Mattress', 111, 66, 18, 25, 4, mat([117, 78, 1, 1]), y=7,
            top=mat([111, 69, 18, 23]))
        box('Pillow', 112, 65, 16, 5, 4, mat([119, 69, 1, 1]), y=11)
        box('Headboard', 109, 61, 22, 3, 18, bed)

    # Shell openings agree with the original warp cells. Separate stripe meshes
    # bound the ordinary voxel compiler's memory/work; no mesher limit is raised.
    if x0 == 0:
        gap0, gap1 = (28, 52) if floor == 1 else (16, 32)
        box('North wall west', 0, north-4, gap0, 4, 40)
        box('North wall east', gap1, north-4, 64-gap1, 4, 40)
        stair = mat([28, 25, 22, 22] if floor == 1 else [16, 16, 16, 16])
        for step in range(3):
            box(f'Stair {step}', gap0, north-8*(step+1), gap1-gap0, 8, 2+step*4, stair)
        box('West wall', 0, north, 3, h*16-north, 40)
        if floor == 1:
            box('Door wall west', 0, h*16-3, 16, 3, 40)
            box('Door wall east', 48, h*16-3, 16, 3, 40)
        else: box('South wall', 0, h*16-3, 64, 3, 40)
    else:
        box('North wall', 64, north-4, (w-4)*16, 4, 40)
        box('East wall', w*16-3, north, 3, h*16-north, 40)
        box('South wall', 64, h*16-3, (w-4)*16, 3, 40)
    box('Ceiling', x0*16, north-4, width*16, h*16-north+4, 1, y=40)
    return s.finish(output)


def build(starter, decomp, output):
    result = copy.deepcopy(starter)
    result['version'] = 7
    result['patterns'] = [p for p in result['patterns'] if not p['id'].startswith('indoor-may-')]
    terrain = result.setdefault('terrain', {'version': 1}).setdefault('maps', [])
    terrain[:] = [m for m in terrain if (m['group'], m['number']) not in ((1, 2), (1, 3))]
    output.mkdir(parents=True, exist_ok=True)
    report = []
    for floor in (1, 2):
        data = source(decomp, floor)
        name, w, h, blocks, definitions, _, art = data
        art.save(output / f'{name}.png')
        patterns = [stripe(data, floor, 0, 4, output), stripe(data, floor, 4, w-4, output)]
        result['patterns'].extend(patterns)
        cells = [dict(x=x+7, y=y+7, expected=blocks[y*w+x], underlay=-1,
                      surfaces=[dict(layer=blocks[y*w+x] >> 12, height=0, thickness=0,
                                     kind='ground', top=-1, side=513)])
                 for y in range(h) for x in range(w)]
        if floor == 1:
            for c in cells:
                if 12<=c['x']<=15 and 13<=c['y']<=14:
                    c['underlay']=0x2a1  # same tileset's plain carpet below chairs/table
        terrain.append(dict(group=1, number=floor+1, width=w+15, height=h+14, cells=cells,
                            tiles=[dict(id=i, **definitions[i]) for i in sorted({513,0x2a1} | {b & 1023 for b in blocks})]))
        report.append(dict(room=f'MAP_LITTLEROOT_TOWN_MAYS_HOUSE_{floor}F', patterns=[p['id'] for p in patterns],
                           parts=[len(p['parts']) for p in patterns], cells=len(cells), floor_height=0))
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT / 'mod-assets/voxel-world-v6.json')
    parser.add_argument('--decomp', type=Path, default=ROOT / 'third_party/pokeruby')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/indoor-house/pack.json')
    args = parser.parse_args()
    if args.pack.resolve() == args.out.resolve(): parser.error('Keep the input pack separate')
    result, report = build(json.loads(args.pack.read_bytes()), args.decomp, args.out.parent)
    args.out.write_text(json.dumps(result, separators=(',', ':'))+'\n', encoding='utf-8')
    (args.out.parent / 'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'Built both house floors: {sum(sum(r["parts"]) for r in report)} parts; original maps preserved')


if __name__ == '__main__':
    main()
