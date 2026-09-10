"""Original source-adapter fixtures; no game assets, build, GL or network."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

from dynamic_inventory import inventory, integer, verify_source

spec = importlib.util.spec_from_file_location("ledger", Path(__file__).with_name("coverage-ledger.py"))
ledger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ledger)


def write(root, path, value):
    destination = root / path
    destination.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(value, dict): value = json.dumps(value, indent=2)
    destination.write_text(value, encoding="utf-8")


def fixture(root):
    constants = {
        "event_objects.h": "#define OBJ_EVENT_GFX_SAMPLE 5\n#define OBJ_EVENT_GFX_ALIAS OBJ_EVENT_GFX_SAMPLE\n#define OBJ_EVENT_GFX_VAR_0 240\n#define MOVEMENT_TYPE_IDLE 0\n",
        "field_effects.h": "#define FLDEFF_SAMPLE 0\n#define FLDEFF_ALIAS FLDEFF_SAMPLE\n",
        "weather.h": "#define WEATHER_CLEAR 2\n#define WEATHER_MIST 9\n#define COORD_EVENT_WEATHER_MIST 7\n",
        "map_types.h": "#define MAP_TYPE_ROUTE 3\n#define MAP_TYPE_6 6\n",
        "metatile_behaviors.h": "#define MB_SAMPLE 1\n",
        "fixture.h": "#define TILE_SAMPLE 123\n#define MAP_DYNAMIC (127 | (127 << 8))\n",
    }
    for name, content in constants.items(): write(root, "include/constants/" + name, content)
    macros = ["map_script", "call", "return", "end", "setmetatile", "setweather", "setvar", "applymovement", "walk_up", "movement_end", "special"]
    write(root, "include/macros/event.inc", "\n".join(f".macro {op}\n.endm" for op in macros))
    for name in ("A", "B"):
        row = dict(id="MAP_SAMPLE_" + name, name="Sample" + name, layout="LAYOUT_SAMPLE", map_type="MAP_TYPE_ROUTE",
                   weather="WEATHER_CLEAR", connections=[], object_events=[], warp_events=[], coord_events=[], bg_events=[])
        if name == "A":
            row.update(map_type="MAP_TYPE_6", connections=[dict(direction="right", offset=-2, map="MAP_SAMPLE_B")],
                       object_events=[dict(graphics_id="OBJ_EVENT_GFX_VAR_0", x=2, y=4, elevation=13,
                                           movement_type="MOVEMENT_TYPE_IDLE", script="SampleA_Enter", flag="FLAG_SAMPLE")],
                       warp_events=[dict(x=1, y=2, elevation=3, dest_map="MAP_SAMPLE_B", dest_warp_id="0"),
                                    dict(x=3, y=2, elevation=3, dest_map="MAP_DYNAMIC", dest_warp_id="255")],
                       coord_events=[dict(type="weather", x=4, y=5, weather="COORD_EVENT_WEATHER_MIST")])
        else:
            row["warp_events"] = [dict(x=0, y=0, dest_map="MAP_SAMPLE_A", dest_warp_id="0")]
        write(root, f"data/maps/Sample{name}/map.json", row)
        write(root, f"data/maps/Sample{name}/scripts.inc", f"Sample{name}_Enter::\n\tcall Shared_Update\n\tend\n")
    a = root / "data/maps/SampleA/scripts.inc"
    a.write_text(a.read_text() + """
SampleA_Changes::
    setmetatile 2, 3, TILE_SAMPLE, 1
#if VERSION_VARIANT
    setweather WEATHER_MIST
#else
    setweather WEATHER_CLEAR
#endif
    applymovement 1, SampleA_Move
    mystery_command 4
    end
SampleA_Move::
    walk_up
    movement_end
""")
    write(root, "data/scripts/shared.inc", "Shared_Update::\n setweather WEATHER_MIST\n return\n")
    write(root, "data/layouts/layouts.json", dict(layouts=[dict(id="LAYOUT_SAMPLE", width=12, height=10,
          primary_tileset="gTileset_Sample", secondary_tileset="gTileset_Sample")]))
    write(root, "data/tilesets/headers.inc", "gTileset_Sample::\n .4byte Tiles\n .4byte Palettes\n .4byte Meta\n .4byte Attr\n .4byte TilesetCB_Sample\n")
    write(root, "src/tileset_anim.c", "void TilesetCB_Sample(void) { AnimateSample(); }\n")
    write(root, "src/data/object_events/sample.h", """
