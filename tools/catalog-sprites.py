"""Export the whole-game source inventory and review sheets, without opening a GUI.

This records source coverage, not approval of inferred 3D shapes. Proposals are
separate from authored overrides; this script never promotes them to release art.
"""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
from studio_paths import executable as native_executable, native_environment
import re
import subprocess

from PIL import Image, ImageDraw, ImageFont
from coverage_source import source_receipt, file_hash

REPO = Path(__file__).resolve().parents[1]
GAME = REPO


def constants(path: Path, prefix: str) -> list[dict]:
    entries = []
    for name, value in re.findall(r"^#define\s+(\w+)\s+([^\r\n]+)", path.read_text(), re.M):
        if name.startswith(prefix):
            entries.append({"name": name, "value": value.split("//", 1)[0].strip(), "status": "unreviewed"})
    return entries


def summarize(directory: Path) -> dict:
    manifest = json.loads((directory / "catalog.json").read_text())
    decomp = GAME / "third_party/pokeruby"
    source_maps = []
    for path in sorted((decomp / "data/maps").glob("*/map.json")):
        data = json.loads(path.read_text())
        source_maps.append(data["id"])
    actual_maps = [m["id"] for m in manifest["maps"]]
    if len(source_maps) != len(set(source_maps)) or sorted(source_maps) != sorted(actual_maps):
        raise RuntimeError("Catalog map set does not exactly match every source map.json ID")
    failures = manifest.get("unexportable_details", [])
    if len(failures) != manifest["unexportable_proposals"]:
        raise RuntimeError("Unexportable proposal count lacks complete diagnostic records; regenerate the catalog")
    for failure in failures:
        if not failure["fragments_complete"]:
            continue
        cells = []
        for path in failure["fragments"]:
            fragment = json.loads((directory / path).read_text())["patterns"][0]
            cells.extend((fragment["source"]["x"] + i % fragment["w"],
                          fragment["source"]["y"] + i // fragment["w"])
                         for i, value in enumerate(fragment["mask"]) if value)
        if len(cells) != failure["member_cells"] or len(set(cells)) != len(cells):
            raise RuntimeError("Failed-proposal fragments lose or duplicate member cells")
    dynamic = {
        "status": "unreviewed",
        "object_graphics": constants(decomp / "include/constants/event_objects.h", "OBJ_EVENT_GFX_"),
        "field_effects": constants(decomp / "include/constants/field_effects.h", "FLDEFF_"),
        "limits": [
            "Object-event references are spawn data, not a live runtime sprite capture.",
            "Graphics constants include aliases and variable IDs; their count is not a unique-image count.",
            "Field-effect constants do not cover all battle animation, UI or scripted graphics.",
            "No 3D asset or runtime presentation is marked approved by this inventory.",
        ],
    }
    (directory / "dynamic-inventory.json").write_text(json.dumps(dynamic, indent=2) + "\n")
    kind_counts = collections.Counter(asset["kind"] for asset in manifest["assets"])
    type_counts = collections.Counter(m["type"] for m in manifest["maps"])
    tileset_counts = collections.Counter((m["primary"], m["secondary"]) for m in manifest["maps"])
    sheet_dir = directory / "sheets"
    sheet_dir.mkdir(exist_ok=True)
    font = ImageFont.load_default()
    sheets = []
    # A contact sheet page is small enough to inspect at native resolution.
    # Keep exact nearest-neighbour source pixels, including visible ground art.
    for category in ("structure", "prop", "mass", "flat"):
        assets = [a for a in manifest["assets"] if a["kind"] == category]
        for offset in range(0, len(assets), 24):
            page = assets[offset:offset + 24]
            canvas = Image.new("RGB", (1200, 1040), "#151b25")
            draw = ImageDraw.Draw(canvas)
            draw.text((16, 12), f"{category.upper()} | source drawings {offset + 1}-{offset + len(page)} of {len(assets)} | UNREVIEWED", fill="#e7edf6", font=font)
            for index, asset in enumerate(page):
                x, y = 12 + index % 6 * 198, 40 + index // 6 * 248
                with Image.open(directory / asset["art"]) as source:
                    scale = min(180 / source.width, 170 / source.height, 4)
                    size = (max(1, int(source.width * scale)), max(1, int(source.height * scale)))
                    preview = source.resize(size, Image.Resampling.NEAREST)
                    canvas.paste(preview, (x + (184 - size[0]) // 2, y + (174 - size[1]) // 2))
                place = asset["occurrences"][0]
                draw.text((x, y + 178), asset["id"].removeprefix("asset-"), fill="#80ccec", font=font)
                draw.text((x, y + 194), f"{asset['width']}x{asset['height']} | {sum(p['count'] for p in asset['occurrences'])} occurrences", fill="#e7edf6", font=font)
                draw.text((x, y + 210), place["map"].removeprefix("MAP_")[:28], fill="#a6b2c3", font=font)
                draw.text((x, y + 226), f"backup {place['x']},{place['y']}", fill="#a6b2c3", font=font)
            name = f"{category}-{offset // 24 + 1:03}.png"
            canvas.save(sheet_dir / name)
            sheets.append((category, name))
    lines = [
        "# Whole-game sprite inventory", "",
        f"Scanned **{manifest['map_count']} maps**, independently checked against every source map.json.",
        f"Found **{manifest['asset_count']} static drawing families** and **{manifest['object_events']} placed object events**.",
        f"Map load failures: **{manifest['failed_maps']}**. Unexportable proposals: **{manifest['unexportable_proposals']}**.", "",
        "**Visual review is pending. This report does not certify the game for release.**", "",
        "Generated proposals preserve source membership and tileset definitions. They contain no new authored model or painted mask.",
        "A drawing can recur with different palettes; exact source pixels distinguish those variants. Connection padding is not counted as another map.", "",
        "Assets with the same matcher_group share runtime matching rules even when their source pixels differ. Do not combine their proposals as independent overrides: author one compatible model for the group, or explicitly extend the matching contract first.", "",
        "## Static source families", "", "| Class | Families |", "|---|---:|",
        *[f"| {kind} | {count} |" for kind, count in sorted(kind_counts.items())], "",
        "## Map types", "", "| Type | Maps |", "|---|---:|",
        *[f"| {kind} | {count} |" for kind, count in sorted(type_counts.items())], "",
        "## Source sheets", "",
        *[f"- [{category}: {name}](sheets/{name})" for category, name in sheets], "",
        "## Largest tileset families", "", "| Primary / secondary | Maps |", "|---|---:|",
        *[f"| {primary} / {secondary} | {count} |" for (primary, secondary), count in tileset_counts.most_common()], "",
        "## Failed or incomplete maps", "",
        *[f"- {m['id']}: {m['error'] or str(m['unexportable']) + ' proposals could not be exported'}" for m in manifest["maps"] if m["error"] or m["unexportable"]], "",
        "Oversized objects retain their exact bounds, complete source preview and non-overlapping editable fragments in catalog.json/unexportable_details. These are source-recovery records, not approved terrain models.", "",
        *[f"- {f['map']} at {f['x']},{f['y']}: {f['width']}x{f['height']}, {f['member_cells']} cells; {f['reason']}. "
          + (f"[Source preview]({f['art']}); " if f['art'] else "")
          + f"{len(f['fragments'])} fragments (complete={f['fragments_complete']})." for f in failures], "",
        "## Characters and effects", "",
        "See [dynamic inventory](dynamic-inventory.json) for source graphics/effect constants and their limits.",
        f"Live animation, sprite anchoring, occlusion, UI, battle presentation and transitions belong to the integration handoff at {REPO / '_docs/handoff-game-integration.md'}.", "",
        "## Review an asset in the studio", "",
        "Use the asset's first occurrence map and its proposal file as input; choose a distinct output file. Coordinates in catalog.json are backup-map coordinates.",
        "The unmodified source/inference is the starting point. Approve only after checking source orientation, masking, placement and several 3D views, then validate every matching map.", "",
    ]
    (directory / "README.md").write_text("\n".join(lines), encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=GAME / "build/sprite-catalog")
    parser.add_argument("--summarize-only", action="store_true")
    args = parser.parse_args()
    directory = args.out.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    exit_code = 0
    if not args.summarize_only:
        receipt = source_receipt(GAME / "third_party/pokeruby", native_executable('rubyvr_studio'))
        report = directory / "catalog.json"
        previous_stamp = report.stat().st_mtime_ns if report.exists() else None
        with (directory / "catalog.log").open("w", encoding="utf-8") as log:
            result = subprocess.run([str(native_executable('rubyvr_studio')), "--catalog", str(directory)], cwd=GAME, stdout=log, stderr=subprocess.STDOUT)
        exit_code = result.returncode
        if not report.exists() or report.stat().st_mtime_ns == previous_stamp:
            raise RuntimeError(f"Catalog did not produce a report; see {directory / 'catalog.log'}")
        if receipt != source_receipt(GAME / "third_party/pokeruby", native_executable('rubyvr_studio')):
            raise RuntimeError("Source inputs changed during export; regenerate the catalog")
        receipt["catalog_sha256"] = file_hash(report)
        (directory / "source-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    manifest = summarize(directory)
    print(f"{manifest['map_count']} maps; {manifest['asset_count']} static families; {manifest['failed_maps']} failed maps; {manifest['unexportable_proposals']} unexportable proposals.")
    print(f"Source review: {directory / 'README.md'}")
    print("Visual review remains pending; no release approval was inferred.")
    return exit_code or int(not manifest.get("export_complete", False))


if __name__ == "__main__":
    raise SystemExit(main())
