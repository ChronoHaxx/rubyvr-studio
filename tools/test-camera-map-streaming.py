"""Real SDL travel, resident-window churn and exact editing-state preservation."""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/camera-map-streaming'
spec=importlib.util.spec_from_file_location('connected_test',ROOT/'tools/test-connected-studio.py')
test=importlib.util.module_from_spec(spec);spec.loader.exec_module(test)


def flight(events,start,end,scan):
    events.extend([dict(frame=start-5,type='motion',x=800,y=800),
        dict(frame=start-4,type='down',x=800,y=800,button=3),
        dict(frame=start-3,type='key-down',scan=225,mod=1),
        dict(frame=start,type='key-down',scan=scan,mod=1),
        dict(frame=end,type='key-up',scan=scan,mod=1),
        dict(frame=end+2,type='key-up',scan=225),
        dict(frame=end+4,type='up',x=800,y=800,button=3),
        dict(frame=end+7,type='wait-stream')])


def positions(state):
    return {m['id']:(m['x'],m['z']) for m in state['stream_maps']}


def cancel_pending():
    events=[];flight(events,35,40,22)
    test.ui.key(events,46,41)
    test.ui.click(events,110,1082,17)
    test.ui.key(events,140,41)
    events.append(dict(frame=160,type='quit'))
    states=test.run('cancel',events,[(18,'before'),(45,'pending'),(100,'drained'),
        (130,'again'),(150,'editing')],frames=170,connected=True,
        camera=dict(yaw=0,pitch=0,dist=8,tx=17,ty=8,tz=18))
    before=states['before'];assert states['pending']['stream_pending'],'Cancellation did not overlap a worker'
    baseline=(OUT/'cancel.png.before.working.json').read_bytes()
    for name,s in states.items():
        assert (OUT/f'cancel.png.{name}.working.json').read_bytes()==baseline
        assert not s['unsaved'] and not s['draft_dirty'] and not s['stream_errors']
    for name in ['drained','editing']:
        s=states[name]
        assert not s['exploring'] and not s['stream_pending'] and s['region_maps']==s['region_bytes']==0
        assert s['camera']==before['camera'] and s['camera_target']==before['camera_target']
    assert states['again']['region_hash']==before['region_hash']
    assert not states['again']['stream_updates'],'Cancelled result replaced the reentered scene'
    return dict(status='PASS',checkpoints=len(states),cancelled_while_pending=True,
        late_publication=False,document_unchanged=True,camera_restored=True)


def beyond_initial():
    # No authored heights: use source floors to verify topology independently
    # of the six-map terrain pack's deliberately unreviewed frontiers.
    floors=OUT/'source-floors.json';floors.write_text('{"version":6,"patterns":[]}\n')
    events=[];flight(events,35,73,4);flight(events,120,158,7)
    events.append(dict(frame=210,type='quit'))
    states=test.run('beyond',events,[(18,'petalburg'),(100,'route104'),(190,'returned')],
        frames=220,connected=True,map_id='MAP_PETALBURG_CITY',source=floors,
        camera=dict(yaw=0,pitch=.4,dist=8,tx=17,ty=10,tz=8))
    initial,west,back=[states[n] for n in ['petalburg','route104','returned']]
    assert initial['stream_anchor']=='MAP_PETALBURG_CITY' and initial['region_maps']==3
    assert west['stream_anchor']=='MAP_ROUTE104' and west['region_maps']==4
    new=set(positions(west))-set(positions(initial))
    assert new=={'MAP_RUSTBORO_CITY','MAP_ROUTE105'},new
    assert 'MAP_ROUTE102' not in positions(west)
    assert back['stream_anchor']=='MAP_PETALBURG_CITY' and back['region_hash']==initial['region_hash']
    known={}
    for name,s in states.items():
        assert not s['stream_pending'] and not s['stream_errors']
        for key,value in positions(s).items():
            if key in known:assert known[key]==value,(key,value,known[key])
            known[key]=value
        assert (OUT/f'beyond.png.{name}.working.json').read_bytes()==(OUT/'beyond.png.petalburg.working.json').read_bytes()
    assert positions(back)==positions(initial)
    return dict(status='PASS',fixture='Source floors without authored terrain; not new terrain acceptance',
        new_maps=sorted(new),checkpoints=len(states),fixed_origins=known,returned_hash=back['region_hash'])


