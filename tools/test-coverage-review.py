"""Read-only Studio export and native reader checks with original synthetic data."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import coverage_review as view

spec = importlib.util.spec_from_file_location('fixture', Path(__file__).with_name('test-coverage-ledger.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
ledger = fixture.ledger


class ReviewTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        catalog, pack, audit, receipt = fixture.fixture(self.root)
        self.pack = self.root/'pack.json'
        self.pack.write_text(json.dumps(pack))
        self.db_path = self.root/'ledger.sqlite'
        self.db = ledger.open_db(self.db_path)
        ledger.sync_rows(self.db, ledger.build_entries(self.root, pack, audit, receipt), dict(
            source_version='synthetic-v1', catalog_directory=str(self.root),
            catalog_sha256=hashlib.sha256((self.root/'catalog.json').read_bytes()).hexdigest(),
            pack_sha256=hashlib.sha256(self.pack.read_bytes()).hexdigest()))
        # Keep output outside the catalog, just as in the real workspace.
        self.output_temp = tempfile.TemporaryDirectory()
        self.output = Path(self.output_temp.name)/'studio/index.json'
        self.instance = self.db.execute("SELECT id FROM entries WHERE kind='instance' AND boundary='map_body'").fetchone()[0]

    def tearDown(self):
        self.db.close()
        self.temp.cleanup()
        self.output_temp.cleanup()

    def export(self):
        db = view.open_readonly(self.db_path)
        try:
            view.export_studio(db, self.pack, self.output)
        finally:
            db.close()
        index = json.loads(self.output.read_text())
        path = self.output.parent/index['directory']/'MAP_SYNTHETIC.json'
        return index, path, json.loads(path.read_text())

    def review(self, id, stage='visual', result='accepted'):
        ledger.review(self.db, id, stage, result, 'synthetic://evidence', 'Original fixture review')

    def test_read_only_deterministic_and_backup_coordinates(self):
        before = self.db_path.read_bytes()
        index, _, room = self.export()
        again, _, _ = self.export()
        self.assertEqual(index['directory'], again['directory'])
        self.assertEqual(before, self.db_path.read_bytes())
        self.assertEqual((room['width'], room['height']), (115, 54))
        padding = next(r for r in room['rows'] if r['boundary']=='padding')
        self.assertEqual((padding['x'], padding['y']), (1, 1))
        self.assertTrue(any(r['kind']=='export_gap' and r['w']==65 for r in room['rows']))
        self.assertEqual(index['pack_fingerprint'], view.fingerprint(self.pack.read_bytes()))

    def test_related_acceptance_never_approves_a_placement(self):
        self.review('model:model-part')
        _, _, room = self.export()
        own = next(r for r in room['rows'] if r['id']==self.instance)
        self.assertEqual((own['visual'], own['live'], own['headset']), ('unreviewed',)*3)
        self.assertIn('Related model:model-part / visual: accepted', own['notes'])
        self.assertEqual(own['flags'] & 8, 0)

    def test_independent_stages_and_related_rejection(self):
        self.review(self.instance)
        self.review(self.instance, 'live', 'blocked')
        self.review('model:model-part', result='rejected')
        _, _, room = self.export()
        own = next(r for r in room['rows'] if r['id']==self.instance)
        self.assertEqual((own['visual'], own['live'], own['headset']), ('accepted', 'blocked', 'unreviewed'))
        self.assertTrue(own['flags'] & 8)
        self.assertFalse(own['flags'] & 4)
        self.assertIn('Related model:model-part / visual: rejected', own['notes'])

    def test_stale_review_stays_unreviewed_and_retains_failed_evidence(self):
        self.review(self.instance)
        self.review(self.instance, 'disposition', 'intentional_flat')
        self.review('model:model-part', result='rejected')
        with self.db:
            self.db.execute("UPDATE reviews SET invalidated='source changed'")
        _, _, room = self.export()
        own = next(r for r in room['rows'] if r['id']==self.instance)
        self.assertEqual((own['visual'], own['disposition']), ('stale', 'unresolved'))
        self.assertEqual(own['flags'] & 14, 14)
        self.assertIn('Previous result: rejected; source changed', own['notes'])

    def test_first_member_anchor_and_partial_coverage(self):
        path = self.root/'proposals/family-building.json'
        p = json.loads(path.read_text());p['patterns'][0]['mask']=[0,1]
        path.write_text(json.dumps(p))
        _, _, room = self.export()
        r = next(r for r in room['rows'] if r['kind']=='placement' and r['category']=='structure')
        self.assertEqual((r['anchor_x'], r['anchor_y']), (8, 7))
        self.assertEqual(r['implementation'], 'partial')
        self.assertTrue(r['flags'] & 1)

    def test_failed_export_preserves_existing_index(self):
        self.export();before = self.output.read_bytes()
        self.pack.write_text(self.pack.read_text()+' ')
        with self.assertRaisesRegex(ValueError, 'Pack differs'):
            self.export()
        self.assertEqual(self.output.read_bytes(), before)

    def test_proposal_traversal_and_bad_footprint_rejected(self):
        with self.db:
            self.db.execute("UPDATE entries SET detail=? WHERE id='family:family-building'", (json.dumps(dict(proposal='../outside.json')),))
        with self.assertRaisesRegex(ValueError, 'escapes catalog'):
            self.export()
        self.assertFalse(self.output.exists())
        with self.db:
            self.db.execute("UPDATE entries SET detail=? WHERE id='family:family-building'", (json.dumps(dict(proposal='proposals/family-building.json')),))
        path = self.root/'proposals/family-building.json'
        p = json.loads(path.read_text());p['patterns'][0]['mask']=[0,0]
        path.write_text(json.dumps(p))
        with self.assertRaisesRegex(ValueError, 'Invalid source footprint'):
            self.export()

    def test_output_cannot_overwrite_inputs_and_changed_catalog_is_rejected(self):
        output = self.output
        for path in (self.pack, self.db_path, self.root/'catalog.json'):
            before = path.read_bytes();self.output = path
            with self.assertRaisesRegex(ValueError, 'overwrite|outside'):
                self.export()
            self.assertEqual(path.read_bytes(), before)
        self.output = output
        self.export();before = self.output.read_bytes()
        path = self.root/'catalog.json';path.write_text(path.read_text()+' ')
        with self.assertRaisesRegex(ValueError, 'Catalog differs'):
            self.export()
        self.assertEqual(self.output.read_bytes(), before)

    def test_native_reader_filters_and_malformed_data(self):
        executable = os.environ.get('RUBYVR_REVIEW_BATCH')
        if not executable:
            self.skipTest('Set RUBYVR_REVIEW_BATCH to run the native reader after building')
        _, path, room = self.export()

        def run():
            return subprocess.run([executable, '--check-review-index', str(self.output)], capture_output=True,
                                  text=True, timeout=20, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))

        result = run();self.assertEqual(result.returncode, 0, result.stderr)
        result = json.loads(result.stdout)
        self.assertEqual(result['rows'], 6)
        self.assertEqual(result['default_visible'], 3)  # source group, model, oversized terrain gap
        # Partial structure + two flat cells lack models; the terrain gap is
        # separately blocked, so it appears under Failed rather than No model.
        self.assertEqual(result['filters'], [6, 3, 4, 6, 1])
        room['rows'][1]['id'] = room['rows'][0]['id']
        path.write_text(json.dumps(room))
        result = run();self.assertNotEqual(result.returncode, 0)
        self.assertIn('duplicate identity', result.stderr)
        self.export()
        index = json.loads(self.output.read_text());index['directory']='../outside'
        self.output.write_text(json.dumps(index))
        result = run();self.assertNotEqual(result.returncode, 0)
        self.assertIn('snapshot directory', result.stderr)


if __name__ == '__main__':
    unittest.main()
