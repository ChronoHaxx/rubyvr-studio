# SPDX-License-Identifier: GPL-3.0-or-later
"""Build reusable common-interior recipes from the user's local Ruby source.

Profiles describe furniture roles and physical heights in native pixels. Map
collision/priority bits never supply height. These are first-pass editable
rooms, not a declaration of finished art or native acceptance for every map.
"""
import argparse
import copy
import importlib.util
import json
from collections import Counter
from pathlib import Path

import interior_source as source

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('voxel_world',ROOT/'tools/build-voxel-world.py')
world=importlib.util.module_from_spec(spec)
spec.loader.exec_module(world)

# Explicit tileset profiles: role -> (height, metatiles). They are local art
# references, not copied mod implementation. Unclassified blocked cells remain
# visibly solid low fixtures, and are counted for subsequent art review.
PROFILES={
 'gTileset_GenericBuilding':dict(floor=0x223,wall=0x20d,roles={
    'Cabinet':(32,[0x279,0x27a,0x281,0x282,0x286,0x287,0x28e,0x28f]),
    'Plant':(24,[0x298,0x22a]),'Table':(16,[0x248,0x249,0x250,0x251]),
    'Wall':(40,[0x205,0x207,0x20d,0x20f])},
    remove=[0x289,0x28a,0x296,0x297,0x290,0x222,0x224],chairs=[0x2e2,0x2e3]),
 'gTileset_Shop':dict(floor=0x201,wall=0x22a,roles={
    'Shelf':(28,[0x228,0x229,0x22b,0x22c,0x233,0x234,0x23b,0x23c]),
    'Counter':(16,[0x232,0x23a,0x240,0x241,0x242]),'Plant':(24,[0x22d]),
    'Wall':(40,[0x212,0x213,0x21b,0x22a])},
    remove=[0x230,0x231,0x221,0x21d,0x225],chairs=[]),
 'gTileset_PokemonCenter':dict(floor=0x202,wall=0x249,roles={
    'Counter':(16,[0x258,0x221,0x205,0x259,0x25d]),
    'Healing machine':(24,[0x222,0x223,0x22a,0x22b,0x248,0x250,0x25b,0x251]),
    'Cabinet':(30,[0x269,0x26a,0x271,0x272]),'Table':(16,[0x23d,0x23e,0x245,0x246]),
    'Computer':(30,[0x004,0x005]),'Wall':(40,[0x249,0x252,0x253,0x215,0x216])},
    remove=[0x219,0x279,0x27a,0x224,0x21d,0x21e,0x260,0x26c,0x26d,0x26e,0x26f,0x206],
    chairs=[0x226,0x234]),
 'gTileset_Lab':dict(floor=0x202,wall=0x20a,roles={
    'Bookshelf':(30,[0x218,0x219,0x220,0x22c,0x22d,0x234,0x235]),
    'Desk':(18,[0x21a,0x21b,0x20b,0x20c,0x20d,0x20e]),
    'Crate':(16,[0x231,0x242,0x244]),'Machine':(24,[0x214,0x215,0x216,0x21c,0x21d,0x21e]),
    'Wall':(40,[0x208,0x209,0x20a])},
    remove=[0x221,0x222,0x223,0x233,0x23e,0x224,0x225,0x236,0x237,0x229,0x23a,0x247,0x23f],
    chairs=[0x241])}


def plan(room):
    profile=PROFILES[room.secondary]
    lookup={tile:(name,height) for name,(height,tiles) in profile['roles'].items() for tile in tiles}
    removed=set(profile['remove'])
    # Leave every native warp clear, including escalators within the room.
    warps={(int(w['x']),int(w['y'])) for w in room.warps}
    objects={};unknown=set()
    for y in range(room.height):
        for x in range(room.width):
            raw=room.blocks[y*room.width+x];tile=raw&1023
            role=lookup.get(tile)
            edge=x in (0,room.width-1) or y in (0,room.height-1)
            if (x,y) in warps:continue
            if y==0:objects[x,y]=('North wall',40,'wall');continue
            if role and (raw&0xc00):objects[x,y]=(role[0],role[1],'solid')
            elif tile in profile['chairs']:objects[x,y]=('Seat',9,'seat')
            elif raw&0xc00:
                # Escalator side cells are owned by Ruby's warp, not furniture.
                if any(abs(x-wx)+abs(y-wy)<=1 for wx,wy in warps):continue
                objects[x,y]=('Fixture',16,'solid');unknown.add(tile)
            if edge and not raw&0xc00:
                # Slim room shell on ordinary boundary cells. Door cells above
                # are explicit openings; no guessed teleport or collision edit.
                objects.setdefault((x,y),('Boundary wall',40,'edge'))
    return profile,objects,removed,unknown,warps