const union AnimCmd SampleWalk[] = {
    ANIMCMD_FRAME(2, 7),
    ANIMCMD_JUMP(0)
};
const struct ObjectEventGraphicsInfo SampleGraphic = { .width = 12, .height = 20 };
""")
    write(root, "src/sample.c", """
void MapGridSetMetatileIdAt(int x, int y, int tile);
void ExampleChange(void)
{
    // MapGridSetMetatileIdAt(9, 9, 9);
    const char *ignored = "SetWeather(99)";
    MapGridSetMetatileIdAt(1 + offset, 4, tile);
}
""")
    # Fingerprinted byte fixtures, deliberately not renderable game images.
    write(root, "data/tilesets/primary/sample/anim/0.png", "original synthetic frame bytes")
    write(root, "graphics/object_events/sample.png", "original synthetic actor bytes")


class DynamicInventoryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        fixture(self.root)
        self.data = inventory(self.root, ["MAP_SAMPLE_A", "MAP_SAMPLE_B"])
        self.records = {r["id"]: r for r in self.data["records"]}

    def tearDown(self):
        self.temp.cleanup()

    def change_map(self, name, change):
        path = self.root / f"data/maps/Sample{name}/map.json"
        value = json.loads(path.read_text())
        change(value)
        path.write_text(json.dumps(value))

    def test_exact_events_keep_layout_coordinates_and_dynamic_warps(self):
        r = self.records["state:object_event:MAP_SAMPLE_A:0"]
        self.assertEqual((r["x"], r["y"]), (2, 4))
        self.assertEqual(r["detail"]["data"]["elevation"], 13)
        self.assertTrue(r["detail"]["elevation_is_gameplay_layer"])
        self.assertEqual(r["detail"]["local_object_id"], 1)
        self.assertEqual(r["detail"]["graphics_resolution"], "runtime_variable")
        self.assertEqual(self.records["state:warp:MAP_SAMPLE_A:0"]["detail"]["target_event"], "state:warp:MAP_SAMPLE_B:0")
        dynamic = self.records["state:warp:MAP_SAMPLE_A:1"]["detail"]
        self.assertEqual(dynamic["source_status"], "runtime_resolution")
        self.assertNotIn("target_event", dynamic)
        self.assertIn("ignored", dynamic["target_resolution"])

    def test_aliases_variables_and_weather_namespaces_stay_distinct(self):
        alias = self.records["state:definition:OBJ_EVENT_GFX_ALIAS:0"]["detail"]
        self.assertEqual(alias["integer"], 5)
        self.assertTrue(alias["alias"])
        self.assertEqual(self.records["state:definition:WEATHER_MIST:0"]["detail"]["integer"], 9)
        self.assertEqual(self.records["state:definition:COORD_EVENT_WEATHER_MIST:0"]["detail"]["integer"], 7)
        event = self.records["state:coordinate_event:MAP_SAMPLE_A:0"]["detail"]
        self.assertIn("not interchangeable", event["weather_namespace"])
        self.assertIsNone(integer("__import__('os').system('unsafe')", {}))
        self.assertIsNone(integer("UNKNOWN + 1", {}))

    def test_conditional_branches_unknown_commands_and_tile_arguments_retained(self):
        rows = [r for r in self.data["records"] if r["category"] == "script_weather_time" and r["detail"]["label"] == "SampleA_Changes"]
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0]["detail"]["conditions"], rows[1]["detail"]["conditions"])
        tile = next(r for r in self.data["records"] if r["category"] == "script_tile_change")
        self.assertEqual(tile["detail"]["argument_values"], [2, 3, 123, 1])
        self.assertEqual(self.data["unknown_commands"], {"mystery_command": 1})
        self.assertEqual(self.data["counts"]["unclassified_script_command"], 1)

    def test_shared_helpers_are_visible_in_each_referencing_map(self):
        shared = next(r for r in self.data["records"] if r["category"] == "script_block" and r["label"] == "Shared_Update")
        self.assertIsNone(shared["map_id"])
        self.assertEqual(shared["detail"]["related_maps"], ["MAP_SAMPLE_A", "MAP_SAMPLE_B"])
        db = ledger.open_db(self.root / "test.sqlite")
        try:
            ledger.sync_rows(db, ledger.dynamic_entries(self.data), {})
            report = ledger.report(db, "MAP_SAMPLE_B", "script_block")
            self.assertIn(shared["id"], {r["id"] for r in report["entries"]})
            self.assertNotIn("state:object_event:MAP_SAMPLE_A:0", {r["id"] for r in report["entries"]})
            self.assertEqual(report["review_states"]["live"], {"unreviewed": len(report["entries"])})
            self.assertIn("Source state inventory", ledger.markdown(report))
        finally:
            db.close()

    def test_unresolved_warp_and_script_targets_are_not_dropped(self):
        self.change_map("A", lambda m: m["warp_events"][0].update(dest_map="MAP_MISSING"))
        self.change_map("A", lambda m: m["object_events"][0].update(graphics_id="OBJ_EVENT_GFX_SAMPLE", script="Missing_Script"))
        rows = {r["id"]: r for r in inventory(self.root)["records"]}
        self.assertEqual(rows["state:warp:MAP_SAMPLE_A:0"]["detail"]["source_status"], "unresolved_reference")
        self.assertEqual(rows["state:object_event:MAP_SAMPLE_A:0"]["detail"]["script"]["resolution"], "unresolved_reference")
        self.change_map("A", lambda m: m["warp_events"][0].update(dest_map="MAP_SAMPLE_B", dest_warp_id="88"))
        rows = {r["id"]: r for r in inventory(self.root)["records"]}
        self.assertEqual(rows["state:warp:MAP_SAMPLE_A:0"]["detail"]["target_resolution"], "unresolved_warp_index")

    def test_source_and_adapter_changes_invalidate_reviews_without_approving_anything(self):
        db = ledger.open_db(self.root / "test.sqlite")
        id = "state:object_event:MAP_SAMPLE_A:0"
        try:
            rows = ledger.dynamic_entries(self.data)
            ledger.sync_rows(db, rows, {})
            self.assertTrue(all(r["disposition"] == "unresolved" for r in rows))
            ledger.review(db, id, "visual", "accepted", "original-test-evidence", "Synthetic retention check")
            ledger.sync_rows(db, ledger.dynamic_entries(inventory(self.root)), {})
            self.assertEqual(db.execute("SELECT invalidated FROM reviews").fetchone()[0], "")
            self.change_map("B", lambda m: m.update(weather="WEATHER_MIST"))
            ledger.sync_rows(db, ledger.dynamic_entries(inventory(self.root)), {})
            self.assertEqual(db.execute("SELECT invalidated FROM reviews").fetchone()[0], "")
            self.change_map("A", lambda m: m["object_events"][0].update(x=8))
            ledger.sync_rows(db, ledger.dynamic_entries(inventory(self.root)), {})
            self.assertIn("source dependency", db.execute("SELECT invalidated FROM reviews").fetchone()[0])
            self.assertEqual(db.execute("SELECT result FROM reviews").fetchone()[0], "accepted")
            self.assertEqual(db.execute("SELECT count(*) FROM reviews WHERE stage='live'").fetchone()[0], 0)
        finally:
            db.close()

    def test_shared_script_changes_invalidate_callers(self):
        before = self.records["state:object_event:MAP_SAMPLE_A:0"]["source_hash"]
        write(self.root, "data/scripts/shared.inc", "Shared_Update::\n setweather WEATHER_CLEAR\n return\n")
        after = {r["id"]: r for r in inventory(self.root)["records"]}
        self.assertNotEqual(before, after["state:object_event:MAP_SAMPLE_A:0"]["source_hash"])
        self.assertNotEqual(self.records["state:map_environment:MAP_SAMPLE_A"]["source_hash"],
                            after["state:map_environment:MAP_SAMPLE_A"]["source_hash"])

    def test_assembly_conditions_are_recorded_without_selecting_a_version(self):
        write(self.root, "data/scripts/shared.inc", """Shared_Update::
