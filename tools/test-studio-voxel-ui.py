"""Manual voxel authoring through real hidden SDL input, with fresh reopen.

The source fixture is a scratch v5 mask; no production asset pack is changed.
Coordinates were inspected in the 1600x950 rendered editor. Checks compare
complete saved definitions, material addresses, masks and production meshes.
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
from datetime import datetime, timezone

parser = argparse.ArgumentParser()
parser.add_argument('--inspect', action='store_true', help='Capture the journey without claiming acceptance')
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
game = repo
build = game / 'build'
exe = build / 'rubyvr_gui.exe'
asset = repo / 'mod-assets/voxel-house-v6.json'
original_hash = hashlib.sha256(asset.read_bytes()).hexdigest()
fixture = json.loads(asset.read_text())
fixture['version'] = 5
p = fixture['patterns'][0]
p.pop('voxel')
p['parts'] = []
p['model_seeded'] = False
p['name'] = 'Manual authoring check'
input_path = build / 'studio-voxel-ui-input.json'
input_path.write_text(json.dumps(fixture), encoding='utf-8')
saved = build / 'studio-voxel-ui-saved.json'
prefix = build / 'studio-voxel-ui'
report = build / 'studio-voxel-ui-pass.json'
evidence = dict(status='RUNNING', started_utc=datetime.now(timezone.utc).isoformat(),
    executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
report.write_text(json.dumps(evidence, indent=2), encoding='utf-8')


class Journey:
    def __init__(self):
        self.frame = 8
        self.events = []
        self.points = []

    def event(self, offset, kind, **values):
        self.events.append(dict(frame=self.frame + offset, type=kind, **values))

    def move(self, x, y):
        self.event(0, 'motion', x=x, y=y)
        self.frame += 3

    def click(self, x, y):
        self.event(0, 'motion', x=x, y=y)
        self.event(1, 'down', x=x, y=y)
        self.event(2, 'up', x=x, y=y)
        self.frame += 9

    def key(self, scan, mod=0):
        self.event(0, 'key-down', scan=scan, mod=mod)
        self.event(1, 'key-up', scan=scan)
        self.frame += 6

    def drag(self, x, y, end_x, end_y):
        self.event(0, 'motion', x=x, y=y)
        self.event(2, 'down', x=x, y=y)
        self.event(6, 'motion', x=end_x, y=end_y)
        self.event(8, 'up', x=end_x, y=end_y)
        self.frame += 14

    def point(self, name):
        self.points.append(dict(frame=self.frame, name=name))
        self.frame += 3

    def run(self, name, source, output, scenario='gui-probe-scene.json', mode=None):
        path = build / name
        script = path.with_name(name + '-events.json')
        script.write_text(json.dumps(dict(frames=self.frame + 3, events=self.events,
            checkpoints=self.points, chapters=[])), encoding='utf-8')
        command = [str(exe), '--map', 'MAP_OLDALE_TOWN', '--overrides', str(source),
            '--out', str(output), '--probe', str(repo / 'tools' / scenario),
            '--showcase', str(script), '--probe-out', str(path)]
        if mode:
            command += ['--mode', mode]
        with path.with_suffix('.out.log').open('w') as so, path.with_suffix('.err.log').open('w') as se:
            subprocess.run(command, cwd=game, stdout=so, stderr=se, stdin=subprocess.DEVNULL,
                env={k:v for k,v in os.environ.items() if not k.startswith('RUBYVR_')}, check=True, timeout=180)
        result = json.loads(path.with_suffix('.json').read_text())
        return result, {point['name']:point for point in result['checkpoints']}


j = Journey()
j.click(80, 252)       # Authored definition, including its source placement.
j.click(526, 18)       # MASK.
j.click(1460, 688)     # Explicit START VOXEL MODEL.
j.point('started')
j.drag(780, 318, 834, 372)  # Source rectangle [34,42,14,14], the window.
j.point('selected')
j.click(1460, 493)     # SPLIT SELECTION.
j.point('split')
j.click(1420, 450)     # Front -1 px.
j.point('recessed')
j.key(29, 64)         # Ctrl+Z.
j.point('undo-depth')
j.key(28, 64)         # Ctrl+Y.
j.point('redo-depth')
j.key(8)              # E: erase selected relief pixels.
j.click(794, 332)      # Source pixel [37,45].
j.point('erased')
j.key(21)             # R: restore.
j.click(794, 332)
j.point('restored')
j.key(8)
j.event(0, 'motion', x=804, y=342)
j.event(2, 'down', x=804, y=342)
j.event(6, 'motion', x=1000, y=800)  # Leave source surface while captured.
j.frame += 8
j.key(41)             # Escape cancels the whole gesture.
j.event(0, 'up', x=1000, y=800)
j.frame += 4
j.point('cancelled')
j.key(5)              # B: selection rectangle.
j.move(800, 300)
j.event(0, 'wheel', amount=1)
j.frame += 5
j.point('source-zoom')
j.click(558, 118)      # Fit source.
j.point('source-fit')
j.drag(661, 161, 681, 178)  # Opaque roof-art crop [5,4,6,5].
j.click(1495, 262)     # + Roof.
j.point('roof')
j.drag(722, 227, 731, 236)  # Alternative object-only crop [20,20,3,3].
j.key(40, 64)         # Ctrl+Enter: assign current solid face from selection.
j.point('face-art')
j.key(7, 64)          # Ctrl+D: duplicate.
j.point('duplicate')
j.key(29, 64)
j.point('undo-duplicate')
j.key(76)             # Delete selected roof.
j.point('deleted')
j.key(29, 64)
j.point('undo-delete')
j.key(47)             # [ previous part.
j.point('previous-part')
j.key(48)             # ] next part.
j.point('next-part')
j.move(800, 620)
j.key(30)             # 1: front orthographic.
j.point('front')
j.key(32)             # 3: right orthographic.
j.point('side')
j.key(36)             # 7: exact top, including the pole camera basis.
j.point('top')
j.key(39)             # 0: isometric.
j.click(588, 894)      # Neutral geometry view, after the Resize toolbar label.
j.point('neutral')
j.click(300, 118)      # Source tool menu.
j.click(280, 202)      # Paint shadow.
j.click(460, 118)      # Gesture menu.
j.click(460, 183)      # Flood connected color.
j.point('flood-tool')
j.click(640, 310)      # Connected ground pixels on left source margin.
j.point('shadow')
j.key(29, 64)
j.point('undo-shadow')
j.click(1460, 856)     # Pinned APPLY + SAVE.
j.point('saved')
j.click(630, 18)       # DIORAMA.
j.point('diorama')
j.event(0, 'quit')
j.frame += 4
result, points = j.run('studio-voxel-ui', input_path, saved)
if args.inspect:
    print(json.dumps(points, indent=2))
    report.write_text(json.dumps({**evidence, 'status':'PARTIAL'}, indent=2), encoding='utf-8')
    raise SystemExit(0)

checks = []


def require(ok, message):
    if not ok:
        report.write_text(json.dumps({**evidence, 'status':'FAIL', 'failure':message, 'checks':checks}, indent=2), encoding='utf-8')
        raise SystemExit('[voxel-ui] FAIL: ' + message)
    checks.append(message)


def document(name, stem=prefix, kind='draft'):
    return json.loads(Path(f'{stem}.png.{name}.{kind}.json').read_text())


def pattern(name):
    return document(name)['patterns'][0]


def part(name, index=-1):
    return pattern(name)['parts'][index]


def decode(mask):
    return [mask['first'] ^ (i % 2) for i, count in enumerate(mask['runs']) for _ in range(count)]


require(result['ok'] and result['closed'], 'the real SDL journey saves and closes without a pending decision')
require(points['started']['mode'] == 2 and points['started']['parts'] == 1, 'start from an existing mask through the visible button')
require(document('started')['version'] == 6 and document('started', kind='working')['version'] == 5, 'explicit draft upgrade preserves the previous applied format')
require(points['selected']['pixel_selection'] == [34,42,14,14], 'drag selects exact source pixel bounds')
require(pattern('selected') == pattern('started') and points['selected']['undo'] == points['started']['undo'], 'selection changes neither asset data nor undo history')
require(points['split']['parts'] == 2 and part('split')['art_region'] == [34,42,14,14], 'split creates a cropped relief through the inspector')
parent = decode(part('split', 0)['local_mask'])
child = decode(part('split')['local_mask'])
require(sum(parent) + sum(child) == sum(decode(part('started')['local_mask'])), 'split preserves the total authored pixels without duplicating them')
require(math.isclose(part('recessed')['position'][2], part('split')['position'][2]-1/16), 'Front minus control creates exactly one pixel of recess')
require(points['recessed']['preview_hash'] != points['split']['preview_hash'], 'recess changes actual production geometry')
require(pattern('undo-depth') == pattern('split') and pattern('redo-depth') == pattern('recessed'), 'keyboard undo and redo restore the complete depth edit')
require(sum(decode(part('erased')['local_mask'])) == sum(child)-1, 'erase removes one pixel from only the selected relief')
require(part('erased', 0) == part('recessed', 0), 'local relief editing preserves the parent part')
require(pattern('restored') == pattern('recessed'), 'restore returns the exact mask and transform')
require(pattern('cancelled') == pattern('restored') and points['cancelled']['undo'] == points['restored']['undo'], 'Escape cancels painting after the cursor leaves the canvas')
require(points['source-zoom']['source_canvas'][2] > points['cancelled']['source_canvas'][2] and points['source-zoom']['camera'] == points['cancelled']['camera'], 'source zoom is independent of the model camera')
require(points['source-fit']['source_canvas'] == points['started']['source_canvas'], 'Fit restores source framing')
require(points['roof']['parts'] == 3 and part('roof')['kind'] == 'wedge', 'add a stepped roof from selected source art')
require(part('face-art')['surfaces'][0]['region'] == [20,20,3,3] and part('face-art')['surfaces'][1:] == part('roof')['surfaces'][1:], 'face assignment changes only the chosen face')
require(points['duplicate']['parts'] == 4 and part('duplicate')['id'] != part('duplicate', -2)['id'], 'duplicate preserves geometry with a distinct identity')
require(pattern('undo-duplicate') == pattern('face-art'), 'undo duplicate is exact')
require(points['deleted']['parts'] == 2 and pattern('undo-delete') == pattern('face-art'), 'delete and undo restore the selected part')
require(points['previous-part']['selected_part'] != points['next-part']['selected_part'] and points['next-part']['selected_part'] == points['undo-delete']['selected_part'], 'bracket keys cycle part selection')
require(points['front']['orthographic'] and abs(points['front']['camera'][0]) < 1e-6 and abs(points['front']['camera'][1]) < 1e-6, 'front shortcut provides an exact orthographic view')
require(math.isclose(points['side']['camera'][0], math.pi/2, abs_tol=1e-5), 'side shortcut is perpendicular to the facade')
require(math.isclose(points['top']['camera'][1], math.pi/2, abs_tol=1e-5), 'top shortcut reaches the exact pole without perspective distortion')
require(points['neutral']['neutral'] and pattern('neutral') == pattern('face-art'), 'neutral view changes presentation without modifying the model')
require(points['flood-tool']['pixel_tool'] == 3 and points['flood-tool']['pixel_shape'] == 2, 'visible menus select shadow ownership and connected flood')
require(sum(decode(pattern('shadow')['voxel']['shadow'])) > 0 and pattern('shadow')['cutout'] == pattern('neutral')['cutout'], 'flood moves connected ground to shadow without erasing object pixels')
require(pattern('undo-shadow') == pattern('neutral'), 'undo restores exact role masks')
require(document('saved', kind='working') == json.loads(saved.read_text()), 'pinned Apply + Save writes the complete working document')
require(points['saved']['draft_dirty'] is False and points['saved']['unsaved'] is False and points['diorama']['raised'] == 2, 'save clears dirty markers and the model appears at both accepted placements')

reopen = Journey()
reopen.click(80, 252)  # First model row beneath the room/search filter.
reopen.click(575, 18)
reopen.point('reopened')
reopen.click(1460, 856)
reopen.point('resaved')
reopen.event(0, 'quit')
reopen.frame += 4
copy_path = build / 'studio-voxel-ui-resaved.json'
reopened, rp = reopen.run('studio-voxel-ui-reopen', saved, copy_path, mode='diorama')
require(reopened['ok'] and reopened['closed'] and json.loads(copy_path.read_text()) == json.loads(saved.read_text()), 'fresh process opens and resaves identical authoring data')
require(rp['reopened']['preview_hash'] == points['saved']['preview_hash'], 'fresh process rebuilds identical production geometry')

small = Journey()
small.click(80, 252)
small.click(575, 18)
small.click(1060, 155)
small.point('small')
small.click(1140, 626)
small.point('small-saved')
small.event(0, 'quit')
small.frame += 4
small_path = build / 'studio-voxel-ui-small-saved.json'
sr, sp = small.run('studio-voxel-ui-small', saved, small_path, scenario='gui-probe-small.json')
require(sr['ok'] and sr['closed'] and json.loads(small_path.read_text()) == json.loads(saved.read_text()), 'Apply + Save remains reachable at 1280x720')
require(sp['small']['mode'] == 2 and sp['small']['selected_part'], 'model and part selection work at the minimum window size')
require(hashlib.sha256(asset.read_bytes()).hexdigest() == original_hash, 'production asset fixture remains unchanged')
report.write_text(json.dumps({**evidence, 'status':'PASS', 'checks':checks, 'count':len(checks),
    'saved':str(saved), 'captures':str(prefix), 'runtime':'not run', 'headset':'not run'}, indent=2), encoding='utf-8')
print(f'[voxel-ui] PASS checks={len(checks)} report={report}')
