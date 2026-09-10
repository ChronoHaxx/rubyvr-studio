"""Original synthetic M1 fixture: no game art, source checkout, GL or network."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("ledger", Path(__file__).with_name("coverage-ledger.py"))
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)


def pattern(id, width=1, x=7, y=7):
    return dict(id=id, name=id, source=dict(room="MAP_SYNTHETIC", x=x, y=y),
                w=width, extent=1, ids=[1]*width, mask=[1]*width,
                tiles={"1": dict(entries=[0]*8, attr=0)}, parts=[])


def fixture(root):
    (root / "proposals").mkdir()
    (root / "art").mkdir()
    p = pattern("family-building", width=2)
    flat = pattern("family-floor", x=10)
    families = []
    for source, kind, positions in ((p, "structure", [[7, 7]]), (flat, "flat", [[10, 7], [11, 7]])):
        id = source["id"]
        (root / "proposals" / (id + ".json")).write_text(json.dumps(dict(patterns=[source])))
        # The ledger hashes source bytes; this original stand-in deliberately
        # does not contain or pretend to render an extracted game sprite.
        (root / "art" / (id + ".fixture")).write_bytes(b"original synthetic source bytes")
        families.append(dict(id=id, kind=kind, primary="Synthetic", secondary="Fixture", width=source["w"], height=1,
                             proposal=f"proposals/{id}.json", art=f"art/{id}.fixture",
                             occurrences=[dict(map="MAP_SYNTHETIC", x=positions[0][0], y=7, count=len(positions),
                                               crosses_layout_boundary=False, positions=positions)]))
    fragments = []
    for name, w, x in (("left", 32, 20), ("right", 33, 52)):
        path = f"proposals/fragment-{name}.json"
        (root / path).write_text(json.dumps(dict(patterns=[pattern(name, width=w, x=x, y=20)])))
        fragments.append(path)
    catalog = dict(version=2, map_count=1, asset_count=2, failed_maps=0, unexportable_proposals=1,
                   maps=[dict(id="MAP_SYNTHETIC", type="test", primary="Synthetic", secondary="Fixture", width=100,
                              height=40, objects=1, flat_cells=2, object_events=0, error="")], assets=families,
                   unexportable_details=[dict(map="MAP_SYNTHETIC", kind="mass", reason="Oversized fixture", x=20, y=20,
                                              width=65, height=1, member_cells=65, fragments_complete=True, fragments=fragments)])
    (root / "catalog.json").write_text(json.dumps(catalog))
    model = pattern("model-part")
    model["parts"] = [dict(kind="box", size=[1, 1, 1])]
    audit = dict(maps_checked=1, assets=[dict(id=model["id"], source_matches=True, rejected=[], accepted=[
        dict(map="MAP_SYNTHETIC", x=7, y=7, crosses_layout_boundary=False),
        dict(map="MAP_SYNTHETIC", x=1, y=1, crosses_layout_boundary=True)])])
    return catalog, dict(patterns=[model]), audit, dict(files={"graphics/tilesets/synthetic.fixture": "synthetic-v1"})


class CoverageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.catalog, self.pack, self.audit, self.receipt = fixture(self.root)
        self.db = ledger.open_db(self.root / "ledger.sqlite")
        self.rows = self.build()
        ledger.sync_rows(self.db, self.rows, {"source_version": "synthetic"})

    def tearDown(self):
        self.db.close()
        self.temp.cleanup()

    def build(self):
        return ledger.build_entries(self.root, self.pack, self.audit, self.receipt)

    def accepted(self, id="family:family-building", stage="visual", result="accepted"):
        ledger.review(self.db, id, stage, result, "synthetic://review-evidence", "Original fixture review")

    def state(self, id="family:family-building"):
        return next(r for r in ledger.report(self.db)["entries"] if r["id"] == id)

    def test_exact_flat_coordinates_and_partial_coverage(self):
        rows = {r["id"]: r for r in self.rows}
        self.assertIn("placement:MAP_SYNTHETIC:11:7:family-floor", rows)
        self.assertEqual(rows["family:family-building"]["implementation"], "partial")
        self.assertEqual(rows["family:family-floor"]["implementation"], "unmodeled")
        self.assertEqual(sum(r["kind"] == "instance" and r["boundary"] == "padding" for r in self.rows), 1)
        gap = next(r for r in self.rows if r["kind"] == "export_gap")
        self.assertEqual(len(json.loads(gap["detail"])["fragments"]), 2)
        self.assertEqual(sum(r["kind"] == "pending" for r in self.rows), 5)

    def test_unchanged_sync_preserves_reviews_and_is_deterministic(self):
        self.accepted()
        before = ledger.report(self.db)
        written = self.db.total_changes
        changes = ledger.sync_rows(self.db, self.build(), {})
        self.assertEqual(changes, {"unchanged": len(self.rows)})
        self.assertEqual(self.db.total_changes, written)
        self.assertEqual(before, ledger.report(self.db))
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "accepted")
        self.assertEqual(self.state()["reviews"]["live"]["status"], "unreviewed")
        self.assertEqual(self.state()["reviews"]["headset"]["status"], "unreviewed")

    def test_recipe_change_invalidates_visual_not_disposition(self):
        self.accepted()
        self.accepted(stage="disposition", result="model")
        self.pack["patterns"][0]["parts"][0]["size"][1] = 2
        ledger.sync_rows(self.db, self.build(), {})
        state = self.state()
        self.assertEqual(state["reviews"]["visual"]["status"], "stale")
        self.assertIn("recipe or renderer dependency changed", state["reviews"]["visual"]["reason"])
        self.assertEqual(state["disposition"], "model")
        self.assertEqual(self.db.execute("SELECT result FROM reviews WHERE stage='visual'").fetchone()[0], "accepted")

    def test_source_change_invalidates_without_losing_history(self):
        self.accepted()
        self.accepted(stage="headset")
        self.accepted(stage="disposition", result="model")
        (self.root / "art/family-building.fixture").write_bytes(b"changed source")
        ledger.sync_rows(self.db, self.build(), {})
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "stale")
        self.assertEqual(self.state()["reviews"]["headset"]["status"], "stale")
        self.assertEqual(self.state()["disposition"], "unresolved")
        self.assertEqual(self.db.execute("SELECT count(*) FROM reviews").fetchone()[0], 3)

    def test_removal_and_return_require_review_again(self):
        self.accepted()
        defect = dict(id="synthetic-defect", entry_id="family:family-building", milestone="M4",
                      title="Original fixture defect", certainty="observed")
        remaining = [r for r in self.rows if r["id"] != "family:family-building"]
        ledger.sync_rows(self.db, remaining, {}, [defect])
        self.assertEqual(ledger.report(self.db)["removed_entries"], 1)
        self.assertEqual(len(ledger.report(self.db)["defects"]), 1)
        ledger.sync_rows(self.db, self.rows, {})
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "stale")

    def test_duplicate_entry_leaves_previous_ledger_intact(self):
        self.accepted()
        before = ledger.report(self.db)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            ledger.sync_rows(self.db, self.rows + [self.rows[0]], {})
        self.assertEqual(before, ledger.report(self.db))

    def test_duplicate_catalog_family_and_placement_rejected(self):
        self.catalog["assets"].append(copy.deepcopy(self.catalog["assets"][0]))
        (self.root / "catalog.json").write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, "Duplicate family"):
            self.build()
        self.catalog["assets"].pop()
        self.catalog["assets"][0]["occurrences"] *= 2
        (self.root / "catalog.json").write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, "Duplicate source placement"):
            self.build()

    def test_different_ids_cannot_duplicate_the_same_source_placement(self):
        duplicate = copy.deepcopy(self.catalog["assets"][0])
        duplicate["id"] = "renamed-duplicate"
        self.catalog["assets"].append(duplicate)
        self.catalog["asset_count"] += 1
        (self.root / "catalog.json").write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, "Duplicate source placement"):
            self.build()

    def test_missing_coordinates_are_not_fabricated(self):
        self.catalog["assets"][1]["occurrences"][0].pop("positions")
        (self.root / "catalog.json").write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, "Missing exact placement"):
            self.build()

    def test_changed_recipe_does_not_invalidate_unrelated_floor(self):
        self.accepted("family:family-floor")
        self.pack["patterns"][0]["parts"][0]["size"][1] = 4
        ledger.sync_rows(self.db, self.build(), {})
        self.assertEqual(self.state("family:family-floor")["reviews"]["visual"]["status"], "accepted")

    def test_renderer_change_invalidates_model_review(self):
        self.accepted()
        self.receipt["batch_sha256"] = "new synthetic renderer"
        ledger.sync_rows(self.db, self.build(), {})
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "stale")

    def test_overlap_and_incomplete_fragments_rejected(self):
        self.audit["assets"][0]["accepted"].append(copy.deepcopy(self.audit["assets"][0]["accepted"][0]))
        with self.assertRaisesRegex(ValueError, "overlap|Duplicate"):
            self.build()
        self.audit["assets"][0]["accepted"].pop()
        self.catalog["unexportable_details"][0]["member_cells"] += 1
        (self.root / "catalog.json").write_text(json.dumps(self.catalog))
        with self.assertRaisesRegex(ValueError, "lose source cells"):
            self.build()

    def test_disposition_and_review_require_explicit_evidence(self):
        with self.assertRaises(ValueError):
            ledger.review(self.db, "family:family-building", "visual", "accepted", "", "")
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "unreviewed")
        self.accepted(stage="visual", result="rejected")
        self.assertEqual(self.state()["reviews"]["visual"]["status"], "rejected")
        self.assertEqual(self.state()["implementation"], "partial")


if __name__ == "__main__":
    unittest.main(verbosity=2)
