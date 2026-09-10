"""Regress Oldale's missed trees, clipped crowns and adjacent floor fragments.

Consumes review-voxel-world.py's immutable pack and actual production captures.
The expected placements and 35px silhouette come from inspecting Oldale's map
and the crown extending above its repeating 32px tree body, not mesh hashes.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OLDALE_TREES = {
    (7, 7), (9, 7), (11, 7), (13, 7), (19, 7), (21, 7), (23, 7), (25, 7),
    (7, 9), (9, 9), (23, 9), (25, 9), (7, 11), (25, 11), (7, 13), (25, 13),
    (7, 15), (7, 19), (25, 19), (7, 21), (25, 21), (7, 23), (9, 23),
    (25, 23), (7, 25), (9, 25), (23, 25), (25, 25),
}
CROWN_TILES = {462, 463, 519, 632, 454, 455}


def pixels(mask):
    result, value = [], mask["first"]
    for length in mask["runs"]:
        result.extend([value] * length)
        value = 1 - value
    assert len(result) == mask["w"] * mask["h"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", type=Path,
                        default=ROOT / "build/voxel-world-review/final")
    base = parser.parse_args().review.resolve()
    immutable = base / "input.json"
    report = json.loads((base / "report.json").read_text())
    assert report["status"] == "PASS"
    assert hashlib.sha256(immutable.read_bytes()).hexdigest() == report["pack_sha256"]
    patterns = {p["id"]: p for p in json.loads(immutable.read_text())["patterns"]}
    audit = json.loads((base / "topology.json").read_text())
    assert audit["status"] == "PASS" and not audit["failed_maps"]
    topology = {a["id"]: a for a in audit["assets"]}
    renders = {a["id"]: a for a in json.loads((base / "renders/renders.json").read_text())["assets"]}
    assert all(a["source_matches"] and not a["rejected"] for a in topology.values())
    # Crown parts identify the broad-tree family, but expected map positions
    # are independently fixed above, including both formerly flat trees.
    broad = {id for id, p in patterns.items()
             if any(s["name"].startswith("Crown depth ") for s in p["parts"])}
    placed = Counter((m["x"], m["y"]) for id in broad for m in topology[id]["accepted"]
                     if m["map"] == "MAP_OLDALE_TOWN" and not m["crosses_layout_boundary"])
    assert placed == Counter({point: 1 for point in OLDALE_TREES}), placed
    silhouettes = []
    for id in sorted(broad):
        a = renders[id]
        assert a["bounds_min"][1] == 0, (id, "trunk must reach ground")
        assert a["bounds_max"][1] == 35 / 16, (id, "complete crown height")
        assert a["closed_connected"] and a["exact_resave"], id
        # Asset review fixes six screen pixels per source pixel. Both ends
        # must be visible, and no empty horizontal band may split the tree.
        for face in ("front", "back"):
            im = Image.open(base / "renders" / f"{id}-{face}.png").convert("RGB")
            bg = im.getpixel((0, 0))
            rows = Counter(y for y in range(im.height) for x in range(im.width)
                           if im.getpixel((x, y)) != bg)
            assert max(rows) - min(rows) + 1 == 210, (id, face, "height")
            assert rows[min(rows)] == 12, (id, face, "pointed crown")
            assert len(rows) == 210, (id, face, "gap through tree")
        silhouettes.append(id)
    floor_tiles = set()
    cleared = 0
    for id, a in topology.items():
        p = patterns[id]
        # Metatile IDs are local to a tileset pair: e.g. another tileset's 519
        # is a shrub, not this General/Petalburg crown-cleanup family.
        if not (a.get('ground_only') or p.get('follow_ground')):
            continue
        if len(p['ids']) != 1 or p['ids'][0] not in CROWN_TILES:
            continue
        assert p["w"] == p["extent"] == 1 and renders[id]["exact_resave"]
        obj, shadow, ground = pixels(p["cutout"]), pixels(p["voxel"]["shadow"]), pixels(p["voxel"]["ground"])
        assert sum(shadow) == 17
        assert all(o + s + g == 1 for o, s, g in zip(obj, shadow, ground))
        if p.get('follow_ground'):
            assert p['ids'][0] in (454,455) and any(obj) and a['closed_connected'] and a['triangles'] > 0
        else:
            assert a.get('ground_only') and not p['parts'] and a['triangles']==0 and not any(obj) and sum(ground)==239
        # Original cap lives only in the bottom five rows of these ground
        # tiles. Grass dots elsewhere must survive the production role image.
        assert not any(shadow[:11 * 16])
        source = Image.open(base / "renders" / f"{id}-source.png").convert("RGB")
        actual = Image.open(base / "renders" / f"{id}-ground.png").convert("RGB")
        assert all(actual.getpixel((i % 16, i // 16)) == source.getpixel((i % 16, i // 16))
                   for i, keep in enumerate(ground) if keep)
        floor_tiles.add(p["ids"][0])
        cleared += sum(shadow)
    assert floor_tiles == CROWN_TILES, floor_tiles
    # The roof/tree shared tile cannot be claimed twice or let the cropped
    # Oldale Mart displace Slateport's distinct model in other city tilesets.
    mart = patterns["asset-a45ce46dbfd22877-crop"]
    tree = patterns["asset-a45ce46dbfd22877-tree"]
    assert mart["mask"][3] == 0 and "645" in mart["tiles"]
    assert tree["ids"][2] == 645 and tree["mask"][2] == 1
    assert topology["asset-c3b16e8e17c6c781"]["source_matches"]
    result = dict(status="PASS", oldale_trees=len(placed), crown_models=len(silhouettes),
                  crown_height_pixels=35, floor_variants=len(floor_tiles), cleared_pixels=cleared,
                  all_trunks_touch_ground=True, front_back_without_gaps=True,
                  all_map_source_claims_preserved=True)
    (base / "tree-regression.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"[voxel-trees] PASS {len(placed)} Oldale trees, {len(silhouettes)} complete crowns, "
          f"{len(floor_tiles)} floor variants, ground contact and exact save/reopen")


if __name__ == "__main__":
    main()
