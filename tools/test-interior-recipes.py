# SPDX-License-Identifier: GPL-3.0-or-later
"""Original synthetic room: no game source, ROM, graphics context or input."""
import copy
import importlib.util
from pathlib import Path
from unittest.mock import patch
from PIL import Image
from interior_source import Room

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('interiors',root/'tools/build-interior-scenes.py')
recipes=importlib.util.module_from_spec(spec);spec.loader.exec_module(recipes)
defs={i:dict(attr=0,entries=[0]*8) for i in (0x223,0x205,0x248)}
blocks=[0x3223]*36
for x in range(6):blocks[x]=0x3605
blocks[3*6+3]=0x3648
room=Room('SyntheticRoom',8,3,6,6,blocks,defs,[(1,1)]*(96*96),
          Image.new('RGB',(96,96),(192,184,168)),'gTileset_GenericBuilding',
          [dict(x=2,y=5)],'MAP_SYNTHETIC_ROOM')
profile,objects,removed,unknown,warps=recipes.plan(room)
assert (2,5) not in objects and warps=={(2,5)}
assert objects[3,3][1]==16
changed=copy.copy(room);changed.blocks=[(b&0xfff)|0xa000 for b in blocks]
assert recipes.plan(changed)[1]==objects, 'sprite priority cannot change furniture height'
pieces=[recipes.fragment(room,profile,objects,removed,x,y,min(4,6-x),min(4,6-y))
        for y in range(0,6,4) for x in range(0,6,4)]
for p in pieces:
    assert p['source']['room']=='MAP_SYNTHETIC_ROOM'
    assert len(p['parts'])<=64
    for part in p['parts']:
        # Every face samples existing owned source pixels, at native resolution.
        for face in part['surfaces']:
            x,y,w,h=face['region'];assert min(w,h)>0 and 0<=x<x+w<=p['w']*16 and 0<=y<y+h<=p['extent']*16
        if part['position'][1]>=1.25:continue
        left=p['source']['x']+part['position'][0];right=left+part['size'][0]
        back=p['source']['y']+p['extent']-.5+part['position'][2]-part['size'][2]/2
        front=back+part['size'][2]
        assert not (9.5+.22>left and 9.5-.22<right and 12.5+.22>back and 12.5-.22<front), 'warp must stay open'
record=(8,3,'SyntheticRoom',{},dict(primary_tileset='gTileset_Building',secondary_tileset=room.secondary))
starter={'version':7,'patterns':[{'id':'keep-my-work'}],'terrain':{'version':1,'maps':[]}}
with patch.object(recipes.source,'rooms',return_value=[record]),patch.object(recipes.source,'load',return_value=room):
    first,report=recipes.build(starter,Path('.'),Path('.'))
    second,_=recipes.build(first,Path('.'),Path('.'))
assert first==second and first['patterns'][0]==starter['patterns'][0]
assert starter['version']==7 and len(starter['patterns'])==1
assert report[0]['native_warps']==1
assert all(c['surfaces'][0]['height']==0 and c['surfaces'][0]['layer']==3 for c in first['terrain']['maps'][0]['cells'])
print('PASS: original synthetic interior recipe, warp clearance, source identity, layer independence and idempotent merge')
