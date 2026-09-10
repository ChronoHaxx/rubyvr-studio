"""Actual SDL journeys for the M1 review browser; needs local catalog/source/GL."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'mod-assets/voxel-world-v6.json'


class Journey:
    def __init__(self):
        self.events, self.points = [], []

    def event(self, frame, kind, **values):
        self.events.append(dict(frame=frame, type=kind, **values))

    def click(self, frame, x, y):
        for n, kind in enumerate(('motion', 'down', 'up')):
            self.event(frame+n, kind, x=x, y=y)

    def key(self, frame, scan, mod=0):
        self.event(frame, 'key-down', scan=scan, mod=mod)
        self.event(frame+1, 'key-up', scan=scan)

    def point(self, frame, name):
        self.points.append(dict(frame=frame, name=name))

    def run(self, prefix, scenario, frames):
        script = dict(frames=frames, events=self.events, checkpoints=self.points, chapters=[], record=0)
        (ROOT/(prefix+'-events.json')).write_text(json.dumps(script), encoding='utf-8')
        env = os.environ.copy()
        env['PATH'] = env.get('RUBYVR_MINGW_BIN', r'C:\msys64\mingw64\bin')+os.pathsep+env['PATH']
        with (ROOT/(prefix+'.log')).open('w', encoding='utf-8') as log:
            subprocess.run([str(ROOT/'build/rubyvr_gui.exe'), '--map', 'MAP_OLDALE_TOWN',
                            '--mode', 'diorama', '--overrides', str(SOURCE), '--out', prefix+'-saved.json',
                            '--probe', f'tools/gui-probe-{scenario}.json', '--showcase', prefix+'-events.json',
                            '--probe-out', prefix], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=90, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        result = json.loads((ROOT/(prefix+'.json')).read_text())
        assert result['ok'], f'{prefix}: capture failed'
        return result, {s['name']:s for s in result['checkpoints']}


def main():
    original = SOURCE.read_bytes()
    ledger = ROOT/'build/coverage/ledger.sqlite'
    db_before = ledger.read_bytes()
    report = dict(status='RUNNING', layouts=[], gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest())
    try:
        for size, scenario, left, top, bottom in [('wide', 'scene', 240, 140, 810), ('small', 'small', 80, 39, 681)]:
            j = Journey();prefix='build/studio-review-'+size
            j.point(8, 'baseline')
            j.click(12,930,17)
            j.click(25,left+100,top+95);j.event(30,'text',text='Sootopolis')
            j.click(40,left+100,top+136)
            j.click(50,left+300,top+106);j.click(65,left+300,top+205)
            j.click(75,left+410,top+197);j.point(85,'failed')
            j.click(98,left+310,bottom-148);j.point(112,'gym')
            # An ordinary drag orbits the reviewed placement.
            j.event(120,'motion',x=770 if size=='wide' else 610,y=665 if size=='wide' else 502)
            j.event(122,'down',x=770 if size=='wide' else 610,y=665 if size=='wide' else 502)
            j.event(129,'motion',x=870 if size=='wide' else 710,y=545 if size=='wide' else 382)
            j.event(133,'up',x=870 if size=='wide' else 710,y=545 if size=='wide' else 382)
            j.point(140,'orbit')
            j.click(150,930,17)
            j.click(160,left+300,top+106);j.click(170,left+300,top+149)
            j.point(180,'missing')
            j.click(185,left+410,top+197);j.point(192,'source-intent');j.click(198,left+310,bottom-148)
            j.point(210,'source')
            j.click(220,930,17)
            j.click(230,left+100,top+95);j.key(235,4,64);j.event(240,'text',text='Oldale')
            j.click(250,left+100,top+136)
            j.click(260,left+300,top+106);j.click(270,left+300,top+168)
            j.point(280,'unresolved')
            j.click(290,left+248,top+130);j.point(300,'unresolved-ground')
            j.click(310,left+248,top+130)
            j.click(320,left+300,top+106);j.click(330,left+300,top+187)
            j.point(340,'unreviewed')
            j.click(350,left+248,top+130);j.click(360,left+370,top+130)
            j.point(370,'all-copies')
            j.click(380,left+550,top+106);j.event(385,'text',text='zz-no-match')
            j.point(395,'empty')
            j.key(405,4,64);j.key(410,42)
            j.point(420,'restored')
            j.click(430,left+1108,top+10)
            j.key(440,22,64);j.point(450,'saved');j.event(460,'quit')
            result, states = j.run(prefix,scenario,465)
            assert result['closed'], f'{size}: did not close cleanly'
            assert states['failed']['review_filter']==4 and states['failed']['review_visible']==2
            target='instance:MAP_SOOTOPOLIS_CITY:35:35:asset-544bfd83afef3991'
            assert states['failed']['review_selected']==target
            assert states['gym']['map_id']=='MAP_SOOTOPOLIS_CITY' and states['gym']['review_focus']==target
            assert states['gym']['camera_target'][0::2]==[38,37.5]
            assert states['orbit']['camera']!=states['gym']['camera']
            assert states['missing']['review_filter']==1 and states['missing']['review_visible']>0
            assert states['source']['review_focus'].startswith('placement:MAP_SOOTOPOLIS_CITY:15:47:')
            assert states['source']['camera_target'][0::2]==[16,48]  # centre of the 2x2 source footprint
            # The starter now supplies a known intent for all nonflat Oldale
            # objects. This is not visual approval; unclaimed ground stays in
            # the unresolved queue when explicitly included.
            assert states['unresolved']['review_filter']==2 and states['unresolved']['review_visible']==0
            assert states['unresolved-ground']['review_filter']==2 and states['unresolved-ground']['review_visible']>0
            assert states['unresolved-ground']['review_flat']
            assert states['unreviewed']['review_filter']==3 and states['unreviewed']['review_visible']>0
            assert states['all-copies']['review_visible']==393 and states['all-copies']['review_flat'] and states['all-copies']['review_padding']
            assert states['empty']['review_visible']==0 and states['restored']['review_visible']==393
            baseline=json.loads((ROOT/(prefix+'.png.baseline.working.json')).read_text())
            for name,state in states.items():
                assert not state['draft_dirty'] and not state['unsaved'], f'{size}/{name}: browsing dirtied artwork'
                assert json.loads((ROOT/(prefix+'.png.'+name+'.working.json')).read_text())==baseline
            assert json.loads((ROOT/(prefix+'-saved.json')).read_text())==baseline
            report['layouts'].append(dict(size=size,status='PASS',checkpoints=len(states),prefix=prefix))
            print('PASS review browser: '+size,flush=True)
        # Open the queue while a real Ctrl+D authoring edit is pending. Browser
        # shortcuts cannot alter it; cancelling navigation retains it exactly.
        j=Journey();prefix='build/studio-review-guard'
        j.point(8,'baseline');j.click(12,80,252);j.click(24,180,48);j.key(35,48)
        j.point(42,'selected');j.key(46,7,64);j.point(55,'dirty');j.click(60,1000,17)
        j.click(70,340,235);j.event(76,'text',text='Sootopolis');j.click(86,340,276)
        j.click(98,540,246);j.click(110,540,345);j.click(120,650,337);j.point(130,'review')
        j.key(135,7,64);j.key(139,76);j.point(145,'shortcuts')
        j.click(155,550,662);j.point(167,'guard');j.click(175,869,198);j.point(184,'cancelled')
        j.key(190,29,64);j.point(200,'restored');j.event(210,'quit')
        result,s=j.run(prefix,'scene',216)
        assert result['closed']
        assert s['dirty']['parts']==s['selected']['parts']+1 and s['dirty']['draft_dirty']
        assert s['review']['review_historical']
        assert s['guard']['pending']==10 and s['guard']['map_id']=='MAP_OLDALE_TOWN'
        assert s['cancelled']['pending']==0 and s['cancelled']['map_id']=='MAP_OLDALE_TOWN'
        original_draft=json.loads((ROOT/(prefix+'.png.dirty.draft.json')).read_text())
        for name in ('review','shortcuts','guard','cancelled'):
            assert s[name]['draft_dirty'] and not s[name]['unsaved'], name
            assert json.loads((ROOT/(prefix+'.png.'+name+'.draft.json')).read_text())==original_draft, name
        assert not s['restored']['draft_dirty'] and s['restored']['parts']==s['selected']['parts']
        assert json.loads((ROOT/(prefix+'.png.restored.draft.json')).read_text())==json.loads((ROOT/(prefix+'.png.selected.draft.json')).read_text())
        report['edit_guard']=dict(status='PASS',checkpoints=len(s),prefix=prefix)
        print('PASS review browser: edit guard and keyboard ownership',flush=True)
        assert SOURCE.read_bytes()==original, 'Starter changed'
        assert ledger.read_bytes()==db_before, 'Review browsing changed ledger'
        report['status']='PASS'
    except Exception as exc:
        report['status'],report['error']='FAIL',str(exc)
        raise
    finally:
        (ROOT/'build/studio-review-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
