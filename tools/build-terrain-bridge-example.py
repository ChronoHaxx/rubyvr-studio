# SPDX-License-Identifier: GPL-3.0-or-later
"""Add the reviewed Route 104 pond/boardwalk to the unchanged six-map example.

Requires locally prepared source assets. Generated terrain/art stay local.
Route 104 has a Rustboro atlas; do not load its IDs through Petalburg's atlas.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path

from terrain_bridges import apply_bridge

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('regions', ROOT / 'tools/build-terrain-region-example.py')
regions = importlib.util.module_from_spec(spec)
spec.loader.exec_module(regions)


def source(recipe):
    root = regions.SOURCE
    layouts = {item['id']: item for item in json.loads((root / 'data/layouts/layouts.json').read_bytes())['layouts']}
    info = json.loads((root / 'data/maps/Route104/map.json').read_bytes())
    layout = layouts[info['layout']]
    if (recipe['map'] != 'Route104' or recipe['size'] != [40, 80] or
            [layout['width'], layout['height']] != recipe['size'] or
            (layout['primary_tileset'], layout['secondary_tileset']) != ('gTileset_General', 'gTileset_Rustboro')):
        raise ValueError('Route 104 identity/dimensions/tileset pair changed; review the source')
    definitions = {}
    for offset, folder in ((0, 'primary/general'), (512, 'secondary/rustboro')):
        base = root / 'data/tilesets' / folder
        entries = regions.u16(base / 'metatiles.bin')
        for i, attr in enumerate(regions.u16(base / 'metatile_attributes.bin')):
            definitions[offset+i] = dict(id=offset+i, attr=attr, entries=entries[i*8:i*8+8])
    blocks = regions.u16(root / layout['blockdata_filepath'])
    if len(blocks) != 3200:
        raise ValueError('Route 104 packed source size changed')
    groups = json.loads((root / 'data/maps/map_groups.json').read_bytes())
    identity = next((g, names.index('Route104')) for g, key in enumerate(groups['group_order'])
                    if 'Route104' in (names := groups[key]))
    return blocks, definitions, identity


def build(recipe, starter, regional_recipe):
    if recipe.get('version') != 1:
        raise ValueError('Unsupported bridge recipe version')
    result, base_report = regions.build(regional_recipe, starter)
    result = copy.deepcopy(result)
    blocks, definitions, (group, number) = source(recipe)
    bridge = recipe['bridge']
    # Explicit context baseline matches Petalburg's 16 px west edge. It is not
    # a completed geography proposal for Route 104's southern coast/cliffs.
    cells = [dict(x=x+7, y=y+7, expected=blocks[y*40+x], underlay=-1,
                  surfaces=[dict(layer=blocks[y*40+x] >> 12, height=16, thickness=16,
                                 kind='ground', top=-1, side=135, side_offset=8)])
             for y in range(80) for x in range(40)]
    cells, report = apply_bridge(40, 80, blocks, definitions, cells, bridge)
    used = {135, bridge['water_tile'], bridge['side_tile']} | {p & 1023 for p in blocks}
    result['terrain']['maps'].append(dict(group=group, number=number, width=55, height=94,
        cells=cells, tiles=[definitions[i] for i in sorted(used)]))
    report.update(map='MAP_ROUTE104', source_size=[40, 80], source_tilesets=['General', 'Rustboro'],
                  context_height=16, baseline_maps_preserved=len(base_report['maps']),
                  assumptions=recipe['assumptions'], scope='Authored pond and bridge; no live/headset acceptance')
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--recipes', type=Path, default=ROOT / 'recipes/terrain-bridge.json')
    parser.add_argument('--pack', type=Path, default=ROOT / 'mod-assets/voxel-world-v6.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/terrain-bridge/regions.json')
    parser.add_argument('--report', type=Path, default=ROOT / 'build/terrain-bridge/report.json')
    args = parser.parse_args()
    regional_path = ROOT / 'recipes/terrain-regions.json'
    if len({p.resolve() for p in (args.recipes, args.pack, args.out, args.report, regional_path)}) != 5:
        parser.error('Inputs, output and report must be separate paths')
    result, report = build(json.loads(args.recipes.read_bytes()), json.loads(args.pack.read_bytes()),
                           json.loads(regional_path.read_bytes()))
    data = (json.dumps(result, separators=(',', ':')) + '\n').encode()
    report['pack_sha256'] = hashlib.sha256(data).hexdigest()
    for path in (args.out, args.report):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"PASS: Route 104, {report['deck_cells']} deck cells, {report['water_cells'] + report['deck_cells']} water positions; "
          f"{report['clearance']} px clearance; six original maps unchanged")


if __name__ == '__main__':
    main()
