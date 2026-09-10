"""M1 coverage ledger. Sync local source states, query work and retain reviews.

Model matching is implementation evidence, never visual/live/headset approval.
The SQLite database and generated reports stay local; only original fixtures,
review tooling and asset-free defect descriptions belong in the repository.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys

from coverage_source import digest, file_hash, source_receipt
from dynamic_inventory import inventory as inventory_dynamics, verify_source as verify_dynamic_source
import coverage_review
import coverage_disposition
import native_trace

ROOT = Path(__file__).resolve().parents[1]
STAGES = ("visual", "live", "headset", "disposition")
DISPOSITIONS = coverage_disposition.DISPOSITIONS
RESULTS = ("accepted", "rejected", "blocked")


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def local_file(root, relative):
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError(f"Catalog path escapes its directory: {relative}")
    return path


def unique(rows, key, label):
    indexed = {}
    for row in rows:
        value = row[key]
        if value in indexed:
            raise ValueError(f"Duplicate {label}: {value}")
        indexed[value] = row
    return indexed


def open_db(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    db.row_factory = sqlite3.Row
    version = db.execute("PRAGMA user_version").fetchone()[0]
    if version not in (0, 1):
        raise ValueError(f"Unsupported coverage ledger version {version}")
    db.executescript("""
        CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS entries(
            id TEXT PRIMARY KEY, kind TEXT NOT NULL, label TEXT NOT NULL,
            map_id TEXT, family_id TEXT, category TEXT, boundary TEXT,
            x INTEGER, y INTEGER, source_hash TEXT NOT NULL, recipe_hash TEXT NOT NULL,
            implementation TEXT NOT NULL, disposition TEXT NOT NULL,
            detail TEXT NOT NULL, present INTEGER NOT NULL DEFAULT 1);
        CREATE INDEX IF NOT EXISTS entries_map ON entries(map_id, kind, present);
        CREATE TABLE IF NOT EXISTS reviews(
            id INTEGER PRIMARY KEY, entry_id TEXT NOT NULL, stage TEXT NOT NULL,
            result TEXT NOT NULL, evidence TEXT NOT NULL, note TEXT NOT NULL,
            source_hash TEXT NOT NULL, recipe_hash TEXT NOT NULL,
            invalidated TEXT NOT NULL DEFAULT '');
        CREATE INDEX IF NOT EXISTS reviews_entry ON reviews(entry_id, stage, id);
        CREATE TABLE IF NOT EXISTS defects(id TEXT PRIMARY KEY, entry_id TEXT NOT NULL,
            milestone TEXT NOT NULL, title TEXT NOT NULL, certainty TEXT NOT NULL,
            status TEXT NOT NULL, detail TEXT NOT NULL);
        PRAGMA user_version=1;
    """)
    return db


def entry(id, kind, label, source, recipe=None, **fields):
    defaults = dict(map_id=None, family_id=None, category=None, boundary=None, x=None, y=None,
                    implementation="unmodeled", disposition="unresolved", detail={})
    defaults.update(fields)
    defaults["detail"] = json.dumps(defaults["detail"], sort_keys=True)
    return dict(id=id, kind=kind, label=label, source_hash=digest(source),
                recipe_hash=digest(recipe), **defaults)


def member_cells(p, x, y):
    return {(x + i % p["w"], y + i // p["w"]) for i, value in enumerate(p["mask"]) if value}


def normalized(p):
    return {k: v for k, v in p.items() if k not in ("id", "name", "source", "model_seeded")}


def aggregate(values):
    values = set(values)
    if not values or values == {"unmodeled"}:
        return "unmodeled"
    return values.pop() if len(values) == 1 else "partial"


def dynamic_entries(manifest, renderer_sha256=None):
    if manifest.get("version") != 1:
        raise ValueError("Unsupported dynamic inventory version")
    maps = set(manifest["map_ids"])
    rows = []
    for record in manifest["records"]:
        if ((record["map_id"] and record["map_id"] not in maps)
                or not set(record["detail"].get("related_maps", ())).issubset(maps)):
            raise ValueError("Dynamic record references an unknown source map")
        rows.append(entry(record["id"], "state", record["label"], record["source_hash"],
                          dict(inventory_adapter=manifest["adapter_sha256"], renderer_sha256=renderer_sha256),
                          category=record["category"], map_id=record["map_id"],
                          x=record["x"], y=record["y"],
                          implementation="pending_runtime", detail=record["detail"]))
    unique(rows, "id", "dynamic state")
    return rows


def build_entries(catalog_dir, pack, audit, receipt, dynamic=None):
    catalog_dir = Path(catalog_dir)
    catalog = read(catalog_dir / "catalog.json")
    if catalog.get("version") != 2:
        raise ValueError("Regenerate catalog v2; v1 omitted individual flat placement coordinates")
    maps = unique(catalog["maps"], "id", "map ID")
    families = unique(catalog["assets"], "id", "family ID")
    models = unique(pack["patterns"], "id", "model ID")
    checked = unique(audit["assets"], "id", "audit model ID")
    if len(maps) != catalog["map_count"] or len(families) != catalog["asset_count"]:
        raise ValueError("Catalog counts disagree with its entries")
    if set(checked) != set(models) or audit["maps_checked"] != len(maps):
        raise ValueError("Audit does not cover this pack and map inventory")
    # Raw indexed graphics/palettes are also dependencies: identical displayed
    # RGB alone cannot prove source identity. This intentionally invalidates
    # conservatively across families when the shared tileset inputs change.
    graphics = {k: v for k, v in receipt["files"].items()
                if k.startswith(("data/tilesets/", "graphics/tilesets/", "include/constants/"))
                or k == "src/data/graphics.c"}
    graphics_hash = digest(graphics)
    proposals, source_hashes = {}, {}
    map_sources = defaultdict(list)
    for id, family in sorted(families.items()):
        p = read(local_file(catalog_dir, family["proposal"]))["patterns"][0]
        if len(p["mask"]) != p["w"] * p["extent"] or not any(p["mask"]):
            raise ValueError(f"Invalid source membership: {id}")
        proposals[id] = p
        source_hashes[id] = digest([normalized(p), file_hash(local_file(catalog_dir, family["art"])),
                                   family["kind"], graphics_hash])
        for place in family["occurrences"]:
            if place["map"] not in maps:
                raise ValueError(f"Unknown placement map: {place['map']}")
            map_sources[place["map"]].append([id, source_hashes[id], place])
    map_hashes = {id: digest([row, sorted(map_sources[id], key=lambda x: json.dumps(x, sort_keys=True))])
                  for id, row in maps.items()}
    model_hashes = {id: digest([normalized(p), receipt.get("batch_sha256")]) for id, p in models.items()}
    claims = defaultdict(dict)
    rows = []
    for id, p in sorted(models.items()):
        a = checked[id]
        floor = a.get("ground_only", False)
        rows.append(entry("model:" + id, "model", p["name"],
                          [graphics_hash, p.get("tiles"), map_hashes.get(p["source"]["room"])],
                          model_hashes[id], category="ground_mask" if floor else "model",
                          implementation="ground_only" if floor else "modeled",
                          disposition="intentional_flat" if floor else "model",
                          detail=dict(accepted=len(a["accepted"]), rejected=len(a["rejected"]),
                                      source_matches=a["source_matches"])))
        for accepted, places in ((True, a["accepted"]), (False, a["rejected"])):
            for m in places:
                if m["map"] not in maps:
                    raise ValueError(f"Audit references unknown map: {m['map']}")
                key = f"instance:{m['map']}:{m['x']}:{m['y']}:{id}"
                rows.append(entry(key, "instance", p["name"], map_hashes[m["map"]], model_hashes[id],
                                  map_id=m["map"], x=m["x"], y=m["y"],
                                  boundary="padding" if m["crosses_layout_boundary"] else "map_body",
                                  category="ground_mask" if floor else "model",
                                  implementation=("ground_only" if floor else "modeled") if accepted else "conflict",
                                  disposition="intentional_flat" if floor else "model", detail=dict(model=id)))
                if accepted:
                    for cell in member_cells(p, m["x"], m["y"]):
                        if cell in claims[m["map"]]:
                            raise ValueError(f"Audit claims overlap: {m['map']} {cell}")
                        claims[m["map"]][cell] = (id, floor)
    family_implementations = defaultdict(list)
    family_models = defaultdict(set)
    map_implementations = defaultdict(list)
    map_models = defaultdict(set)
    source_cells = defaultdict(set)
    for id, family in sorted(families.items()):
        p = proposals[id]
        for occurrence in family["occurrences"]:
            positions = occurrence.get("positions", [[occurrence["x"], occurrence["y"]]])
            if len(positions) != occurrence["count"]:
                raise ValueError(f"Missing exact placement coordinates: {id}")
            for x, y in positions:
                map_id = occurrence["map"]
                cells = member_cells(p, x, y)
                if cells & source_cells[map_id]:
                    raise ValueError(f"Duplicate source placement membership: {map_id} {x},{y}")
                source_cells[map_id].update(cells)
                found = [claims[map_id][cell] for cell in cells if cell in claims[map_id]]
                matched = sorted({m for m, floor in found})
                if not found:
                    implementation = "unmodeled"
                elif len(found) != len(cells):
                    implementation = "partial"
                else:
                    implementation = "ground_only" if all(floor for m, floor in found) else "modeled"
                family_implementations[id].append(implementation)
                family_models[id].update(matched)
                map_implementations[map_id].append(implementation)
                map_models[map_id].update(matched)
                rows.append(entry(f"placement:{map_id}:{x}:{y}:{id}", "placement", id,
                                  [source_hashes[id], map_hashes[map_id]],
                                  [receipt.get("batch_sha256"), [[m, model_hashes[m]] for m in matched]], map_id=map_id, family_id=id,
                                  category=family["kind"], x=x, y=y,
                                  boundary="crosses_boundary" if occurrence["crosses_layout_boundary"] else "map_body",
                                  implementation=implementation,
                                  detail=dict(member_cells=len(cells), claimed_cells=len(found), models=matched,
                                              art=family["art"], proposal=family["proposal"])))
        rows.append(entry("family:" + id, "family", id, source_hashes[id],
                          [receipt.get("batch_sha256"), [[m, model_hashes[m]] for m in sorted(family_models[id])]], family_id=id,
                          category=family["kind"], implementation=aggregate(family_implementations[id]),
                          detail=dict(placements=len(family_implementations[id]),
                                      primary=family["primary"], secondary=family["secondary"],
                                      art=family["art"], proposal=family["proposal"])))
    for id, row in sorted(maps.items()):
        rows.append(entry("map:" + id, "map", id, map_hashes[id],
                          [receipt.get("batch_sha256"), [[m, model_hashes[m]] for m in sorted(map_models[id])]], map_id=id,
                          category=row["type"], implementation="blocked" if row["error"] else aggregate(map_implementations[id]),
                          detail=dict(width=row["width"], height=row["height"], error=row["error"],
                                      primary=row["primary"], secondary=row["secondary"],
                                      object_event_references=row["object_events"])))
    failures = catalog["unexportable_details"]
    if len(failures) != catalog["unexportable_proposals"]:
        raise ValueError("Missing oversized/export-failure records")
    for f in failures:
        fragments, members = [], set()
        for path in f["fragments"]:
            p = read(local_file(catalog_dir, path))["patterns"][0]
            cells = member_cells(p, p["source"]["x"], p["source"]["y"])
            if cells & members:
                raise ValueError("Oversized proposal fragments overlap")
            members.update(cells)
            fragments.append([path, digest(normalized(p))])
        if f["fragments_complete"] and len(members) != f["member_cells"]:
            raise ValueError("Oversized proposal fragments lose source cells")
        rows.append(entry(f"export_gap:{f['map']}:{f['x']}:{f['y']}", "export_gap", f["reason"],
                          [f, fragments], map_id=f["map"], category=f["kind"], x=f["x"], y=f["y"],
                          implementation="blocked", detail=f))
    for state in ("actors_and_effects", "tile_replacements_and_animation", "warps_and_connections",
                  "weather_and_time", "menus_battles_and_gameplay"):
        if dynamic and state == "warps_and_connections":
            continue  # Concrete entries now retain unresolved destination cases.
        rows.append(entry("pending:" + state, "pending", state.replace("_", " "), state,
                          implementation="pending_inventory", detail=dict(scope=(
                              "Source records inventoried; reachable combinations, indirect/native changes and complete presentation sequences still need audit"
                              if dynamic else "Not inventoried by the static ledger"))))
    if dynamic:
        if set(dynamic["map_ids"]) != set(maps):
            raise ValueError("Dynamic inventory map set does not match the static catalog")
        rows.extend(dynamic_entries(dynamic, receipt.get("batch_sha256")))
    unique(rows, "id", "ledger entry/placement")
    return rows


def sync_rows(db, rows, metadata, defects=()):
    # Validate before the transaction. Never collapse duplicates or partially
    # replace a user's ledger after a failed import.
    incoming = unique(rows, "id", "ledger entry/placement")
    unique(defects, "id", "defect ID")
    old = {r["id"]: dict(r) for r in db.execute("SELECT * FROM entries")}
    for defect in defects:
        if defect["entry_id"] not in incoming and defect["entry_id"] not in old:
            raise ValueError(f"Defect target missing: {defect['entry_id']}")
    changes = Counter()
    columns = list(rows[0]) if rows else []
    with db:
        for row in rows:
            previous = old.get(row["id"])
            reasons = []
            if previous:
                if previous["source_hash"] != row["source_hash"]:
                    reasons.append("source dependency changed")
                if previous["recipe_hash"] != row["recipe_hash"]:
                    reasons.append("recipe or renderer dependency changed")
                if not previous["present"]:
                    reasons.append("entry returned after removal; review again")
            if reasons:
                changes["changed"] += 1
                db.execute("UPDATE reviews SET invalidated=? WHERE entry_id=? AND invalidated='' AND stage!='disposition'",
                           ("; ".join(reasons), row["id"]))
                if "source dependency changed" in reasons or not previous["present"]:
                    db.execute("UPDATE reviews SET invalidated=? WHERE entry_id=? AND stage='disposition' AND invalidated=''",
                               ("; ".join(reasons), row["id"]))
            else:
                changes["unchanged" if previous else "added"] += 1
            if not previous or not previous["present"] or any(row[c] != previous[c] for c in columns):
                db.execute(f"INSERT INTO entries({','.join(columns)},present) VALUES({','.join('?' for _ in columns)},1) "
                           f"ON CONFLICT(id) DO UPDATE SET {','.join(c+'=excluded.'+c for c in columns if c!='id')},present=1",
                           [row[c] for c in columns])
        for id, previous in old.items():
            if id not in incoming and previous["present"]:
                changes["removed"] += 1
                db.execute("UPDATE entries SET present=0 WHERE id=?", (id,))
                db.execute("UPDATE reviews SET invalidated='entry removed from current inventory' WHERE entry_id=? AND invalidated=''", (id,))
        for key, value in metadata.items():
            db.execute("INSERT OR REPLACE INTO metadata VALUES(?,?)", (key, json.dumps(value, sort_keys=True)))
        for d in defects:
            db.execute("INSERT OR REPLACE INTO defects VALUES(?,?,?,?,?,?,?)",
                       (d["id"], d["entry_id"], d["milestone"], d["title"], d["certainty"], d.get("status", "open"), json.dumps(d, sort_keys=True)))
    return dict(changes)


def review(db, id, stage, result, evidence, note):
    row = db.execute("SELECT * FROM entries WHERE id=? AND present=1", (id,)).fetchone()
    if not row or stage not in STAGES or result not in (DISPOSITIONS if stage == "disposition" else RESULTS):
        raise ValueError("Unknown entry, review stage or result")
    if not evidence.strip() or not note.strip():
        raise ValueError("A review needs an evidence reference and a note")
    with db:
        db.execute("INSERT INTO reviews(entry_id,stage,result,evidence,note,source_hash,recipe_hash) VALUES(?,?,?,?,?,?,?)",
                   (id, stage, result, evidence, note, row["source_hash"], row["recipe_hash"]))


def report(db, map_id=None, category=None, disposition=None, milestone=None):
    # Entries, authored roles, reviews and metadata must describe one snapshot,
    # including when another process syncs the ledger during report generation.
    if db.in_transaction:
        return _report(db, map_id, category, disposition, milestone)
    db.execute('BEGIN')
    try:
        return _report(db, map_id, category, disposition, milestone)
    finally:
        db.rollback()  # End only the read transaction we own.


def _report(db, map_id=None, category=None, disposition=None, milestone=None):
    if disposition and disposition not in DISPOSITIONS:
        raise ValueError('Unknown disposition filter')
    if milestone and milestone not in coverage_disposition.MILESTONES:
        raise ValueError('Unknown milestone filter')
    query = "SELECT * FROM entries WHERE present=1"
    args = []
    if map_id:
        query += " AND map_id=?"
        args.append(map_id)
    rows = [dict(r) for r in db.execute(query + " ORDER BY id", args)]
    if map_id and not rows:
        raise ValueError(f"Unknown map: {map_id}")
    if map_id:
        # Shared script/definition associations are source references, not proof
        # that every branch executes in that map. Include each entry only once.
        selected = {r["id"] for r in rows}
        for r in db.execute("SELECT * FROM entries WHERE present=1 AND kind='state'"):
            if r["id"] not in selected and map_id in json.loads(r["detail"]).get("related_maps", []):
                rows.append(dict(r))
    if category:
        rows = [r for r in rows if r["category"] == category]
    rows.sort(key=lambda r: r["id"])
    reviews = {(r["entry_id"], r["stage"]): dict(r) for r in db.execute("SELECT * FROM reviews ORDER BY id")}
    policy = coverage_disposition.Policy(db)
    counts = Counter()
    stages = {s: Counter() for s in STAGES}
    shown = []
    for row in rows:
        row['detail'] = json.loads(row['detail'])
        row['classification'] = policy.classify(row, reviews.get((row['id'], 'disposition')))
        row['disposition'] = row['classification']['disposition']
        if disposition and row['disposition'] != disposition: continue
        if milestone and row['classification']['milestone'] != milestone: continue
        shown.append(row)
        counts[(row["kind"], row["implementation"], row["boundary"] or "n/a")] += 1
        row["reviews"] = {}
        for stage in STAGES:
            r = reviews.get((row["id"], stage))
            value = ("stale" if r["invalidated"] else r["result"]) if r else "unreviewed"
            row["reviews"][stage] = dict(status=value, reason=r["invalidated"] if r else "")
            stages[stage][value] += 1
    rows = shown
    treatments = Counter((r['kind'], r['disposition'], r['classification']['milestone'], r['classification']['basis']) for r in rows)
    selected = {r["id"] for r in rows}
    defects = [json.loads(r[0]) for r in db.execute("SELECT detail FROM defects ORDER BY id")
               if not map_id or json.loads(r[0])["entry_id"] in selected
               or json.loads(r[0]).get("map") == map_id]
    catalog_dir = db.execute("SELECT value FROM metadata WHERE key='catalog_directory'").fetchone()
    source_dir = db.execute("SELECT value FROM metadata WHERE key='source_directory'").fetchone()
    return dict(scope="static coverage and source state candidates; runtime combinations and presentation remain unverified",
                catalog_directory=json.loads(catalog_dir[0]) if catalog_dir else None,
                source_directory=json.loads(source_dir[0]) if source_dir else None,
                category=category, map=map_id, disposition=disposition, milestone=milestone,
                classification_policy=policy.identity,
                treatments=[dict(kind=k, disposition=d, milestone=m, basis=b, count=n) for (k,d,m,b),n in sorted(treatments.items())],
                counts=[dict(kind=k, implementation=i, boundary=b, count=n)
                                   for (k, i, b), n in sorted(counts.items())],
                review_states={s: dict(c) for s, c in stages.items()},
                removed_entries=db.execute("SELECT count(*) FROM entries WHERE present=0").fetchone()[0],
                defects=defects, entries=rows)


def markdown(data, limit=30):
    lines = ["# Coverage ledger" + (" — " + data["map"] if data["map"] else ""), "",
             "Source inventory and model availability are separate from visual, live and headset approval.",
             "Dynamic records identify source candidates; reachable combinations and presentation remain unverified.",
             "No whole-game completion percentage is inferred.", "",
             "| Entry type | Implementation | Location | Count |", "|---|---|---|---:|"]
    for c in data["counts"]:
        lines.append(f"| {c['kind']} | {c['implementation']} | {c['boundary']} | {c['count']} |")
    lines += ['', '## Intended treatment and work ownership', '',
              'Rules explain this recorded inventory snapshot. Recorded dispositions take precedence; neither supplies visual/live/headset approval.',
              '`runtime_state` controls presentation and has no standalone drawing. Unknown roles remain unresolved.', '',
              '| Entry type | Treatment | Milestone | Basis | Count |', '|---|---|---|---|---:|']
    for c in data['treatments']:
        lines.append(f"| {c['kind']} | {c['disposition']} | {c['milestone']} | {c['basis']} | {c['count']} |")
    placements = [r for r in data["entries"] if r["kind"] == "placement"]
    nonflat = [r for r in placements if r["category"] != "flat"]
    lines += ["", f"Source grouping: {len(nonflat):,} object groups and {len(placements)-len(nonflat):,} individual flat-tile placements.",
              "A source group can combine terrain and several objects; it is not necessarily one building or tree."]
    states = [r for r in data["entries"] if r["kind"] == "state"]
    if states:
        lines += ["", "## Source state inventory", "",
                  "Map events use unpadded layout coordinates; static placements use backup-map coordinates.",
                  "Shared script associations are conservative source references. Variables and conditional branches have not been executed.", "",
                  "| Category | Source status | Count |", "|---|---|---:|"]
        state_counts = Counter((r["category"], r["detail"]["source_status"]) for r in states)
        lines += [f"| {c} | {s} | {n} |" for (c, s), n in sorted(state_counts.items())]
        lines += ["", "### Source state queue", ""]
        states.sort(key=lambda r: (r["detail"]["source_status"] not in ("unresolved_reference", "runtime_resolution"),
                                    r["category"] == "script_block", r["id"]))
        for r in states[:limit]:
            location = r["detail"].get("source", {})
            source_link = location.get("path", "")
            if source_link and data.get("source_directory"):
                path = (Path(data["source_directory"]) / source_link).as_posix()
                if location.get("line"): path += ":" + str(location["line"])
                source_link = f"[source](<{path}>)"
            label = r["label"].replace("`", "'").replace("\n", " ")
            lines.append(f"- **{label}** — {r['detail']['source_status']}; {source_link}; `{r['id']}`")
            intent = r['classification']
            lines.append(f"  Treatment: {r['disposition']} · {intent['milestone']}. {intent['reason']} Next: {intent['next_action']}")
        lines += ["", f"Showing {min(limit,len(states))} of {len(states)} source states. Use `--category` or JSON export to inspect a category completely."]
    lines += native_trace.markdown(states, limit)
    lines += ["", "## Independent review states", ""]
    lines += [f"- {s}: " + ", ".join(f"{k} {v}" for k, v in sorted(values.items()))
              for s, values in data["review_states"].items()]
    lines += ["", "## Recorded defects", ""]
    lines += [f"- **{d['id']} · {d['milestone']} · {d['certainty']}**: {d['title']} ({d.get('status','open')})"
              for d in data["defects"]] or ["No recorded defects. This does not imply visual acceptance."]
    lines += ["", "## Review queue", "", "Unmodeled flat drawings need a disposition; they are not automatically missing 3D objects.", ""]
    queue = [r for r in data["entries"] if r["kind"] in ("family", "export_gap", "instance", "pending")
             or (data["map"] and r["kind"] == "placement" and r["category"] != "flat")]
    queue.sort(key=lambda r: (r["implementation"] not in ("blocked", "conflict", "unmodeled", "partial"),
                              r["kind"] != "export_gap", r["id"]))
    for r in queue[:limit]:
        source = ""
        if data["catalog_directory"] and r["detail"].get("art"):
            path = (Path(data["catalog_directory"]) / r["detail"]["art"]).as_posix()
            source = f"; [source drawing](<{path}>)"
        intent = r['classification']
        lines.append(f"- `{r['id']}` — {r['label']}; {r['implementation']}; {r['disposition']} · {intent['milestone']}; visual {r['reviews']['visual']['status']}{source}")
        lines.append(f"  {intent['reason']} Next: {intent['next_action']}")
    lines += ["", f"Showing {min(limit,len(queue))} of {len(queue)} queue entries. Use `show` for full history; JSON export retains every entry.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db", type=Path, default=ROOT / "build/coverage/ledger.sqlite")
    sub = parser.add_subparsers(dest="command", required=True)
    sync = sub.add_parser("sync", help="Refresh source/recipe coverage; retain or invalidate prior reviews")
    sync.add_argument("--catalog", type=Path, default=ROOT / "build/sprite-catalog")
    sync.add_argument("--pack", type=Path, default=ROOT / "mod-assets/voxel-world-v6.json")
    sync.add_argument("--source", type=Path, default=ROOT / "third_party/pokeruby")
    sync.add_argument("--batch", type=Path, default=ROOT / "build/rubyvr_studio.exe")
    sync.add_argument("--defects", type=Path, default=ROOT / "docs/known-scenery-defects.json")
    query = sub.add_parser("report", help="Write remaining work, independently tracked states and known defects")
    query.add_argument("--map")
    query.add_argument("--category", help="Filter source/state categories, e.g. warp or script_tile_change")
    query.add_argument('--disposition', choices=DISPOSITIONS, help='Filter effective intended treatment, including recorded decisions')
    query.add_argument('--milestone', choices=coverage_disposition.MILESTONES, help='Filter the milestone owning the next action')
    query.add_argument("--out", type=Path)
    query.add_argument("--json", action="store_true")
    query.add_argument("--limit", type=int, default=30)
    browser = sub.add_parser("export-studio", help="Export a read-only, per-map Studio review snapshot")
    browser.add_argument("--pack", type=Path, default=ROOT / "mod-assets/voxel-world-v6.json")
    browser.add_argument("--out", type=Path, default=ROOT / "build/coverage/studio/index.json")
    show = sub.add_parser("show", help="Inspect one entry, including retained and invalidated reviews")
    show.add_argument("id")
    mark = sub.add_parser("review", help="Record one explicit evidence-backed review or disposition")
    mark.add_argument("id")
    mark.add_argument("--stage", choices=STAGES, required=True)
    mark.add_argument("--result", required=True)
    mark.add_argument("--evidence", required=True)
    mark.add_argument("--note", required=True)
    args = parser.parse_args()
    if args.command != "sync" and not args.db.exists():
        raise ValueError("No ledger exists; run sync first")
    db = coverage_review.open_readonly(args.db) if args.command in ('export-studio', 'report', 'show') else open_db(args.db)
    try:
        if args.command == "sync":
            receipt = read(args.catalog / "source-receipt.json")
            current = source_receipt(args.source, args.batch)
            if any(receipt.get(k) != v for k, v in current.items()) or receipt.get("catalog_sha256") != file_hash(args.catalog / "catalog.json"):
                raise ValueError("Catalog source/binary changed. Run tools/catalog-sprites.py before syncing reviews")
            dynamic = inventory_dynamics(args.source, [m["id"] for m in read(args.catalog / "catalog.json")["maps"]])
            audit_path = args.db.parent / "current-matches.json"
            previous_stamp = audit_path.stat().st_mtime_ns if audit_path.exists() else None
            pack_hash = file_hash(args.pack)
            env = os.environ.copy()
            env["PATH"] = env.get("RUBYVR_MINGW_BIN", r"C:\msys64\mingw64\bin") + os.pathsep + env["PATH"]
            with audit_path.with_suffix(".log").open("w") as log:
                result = subprocess.run([str(args.batch.resolve()), "--audit-assets", str(audit_path.resolve()),
                                         "--overrides", str(args.pack.resolve())], cwd=ROOT, env=env,
                                        stdout=log, stderr=log, timeout=600)
            if result.returncode not in (0, 1) or not audit_path.exists() or audit_path.stat().st_mtime_ns == previous_stamp:
                raise ValueError("Native match audit failed; prior ledger retained")
            if current != source_receipt(args.source, args.batch) or pack_hash != file_hash(args.pack):
                raise ValueError("Source or pack changed during match audit; prior ledger retained")
            verify_dynamic_source(args.source, dynamic)
            rows = build_entries(args.catalog, read(args.pack), read(audit_path), receipt, dynamic)
            dynamic_path = args.db.parent / "source-states.json"
            temporary = dynamic_path.with_suffix(".json.tmp")
            temporary.write_text(json.dumps(dynamic, indent=2) + "\n", encoding="utf-8")
            temporary.replace(dynamic_path)
            defects = read(args.defects)["defects"] if args.defects.exists() else []
            changes = sync_rows(db, rows, dict(source_version=receipt["source_hash"],
                                              catalog_directory=str(args.catalog.resolve()),
                                              source_directory=str(args.source.resolve()),
                                              dynamic_source_sha256=dynamic["source_hash"],
                                              dynamic_adapter_sha256=dynamic["adapter_sha256"],
                                              dynamic_counts=dynamic["counts"],
                                              catalog_sha256=receipt["catalog_sha256"], pack_sha256=pack_hash,
                                              batch_sha256=receipt["batch_sha256"]), defects)
            print(json.dumps(dict(entries=len(rows), changes=changes, database=str(args.db)), indent=2))
        elif args.command == "export-studio":
            print(json.dumps(coverage_review.export_studio(db, args.pack, args.out), indent=2))
        elif args.command == "review":
            review(db, args.id, args.stage, args.result, args.evidence, args.note)
            print("Recorded. Other review stages remain unchanged.")
        elif args.command == "show":
            db.execute('BEGIN')
            row = db.execute("SELECT * FROM entries WHERE id=?", (args.id,)).fetchone()
            if not row:
                raise ValueError("Unknown ledger entry")
            history = [dict(r) for r in db.execute("SELECT * FROM reviews WHERE entry_id=? ORDER BY id", (args.id,))]
            dispositions = [r for r in history if r['stage']=='disposition']
            classification = coverage_disposition.Policy(db).classify(row, dispositions[-1] if dispositions else None)
            print(json.dumps(dict(entry=dict(row), classification=classification, reviews=history), indent=2))
        else:
            data = report(db, args.map, args.category, args.disposition, args.milestone)
            output = json.dumps(data, indent=2) + "\n" if args.json else markdown(data, args.limit)
            if args.out:
                args.out.parent.mkdir(parents=True, exist_ok=True)
                args.out.write_text(output, encoding="utf-8")
                print(args.out)
            else:
                print(output)
    finally:
        db.close()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, sqlite3.Error) as exc:
        print(f"coverage-ledger: {exc}", file=sys.stderr)
        raise SystemExit(1)
