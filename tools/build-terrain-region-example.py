"""Compile the reviewed local terrain recipe using pinned source guards.

Generated packs/source art stay local. This does not alter the default scenery
or complete Hoenn geography. See recipes/terrain-regions.json for assumptions.
"""
import argparse
import hashlib
import importlib.util
import json
import struct
from pathlib import Path

from terrain_regions import solve
from terrain_water import select_water

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'third_party/pokeruby'


def u16(path):
    data=path.read_bytes()
    return struct.unpack('<'+'H'*(len(data)//2),data)


def rectangle(rect,width,height):
    x,y,w,h,value=rect
    if not all(type(v) is int for v in rect) or not (0<=x<x+w<=width and 0<=y<y+h<=height and 0<=value<=256):
        raise ValueError(f'Invalid authored rectangle: {rect}')
    return [(cx,cy,value) for cy in range(y,y+h) for cx in range(x,x+w)]


def build(recipe,pack):
    if recipe['version']!=1: raise ValueError('Unsupported regional recipe version')
    if pack.get('terrain',{}).get('maps'):
        raise ValueError('Use a terrain-free input pack; this example does not merge existing authored terrain')
    layouts={l['id']:l for l in json.loads((SOURCE/'data/layouts/layouts.json').read_bytes())['layouts']}
    groups=json.loads((SOURCE/'data/maps/map_groups.json').read_bytes())
    identities={name:(g,i) for g,key in enumerate(groups['group_order']) for i,name in enumerate(groups[key])}
    definitions={}
    for offset,folder in ((0,'primary/general'),(512,'secondary/petalburg')):
        base=SOURCE/'data/tilesets'/folder
        entries,attrs=u16(base/'metatiles.bin'),u16(base/'metatile_attributes.bin')
        for i,attr in enumerate(attrs):
            definitions[offset+i]=dict(id=offset+i,attr=attr,entries=entries[i*8:i*8+8])
    raw={}
    for cfg in recipe['maps']:
        name=cfg['name']
        if name in raw or name not in identities: raise ValueError(f'Duplicate/unknown source map {name}')
        info=json.loads((SOURCE/'data/maps'/name/'map.json').read_bytes())
        layout=layouts[info['layout']]
        if [layout['width'],layout['height']]!=cfg['size']:
            raise ValueError(f'{name}: source dimensions changed')
        if (layout['primary_tileset'],layout['secondary_tileset'])!=('gTileset_General','gTileset_Petalburg'):
            raise ValueError(f'{name}: tileset pair changed; inspect its materials')
        w,h=cfg['size'];blocks=u16(SOURCE/layout['blockdata_filepath'])
        if len(blocks)!=w*h or (w+15)*(h+14)>10240: raise ValueError(f'{name}: invalid source size')
        raw[name]=dict(config=cfg,info=info,blocks=blocks,width=w,height=h)
    names={m['info']['id']:name for name,m in raw.items()}
    model={};frontiers=[]
    for name,m in raw.items():
        cfg=m['config'];w,h=m['width'],m['height'];clear=set()
        for guard in cfg.get('guards',[]):
            x,y=guard['start'];axis=guard['axis']
            if axis not in ('x','y'): raise ValueError('Invalid source guard direction')
            for i,expected in enumerate(guard['ids']):
                gx,gy=x+i*(axis=='x'),y+i*(axis=='y')
                if not (0<=gx<w and 0<=gy<h) or m['blocks'][gy*w+gx]&1023!=expected:
                    raise ValueError(f'{name}: source ledge changed at {gx},{gy}')
                clear.add((gx,gy))
        m['clear']=clear
        m['water'],water_pins,m['water_review']=select_water(w,h,m['blocks'],
            {i:t['attr'] for i,t in definitions.items()},cfg.get('water',[]))
        if clear & water_pins.keys():
            raise ValueError(f'{name}: water/shore selection overlaps an authored ledge; review its boundaries')
        seeds={(x,y):cfg.get('base',cfg.get('flat',0)) for y in range(h) for x in range(w)}
        for r in cfg.get('seed_regions',[]):
            for x,y,value in rectangle(r,w,h): seeds[x,y]=value
        fixed=[]
        if 'flat' in cfg:
            fixed=[(x,y,[cfg['flat']]*4) for y in range(h) for x in range(w)]
        for r in cfg.get('pins',[]):
            fixed.extend((x,y,[value]*4) for x,y,value in rectangle(r,w,h))
        if cfg.get('fixed_profile'):
            if cfg['fixed_profile']!='route101' or (w,h)!=(20,20): raise ValueError('Unsupported retained profile')
            spec=importlib.util.spec_from_file_location('legacy_terrain',ROOT/'tools/terrain-seam-fixture.py')
            legacy=importlib.util.module_from_spec(spec);spec.loader.exec_module(legacy)
            fixed.extend((x,y,v) for (x,y),v in legacy.route_corners().items())
        fixed.extend((x,y,[value]*4) for (x,y),value in water_pins.items())
        cliffs=[]
        for stroke in cfg.get('cliffs',[]):
            axis=stroke['axis'];x,y=stroke['start']
            if type(stroke['length']) is not int or not 1<=stroke['length']<=100:
                raise ValueError('Invalid cliff length')
            cliffs.extend((axis,x+i*(axis=='s'),y+i*(axis=='e'),stroke['before'],stroke['after'])
                          for i in range(stroke['length']))
        connections=[]
        for c in m['info'].get('connections',[]) or []:
            target=names.get(c['map'],c['map'])
            connections.append(dict(map=target,direction=c['direction'],offset=c['offset']))
            if target not in raw: frontiers.append(dict(map=name,direction=c['direction'],neighbour=c['map'],status='not authored by this example'))
        model[name]=dict(width=w,height=h,connections=connections,seed=lambda x,y,s=seeds:s[x,y],
                         fixed=fixed,cliffs=cliffs,pin_cliffs=not bool(cfg.get('fixed_profile')))
    corners,report=solve(model,recipe['anchor'])
    maps=[];summaries=[]
    for name,m in raw.items():
        w,h=m['width'],m['height'];cells=[];used={135};grades=0
        for (x,y),(nw,ne,sw,se) in corners[name].items():
            packed=m['blocks'][y*w+x];top=1 if (x,y) in m['clear'] else -1
            surface=dict(layer=packed>>12,height=nw,thickness=nw,kind='ground',top=top,side=135,side_offset=8)
            if (x,y) in m['water']:
                if any(v!=m['water'][x,y] for v in (nw,ne,sw,se)):
                    raise ValueError(f'{name}: water constraints lost their authored level')
                surface.update(kind='water',thickness=0,side=-1,side_offset=0)
            for key,value in dict(rise_x=ne-nw,rise_z=sw-nw,corner_delta=se-ne-sw+nw).items():
                if value: surface[key]=value
            grades+=len({nw,ne,sw,se})>1
            cells.append(dict(x=x+7,y=y+7,expected=packed,underlay=top,surfaces=[surface]))
            used.add(packed&1023)
            if top>=0: used.add(top)
        g,n=identities[name]
        maps.append(dict(group=g,number=n,width=w+15,height=h+14,cells=cells,tiles=[definitions[i] for i in sorted(used)]))
        summaries.append(dict(map=name,cells=len(cells),graded_cells=grades,source_ledge_cells=len(m['clear']),
            water_regions=m['water_review'],
            minimum_pixels=min(min(c) for c in corners[name].values()),maximum_pixels=max(max(c) for c in corners[name].values()),
            review_scope=m['config']['review']))
    result=dict(pack,version=7,terrain=dict(version=1,maps=maps))
    report.update(maps=summaries,frontiers=frontiers,assumptions=recipe['assumptions'],
                  source_guards='all declared ledges and water/shore selections matched',source_revision_status='use the pinned checkout from the build guide',
                  scope='local authored regions and connection context; no live/headset acceptance')
    return result,report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--recipes',type=Path,default=ROOT/'recipes/terrain-regions.json')
    p.add_argument('--pack',type=Path,default=ROOT/'mod-assets/voxel-world-v6.json')
    p.add_argument('--out',type=Path,default=ROOT/'build/terrain-regions/regions.json')
    p.add_argument('--report',type=Path,default=ROOT/'build/terrain-regions/region-report.json')
    a=p.parse_args()
    if len({a.out.resolve(),a.report.resolve(),a.pack.resolve(),a.recipes.resolve()})!=4:
        p.error('Input pack, recipes, output and report must be separate paths')
    result,report=build(json.loads(a.recipes.read_bytes()),json.loads(a.pack.read_bytes()))
    data=(json.dumps(result,separators=(',',':'))+'\n').encode()
    report['pack_sha256']=hashlib.sha256(data).hexdigest()
    for path in (a.out,a.report): path.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_bytes(data)
    a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f"PASS: {len(report['maps'])} maps, {report['shared_map_edges']} shared map edges, "
          f"{report['continuous_internal_edges']} continuous internal edges; {len(report['frontiers'])} unreviewed connections")


if __name__=='__main__': main()
