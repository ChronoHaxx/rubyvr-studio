"""Actual SDL connected-area entry, flight, input ownership and atlas checks."""
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import subprocess
from PIL import Image,ImageChops

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/connected-scene'
PACK=ROOT/'build/terrain-regions/regions.json'
spec=importlib.util.spec_from_file_location('terrain_ui',ROOT/'tools/test-studio-terrain.py')
ui=importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)


def run(name,events,points,*,width=1600,height=1100,frames=325,camera=None,map_id='MAP_OLDALE_TOWN',source=PACK,connected=False):
    OUT.mkdir(parents=True,exist_ok=True)
    scenario=json.loads((ROOT/'tools/gui-probe-scene.json').read_bytes())
    scenario['viewport']=dict(w=width,h=height)
    scenario['camera'].update(camera or dict(yaw=0,pitch=0,dist=8,tx=17,ty=5,tz=8))
    prefix=OUT/name
    Path(str(prefix)+'-scenario.json').write_text(json.dumps(scenario))
    script=dict(frames=frames,events=events,record=0,measure_environment=1,chapters=[],checkpoints=[dict(frame=f,name=n) for f,n in points])
    Path(str(prefix)+'-events.json').write_text(json.dumps(script))
    env=os.environ.copy();env['PATH']=env.get('RUBYVR_MINGW_BIN',r'C:\msys64\mingw64\bin')+os.pathsep+env.get('PATH','')
    with Path(str(prefix)+'.log').open('w') as log:
        subprocess.run([str(ROOT/'build/rubyvr_gui.exe')]+(['--connected'] if connected else [])+['--map',map_id,'--mode','diorama','--overrides',str(source),
            '--out',str(prefix)+'-saved.json','--probe',str(prefix)+'-scenario.json','--showcase',str(prefix)+'-events.json',
            '--probe-out',str(prefix)],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240,
            creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    result=json.loads(Path(str(prefix)+'.json').read_bytes());assert result['ok'] and result['closed']
    return {p['name']:p for p in result['checkpoints']}


def main():
    original=PACK.read_bytes();reports=[]
    for name,width,height in [('wide',1600,1100),('small',1280,720)]:
        name='connected-'+name
        events=[];ui.click(events,22,1082,17)
        ui.key(events,50,29,64);ui.click(events,58,80,254);ui.click(events,66,480,210)
        vx,vy=800,int(height*.78)
        events += [dict(frame=80,type='motion',x=vx,y=vy),dict(frame=81,type='down',x=vx,y=vy,button=3),
                   dict(frame=83,type='key-down',scan=26),dict(frame=183,type='key-up',scan=26),
                   dict(frame=185,type='up',x=vx,y=vy,button=3)]
        ui.click(events,225,180,17);ui.click(events,250,1082,17);ui.click(events,275,180,17)
        ui.key(events,297,22,64);events.append(dict(frame=320,type='quit'))
        points=[(18,'before'),(43,'loaded'),(75,'guarded'),(150,'crossed'),(196,'stopped'),(220,'idle'),
                (241,'returned'),(268,'again'),(290,'returned-again'),(313,'saved')]
        states=run(name,events,points,width=width,height=height)
        before,loaded=states['before'],states['loaded']
        assert not before['exploring'] and loaded['exploring'] and loaded['region_maps']==6
        assert loaded['mesh_uploads']==before['mesh_uploads']+6
        assert 0<loaded['region_models']<loaded['region_model_instances']
        assert loaded['region_stored_vertices']<loaded['region_vertices']/2
        assert loaded['region_rejected']==loaded['region_unresolved']==0
        baseline=Path(str(OUT/name)+'.png.before.working.json').read_bytes()
        for n in states:
            s=states[n]
            assert s['room_hash']==before['room_hash'] and s['undo']==before['undo'] and s['map_id']==before['map_id']
            assert not s['draft_dirty'] and not s['unsaved']
            assert Path(str(OUT/name)+f'.png.{n}.working.json').read_bytes()==baseline
        for n in ('guarded','crossed','stopped','idle'):
            s=states[n];assert s['region_hash']==loaded['region_hash'] and s['mesh_uploads']==loaded['mesh_uploads']
            assert 0<s['region_draw_calls']<=s['region_batches']
        crossed=states['crossed'];yaw,pitch,dist=crossed['camera']
        eye_z=crossed['camera_target'][2]+math.cos(yaw)*math.cos(pitch)*dist
        assert -13<eye_z<7,eye_z # Camera has crossed Oldale's north boundary into Route 103.
        assert states['stopped']['camera_target']==states['idle']['camera_target']
        for n in ('returned','returned-again'):
            s=states[n];assert not s['exploring'] and s['region_maps']==s['region_bytes']==0
            assert s['camera']==before['camera'] and s['camera_target']==before['camera_target']
        assert states['again']['region_hash']==loaded['region_hash']
        assert states['again']['mesh_uploads']==loaded['mesh_uploads']+6
        assert json.loads(Path(str(OUT/name)+'-saved.json').read_bytes())==json.loads(baseline)
        reports.append(dict(viewport=[width,height],checkpoints=len(states),vertices=loaded['region_vertices'],
            stored_vertices=loaded['region_stored_vertices'],models=loaded['region_models'],
            model_instances=loaded['region_model_instances'],deformed_instances=loaded['region_deformed_instances'],
            batches=loaded['region_batches'],
            gpu_bytes=loaded['region_bytes'],region_hash=loaded['region_hash'],crossed_eye_z=eye_z,
            loaded_draw_ms=loaded['preview_draw_ms'],crossed_draw_ms=crossed['preview_draw_ms'],
            crossed_draw_calls=crossed['region_draw_calls']))
    # Compare another atlas against its own single-map render at the same
    # world-space camera. Empty patterns isolate material binding from scenery.
    floors=OUT/'source-floors.json';floors.write_text('{"version":6,"patterns":[]}\n')
    camera=dict(yaw=0,pitch=1.5,dist=8,tx=27,ty=1,tz=52)
    normal=run('atlas-own',[dict(frame=25,type='quit')],[(20,'view')],frames=30,map_id='MAP_ROUTE104',source=floors,camera=camera)['view']
    events=[];ui.click(events,22,1082,17);events.append(dict(frame=55,type='quit'))
    camera.update(tx=-13,tz=2)
    region=run('atlas-connected',events,[(45,'view')],frames=60,map_id='MAP_PETALBURG_CITY',source=floors,camera=camera)['view']
    assert region['region_maps']==3 and region['region_draw_calls']<region['region_maps']
    x,y,w,h=map(int,normal['view']);crop=(x+w//2-100,y+h//2-80,x+w//2+100,y+h//2+80)
    images=[Image.open(OUT/(n+'.png.view.png')).convert('RGB').crop(crop) for n in ('atlas-own','atlas-connected')]
    assert ImageChops.difference(*images).getbbox() is None,'Neighbour atlas differs from its own actual render'
    assert PACK.read_bytes()==original
    result=dict(status='PASS',scope='Six-map desktop preview; no live traversal/headset claim',
        gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(),
        input_sha256=hashlib.sha256(original).hexdigest(),layouts=reports,atlas_pixels_identical=32000,
        source_unchanged=True,document_unchanged=True,zero_camera_mesh_uploads=True,
        render_measurement='Synchronized single-eye GL draw, including sky; excludes UI, CPU builds and driver overhead; not headset frame time')
    (OUT/'studio-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
