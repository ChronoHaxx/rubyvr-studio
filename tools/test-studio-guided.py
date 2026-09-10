"""Exercise the visible select/mask/shape/scene/save path with hidden SDL input."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = ROOT / 'mod-assets/voxel-world-v6.json'
    original = source.read_bytes()
    env = os.environ.copy()
    env['PATH'] = env.get('RUBYVR_MINGW_BIN', r'C:\msys64\mingw64\bin') + os.pathsep + env['PATH']
    report = dict(status='RUNNING', started_utc=datetime.now(timezone.utc).isoformat(),
                  gui_sha256=hashlib.sha256((ROOT/'build/rubyvr_gui.exe').read_bytes()).hexdigest(), layouts=[])
    target = ROOT / 'build/guided-verification.json'
    target.write_text(json.dumps(report, indent=2), encoding='utf-8')
    try:
        for size, scenario in [('wide', 'scene'), ('small', 'small')]:
            prefix = f'build/guided-{size}'
            events = []
            def click(frame, x, y):
                events.extend([dict(frame=frame,type='motion',x=x,y=y),
                               dict(frame=frame+1,type='down',x=x,y=y),
                               dict(frame=frame+2,type='up',x=x,y=y)])
            def key(frame, scan, mod=0):
                events.extend([dict(frame=frame,type='key-down',scan=scan,mod=mod),
                               dict(frame=frame+1,type='key-up',scan=scan)])
            points = [(8,'start'), (21,'selected'), (34,'model'), (47,'part'),
                      (65,'mask'), (81,'shape'), (95,'saved'), (111,'scene')]
            click(12,80,252)       # First named model, below the search field.
            events.append(dict(frame=19,type='motion',x=800,y=420))
            click(26,180,48)       # Visible Edit selected model action.
            key(39,48)            # ] selects/cycles a part.
            key(43,9)             # F focuses the model.
            click(56,526,18)      # Mask tab.
            click(72,180,48)      # Visible Next: shape model action.
            key(87,22,64)         # Ctrl+S saves the current working copy.
            click(102,180,48)     # Visible View in scene action.
            events.append(dict(frame=116,type='quit'))
            script = dict(frames=120,events=events,record=0,
                          checkpoints=[dict(frame=f,name=n) for f,n in points], chapters=[])
            (ROOT/(prefix+'-events.json')).write_text(json.dumps(script),encoding='utf-8')
            command = [str(ROOT/'build/rubyvr_gui.exe'), '--map','MAP_OLDALE_TOWN',
                       '--mode','diorama','--overrides','mod-assets/voxel-world-v6.json',
                       '--out',prefix+'-saved.json','--probe',f'tools/gui-probe-{scenario}.json',
                       '--showcase',prefix+'-events.json','--probe-out',prefix]
            with (ROOT/(prefix+'.log')).open('w',encoding='utf-8') as log:
                subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,
                               check=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            result = json.loads((ROOT/(prefix+'.json')).read_text())
            states = {row['name']:row for row in result['checkpoints']}
            assert result['ok'] and result['closed'], f'{size}: replay did not complete cleanly'
            assert states['selected']['parts'] > 0, f'{size}: first model was not selected'
            assert [states[name]['mode'] for name in ['start','selected','model','mask','shape','scene']] == [3,3,2,1,2,3], f'{size}: next action did not navigate the expected modes'
            assert states['part']['selected_part'], f'{size}: part selection failed'
            assert states['shape']['preview_hash'] == states['model']['preview_hash'], f'{size}: navigation changed geometry'
            assert not states['saved']['unsaved'] and not states['saved']['draft_dirty'], f'{size}: save did not clear dirty state'
            # The writer omits explicit [0,0] default artwork offsets. Compare
            # the loaded document before/after browsing, and separately guard
            # the raw input bytes; this avoids treating optional spelling as an edit.
            baseline = json.loads((ROOT/(prefix+'.png.start.working.json')).read_text())
            assert json.loads((ROOT/(prefix+'-saved.json')).read_text()) == baseline, f'{size}: browsing/navigation changed the saved pack'
            assert source.read_bytes() == original, 'input starter was modified'
            report['layouts'].append(dict(size=size,status='PASS',checkpoints=len(states),prefix=prefix))
            print(f'PASS guided workflow: {size}', flush=True)
        report['status'] = 'PASS'
    except Exception as exc:
        report['status'],report['error'] = 'FAIL',str(exc)
        raise
    finally:
        target.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
