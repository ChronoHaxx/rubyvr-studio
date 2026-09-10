"""Author the bounded v6 house candidate; retain the rejected v5 pack unchanged.

Coordinates and masks are explicit pixel geometry, never colour thresholds.
Depth is authored from structural regions, not the drawing's brightness.
"""
import copy
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def rle(w, h, values):
    runs, previous, length = [], values[0], 0
    for value in values:
        if value != previous:
            runs.append(length)
            length, previous = 0, value
        length += 1
    runs.append(length)
    return dict(w=w, h=h, first=values[0], runs=runs)


def inside(x, y, r):
    return r[0] <= x < r[0] + r[2] and r[1] <= y < r[1] + r[3]


def build():
    source = ROOT / 'build/sprite-catalog/proposals/asset-f4f6146cc2fdc85f.json'
    p = copy.deepcopy(json.loads(source.read_text())['patterns'][0])
    p['name'] = 'Oldale voxel house candidate'
    obj, ground, shadow = [], [], []
    for y in range(64):
        for x in range(64):
            # The upper silhouette's one-pixel gaps are background. The lower
            # drawing has a left grass margin, right cast shadow and floor shadow.
            foreground = ((y == 0 and x != 0 and x % 4 != 3) or
                          1 <= y <= 35 or (y == 36 and 1 <= x <= 62) or
                          (37 <= y <= 62 and 2 <= x <= 61))
            cast_shadow = not foreground and ((y >= 36 and x >= 62) or (y == 63 and x >= 2))
            obj.append(int(foreground))
            shadow.append(int(cast_shadow))
            ground.append(int(not foreground and not cast_shadow))
    p['cutout'] = rle(64, 64, obj)
    p['voxel'] = dict(pixels_per_cell=16, ground=rle(64, 64, ground), shadow=rle(64, 64, shadow))
    p['model_seeded'] = True
    parts = []

    def solid(name, xyz, size, materials, kind='box', **extra):
        # All input dimensions here are source-pixel units. Only serialization
        # converts to the existing group's cell coordinates.
        parts.append(dict(id=p['id']+'-'+name, name=name, kind=kind,
                          position=[n/16 for n in xyz], size=[n/16 for n in size],
                          angles=[0, 0, 0], surfaces=[dict(region=r) for r in materials], **extra))

    def relief(name, z_back, z_front, keep):
        r = [2, 40, 60, 23]
        values = [int(keep(x, y)) for y in range(40, 63) for x in range(2, 62)]
        parts.append(dict(id=p['id']+'-'+name, name=name, kind='billboard',
                          position=[2/16, 0, (z_back+z_front)/32],
                          size=[60/16, 23/16, (z_front-z_back)/16], angles=[0, 0, 0],
                          art_region=r, local_mask=rle(60, 23, values)))

    # Hidden walls repeat the drawing's horizontal siding. No facade, door,
    # roof or background supplies these surfaces. Unseen openings are not assumed.
    # The 34 roof rows are a foreshortened drawing, not 34 units of floor depth.
    # Use a 45-degree starter assumption and equal front/rear roof runs. Keeping
    # the visible front at Z=2 also keeps the facade and ground anchor unchanged.
    # This is an editable house recipe, not depth inferred from pixel brightness.
    roof_depth = 2 * round(34 / math.sin(math.pi / 4) / 2)
    roof_run = roof_depth / 2
    roof_front = 2
    roof_back = roof_front - roof_depth
    ridge_z = roof_front - roof_run
    wall_back = roof_back + 2
    backing_front = -4
    backing_depth = backing_front - wall_back
    backing_z = (backing_front + wall_back) / 2
    siding, foundation = [4, 40, 3, 18], [4, 58, 3, 4]
    solid('foundation', [2, 0, backing_z], [60, 5, backing_depth], [foundation]*6)
    solid('siding', [2, 5, backing_z], [60, 18, backing_depth], [siding]*6)
    # Backing preserves every original facade texel, behind explicitly inset panes.
    relief('facade-backing', -4, -3, lambda x, y: True)
    panes = [[34, 43, 5, 2], [41, 43, 5, 2], [34, 46, 5, 3], [41, 46, 5, 3],
             [34, 50, 5, 3], [41, 50, 5, 3]]
    door = [17, 45, 14, 17]
    relief('wall-and-frames', -3, 0, lambda x, y: not inside(x, y, door) and not any(inside(x, y, r) for r in panes))
    relief('window-panes', -3, -1, lambda x, y: any(inside(x, y, r) for r in panes))
    relief('door-leaf', -3, -2, lambda x, y: inside(x, y, door) and not inside(x, y, [20, 48, 8, 4]))
    relief('posts-and-sill', 0, 1, lambda x, y: inside(x, y, [8, 40, 8, 23]) or inside(x, y, [48, 40, 8, 23]) or inside(x, y, [32, 54, 16, 1]))
    # Front fascia is mapped once at native scale. Other rims repeat its actual
    # dark course, instead of stretching roof stripes across a triangular gable.
    fascia = [3, 37, 58, 3]
    solid('fascia', [2, 23, ridge_z], [60, 4, -wall_back], [[2, 36, 60, 4]]+[[3, 36, 1, 4]]*5)
    solid('eave', [0, 27, ridge_z], [64, 2, roof_depth], [[0, 34, 64, 2], fascia, fascia, fascia, fascia, [1, 36, 62, 1]])
    gable = [4, 40, 3, 18]
    solid('front-roof', [0, 29, ridge_z + roof_run/2], [64, 10, roof_run], [fascia, gable, gable, gable, [0, 14, 64, 20], fascia],
          'wedge', axis='z', direction=-1)
    solid('rear-roof', [0, 29, ridge_z - roof_run/2], [64, 10, roof_run], [gable, fascia, gable, gable, [0, 1, 64, 10], fascia],
          'wedge', axis='z', direction=1)
    solid('ridge', [0, 38, ridge_z], [64, 2, 4], [[0, 12, 64, 2]]*4+[[0, 10, 64, 4], fascia])
    p['parts'] = parts
    out = ROOT / 'mod-assets/voxel-house-v6.json'
    out.write_text(json.dumps(dict(version=6, patterns=[p]), indent=2)+'\n')
    print(out)


if __name__ == '__main__':
    build()