def main():
    OUT.mkdir(parents=True,exist_ok=True);test.OUT=OUT
    original=test.PACK.read_bytes()
    events=[];test.ui.click(events,6,470,501);test.ui.click(events,12,460,564)
    for start,end,scan in [(35,85,22),(120,170,26),(200,285,4),(315,400,7)]:
        flight(events,start,end,scan)
    test.ui.click(events,430,180,17)
    test.ui.click(events,455,1082,17)
    test.ui.key(events,480,41)
    events.append(dict(frame=510,type='quit'))
    points=[(18,'oldale'),(51,'south-start'),(70,'route101'),(83,'south-end'),(100,'littleroot'),
            (143,'north-travel'),(185,'oldale-return'),(222,'west-start'),(254,'route102'),
            (283,'west-end'),(300,'petalburg'),(351,'east-travel'),(415,'oldale-again'),
            (440,'editing'),(470,'reentered'),(495,'escape')]
    states=test.run('travel',events,points,frames=520,connected=True,
        camera=dict(yaw=0,pitch=0,dist=8,tx=17,ty=8,tz=8))
    start=states['oldale'];known=positions(start)
    assert start['region_maps']==6 and start['stream_anchor']=='MAP_OLDALE_TOWN'
    expected={'littleroot':('MAP_LITTLEROOT_TOWN',3),'oldale-return':('MAP_OLDALE_TOWN',6),
              'petalburg':('MAP_PETALBURG_CITY',3),'oldale-again':('MAP_OLDALE_TOWN',6)}
    for name,(anchor,count) in expected.items():
        s=states[name]
        assert not s['stream_pending'] and not s['stream_errors'],(name,s)
        assert s['stream_anchor']==anchor and s['region_maps']==count,(name,s['stream_anchor'],s['region_maps'])
    baseline=(OUT/'travel.png.oldale.working.json').read_bytes()
    for name,s in states.items():
        assert s['map_id']==start['map_id'] and s['room_hash']==start['room_hash']
        assert s['undo']==start['undo'] and not s['unsaved'] and not s['draft_dirty']
        assert (OUT/f'travel.png.{name}.working.json').read_bytes()==baseline
        assert all(known[key]==value for key,value in positions(s).items()),(name,positions(s))
        assert s['region_maps']<=9 and s['region_stored_vertices']<=8000000
        assert not s['region_rejected'] and not s['region_unresolved']
    end=states['oldale-again']
    assert end['stream_unloaded']>=6 and end['stream_loaded']>=12,end
    assert end['stream_reused']>0 and end['stream_pending_frames']>0
    assert end['region_hash']==start['region_hash'],'Returned region changed its ordered geometry'
    for name in ['littleroot','petalburg']:
        assert states[name]['region_bytes']<start['region_bytes']
    for name in ['editing','escape']:
        s=states[name]
        assert not s['exploring'] and s['region_maps']==s['region_bytes']==0
        assert s['camera']==start['camera'] and s['camera_target']==start['camera_target']
    assert states['reentered']['region_hash']==start['region_hash']
    assert test.PACK.read_bytes()==original
    cancellation=cancel_pending();beyond=beyond_initial()
    report=dict(status='PASS',scope='Camera-driven residency in the six authored maps; desktop source/GL only',
        gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(),
        input_sha256=hashlib.sha256(original).hexdigest(),checkpoints=len(states),fixed_origins=known,
        settled={name:{key:states[name][key] for key in ['stream_anchor','region_maps','region_bytes','region_hash',
            'stream_updates','stream_loaded','stream_unloaded','stream_reused','stream_build_ms','stream_main_ms','stream_pending_frames']}
            for name in ['oldale',*expected]},document_unchanged=True,source_unchanged=True,
        cancellation=cancellation,beyond_initial=beyond,
        measurements='Resident GPU mesh/material bytes; main-thread streaming-update maximum excludes rendering/UI and is not headset FPS.')
    (OUT/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
