"""Audit compact geometry, real travel through six maps and optional PR #30 views.

Requires the built tools and the generated terrain-regions pack. --baseline
accepts an ignored directory containing PR #30's matching GUI probe captures
and studio-verification.json; no executable, source assets or pack is published.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
from PIL import Image,ImageChops

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/region-model-reuse'
spec=importlib.util.spec_from_file_location('connected',ROOT/'tools/test-connected-studio.py')
test=importlib.util.module_from_spec(spec);spec.loader.exec_module(test)


def eye(s):
    yaw,pitch,dist=s['camera'];x,y,z=s['camera_target']
    return [x+math.sin(yaw)*math.cos(pitch)*dist,y+math.sin(pitch)*dist,z+math.cos(yaw)*math.cos(pitch)*dist]


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--baseline',type=Path)
    args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
    with (OUT/'native.log').open('w') as log:
        subprocess.run([str(ROOT/'build/rubyvr_studio.exe'),'--test-connected'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=120)
    with (OUT/'source.log').open('w') as log:
        subprocess.run([str(ROOT/'build/rubyvr_studio.exe'),'--test-connected-source','third_party/pokeruby',str(test.PACK),
            str(OUT/'source-verification.json')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    test.main()
    common=json.loads((test.OUT/'studio-verification.json').read_bytes())
    source=json.loads((OUT/'source-verification.json').read_bytes())
    four,six,floors=source['scenes']
    assert four['maps']==4 and six['maps']==6 and floors['atlases']>=2
    assert six['geometry_hash']==common['layouts'][0]['region_hash']
    comparisons=[]
    if args.baseline:
        old=json.loads((args.baseline/'studio-verification.json').read_bytes())
        assert old['status']=='PASS' and old['input_sha256']==common['input_sha256']
        assert old['layouts'][0]['region_hash']==four['geometry_hash']
        assert four['gpu_bytes']<old['layouts'][0]['gpu_bytes']/2
        for layout in ('wide','small'):
            name='connected-'+layout
            before=json.loads((args.baseline/(name+'.json')).read_bytes())['checkpoints']
            after=json.loads((test.OUT/(name+'.json')).read_bytes())['checkpoints']
            assert len(before)==len(after)==10
            for a,b in zip(before,after):
                assert a['name']==b['name'] and a['view']==b['view']
                assert a['camera']==b['camera'] and a['camera_target']==b['camera_target']
                # The resident window now changes after crossing Route 103.
                # Historical fixed-window pictures only apply before travel
                # and after the exact editor/initial region is restored.
                if a['name'] in ('crossed','stopped','idle'):continue
                x,y,w,h=map(int,a['view']);box=(x,y,x+w,y+h);suffix='.png.'+a['name']+'.png'
                images=[Image.open(p/(name+suffix)).convert('RGB').crop(box) for p in (args.baseline,test.OUT)]
                assert ImageChops.difference(*images).getbbox() is None,(layout,a['name'])
                comparisons.append(dict(layout=layout,checkpoint=a['name'],pixels=w*h))
    # Real Shift-W flight: Littleroot -> Route 101 -> Oldale -> Route 103.
    test.OUT=OUT
    events=[];test.ui.click(events,22,470,501);test.ui.click(events,29,460,564)
    events.append(dict(frame=40,type='wait-stream'))
    events += [dict(frame=55,type='motion',x=800,y=800),dict(frame=56,type='down',x=800,y=800,button=3),
        dict(frame=60,type='key-down',scan=225,mod=1),dict(frame=64,type='key-down',scan=26,mod=1),
        dict(frame=134,type='key-up',scan=26,mod=1),dict(frame=136,type='key-up',scan=225),
        dict(frame=138,type='up',x=800,y=800,button=3),dict(frame=180,type='wait-stream')]
    test.ui.click(events,205,1470,224)
    for f in (214,219,224):test.ui.click(events,f,997,501)
    events.append(dict(frame=245,type='quit'))
    points=[(52,'littleroot')]+[(f,'travel-'+str(f)) for f in range(70,134,3)]+[(190,'route103'),(237,'overview')]
    states=test.run('north-flight',events,points,frames=255,connected=True,
        camera=dict(yaw=0,pitch=.12,dist=8,tx=17,ty=12,tz=52))
    start=states['littleroot'];end=states['route103']
    assert 47<eye(start)[2]<67 and -13<eye(end)[2]<7,(eye(start),eye(end))
    assert any(27<eye(s)[2]<47 for n,s in states.items() if n.startswith('travel-'))
    assert any(7<eye(s)[2]<27 for n,s in states.items() if n.startswith('travel-'))
    assert start['lighting']=='Noon' and end['camera_target']!=states['overview']['camera_target']
    assert start['stream_anchor']=='MAP_LITTLEROOT_TOWN' and start['region_maps']==3
    assert end['stream_anchor']=='MAP_ROUTE103' and end['region_maps']==4
    initial=(OUT/'north-flight.png.littleroot.working.json').read_bytes()
    origins={}
    for n,s in states.items():
        assert s['exploring'] and 0<s['region_maps']<=6 and not s['stream_errors']
        for m in s['stream_maps']:
            position=(m['x'],m['z'])
            if m['id'] in origins:assert origins[m['id']]==position
            origins[m['id']]=position
        assert 0<s['region_draw_calls']<=s['region_batches']
        assert not s['unsaved'] and not s['draft_dirty']
        assert (OUT/f'north-flight.png.{n}.working.json').read_bytes()==initial
    # Fly west across Route 102's offset connection into Petalburg.
    events=[];test.ui.click(events,22,470,501);test.ui.click(events,29,460,564)
    events += [dict(frame=55,type='motion',x=800,y=800),dict(frame=56,type='down',x=800,y=800,button=3),
        dict(frame=60,type='key-down',scan=26),dict(frame=150,type='key-up',scan=26),
        dict(frame=152,type='up',x=800,y=800,button=3),dict(frame=155,type='wait-stream'),dict(frame=175,type='quit')]
    west=test.run('west-flight',events,[(50,'route102'),(135,'crossed'),(165,'petalburg')],frames=180,connected=True,
        camera=dict(yaw=math.pi/2,pitch=.12,dist=8,tx=-43,ty=9,tz=9))
    assert eye(west['route102'])[0]>-43 and eye(west['petalburg'])[0]<-43
    for s in west.values():
        assert s['exploring'] and 0<s['region_maps']<=6 and not s['stream_errors']
        assert all(origins[m['id']]==(m['x'],m['z']) for m in s['stream_maps'])
    assert west['petalburg']['stream_anchor']=='MAP_PETALBURG_CITY' and west['petalburg']['region_maps']==3
    report=dict(status='PASS',gui_sha256=common['gui_sha256'],batch_sha256=hashlib.sha256((ROOT/'build/rubyvr_studio.exe').read_bytes()).hexdigest(),
        input_sha256=common['input_sha256'],source=source['scenes'],connected_checks=common,
        baseline_views=comparisons,baseline_pixels=sum(v['pixels'] for v in comparisons),
        north_flight=dict(start=eye(start),end=eye(end),checkpoints=len(states)),
        west_flight=dict(start=eye(west['route102']),end=eye(west['petalburg']),checkpoints=len(west)),
        scope='Desktop GL and native equivalence; model reuse with camera-driven residency; no headset acceptance')
    (OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: model reuse and six-map flight; optional baseline pixels:',report['baseline_pixels'])


if __name__=='__main__':main()
