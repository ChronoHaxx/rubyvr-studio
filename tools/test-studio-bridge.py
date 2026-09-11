# SPDX-License-Identifier: GPL-3.0-or-later
"""Local source, production mesh and SDL save/reopen checks for Route 104.

Requires prepared local assets and a native GUI/display. Synthetic SDL input
does not replace the maintainer's physical functional checklist.
"""
import copy
import hashlib
import importlib.util
import json
import subprocess
from pathlib import Path

from studio_paths import executable

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/terrain-bridge'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    compiler = module('bridge_example', 'build-terrain-bridge-example.py')
    inputs = [ROOT / 'recipes/terrain-bridge.json', ROOT / 'recipes/terrain-regions.json',
              ROOT / 'mod-assets/voxel-world-v6.json']
    original = [p.read_bytes() for p in inputs]
    recipe, regional_recipe, starter = [json.loads(data) for data in original]
    pack, report = compiler.build(recipe, starter, regional_recipe)
    base, _ = compiler.regions.build(regional_recipe, starter)
    assert pack['terrain']['maps'][:-1] == base['terrain']['maps']
    assert pack['patterns'] == starter['patterns']
    untouched = copy.deepcopy((recipe, regional_recipe, starter))
    compiler.build(recipe, starter, regional_recipe)
    assert (recipe, regional_recipe, starter) == untouched

    def native(name, data, option, expected):
        path = OUT / (name + '.json')
        path.write_text(json.dumps(data, separators=(',', ':')) + '\n', encoding='utf-8')
        result = subprocess.run([str(executable('rubyvr_studio')), option,
                                 str(compiler.regions.SOURCE), str(path)], cwd=ROOT,
                                capture_output=True, text=True, timeout=180)
        log = result.stdout + result.stderr
        (OUT / (name + '.log')).write_text(log, encoding='utf-8')
        assert (result.returncode == 0) == expected, (name, log[-1800:])
        return log

    base_log = native('baseline-six', base, '--test-terrain-source', True)
    assert '200 joined edges' in base_log
    log = native('regions', pack, '--test-bridge-source', True)
    assert '38 decks, 299 water planes, 4 bank contacts, 30 owner seam edges, 6000' in log
    for change in ('lower-deck', 'water-step', 'raised-approach', 'broken-join', 'wrong-material'):
        altered = copy.deepcopy(pack)
        cells = {(c['x']-7, c['y']-7): c for c in altered['terrain']['maps'][-1]['cells']}
        if change == 'lower-deck': cells[24, 10]['surfaces'][1]['height'] = 15
        if change == 'water-step': cells[21, 10]['surfaces'][0]['height'] = 9
        if change == 'raised-approach': cells[24, 8]['surfaces'][0].update(height=17, thickness=17)
        if change == 'broken-join': cells[39, 50]['surfaces'][0].update(height=17, thickness=17)
        if change == 'wrong-material': cells[24, 10]['surfaces'][1]['top'] = 520
        native(change, altered, '--test-bridge-source', False)

    ui = module('connected_bridge_ui', 'test-connected-studio.py')
    ui.OUT = OUT / 'gui'
    events = []
    ui.ui.click(events, 22, 1082, 17)  # Explore area
    ui.ui.click(events, 58, 180, 17)   # Return to editing
    ui.ui.key(events, 80, 22, 64)      # Ctrl+S through the real editor
    events.append(dict(frame=110, type='quit'))
    camera = dict(yaw=.35, pitch=.9, dist=22, tx=32, ty=1, tz=22)
    states = ui.run('journey', events, [(18, 'editor'), (49, 'area'), (73, 'returned'), (100, 'saved')],
                    frames=115, map_id='MAP_ROUTE104', source=OUT/'regions.json', camera=camera)
    start, area = states['editor'], states['area']
    assert not start['exploring'] and area['exploring'] and area['region_maps'] == 3
    assert area['region_rejected'] == area['region_unresolved'] == area['stream_errors'] == 0
    assert {m['id'] for m in area['stream_maps']} == {'MAP_ROUTE104', 'MAP_PETALBURG_CITY', 'MAP_ROUTE102'}
    baseline = (ui.OUT/'journey.png.editor.working.json').read_bytes()
    for name, state in states.items():
        assert state['room_hash'] == start['room_hash'] and state['undo'] == 0
        assert not state['unsaved'] and not state['draft_dirty']
        assert (ui.OUT/f'journey.png.{name}.working.json').read_bytes() == baseline
    assert not states['returned']['exploring'] and states['returned']['region_maps'] == 0
    saved = ui.OUT/'journey-saved.json'
    assert json.loads(saved.read_bytes()) == json.loads(baseline)
    reopened = ui.run('reopened', [dict(frame=30, type='quit')], [(22, 'view')], frames=35,
                      map_id='MAP_ROUTE104', source=saved, camera=camera)['view']
    assert reopened['room_hash'] == start['room_hash'] and not reopened['unsaved']
    assert (ui.OUT/'reopened.png.view.working.json').read_bytes() == baseline
    assert [p.read_bytes() for p in inputs] == original
    assert json.loads((OUT/'regions.json').read_bytes()) == json.loads(json.dumps(pack))
    report.update(status='PASS', native_decks=38, native_water_planes=299, bank_contacts=4,
                  owner_seam_edges=30, source_matched_triangles=6000, original_seam_checks=200,
                  malformed_native_cases=5, sdl_checkpoints=5,
                  gui_sha256=hashlib.sha256(executable('rubyvr_gui').read_bytes()).hexdigest(),
                  batch_sha256=hashlib.sha256(executable('rubyvr_studio').read_bytes()).hexdigest(),
                  input_sha256=hashlib.sha256((OUT/'regions.json').read_bytes()).hexdigest(),
                  region_maps=area['region_maps'], region_bytes=area['region_bytes'],
                  region_rejected=0, region_unresolved=0, unchanged_source_and_six_maps=True,
                  unchanged_document_and_history=True, exact_save_reopen=True,
                  human_acceptance='PENDING')
    (OUT/'verification.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS: source footprint/guards, production layer/mesh/material/bank/seam checks, five broken inputs refused, five SDL checkpoints, exact save/reopen')


if __name__ == '__main__':
    main()
