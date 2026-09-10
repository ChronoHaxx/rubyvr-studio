"""Actual SDL foundation authoring on a labelled, controlled Oldale slope.

This tests an editor action, not a proposed change to Oldale geography.
Generated terrain packs and source art remain local.
"""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build/terrain-foundations'


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m


def fixture():
    compiler=module('regions','tools/build-terrain-region-example.py')
    starter=json.loads((ROOT/'mod-assets/voxel-world-v6.json').read_bytes())
    pack,_=compiler.build(json.loads((ROOT/'recipes/terrain-regions.json').read_bytes()),starter)
    oldale=pack['terrain']['maps'][0]
    assert (oldale['group'],oldale['number'])==(0,10)
    pack['terrain']['maps']=[oldale]
    for c in oldale['cells']:
        p=c['surfaces'][0]
        p['height']=p['thickness']=max(0,min(32,(17-c['y'])*4))
        p['rise_z']=-4 if 9<=c['y']<17 else 0
    BUILD.mkdir(parents=True,exist_ok=True)
    path=BUILD/'slope.json'
    path.write_text(json.dumps(pack,separators=(',',':'))+'\n',encoding='utf-8')
    return path


def main():
    source=fixture()
    ui=module('terrain_ui','tools/test-studio-terrain.py');ui.BUILD=BUILD
    original=source.read_bytes();reports=[]
    camera=dict(yaw=.7,pitch=.28,dist=10,tx=13,ty=2,tz=13)
    for name,width,height in [('wide',1600,1100),('small',1280,720)]:
        events=[];ui.click(events,22,80,252)
        events.append(dict(frame=28,type='motion',x=800,y=600))
        ui.click(events,45,width-130,480)
        ui.key(events,65,29,64);ui.key(events,85,28,64)
        ui.click(events,105,width-130,480);ui.key(events,125,22,64)
        events.append(dict(frame=145,type='quit'))
        result=ui.run(name,events,[(34,'before'),(56,'levelled'),(76,'undo'),(96,'redo'),
                      (116,'repeat'),(138,'saved')],source=source,width=width,height=height,frames=149,camera=camera)
        states={s['name']:s for s in result['checkpoints']}
        base=states['before'];done=states['levelled']
        assert done['undo']==base['undo']+1 and done['room_hash']!=base['room_hash']
        assert states['undo']['room_hash']==base['room_hash']
        assert states['redo']['room_hash']==done['room_hash']
        assert states['repeat']['undo']==done['undo'] and states['repeat']['room_hash']==done['room_hash']
        assert not states['saved']['unsaved']
        # Compare native serialization on both sides (the source generator's
        # decimal formatting differs from the existing C++ pattern writer).
        baseline=json.loads((BUILD/(name+'.png.before.working.json')).read_bytes())
        for state in states.values():
            assert state['terrain_rejected']==0 and state['unresolved_placements']==0
            assert state['accepted_placements']==base['accepted_placements'] and state['raised']==base['raised']
            current=json.loads((BUILD/(name+'.png.'+state['name']+'.working.json')).read_bytes())
            assert current['patterns']==baseline['patterns']
        saved=json.loads((BUILD/(name+'-saved.json')).read_bytes())
        assert saved['patterns']==baseline['patterns']
        changed=0
        for before,after in zip(baseline['terrain']['maps'][0]['cells'],saved['terrain']['maps'][0]['cells']):
            if 11<=before['x']<=14 and 11<=before['y']<=14:
                expected=json.loads(json.dumps(before));p=expected['surfaces'][0]
                p['height']=p['thickness']=24;p.pop('rise_z',None)
                assert after==expected
                changed+=before!=after
            else:
                # The writer omits optional zero fields.
                expected=json.loads(json.dumps(before))
                if not expected['surfaces'][0].get('rise_z'):expected['surfaces'][0].pop('rise_z',None)
                assert after==expected
        assert changed==16
        reopened=ui.run(name+'-reopened',[dict(frame=24,type='quit')],[(18,'reopened')],
                        source=BUILD/(name+'-saved.json'),width=width,height=height,frames=28,camera=camera)
        assert reopened['checkpoints'][0]['room_hash']==done['room_hash']
        reports.append(dict(viewport=[width,height],checkpoints=7,changed_cells=changed,
                            before_hash=base['room_hash'],after_hash=done['room_hash']))
    assert source.read_bytes()==original
    record=dict(status='PASS',scope='Controlled Oldale slope; editor foundation action, not proposed geography',
                gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(),
                input_sha256=hashlib.sha256(original).hexdigest(),layouts=reports,source_unchanged=True,models_unchanged=True)
    (BUILD/'verification.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(record,indent=2))


if __name__=='__main__':main()
