"""Check complete Center/Mart logos against actual top-down Studio captures.

The expected source rectangle includes the lower arc omitted by the original
recipes. Every source pixel must appear once, at native size, with exact RGB.
Run review-voxel-world.py first; this checks its immutable input and real renders.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_IDS = {
    "asset-8c40aea50e0288d8", "asset-5e860929c099b760",
    "asset-0658851a0ead8380", "asset-88dbd9a6603673b4",
    "asset-a45ce46dbfd22877-crop", "asset-715b1dc2c9f09f4c-crop",
    "asset-c3b16e8e17c6c781", "asset-0f3eae9932158caf", "asset-4e2864904f72f76c",
}
# Inspected from the original 64x64 drawings: both arcs, centre and outer rim.
SOURCE_RECT = (22, 25, 20, 14)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", type=Path,
                        default=ROOT/"build/voxel-world-review/final")
    base = parser.parse_args().review.resolve()
    input_path = base/"input.json"
    pack = json.loads(input_path.read_text())
    report = json.loads((base/"report.json").read_text())
    assert report["status"] == "PASS"
    assert hashlib.sha256(input_path.read_bytes()).hexdigest() == report["pack_sha256"]
    patterns = {p["id"]: p for p in pack["patterns"]}
    assert EXPECTED_IDS <= patterns.keys()
    bx, by, bw, bh = SOURCE_RECT
    expected_pixels = {(x, y) for y in range(by, by+bh) for x in range(bx, bx+bw)}
    results = []
    for id in sorted(EXPECTED_IDS):
        p = patterns[id]
        stamps = [s for s in p["parts"] if s["name"].startswith("Roof emblem rows ")]
        assert stamps, p["name"]
        assert all(s["angles"] == [0, 0, 0] for s in p["parts"])
        target_x = (min(s["position"][0] for s in p["parts"])
                    + max(s["position"][0]+s["size"][0] for s in p["parts"]))/2
        target_z = (min(s["position"][2]-s["size"][2]/2 for s in p["parts"])
                    + max(s["position"][2]+s["size"][2]/2 for s in p["parts"]))/2
        actual = Image.open(base/"renders"/f"{id}-roof.png").convert("RGB")
        source = Image.open(base/"renders"/f"{id}-source.png").convert("RGB")
        samples = Counter()
        for part in stamps:
            x, y, w, h = part["surfaces"][4]["region"]
            for j in range(h):
                for i in range(w):
                    wx = part["position"][0]+(i+.5)/16
                    wz = part["position"][2]-part["size"][2]/2+(j+.5)/16
                    # Asset review uses an eight-cell 768px orthographic view;
                    # the top camera's screen-up direction points towards +Z.
                    sx = round(384+(wx-target_x)*96)
                    sy = round(384-(wz-target_z)*96)
                    assert actual.getpixel((sx, sy)) == source.getpixel((x+i, y+j)), (id, x+i, y+j)
                    samples[(x+i, y+j)] += 1
        assert set(samples) == expected_pixels, (id, "complete source silhouette")
        assert all(n == 1 for n in samples.values()), (id, "each source pixel once")
        results.append(dict(id=id, source_pixels=bw*bh, exact_rendered_rgb=True, complete_logo=True))
    (base/"emblem-pixels.json").write_text(json.dumps(dict(status="PASS", models=results), indent=2)+"\n")
    print(f"[emblem-pixels] PASS {len(results)} models; {sum(r['source_pixels'] for r in results)} complete source pixels")


if __name__ == "__main__":
    main()