.ifdef VARIANT_B
 setweather WEATHER_MIST
.else
 setweather WEATHER_CLEAR
.endif
 return
""")
        data = inventory(self.root)
        rows = [r for r in data["records"] if r["category"] == "script_weather_time" and r["detail"]["label"] == "Shared_Update"]
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["detail"]["conditions"], [[".ifdef VARIANT_B"]])
        self.assertEqual(rows[1]["detail"]["conditions"], [[".ifdef VARIANT_B", ".else"]])
        renderer_a = {r["id"]: r for r in ledger.dynamic_entries(data, "renderer-a")}
        renderer_b = {r["id"]: r for r in ledger.dynamic_entries(data, "renderer-b")}
        self.assertNotEqual(renderer_a[rows[0]["id"]]["recipe_hash"], renderer_b[rows[0]["id"]]["recipe_hash"])

    def test_generated_movements_alias_labels_null_pointers_and_legacy_comments(self):
        write(self.root, "include/macros/movement.inc", ".macro create_movement_action name\n.endm\ncreate_movement_action sample_step\n")
        write(self.root, "data/scripts/shared.inc", "Shared_Update::\nShared_Alias::\n sample_step\n return\n")
        self.change_map("A", lambda m: m["object_events"][0].update(script="0x0"))
        write(self.root, "data/tilesets/headers.inc", "gTileset_Sample::\n .4byte Tiles\n .4byte Palettes\n .4byte Meta\n .4byte Attr\n .4byte NULL\n")
        path = self.root / "src/sample.c"
        path.write_bytes(b"// Original fixture: caf\xe9\n" + path.read_bytes())
        data = inventory(self.root)
        rows = {r["id"]: r for r in data["records"]}
        self.assertEqual(data["counts"]["movement_command"], 1)
        self.assertNotIn("sample_step", data["unknown_commands"])
        alias = next(r for r in data["records"] if r["category"] == "script_block" and r["label"] == "Shared_Update")
        self.assertEqual(alias["detail"]["alias"], "Shared_Alias")
        self.assertEqual(rows["state:object_event:MAP_SAMPLE_A:0"]["detail"]["script"]["resolution"], "none")
        self.assertEqual(rows["state:tileset_callback:gTileset_Sample"]["detail"]["source_status"], "no_callback")
        self.assertEqual(data["legacy_text_encodings"], ["src/sample.c"])

    def test_dynamic_removal_and_parser_changes_preserve_static_history(self):
        db = ledger.open_db(self.root / "test.sqlite")
        id = "state:object_event:MAP_SAMPLE_A:0"
        static = ledger.entry("model:original-fixture", "model", "Original fixture", "same-source", "same-recipe")
        try:
            ledger.sync_rows(db, [static, *ledger.dynamic_entries(self.data)], {})
            ledger.review(db, static["id"], "visual", "rejected", "fixture-image", "Original static rejection")
            ledger.review(db, id, "live", "accepted", "fixture-replay", "Original dynamic evidence")
            changed = copy.deepcopy(self.data)
            changed["adapter_sha256"] = "different-adapter"
            ledger.sync_rows(db, [static, *ledger.dynamic_entries(changed)], {})
            self.assertEqual(db.execute("SELECT invalidated FROM reviews WHERE entry_id=?", (static["id"],)).fetchone()[0], "")
            self.assertIn("renderer dependency", db.execute("SELECT invalidated FROM reviews WHERE entry_id=?", (id,)).fetchone()[0])
            changed["records"] = [r for r in changed["records"] if r["id"] != id]
            ledger.sync_rows(db, [static, *ledger.dynamic_entries(changed)], {})
            self.assertEqual(db.execute("SELECT present FROM entries WHERE id=?", (id,)).fetchone()[0], 0)
            self.assertEqual(db.execute("SELECT count(*) FROM reviews").fetchone()[0], 2)
        finally:
            db.close()

    def test_native_calls_exclude_comments_strings_and_prototypes(self):
        native = [r for r in self.data["records"] if r["category"] == "native_state_reference"]
        self.assertEqual(len(native), 1)
        self.assertEqual(native[0]["detail"]["function"], "ExampleChange")
        self.assertEqual(native[0]["detail"]["arguments"], "1 + offset, 4, tile")
        animation = next(r for r in self.data["records"] if r["category"] == "actor_animation")
        self.assertIn("ANIMCMD_FRAME(2, 7)", animation["detail"]["initializer"])
        self.assertIn("ANIMCMD_JUMP(0)", animation["detail"]["initializer"])
        callback = self.records["state:tileset_callback:gTileset_Sample"]["detail"]
        self.assertEqual(callback["callback"], "TilesetCB_Sample")
        self.assertTrue(callback["callback_found"])

    def test_duplicate_missing_and_changing_inputs_fail_before_sync(self):
        self.assertEqual(self.data, inventory(self.root))
        with self.assertRaisesRegex(ValueError, "map set"):
            inventory(self.root, ["MAP_SAMPLE_A"])
        write(self.root, "data/maps/Duplicate/map.json", json.loads((self.root / "data/maps/SampleA/map.json").read_text()))
        with self.assertRaisesRegex(ValueError, "Duplicate map"):
            inventory(self.root)
        with self.assertRaisesRegex(ValueError, "changed"):
            verify_source(self.root, self.data)
        bad = copy.deepcopy(self.data)
        bad["records"].append(bad["records"][0])
        with self.assertRaisesRegex(ValueError, "Duplicate dynamic"):
            ledger.dynamic_entries(bad)

    def test_malformed_event_collection_is_not_treated_as_empty(self):
        self.change_map("A", lambda m: m.update(object_events={}))
        with self.assertRaisesRegex(ValueError, "Invalid object_events"):
            inventory(self.root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
