"""Replay terrain edits through actual SDL input; inspect production renders.

Uses local source assets and a separate output pack. No live/headset claim.
"""
import json
import os
from pathlib import Path
from studio_paths import executable as native_executable, native_environment
import subprocess
import hashlib
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/terrain-review'


def run(name, events, checkpoints, *, source=None, width=1600, height=950, frames=220,
        map_id='MAP_OLDALE_TOWN', camera=None):
    BUILD.mkdir(parents=True, exist_ok=True)
    source = source or ROOT / 'mod-assets/voxel-world-v6.json'
    scenario = json.loads((ROOT / 'tools/gui-probe-scene.json').read_text())
    scenario['viewport'] = dict(w=width, h=height)
    scenario['camera'].update(yaw=.65, pitch=.6, dist=30, tx=15, ty=1, tz=16)
    if camera:
        scenario['camera'].update(camera)
    scenario_path = BUILD / f'{name}-scenario.json'
    scenario_path.write_text(json.dumps(scenario), encoding='utf-8')
    script_path = BUILD / f'{name}-events.json'
    script_path.write_text(json.dumps(dict(frames=frames, record=0, chapters=[], events=events,
        checkpoints=[dict(frame=f, name=n) for f, n in checkpoints])), encoding='utf-8')
    prefix = BUILD / name
    env = native_environment()
    with (BUILD / f'{name}.log').open('w', encoding='utf-8') as log:
        subprocess.run([str(native_executable('rubyvr_gui')), '--map', map_id, '--mode', 'diorama',
            '--overrides', str(source), '--out', str(prefix)+'-saved.json', '--probe', str(scenario_path),
            '--showcase', str(script_path), '--probe-out', str(prefix)], cwd=ROOT, env=env,
            stdout=log, stderr=subprocess.STDOUT, check=True, timeout=180,
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    report = json.loads(Path(str(prefix)+'.json').read_text())
    assert report['ok'] and report['closed']
    return report


def click(events, frame, x, y):
    events.extend([dict(frame=frame, type='motion', x=x, y=y),
        dict(frame=frame+1, type='down', x=x, y=y), dict(frame=frame+3, type='up', x=x, y=y)])


def key(events, frame, scan, mod=0):
    events.extend([dict(frame=frame, type='key-down', scan=scan, mod=mod),
                   dict(frame=frame+2, type='key-up', scan=scan)])


def rectangle(events, frame, x0, y0, x1, y1):
    x, y = 208+x0*16+8, 108+y0*16+8
    ex, ey = 208+x1*16+8, 108+y1*16+8
    events.extend([dict(frame=frame, type='motion', x=x, y=y),
        dict(frame=frame+1, type='down', x=x, y=y),
        dict(frame=frame+4, type='motion', x=ex, y=ey),
        dict(frame=frame+6, type='up', x=ex, y=ey)])


def text_field(events, frame, x, y, value):
    click(events, frame, x, y)
    key(events, frame+5, 4, 64)
    events.append(dict(frame=frame+9, type='text', text=str(value)))
    key(events, frame+11, 40)


def main():
    source = ROOT / 'mod-assets/voxel-world-v6.json'
    before = source.read_bytes()
    events = []
    click(events, 22, 1002, 17)
    text_field(events, 32, 1380, 290, 124)
    rectangle(events, 50, 10, 10, 16, 15)
    click(events, 66, 1470, 361)
    click(events, 84, 1470, 465)
    rectangle(events, 104, 12, 16, 14, 19)
    click(events, 118, 1400, 390)
    key(events, 140, 29, 64)  # Ctrl+Z
    key(events, 158, 28, 64)  # Ctrl+Y
    key(events, 176, 22, 64)  # Ctrl+S
    events.extend([dict(frame=194, type='motion', x=800, y=650),
        dict(frame=195, type='down', x=800, y=650), dict(frame=200, type='motion', x=1100, y=655),
        dict(frame=203, type='up', x=1100, y=655)])
    events.append(dict(frame=222, type='quit'))
    result = run('wide', events, [(18, 'baseline'), (62, 'selected'), (80, 'plateau'), (98, 'contact'),
        (134, 'steps'), (152, 'undo'), (170, 'redo'), (188, 'saved'), (216, 'reverse')], frames=228)
    states = {s['name']: s for s in result['checkpoints']}
    assert states['selected']['terrain_open'] and states['selected']['undo'] == 0
    assert states['plateau']['terrain_cells'] == 42 and states['plateau']['undo'] == 1
    assert states['steps']['terrain_cells'] == 54 and states['steps']['undo'] == 2
    assert states['undo']['room_hash'] == states['plateau']['room_hash']
    assert states['redo']['room_hash'] == states['steps']['room_hash']
    assert states['saved']['unsaved'] is False
    assert states['reverse']['room_hash'] == states['saved']['room_hash']
    for s in states.values():
        assert s['terrain_rejected'] == 0 and s['unresolved_placements'] == 0
        assert s['raised'] == states['baseline']['raised'] and s['accepted_placements'] == states['baseline']['accepted_placements']

    # Exercise both new slope gestures through SDL, then verify the saved
    # endpoints and whole-region history rather than only a screenshot hash.
    slope_events=[]
    click(slope_events,22,1002,17)
    rectangle(slope_events,35,10,10,12,11)
    click(slope_events,48,1450,521)
    click(slope_events,62,1400,591)
    key(slope_events,82,29,64)
    key(slope_events,98,28,64)
    click(slope_events,114,1530,591)
    key(slope_events,132,22,64)
    slope_events.append(dict(frame=152,type='quit'))
    slopes=run('slopes',slope_events,[(57,'expanded'),(76,'north'),(92,'undo'),
        (108,'redo'),(126,'south'),(146,'saved')],frames=158)
    sl={s['name']:s for s in slopes['checkpoints']}
    assert sl['north']['terrain_cells']==6 and sl['north']['undo']==1
    assert sl['undo']['terrain_cells']==0
    assert sl['redo']['room_hash']==sl['north']['room_hash']
    assert sl['south']['undo']==2 and sl['south']['room_hash']!=sl['north']['room_hash']
    assert not sl['saved']['unsaved']
    slope_pack=json.loads((BUILD/'slopes-saved.json').read_text())
    slope_cells=slope_pack['terrain']['maps'][0]['cells']
    assert len(slope_cells)==6
    assert all(c['surfaces'][0]['rise_z']==8 for c in slope_cells)
    assert all(c['surfaces'][0]['height']==16+(c['y']-10)*8 for c in slope_cells)
    saved = json.loads((BUILD/'wide-saved.json').read_bytes())
    original = json.loads((BUILD/'wide.png.baseline.working.json').read_bytes())
    assert saved['version'] == 7 and saved['patterns'] == original['patterns']
    assert len(saved['terrain']['maps']) == 1
    cells = saved['terrain']['maps'][0]['cells']
    assert len(cells) == 54
    assert all(c['surfaces'][0]['side'] == 124 for c in cells)
    assert {c['y']: c['surfaces'][0]['height'] for c in cells if c['y'] >= 16} == {16:32, 17:24, 18:16, 19:8}
    reopened = run('reopened', [dict(frame=28, type='quit')], [(20, 'reopened')],
                   source=BUILD/'wide-saved.json', frames=32)
    assert reopened['checkpoints'][0]['room_hash'] == states['saved']['room_hash']
    groups = json.loads((ROOT/'third_party/pokeruby/data/maps/map_groups.json').read_bytes())
    tm = saved['terrain']['maps'][0]
    assert groups[groups['group_order'][tm['group']]][tm['number']] == 'OldaleTown'

    small_events = []
    click(small_events, 22, 1002, 17)
    text_field(small_events, 32, 1060, 170, 24)
    click(small_events,50,1187,314)  # pick explicit underlay from source map
    click(small_events,58,208+17*16+8,108+12*16+8)
    rectangle(small_events, 70, 17, 9, 20, 11)
    click(small_events, 86, 1150, 361)
    key(small_events, 106, 29, 64)
    key(small_events, 124, 28, 64)
    click(small_events, 142, 1150, 414)
    key(small_events, 162, 29, 64)
    key(small_events, 180, 22, 64)
    small_events.append(dict(frame=206, type='quit'))
    small = run('small', small_events, [(18,'baseline'),(66,'picked'),(100,'plateau'),(118,'undo'),(136,'redo'),
        (156,'erased'),(174,'restored'),(200,'saved')], source=BUILD/'wide-saved.json', width=1280, height=720, frames=212)
    ss = {s['name']:s for s in small['checkpoints']}
    assert ss['plateau']['terrain_cells']==66 and ss['plateau']['undo']==1
    assert ss['undo']['room_hash']==ss['baseline']['room_hash']==ss['erased']['room_hash']
    assert ss['redo']['room_hash']==ss['plateau']['room_hash']==ss['restored']['room_hash']
    assert ss['saved']['unsaved'] is False
    small_pack=json.loads((BUILD/'small-saved.json').read_bytes())
    small_cells=[c for c in small_pack['terrain']['maps'][0]['cells'] if 17<=c['x']<=20 and 9<=c['y']<=11]
    layouts=json.loads((ROOT/'third_party/pokeruby/data/layouts/layouts.json').read_bytes())['layouts']
    layout=next(l for l in layouts if l['id']=='LAYOUT_OLDALE_TOWN')
    blocks=(ROOT/'third_party/pokeruby'/layout['blockdata_filepath']).read_bytes()
    picked_id=struct.unpack_from('<H',blocks,2*((12-7)*layout['width']+(17-7)))[0]&1023
    assert len(small_cells)==12 and all(c['underlay']==picked_id and c['surfaces'][0]['height']==24 for c in small_cells)
    assert ss['picked']['undo']==0 and ss['picked']['room_hash']==ss['baseline']['room_hash']

    deck_events = []
    click(deck_events,22,1002,17)
    text_field(deck_events,32,1380,220,4)
    text_field(deck_events,50,1380,264,1)
    text_field(deck_events,68,1380,290,124)
    text_field(deck_events,86,1380,170,32)
    rectangle(deck_events,104,17,12,19,13)
    click(deck_events,120,1470,439)
    click(deck_events,138,1470,522)
    click(deck_events,158,1470,522)  # duplicate layer must be refused atomically
    click(deck_events,176,1470,547)
    key(deck_events,194,22,64)
    key(deck_events,214,29,64)
    key(deck_events,234,28,64)
    key(deck_events,252,22,64)
    deck_events.append(dict(frame=274,type='quit'))
    deck = run('deck',deck_events,[(134,'panel'),(152,'added'),(172,'rejected'),(208,'saved'),(228,'undo'),(268,'redo')],
               source=BUILD/'wide-saved.json',frames=280)
    ds={s['name']:s for s in deck['checkpoints']}
    assert ds['added']['terrain_cells']==60 and ds['added']['undo']==1
    assert ds['rejected']['room_hash']==ds['added']['room_hash'] and ds['rejected']['undo']==1
    assert ds['undo']['terrain_cells']==54 and ds['undo']['room_hash']==states['saved']['room_hash']
    assert ds['redo']['room_hash']==ds['saved']['room_hash'] and ds['redo']['unsaved'] is False
    deck_pack=json.loads((BUILD/'deck-saved.json').read_bytes())
    decks=[c for c in deck_pack['terrain']['maps'][0]['cells'] if len(c['surfaces'])==2]
    assert len(decks)==6 and all([(p['layer'],p['height'],p['thickness']) for p in c['surfaces']]==[(3,0,0),(4,32,4)] for c in decks)
    assert source.read_bytes() == before
    subprocess.run([sys.executable,str(ROOT/'tools/terrain-seam-fixture.py')],cwd=ROOT,check=True)
    view=dict(yaw=.5,pitch=.6,dist=14,tx=17,ty=.5,tz=13)
    show=[];click(show,22,1002,17)
    baseline=run('seam-before',show+[dict(frame=46,type='quit')],[(40,'front')],
                 map_id='MAP_ROUTE101',camera=view,height=1100,frames=52)
    orbit=show+[dict(frame=50,type='motion',x=900,y=770),dict(frame=51,type='down',x=900,y=770)]
    for frame in range(55,131,5):
        orbit.append(dict(frame=frame,type='motion',x=900+(frame-55)*3,y=770))
    orbit += [dict(frame=132,type='up',x=1125,y=770),dict(frame=152,type='quit')]
    checkpoints=[(40,'front')]+[(f+1,f'orbit-{f}') for f in range(55,131,5)]+[(146,'reverse')]
    seam=run('seam-after',orbit,checkpoints,source=BUILD/'seam-fixture.json',map_id='MAP_ROUTE101',
             camera=view,height=1100,frames=160)
    assert baseline['checkpoints'][0]['terrain_cells']==0
    assert all(s['terrain_cells']==540 and s['terrain_rejected']==0 and s['undo']==0 for s in seam['checkpoints'])
    assert len({s['room_hash'] for s in seam['checkpoints']})==1
    env=native_environment()
    with (BUILD/'source-check.log').open('w',encoding='utf-8') as log:
        subprocess.run([str(native_executable('rubyvr_studio')),'--test-terrain-source',str(ROOT/'third_party/pokeruby'),
                        str(BUILD/'seam-fixture.json')],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    report = dict(status='PASS', scope='Studio desktop; authored terrain lab, not canonical Ruby geography',
        gui_sha256=hashlib.sha256((native_executable('rubyvr_gui')).read_bytes()).hexdigest(),
        source_pack_sha256=hashlib.sha256(before).hexdigest(), checkpoints=len(states)+1+len(ss)+len(ds)+1+len(seam['checkpoints'])+len(sl),
        terrain_cells=54, synthetic_tests='Run rubyvr_studio --test-terrain separately',
        room_hash=states['saved']['room_hash'], raised=states['saved']['raised'],
        seam_room_hash=seam['checkpoints'][0]['room_hash'],
        seam_source_check=(BUILD/'source-check.log').read_text().splitlines()[-1])
    (BUILD/'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
