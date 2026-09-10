"""Original treatment-policy fixtures; no game assets, graphics or network."""
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import coverage_disposition as treatment
import coverage_review

spec = importlib.util.spec_from_file_location('fixture', Path(__file__).with_name('test-coverage-ledger.py'))
fixture = importlib.util.module_from_spec(spec); spec.loader.exec_module(fixture)
ledger = fixture.ledger


class DispositionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.root = Path(self.temp.name)
        self.db_path = self.root/'ledger.sqlite'; self.db = ledger.open_db(self.db_path)
        self.catalog, self.pack, self.audit, self.receipt = fixture.fixture(self.root)
        self.rows = ledger.build_entries(self.root, self.pack, self.audit, self.receipt)
        ledger.sync_rows(self.db, self.rows, {})

    def tearDown(self):
        self.db.close(); self.temp.cleanup()

    def classify(self, row, review=None):
        return treatment.Policy(self.db).classify(row, review)

    def state(self, category, status='discovered', path='src/synthetic.c', **detail):
        return ledger.entry('state:'+category, 'state', category, 'original', category=category,
                            implementation='pending_runtime', detail=dict(source_status=status, source=dict(path=path), **detail))

    def placement(self, **detail):
        return ledger.entry('original-placement', 'placement', 'Drawing', 'source', category='flat',
                            implementation='modeled', detail=dict(member_cells=2, claimed_cells=2, models=['model-part'], **detail))

    def test_complete_authored_role_does_not_approve_a_placement(self):
        r = self.placement(); ledger.sync_rows(self.db, self.rows+[r], {})
        result = ledger.report(self.db)
        shown = next(row for row in result['entries'] if row['id']==r['id'])
        self.assertEqual(shown['disposition'], 'model')
        self.assertEqual(shown['classification']['rule'], 'complete_authored_coverage')
        self.assertEqual([shown['reviews'][s]['status'] for s in ('visual','live','headset')], ['unreviewed']*3)
        self.assertEqual(self.db.execute('SELECT count(*) FROM reviews').fetchone()[0], 0)

    def test_partial_mixed_unknown_and_archived_models_stay_unresolved(self):
        r = self.placement(); d = json.loads(r['detail'])
        for edit in ({'claimed_cells':1}, {'models':[]}, {'models':['missing']}, {'models':['model-part','missing']}):
            with self.subTest(edit=edit):
                self.assertEqual(self.classify(dict(r, detail=dict(d, **edit)))['disposition'], 'unresolved')
        with self.db: self.db.execute("UPDATE entries SET present=0 WHERE id='model:model-part'")
        self.assertEqual(self.classify(r)['disposition'], 'unresolved')

    def test_floor_only_coverage_is_not_terrain_height_or_flat_label_inference(self):
        floor = ledger.entry('model:floor', 'model', 'Floor mask', 's', category='ground_mask',
                             implementation='ground_only', disposition='intentional_flat', detail=dict(source_matches=True))
        ledger.sync_rows(self.db, self.rows+[floor], {})
        r = self.placement(); d = json.loads(r['detail'])
        self.assertEqual(self.classify(dict(r, detail=dict(d, models=['floor'])))['disposition'], 'intentional_flat')
        self.assertEqual(self.classify(dict(r, detail=dict(d, models=['floor','model-part'])))['disposition'], 'unresolved')
        self.assertEqual(self.classify(dict(r, implementation='unmodeled', detail=dict(d, claimed_cells=0, models=[])))['disposition'], 'unresolved')

    def test_field_battle_and_unknown_sprite_domains_stay_distinct(self):
        for path, expected, owner in (('src/data/object_events/anims.h','animated_effect','M6'),
                                      ('src/battle/anim/original.c','animated_effect','M7'),
                                      ('src/battle_effects.c','animated_effect','M7'),
                                      ('src/menu_or_field.c','unresolved','M1')):
            r = self.classify(self.state('actor_animation', path=path))
            self.assertEqual((r['disposition'],r['milestone']), (expected,owner))

    def test_runtime_controls_and_variable_graphics_do_not_invent_values(self):
        for category, owner in (('warp','M5'), ('metatile_behavior','M2'), ('weather','M8'),
                                 ('movement_command','M6'), ('script_presentation','M7')):
            source = self.state(category, status='runtime_resolution', integer=None)
            before = json.dumps(source, sort_keys=True)
            r = self.classify(source)
            self.assertEqual((r['disposition'],r['milestone']), ('runtime_state',owner))
            self.assertEqual(json.dumps(source, sort_keys=True), before)
        graphic = self.state('object_graphic',status='runtime_resolution',integer=None,variable=True)
        self.assertEqual(self.classify(graphic)['disposition'], 'animated_effect')
        self.assertIsNone(json.loads(graphic['detail'])['integer'])

    def test_native_unknown_and_nonnull_animation_routes_are_not_cleared(self):
        for r in (self.state('native_mutation_trace',mutations=[],frontiers=[]), self.state('future_category'),
                  self.state('warp',status='unresolved_reference'), self.state('script_actor_state',recognized=False),
                  self.state('tileset_animation_callback',no_callback=False)):
            self.assertEqual(self.classify(r)['disposition'], 'unresolved')
        self.assertEqual(self.classify(self.state('tileset_animation_callback',no_callback=True))['disposition'], 'runtime_state')

    def test_recorded_decisions_override_defaults_and_stale_intent_is_explicit(self):
        row = self.placement(); ledger.sync_rows(self.db, self.rows+[row], {})
        ledger.review(self.db,row['id'],'disposition','terrain','synthetic://ground','Authored ground decision')
        self.assertEqual(ledger.report(self.db,disposition='terrain',milestone='M2')['entries'][0]['id'],row['id'])
        changed = dict(row, source_hash='changed-source')
        ledger.sync_rows(self.db,self.rows+[changed],{})
        r = next(r for r in ledger.report(self.db)['entries'] if r['id']==row['id'])
        self.assertEqual(r['disposition'],'unresolved')
        self.assertEqual(r['classification']['basis'],'stale_review')
        self.assertEqual(r['classification']['previous_disposition'],'terrain')
        self.assertEqual(self.db.execute('SELECT result FROM reviews').fetchone()[0],'terrain')

    def test_family_review_never_replaces_placement_intent(self):
        ledger.review(self.db,'family:family-building','disposition','intentional_flat','synthetic://family','Original family decision')
        result = ledger.report(self.db)
        self.assertEqual(next(r for r in result['entries'] if r['id']=='family:family-building')['disposition'],'intentional_flat')
        r = next(r for r in result['entries'] if r['kind']=='placement' and r['category']=='structure')
        self.assertEqual(r['disposition'],'unresolved')

    def test_filters_respect_shared_maps_and_new_runtime_disposition(self):
        state = self.state('warp',related_maps=['MAP_SYNTHETIC'])
        ledger.sync_rows(self.db,self.rows+[state],{})
        ledger.review(self.db,state['id'],'disposition','runtime_state','synthetic://warp','Saved target resolved at runtime')
        r = ledger.report(self.db,'MAP_SYNTHETIC','warp','runtime_state','M5')
        self.assertEqual(len(r['entries']),1); self.assertEqual(sum(c['count'] for c in r['counts']),1)
        self.assertEqual(r['entries'][0]['classification']['basis'],'review')
        self.assertIn('runtime_state',ledger.markdown(r))
        for kwargs in (dict(milestone='M99'),dict(disposition='guess')):
            with self.assertRaises(ValueError): ledger.report(self.db,**kwargs)

    def test_cli_reports_and_show_are_read_only_and_filter_effective_decisions(self):
        before = self.db_path.read_bytes()
        tool = str(Path(__file__).with_name('coverage-ledger.py'))
        args = [sys.executable,tool,'--db',str(self.db_path)]
        result = subprocess.run(args+['report','--milestone','M2','--disposition','unresolved','--json'],capture_output=True,text=True,check=True)
        data = json.loads(result.stdout)
        self.assertTrue(data['entries']); self.assertTrue(all(r['kind']=='export_gap' for r in data['entries']))
        shown = subprocess.run(args+['show','model:model-part'],capture_output=True,text=True,check=True)
        self.assertEqual(json.loads(shown.stdout)['classification']['disposition'],'model')
        self.assertEqual(self.db_path.read_bytes(),before)

    def test_studio_and_cli_agree_on_effective_intent_without_review_promotion(self):
        self.pack['patterns'][0].update(w=2, mask=[1,1], ids=[1,1])
        pack_path = self.root/'pack.json'; pack_path.write_text(json.dumps(self.pack))
        rows = ledger.build_entries(self.root,self.pack,self.audit,self.receipt)
        ledger.sync_rows(self.db,rows,dict(source_version='original',catalog_directory=str(self.root),
                         catalog_sha256=hashlib.sha256((self.root/'catalog.json').read_bytes()).hexdigest(),
                         pack_sha256=hashlib.sha256(pack_path.read_bytes()).hexdigest()))
        before = self.db_path.read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)/'index.json'
            coverage_review.export_studio(self.db,pack_path,out)
            index = json.loads(out.read_text()); room = json.loads((out.parent/index['directory']/'MAP_SYNTHETIC.json').read_text())
            shown = next(r for r in room['rows'] if r['kind']=='placement' and r['category']=='structure')
            cli = next(r for r in ledger.report(self.db)['entries'] if r['id']==shown['id'])
            self.assertEqual(shown['disposition'],cli['disposition']); self.assertEqual(shown['disposition'],'model')
            self.assertFalse(shown['flags'] & 2); self.assertTrue(shown['flags'] & 4)
            self.assertIn('model / M4',shown['notes']); self.assertEqual(shown['visual'],'unreviewed')
        self.assertEqual(before,self.db_path.read_bytes())

    def test_concurrent_sync_cannot_mix_source_rows_and_authored_roles(self):
        self.db.execute('PRAGMA journal_mode=WAL')
        row = self.placement(); ledger.sync_rows(self.db,self.rows+[row],{})
        writer = ledger.open_db(self.db_path)
        original = treatment.Policy.__init__
        def change_between_queries(policy, reader):
            with writer: writer.execute("UPDATE entries SET present=0 WHERE id='model:model-part'")
            original(policy,reader)
        try:
            with patch.object(treatment.Policy,'__init__',change_between_queries):
                result = ledger.report(self.db)
            self.assertEqual(next(r for r in result['entries'] if r['id']==row['id'])['disposition'],'model')
            self.assertEqual(self.classify(row)['disposition'],'unresolved')
        finally:
            writer.close()


if __name__=='__main__':
    unittest.main(verbosity=2)