def fragment(room,profile,objects,removed,x0,y0,w,h):
    s=world.Source.__new__(world.Source);s.w=w*16;s.h=h*16
    cells=[(y*room.width+x) for y in range(y0,y0+h) for x in range(x0,x0+w)]
    ids=[room.blocks[i]&1023 for i in cells]
    key=f'indoor-{room.group}-{room.number}-{x0}-{y0}'
    s.p=dict(id=key,name=room.name+f' ({x0},{y0})',
       source=dict(room=room.map_id,x=x0+7,y=y0+7,coordinates='backup-map'),
       indoor=dict(group=room.group,number=room.number,width=room.width+15,height=room.height+14,wall_front=7),
       w=w,extent=h,ids=ids,mask=[1]*len(ids),tiles={str(i):room.definitions[i] for i in sorted(set(ids))},
       apply={'class':'prop'},model_seeded=True,parts=[])
    s.meta=[room.meta[y*room.width*16+x] for y in range(y0*16,(y0+h)*16) for x in range(x0*16,(x0+w)*16)]
    s.art=room.art.crop((x0*16,y0*16,(x0+w)*16,(y0+h)*16))
    # Boundary walls have no sprite to remove: keep their floor, reserving a
    # small owned patch for the ceiling/wall palette material instead.
    s.obj=[False]*(s.w*s.h);s.shadow=[False]*len(s.obj)
    def owned(x,y,m):
        cx,cy=x//16+x0,y//16+y0
        obj=objects.get((cx,cy));tile=room.blocks[cy*room.width+cx]&1023
        return bool(obj and obj[2]!='edge') or tile in removed
    s.mark(owned)
    # Every chunk carries ceiling geometry, even when its floor is empty.
    # Reserve one existing, opaque source texel for a neutral ceiling color.
    candidates=[i for i,m in enumerate(s.meta) if m]
    if not candidates:raise ValueError(f'{room.name}: empty source fragment')
    # Choose the brightest texel in this fragment: still the exact source
    # palette entry, with no baked lighting or imported texture.
    mi=max(candidates,key=lambda i:sum(s.art.getpixel((i%s.w,i//s.w))))
    s.obj[mi]=True;s.shadow[mi]=False
    neutral=[mi%s.w,mi//s.w,1,1]
    def box(label,x,z,width,depth,height,front,top=None,base=0):
        # Side faces use a single source color. A front image appears once at
        # native scale, rather than repeating a monitor/window up a tall box.
        pixels=[(xx,yy) for yy in range(front[1],front[1]+front[3])
                for xx in range(front[0],front[0]+front[2])]
        color=Counter(s.art.getpixel(pt) for pt in pixels).most_common(1)[0][0]
        px,py=next(pt for pt in pixels if s.art.getpixel(pt)==color)
        side=[px,py,1,1]
        if front[2]>1 or front[3]>1:
            ph=min(height,front[3]);panel=[front[0],front[1]+front[3]-ph,front[2],ph]
            s.solid(label+' front',[x,base+height-ph,z+depth-.5-(s.h-8)],
                    [width,ph,1],[panel,side,side,side,side,side])
        # The shared voxel compositor gives the first occupied part priority.
        # Put the inset front before its backing body so the art stays visible.
        s.solid(label,[x,base,z+depth/2-(s.h-8)],[width,height,depth],
                [side,side,side,side,top or side,side])
    for (cx,cy),(name,height,kind) in objects.items():
        if not(x0<=cx<x0+w and y0<=cy<y0+h):continue
        x,z=(cx-x0)*16,(cy-y0)*16
        if kind=='edge':
            if cx==0:box('West wall '+str(cy),x,z,2,16,40,neutral)
            if cx==room.width-1:box('East wall '+str(cy),x+14,z,2,16,40,neutral)
            if cy==room.height-1:box('South wall '+str(cx),x,z+14,16,2,40,neutral)
            continue
        face=s.material([x,z,16,16])
        label=name+f' {cx} {cy}'
        if kind=='wall':box(label,x,z,16,3,40,neutral)
        elif kind=='seat':
            box(label,x+4,z+4,8,8,height,face,face)
            box(label+' back',x+4,z+3,8,2,17,face)
        else:box(label,x,z,16,16,height,face,face)
    # Close the perimeter behind fixtures as well: a low cupboard must not
    # leave a hole between its top and the ceiling in first person.
    warps={(int(v['x']),int(v['y'])) for v in room.warps}
    for cy in range(y0,y0+h):
        for cx in range(x0,x0+w):
            if (cx,cy) in warps or objects.get((cx,cy),('',0,''))[2]=='edge':continue
            x,z=(cx-x0)*16,(cy-y0)*16
            if cx==0:box('Perimeter west '+str(cy),x,z,2,16,40,neutral)
            if cx==room.width-1:box('Perimeter east '+str(cy),x+14,z,2,16,40,neutral)
            if cy==room.height-1:box('Perimeter south '+str(cx),x,z+14,16,2,40,neutral)
    box('Ceiling',0,0,s.w,s.h,1,neutral,base=40)
    return s.finish(None)


def build(starter,decomp,output):
    result=copy.deepcopy(starter);result['version']=8
    records=[r for r in source.rooms(decomp) if r[4]['primary_tileset']=='gTileset_Building'
             and r[4]['secondary_tileset'] in PROFILES and 'TrickHouse' not in r[2]
             and not r[2].endswith('Rooftop')]
    identities={(r[0],r[1]) for r in records}
    result['patterns']=[p for p in result['patterns'] if
        (p.get('indoor',{}).get('group'),p.get('indoor',{}).get('number')) not in identities]
    terrain=result.setdefault('terrain',{'version':1}).setdefault('maps',[])
    terrain[:]=[m for m in terrain if (m['group'],m['number']) not in identities]
    report=[]
    for record in records:
        room=source.load(decomp,record);profile,objects,removed,unknown,warps=plan(room)
        pieces=[fragment(room,profile,objects,removed,x,y,min(4,room.width-x),min(4,room.height-y))
                for y in range(0,room.height,4) for x in range(0,room.width,4)]
        result['patterns'].extend(pieces)
        floor=profile['floor'];cells=[]
        for y in range(room.height):
            for x in range(room.width):
                raw=room.blocks[y*room.width+x];obj=objects.get((x,y))
                recover=bool(obj and obj[2]!='edge') or (raw&1023) in removed
                cells.append(dict(x=x+7,y=y+7,expected=raw,underlay=floor if recover else -1,
                    surfaces=[dict(layer=raw>>12,height=0,thickness=0,kind='ground',top=-1,side=floor)]))
        used={b&1023 for b in room.blocks}|{floor}
        terrain.append(dict(group=room.group,number=room.number,width=room.width+15,height=room.height+14,
            cells=cells,tiles=[dict(id=i,**room.definitions[i]) for i in sorted(used)]))
        report.append(dict(room=room.name,group=room.group,number=room.number,profile=room.secondary,
            fragments=len(pieces),parts=sum(len(p['parts']) for p in pieces),
            provisional_tiles=[f'0x{i:03x}' for i in sorted(unknown)],native_warps=len(warps),
            acceptance='Generated; individual room art and gameplay review pending'))
    if len(result['patterns'])>4096:raise ValueError('Pack exceeds supported pattern limit')
    return result,report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pack',type=Path,default=ROOT/'build/indoor-house/pack.json')
    p.add_argument('--decomp',type=Path,default=ROOT/'third_party/pokeruby')
    p.add_argument('--out',type=Path,default=ROOT/'build/interior-scenes/pack.json')
    args=p.parse_args()
    if args.pack.resolve()==args.out.resolve():p.error('Keep input pack separate')
    result,report=build(json.loads(args.pack.read_bytes()),args.decomp,args.out.parent)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,separators=(',',':'))+'\n',encoding='utf-8')
    args.out.with_name('report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'Generated {len(report)} common interiors; art coverage remains provisional. Pack: {args.out}')

if __name__=='__main__':main()
