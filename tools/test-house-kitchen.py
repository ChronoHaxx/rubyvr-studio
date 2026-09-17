# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the selective kitchen recipe; optional real inputs prove pack isolation.

Default checks require neither game art nor SDL/OpenGL. --decomp adds source
mutation checks; --baseline adds comparison with the previous complete pack.
"""
import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('house', ROOT / 'tools/build-indoor-house-example.py')
house = importlib.util.module_from_spec(spec)
spec.loader.exec_module(house)


def geometry_contract():
    parts = {}
    def box(name, x, z, w, d, h, material=None, y=0, front=None, top=None):
        parts[name] = dict(x=x, z=z, w=w, d=d, h=h, y=y, material=material, front=front, top=top)
    house.kitchen_parts(box, lambda rect: rect)
    for name, p in parts.items():
        assert 128 <= p['x'] < p['x']+p['w'] <= 176, name
        assert 32 <= p['z'] < p['z']+p['d'] <= 48, name  # No new approach/collision footprint.
        assert p['h'] > 0 and p['y'] >= 0, name
        if name.startswith('Kitchen ') and 'tap' not in name:
            assert p['y']+p['h'] <= 16, name  # Waist-height, independent of source priority.
    surface = parts['Kitchen work surface']
    assert (surface['w'], surface['d'], surface['y']+surface['h']) == (32, 11, 16)
    assert surface['top'] == [128, 29, 32, 11]  # One complete sink, no tiled/truncated bottom.
    assert parts['Kitchen back rim']['d'] == 5
    assert parts['Kitchen doors']['front'] == [128, 44, 32, 1]  # Only a plain band repeats.
    assert parts['Kitchen handles']['front'] == [128, 41, 32, 3]
    fridge = parts['Refrigerator']
    assert (fridge['w'], fridge['d'], fridge['h']) == (16, 16, 32)
    assert fridge['front'] == [160, 16, 16, 32]
    assert fridge['material'][2:] == fridge['top'][2:] == [1, 1]  # No side/top handles.
    # A numeric match with unknown source pixels must not enable the adaptation.
    blocks = [0]*99
    for y, row in ((1, [1585, 1586, 1584]), (2, [1593, 1594, 1592])):
        blocks[y*11+8:y*11+11] = row
    unknown = ('LittlerootTown_MaysHouse_1F', 11, 9, blocks, {}, [None]*(176*144), None)
    assert not house.kitchen_supported(unknown)


def local_contract(decomp, baseline):
    data = house.source(decomp, 1)
    assert house.kitchen_supported(data), 'Pinned Ruby kitchen should enable'
    original = house.stripe(data, 1, 4, 7, None)
    for change in ('id', 'collision', 'pixel', 'palette', 'missing', 'room'):
        changed = copy.deepcopy(data)
        if change in ('id', 'collision'):
            changed[3][2*11+8] ^= 1 if change == 'id' else 0x400
        elif change == 'room':
            changed = ('SomeOtherRoom', *changed[1:])
        else:
            i = 30*176+140
            pal, pixel = changed[5][i]
            changed[5][i] = None if change == 'missing' else (
                pal ^ (1 if change == 'palette' else 0), pixel ^ (1 if change == 'pixel' else 0))
        assert not house.kitchen_supported(changed), change
        fallback = house.stripe(changed, 1, 4, 7, None)
        assert 'Kitchen counter' in [p['name'] for p in fallback['parts']], change
        assert not any(p['name']=='Kitchen work surface' for p in fallback['parts']), change
    assert len(original['parts']) <= 64
    # Enforce the real loader's memory/work bounds; shell returns can enlarge
    # the voxel lattice even when the individual parts are small.
    lo = [min(p['position'][i]-(p['size'][i]/2 if i == 2 else 0) for p in original['parts']) for i in range(3)]
    hi = [max(p['position'][i]+p['size'][i]/(2 if i == 2 else 1) for p in original['parts']) for i in range(3)]
    volume = math.prod(math.ceil(b*16)-math.floor(a*16) for a, b in zip(lo, hi))
    assert volume <= 1048576 and volume*len(original['parts']) <= 16777216
    if baseline:
        starter = json.loads(baseline.read_bytes())
        untouched = copy.deepcopy(starter)
        with tempfile.TemporaryDirectory() as tmp:
            result, report = house.build(starter, decomp, Path(tmp))
            assert starter == untouched, 'Generator mutated its input'
            again, _ = house.build(result, decomp, Path(tmp))
            assert again == result, 'Regeneration must be idempotent'
        assert report[0]['kitchen']['status'] == 'adapted'
        assert result['terrain'] == starter['terrain'], 'Terrain/collision/warp guards changed'
        old = {p['id']: p for p in starter['patterns']}
        new = {p['id']: p for p in result['patterns']}
        assert old.keys() == new.keys(), 'Room coverage changed'
        for identity in old:
            if identity != 'indoor-may-1f-4':
                assert old[identity] == new[identity], identity
            else:
                assert {k: v for k, v in old[identity].items() if k != 'parts'} == {
                    k: v for k, v in new[identity].items() if k != 'parts'}, 'Source ownership changed'
                def other(parts):
                    return [p for p in parts if not p['name'].startswith('Kitchen ')
                            and p['name'] not in ('Refrigerator', 'North wall', 'East wall', 'Ceiling')]
                assert other(old[identity]['parts']) == other(new[identity]['parts']), 'Other furnishings changed'
                # At body height, newly occupied geometry must stay inside
                # native blocked source cells. This is a solid-footprint check;
                # the native radius/physical playthrough remains a human check.
                def occupied(parts):
                    cells = set()
                    for p in parts:
                        x, y, z = (v*16 for v in p['position'])
                        w, h, d = (v*16 for v in p['size'])
                        if y >= 20 or y+h <= 0:
                            continue
                        for pz in range(round(z-d/2+136), round(z+d/2+136)):
                            for px in range(round(x+64), round(x+w+64)):
                                cells.add((px, pz))
                    return cells
                before, after = occupied(old[identity]['parts']), occupied(new[identity]['parts'])
                for x, z in after-before:
                    assert data[3][z//16*11+x//16] & 0xC00, ('New walkable obstruction', x, z)
                assert not any(128 <= x < 160 and z >= 48 for x, z in after-before)
        print(f'PASS: {len(old)-1} other patterns, all terrain and source ownership unchanged; six source mutations rejected')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--decomp', type=Path)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    if args.baseline and not args.decomp:
        parser.error('--baseline requires --decomp')
    geometry_contract()
    if args.decomp:
        local_contract(args.decomp, args.baseline)
    print('PASS: kitchen native texel scale, dimensions, footprint and unknown-source fallback')
