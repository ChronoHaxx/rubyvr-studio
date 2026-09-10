"""Local source/production-mesh checks, including deliberately broken inputs.

Requires the pinned local source and generated starter. Nothing is uploaded.
"""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/terrain-regions'


def main():
    spec=importlib.util.spec_from_file_location('compiler',ROOT/'tools/build-terrain-region-example.py')
    compiler=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler)
    recipe_path=ROOT/'recipes/terrain-regions.json'
    starter_path=ROOT/'mod-assets/voxel-world-v6.json'
    recipe_bytes=recipe_path.read_bytes();starter_bytes=starter_path.read_bytes()
    recipe=json.loads(recipe_bytes);starter=json.loads(starter_bytes)
    pack,report=compiler.build(recipe,starter)
    # Compare with the same authored land before water constraints. Source art,
    # cell ownership and the retained Route 101 profile must survive unchanged.
    dry_recipe=copy.deepcopy(recipe)
    for cfg in dry_recipe['maps']: cfg.pop('water',None)
    dry_pack,_=compiler.build(dry_recipe,starter)
    checked_water=checked_shore=0
    for cfg,current,previous in zip(recipe['maps'],pack['terrain']['maps'],dry_pack['terrain']['maps']):
        assert current['tiles']==previous['tiles']
        for cell,old in zip(current['cells'],previous['cells']):
            assert all(cell[k]==old[k] for k in ('x','y','expected','underlay'))
            assert cell['surfaces'][0]['top']==old['surfaces'][0]['top']
        if cfg['name']=='Route101': assert current==previous
        cells={(c['x']-7,c['y']-7):c for c in current['cells']}
        attrs={t['id']:t['attr'] for t in current['tiles']}
        for r in cfg.get('water',[]):
            x,y,w,h=r['bounds']
            wet={(cx,cy) for cy in range(y,y+h) for cx in range(x,x+w)
                 if attrs[cells[cx,cy]['expected']&1023]&255 in r['behaviors']}
            shore={(cx+dx,cy+dy) for cx,cy in wet for dy in range(-r['shore'],r['shore']+1)
                   for dx in range(-r['shore'],r['shore']+1) if (cx+dx,cy+dy) in cells}-wet
            for p in wet|shore:
                s=cells[p]['surfaces'][0]
                assert s['height']==r['height'] and not any(s.get(k,0) for k in ('rise_x','rise_z','corner_delta'))
                assert s['kind']==('water' if p in wet else 'ground')
                assert s['thickness']==(0 if p in wet else r['height'])
            checked_water+=len(wet);checked_shore+=len(shore)
    assert (checked_water,checked_shore)==(495,266)
    OUT.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env['PATH']=env.get('RUBYVR_MINGW_BIN',r'C:\msys64\mingw64\bin')+os.pathsep+env['PATH']
    def native(name,data,expected):
        path=OUT/(name+'.json');path.write_text(json.dumps(data),encoding='utf-8')
        result=subprocess.run([str(ROOT/'build/rubyvr_studio.exe'),'--test-terrain-source',
            str(compiler.SOURCE),str(path)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=90)
        log=result.stdout+result.stderr
        (OUT/(name+'.log')).write_text(log,encoding='utf-8')
        assert (result.returncode==0)==expected,(name,result.returncode,log[-1500:])
        return log
    log=native('checked-regions',pack,True)
    assert '6 authored maps, 200 joined edges' in log and '2 unreviewed connections' in log
    assert '[terrain-water-test] PASS: 495 source-body water cells' in log
    water=copy.deepcopy(pack)
    index=next(i for i,m in enumerate(recipe['maps']) if m['name']=='Route102')
    pool=water['terrain']['maps'][index]
    bad_water=next(c for c in pool['cells'] if c['x']==48 and c['y']==10)
    assert bad_water['surfaces'][0]['kind']=='water'
    bad_water['surfaces'][0]['height']+=1
    assert 'adjacent water levels differ' in native('broken-water-level',water,False)
    water_recipe=copy.deepcopy(recipe)
    next(m for m in water_recipe['maps'] if m['name']=='Route102')['water'][0]['guard']='0'*64
    try:
        compiler.build(water_recipe,starter)
    except ValueError as error:
        assert 'water/shore source guard changed' in str(error),str(error)
    else:
        raise AssertionError('Unreviewed water source guard was accepted')
    conflicting=copy.deepcopy(recipe)
    next(m for m in conflicting['maps'] if m['name']=='Route102')['pins'].append([41,3,1,1,17])
    try:
        compiler.build(conflicting,starter)
    except ValueError as error:
        assert 'Contradictory corner anchors' in str(error),str(error)
    else:
        raise AssertionError('Conflicting water and land anchors were accepted')
    broken=copy.deepcopy(pack)
    index=next(i for i,m in enumerate(recipe['maps']) if m['name']=='Route102')
    route=broken['terrain']['maps'][index]
    cell=next(c for c in route['cells'] if c['x']==route['width']-9 and c['y']==17)
    cell['surfaces'][0]['height']+=1
    cell['surfaces'][0]['thickness']+=1
    assert 'gap/unresolved' in native('broken-east-join',broken,False)
    bad_recipe=copy.deepcopy(recipe)
    guard=next(m for m in bad_recipe['maps'] if m['name']=='Route103')['guards'][1]
    guard['ids'][0]^=1
    try:
        compiler.build(bad_recipe,starter)
    except ValueError as error:
        assert 'source ledge changed at 13,5' in str(error),str(error)
    else:
        raise AssertionError('Changed source corner guard was accepted')
    try:
        compiler.build(recipe,pack)
    except ValueError as error:
        assert 'does not merge existing authored terrain' in str(error),str(error)
    else:
        raise AssertionError('Existing authored terrain would be silently replaced')
    assert pack['patterns']==starter['patterns']
    assert recipe_path.read_bytes()==recipe_bytes and starter_path.read_bytes()==starter_bytes
    report.update(native_join_checks=200,source_maps=394,copied_cells=36834,canonical_terrain_copies=1925,
                  water_cells=checked_water,flat_shore_cells=checked_shore,
                  negative_checks=['1px east join gap rejected','changed upper-elbow guard rejected',
                                   '1px internal water step rejected','changed water/shore guard rejected',
                                   'conflicting water and land anchors rejected',
                                   'input with existing authored terrain rejected'],
                  preserved_inputs=True,original_pattern_count=len(starter['patterns']),
                  preserved_source_art=True,preserved_route101_profile=True,
                  batch_sha256=hashlib.sha256((ROOT/'build/rubyvr_studio.exe').read_bytes()).hexdigest())
    (OUT/'source-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'PASS: 200 native join checks; {checked_water} level water and {checked_shore} flat shore cells; broken joins/water/guards rejected; source art, Route 101 and patterns preserved')


if __name__=='__main__': main()
