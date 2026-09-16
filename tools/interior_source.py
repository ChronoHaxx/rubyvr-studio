# SPDX-License-Identifier: GPL-3.0-or-later
"""Read locally supplied indexed Ruby interior art; no game data is embedded."""
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
import json
import struct
from PIL import Image

FOLDERS = {'gTileset_Building': 'primary/building',
           'gTileset_GenericBuilding': 'secondary/generic_building',
           'gTileset_PokemonCenter': 'secondary/pokemon_center',
           'gTileset_Shop': 'secondary/shop', 'gTileset_Lab': 'secondary/lab',
           'gTileset_BrendansMaysHouse': 'secondary/brendans_mays_house'}

def words(path):
    data=path.read_bytes()
    if len(data)%2: raise ValueError(f'Truncated word data: {path}')
    return list(struct.unpack('<'+'H'*(len(data)//2),data))

@lru_cache(maxsize=12)
def tileset(root,primary,secondary):
    definitions,sheets,palettes={},[],[]
    for offset,name in ((0,primary),(512,secondary)):
        base=Path(root)/'data/tilesets'/FOLDERS[name]
        entries=words(base/'metatiles.bin')
        for i,attr in enumerate(words(base/'metatile_attributes.bin')):
            row=entries[i*8:i*8+8]
            if len(row)!=8: raise ValueError('Incomplete metatile')
            definitions[i+offset]={'attr':attr,'entries':row}
        sheet=Image.open(base/'tiles.png');sheet.load()
        if sheet.mode!='P':raise ValueError('Indexed source art required')
        sheets.append(sheet)
        palettes.append([tuple(map(int,line.split())) for p in range(16)
                         for line in (base/'palettes'/f'{p:02}.pal').read_text().splitlines()[3:19]])
    return definitions,sheets,palettes

@dataclass
class Room:
    name: str
    group: int
    number: int
    width: int
    height: int
    blocks: list
    definitions: dict
    meta: list
    art: Image.Image
    secondary: str
    warps: list
    map_id: str


def rooms(root):
    root=Path(root)
    layouts={x['id']:x for x in json.loads((root/'data/layouts/layouts.json').read_bytes())['layouts']}
    groups=json.loads((root/'data/maps/map_groups.json').read_bytes())
    for group,g in enumerate(groups['group_order']):
        for number,name in enumerate(groups[g]):
            info=json.loads((root/'data/maps'/name/'map.json').read_bytes())
            layout=layouts.get(info.get('layout'))
            if info.get('map_type')=='MAP_TYPE_INDOOR' and layout:
                yield group,number,name,info,layout


def compose(definitions,sheets,palettes,blocks,w,h):
    meta=[];rgb=[]
    for y in range(h*16):
        for x in range(w*16):
            entries=definitions[blocks[y//16*w+x//16]&1023]['entries'];winner=None
            for layer in (0,4):
                e=entries[layer+y%16//8*2+x%16//8];tile=e&1023
                sheet=sheets[tile>=512];tile%=512;tx,ty=x%8,y%8
                if e&1024:tx=7-tx
                if e&2048:ty=7-ty
                sx,sy=tile%(sheet.width//8)*8+tx,tile//(sheet.width//8)*8+ty
                index=sheet.getpixel((sx,sy))&15 if sy<sheet.height else 0
                if index:winner=(e>>12,index)
            meta.append(winner)
            rgb.append(palettes[winner[0]>=6][winner[0]*16+winner[1]] if winner else (0,0,0))
    art=Image.new('RGB',(w*16,h*16));art.putdata(rgb)
    return meta,art


def load(root,record):
    group,number,name,info,layout=record;root=Path(root)
    w,h=layout['width'],layout['height']
    if not (1<=w<=128 and 1<=h<=128):raise ValueError('Room outside recipe bounds')
    definitions,sheets,palettes=tileset(str(root),layout['primary_tileset'],layout['secondary_tileset'])
    blocks=words(root/layout['blockdata_filepath'])
    if len(blocks)!=w*h:raise ValueError('Incomplete room map')
    meta,art=compose(definitions,sheets,palettes,blocks,w,h)
    return Room(name,group,number,w,h,blocks,definitions,meta,art,layout['secondary_tileset'],info.get('warp_events',[]),info['id'])
