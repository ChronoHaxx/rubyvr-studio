"""Local, explicitly authored Oldale / Route 101 seam experiment.

This is an authored regional example, not inferred Hoenn geography. Two 8px
drops follow the inspected ledge outlines. Shared corners grade the open paths
around their ends. Oldale's south edge is 16px; Route 101's south edge meets
Littleroot's existing zero-height ground. Source assets and output stay local.
"""
import json
import argparse
from pathlib import Path
import struct
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/pokeruby'


def u16(path):
    data = path.read_bytes()
    return struct.unpack('<'+'H'*(len(data)//2), data)


def route_corners():
    """Join every ground edge except the three inspected cliff segments.

    Relax the two authored transition bands, pinning the cliff lips and the
    flat land outside those bands. A cliff endpoint shares one corner, so its
    exposed face tapers to zero instead of ending in a wall across the path.
    No colour, collision flag or gameplay elevation chooses a physical height.
    """
    def key(x, y, k): return (y*20+x)*4+k
    parent = list(range(1600))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    def join(a, b): parent[root(b)] = root(a)
    cliffs = {('s', x, 7) for x in range(2, 6)} | {('s', x, 6) for x in range(6, 11)}
    cliffs |= {('e', 5, 7)} | {('s', x, 13) for x in range(8, 12)}
    lips = set()
    for y in range(20):
        for x in range(20):
            for side, pairs in [('e', ((1, 0), (3, 2))), ('s', ((2, 0), (3, 1)))]:
                nx, ny = x+(side == 'e'), y+(side == 's')
                if nx >= 20 or ny >= 20: continue
                for a, b in pairs:
                    ia, ib = key(x,y,a), key(nx,ny,b)
                    if (side,x,y) in cliffs: lips.update((ia,ib))
                    else: join(ia,ib)
    heights, positions, adjacent = defaultdict(list), {}, defaultdict(set)
    for y in range(20):
        for x in range(20):
            level = 16 if y < (8 if x < 6 else 7) else 8 if y < 14 else 0
            for k in range(4):
                i = root(key(x,y,k))
                heights[i].append(level)
                positions[i] = (x+(k&1), y+(k>>1))
            for a,b in ((0,1),(0,2),(1,3),(2,3)):
                ia,ib = root(key(x,y,a)),root(key(x,y,b))
                adjacent[ia].add(ib); adjacent[ib].add(ia)
    lips = {root(i) for i in lips}
    values = {i:sum(v)/len(v) for i,v in heights.items()}
    pinned = {i for i,v in heights.items() if positions[i][1] <= 4 or
              10 <= positions[i][1] <= 11 or positions[i][1] >= 18 or
              (i in lips and min(v) == max(v))}
    for _ in range(160):
        values = {i:values[i] if i in pinned else sum(values[j] for j in adjacent[i])/len(adjacent[i])
                  for i in values}
    corners = {(x,y):[round(values[root(key(x,y,k))]) for k in range(4)]
               for y in range(20) for x in range(20)}
    # Every unmarked edge has exactly the same endpoints on both cells.
    for (x,y),p in corners.items():
        if x < 19 and ('e',x,y) not in cliffs: assert p[1::2] == corners[x+1,y][0::2]
        if y < 19 and ('s',x,y) not in cliffs: assert p[2:] == corners[x,y+1][:2]
    return corners


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack',type=Path,default=ROOT/'mod-assets/voxel-world-v6.json')
    parser.add_argument('--out',type=Path,default=ROOT/'build/terrain-review/seam-fixture.json')
    args = parser.parse_args()
    pack = json.loads(args.pack.read_bytes())
    layouts = json.loads((SOURCE/'data/layouts/layouts.json').read_bytes())['layouts']
    groups = json.loads((SOURCE/'data/maps/map_groups.json').read_bytes())
    definitions = {}
    for offset, folder in [(0, 'primary/general'), (512, 'secondary/petalburg')]:
        base = SOURCE/'data/tilesets'/folder
        entries, attrs = u16(base/'metatiles.bin'), u16(base/'metatile_attributes.bin')
        for i, attr in enumerate(attrs):
            definitions[offset+i] = dict(id=offset+i, attr=attr, entries=entries[i*8:i*8+8])
    maps = []
    route = route_corners()
    for name in ['OldaleTown', 'Route101']:
        info = json.loads((SOURCE/'data/maps'/name/'map.json').read_bytes())
        layout = next(l for l in layouts if l['id'] == info['layout'])
        assert (layout['width'], layout['height']) == (20, 20)
        assert (layout['primary_tileset'], layout['secondary_tileset']) == ('gTileset_General', 'gTileset_Petalburg')
        blocks = u16(SOURCE/layout['blockdata_filepath'])
        if name == 'Route101':
            assert [v&1023 for v in blocks[6*20+6:6*20+11]] == [0x6e,0x87,0x87,0x87,0xd6]
            assert [v&1023 for v in blocks[7*20+2:7*20+7]] == [0xd5,0x87,0x87,0x87,0x8e]
            assert [v&1023 for v in blocks[13*20+8:13*20+12]] == [0xd5,0x87,0x87,0xd6]
        group, number = next((g, groups[key].index(name)) for g, key in enumerate(groups['group_order']) if name in groups[key])
        cells, used = [], set()
        for y in range(20):
            for x in range(20):
                packed = blocks[y*20+x]
                nw, ne, sw, se = route[x,y] if name == 'Route101' else (16,16,16,16)
                ledge = False
                if name == 'Route101':
                    ledge = y in (6, 7, 13) and packed&1023 in (0x6e,0x87,0xd6,0xd5,0x8e)
                top = 1 if ledge else -1
                surface = dict(layer=packed>>12, height=nw, thickness=nw,
                               kind='ground', top=top, side=0x87, side_offset=8)
                for key,value in dict(rise_x=ne-nw,rise_z=sw-nw,corner_delta=se-ne-sw+nw).items():
                    if value: surface[key] = value
                cells.append(dict(x=x+7, y=y+7, expected=packed, underlay=top, surfaces=[surface]))
                used.update([packed&1023, 0x87])
                if top >= 0:
                    used.add(top)
        maps.append(dict(group=group, number=number, width=35, height=34,
                         tiles=[definitions[i] for i in sorted(used)], cells=cells))
    pack['version'] = 7
    pack['terrain'] = dict(version=1, maps=maps)
    output = args.out
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(pack, separators=(',', ':'))+'\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
