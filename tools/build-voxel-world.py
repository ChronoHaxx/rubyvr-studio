"""Build editable v6 scenery recipes from inspected source drawings.

Only the listed recipes are promoted. Pixel ownership uses authored regions and
source palette/index roles, never a brightness-to-height conversion. Geometry is
ordinary Studio parts; the shared production mesher supplies every rendered face.
"""
from __future__ import annotations

import argparse
import collections
import copy
import hashlib
import json
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "build/sprite-catalog"
DECOMP = ROOT / "third_party/pokeruby"


def rle(w, h, values):
    runs, previous, length = [], int(values[0]), 0
    for value in values:
        value = int(value)
        if value != previous:
            runs.append(length)
            previous, length = value, 0
        length += 1
    runs.append(length)
    return dict(w=w, h=h, first=int(values[0]), runs=runs)


def in_rect(x, y, r):
    return r[0] <= x < r[0]+r[2] and r[1] <= y < r[1]+r[3]


class Source:
    def __init__(self, entry, recipe):
        original = json.loads((CATALOG / entry["proposal"]).read_text())["patterns"][0]
        cx, cy, cw, ch = recipe.get("crop", [0, 0, original["w"], original["extent"]])
        assert 0 <= cx and 0 <= cy and cx+cw <= original["w"] and cy+ch <= original["extent"]
        p = copy.deepcopy(original)
        p["w"], p["extent"] = cw, ch
        cells = [(cy+y)*original["w"]+cx+x for y in range(ch) for x in range(cw)]
        p["ids"] = [original["ids"][i] for i in cells]
        p["mask"] = [original["mask"][i] for i in cells]
        # A shared cell can belong to a separate tree model while its tile
        # definition still identifies this tileset variant. Retain that guard:
        # dropping it makes the cropped Oldale Mart also match other cities.
        referenced_ids = {i for i, m in zip(p["ids"], p["mask"]) if m}
        for x,y in recipe.get("exclude_cells", []):
            assert 0 <= x < cw and 0 <= y < ch
            p["mask"][y*cw+x] = 0
            p["ids"][y*cw+x] = 0
        p["tiles"] = {str(k): original["tiles"][str(k)] for k in referenced_ids}
        p["source"]["x"] += cx
        p["source"]["y"] += cy
        p["id"] = recipe.get("id", entry["id"] + ("-crop" if "crop" in recipe else ""))
        p["name"] = recipe["name"]
        p["model_seeded"] = True
        p["parts"] = []
        self.p, self.w, self.h = p, cw*16, ch*16
        self.art = Image.open(CATALOG / entry["art"]).convert("RGB").crop((cx*16, cy*16, (cx+cw)*16, (cy+ch)*16))
        graphics = (DECOMP / "data/tilesets/graphics.inc").read_text()
        graphics_c = (DECOMP / "src/data/graphics.c").read_text()
        sheets = []
        for name in (entry["primary"], entry["secondary"]):
            symbol = name.replace("gTileset_", "gTilesetTiles_")
            match = re.search(r"^"+re.escape(symbol)+r"::[^\n]*\n\s*\.incbin\s+\"([^\"]+)\"", graphics, re.M)
            if not match:
                match = re.search(re.escape(symbol)+r"\s*\[\s*\]\s*=\s*INCBIN_U\d+\(\"([^\"]+)\"\)", graphics_c)
            if not match:
                raise ValueError(f"Missing tile graphics: {symbol}")
            path = match[1].split(".4bpp")[0]+".png"
            sheet = Image.open(DECOMP / path)
            if sheet.mode != "P":
                raise ValueError(f"Expected indexed source pixels: {path}")
            sheets.append(sheet)
        self.meta = []
        for y in range(self.h):
            for x in range(self.w):
                cell = y//16*cw+x//16
                winner = None
                if p["mask"][cell]:
                    entries = p["tiles"][str(p["ids"][cell])]["entries"]
                    for layer in (0, 4):
                        e = entries[layer+(y%16//8)*2+x%16//8]
                        tile = e & 1023
                        sheet = sheets[tile >= 512]
                        tile -= 512 if tile >= 512 else 0
                        tx, ty = x%8, y%8
                        if e & 1024: tx = 7-tx
                        if e & 2048: ty = 7-ty
                        sx, sy = tile%(sheet.width//8)*8+tx, tile//(sheet.width//8)*8+ty
                        if sy >= sheet.height:
                            # The disk snapshot zero-fills VRAM outside the two
                            # loaded tile sheets; an upper layer may cover it.
                            continue
                        index = sheet.getpixel((sx, sy)) & 15
                        if index:
                            winner = (e >> 12, index)
                self.meta.append(winner)
        self.obj = [False] * (self.w*self.h)
        self.shadow = [False] * len(self.obj)

    def mark(self, obj, shadow=lambda x, y, m: False):
        for y in range(self.h):
            for x in range(self.w):
                i = y*self.w+x
                m = self.meta[i]
                self.obj[i] = bool(m and obj(x, y, m))
                self.shadow[i] = bool(m and not self.obj[i] and shadow(x, y, m))

    def material(self, requested):
        """Largest fully object-owned rectangle inside the chosen material area."""
        x0, y0, w, h = requested
        assert 0 <= x0 < x0+w <= self.w and 0 <= y0 < y0+h <= self.h, requested
        heights, best, area = [0]*w, None, 0
        for y in range(y0, y0+h):
            for x in range(w):
                heights[x] = heights[x]+1 if self.obj[y*self.w+x0+x] else 0
            stack = []
            for x in range(w+1):
                value = heights[x] if x < w else 0
                start = x
                while stack and stack[-1][1] > value:
                    left, height = stack.pop()
                    if (x-left)*height > area:
                        area, best = (x-left)*height, [x0+left, y-height+1, x-left, height]
                    start = left
                if not stack or stack[-1][1] < value:
                    stack.append((start, value))
        if not best:
            raise ValueError(f"{self.p['name']}: no object pixels in material selection {requested}")
        return best

    def solid(self, name, pos, size, materials, kind="box", **extra):
        assert min(size) > 0
        self.p["parts"].append(dict(id=self.p["id"]+"-"+name, name=name, kind=kind,
            position=[n/16 for n in pos], size=[n/16 for n in size], angles=[0, 0, 0],
            surfaces=[dict(region=r) for r in materials], **extra))

    def relief(self, name, region, back, front, keep=lambda x, y: True, ybase=None, angles=None, pos=None):
        x0, y0, w, h = region
        mask = [int(self.obj[y*self.w+x] and keep(x, y)) for y in range(y0, y0+h) for x in range(x0, x0+w)]
        if not any(mask):
            return
        position = pos if pos is not None else [x0, self.h-y0-h if ybase is None else ybase, (back+front)/2]
        self.p["parts"].append(dict(id=self.p["id"]+"-"+name, name=name, kind="billboard",
            position=[n/16 for n in position], size=[w/16, h/16, (front-back)/16], angles=angles or [0, 0, 0],
            art_region=region, local_mask=rle(w, h, mask)))

    def finish(self, output):
        p = self.p
        ground = [bool(p["mask"][y//16*p["w"]+x//16] and not self.obj[y*self.w+x] and not self.shadow[y*self.w+x])
                  for y in range(self.h) for x in range(self.w)]
        p["cutout"] = rle(self.w, self.h, self.obj)
        p["voxel"] = dict(pixels_per_cell=16, ground=rle(self.w, self.h, ground), shadow=rle(self.w, self.h, self.shadow))
        assert len(p["parts"]) <= 64 and (p["parts"] or (not any(self.obj) and any(self.shadow))), (p["name"],len(p["parts"]))
        # Authoring evidence shows the exact source role masks used by the pack.
        for role, mask in (("object",self.obj),("shadow",self.shadow),("ground",ground)):
            im = Image.new("RGB",(self.w,self.h),(25,29,32))
            im.putdata([rgb if keep else (25,29,32) for rgb,keep in zip(self.art.getdata(),mask)])
            im.save(output/f"{p['id']}-{role}.png")
        self.art.save(output/f"{p['id']}-source.png")
        return p


def building(s, r):
    w, h = s.w, s.h
    baseline = r.get("baseline",h-1)
    start = r["facade_start"]
    top = r.get("roof_start",0)
    palettes = set(r.get("object_palettes",range(16))) - set(r.get("ground_palettes",[2]))
    bounds = r.get("object_rect",[0,top,w,baseline-top])
    s.mark(lambda x,y,m: in_rect(x,y,bounds) and m[0] in palettes,
           lambda x,y,m: y >= start and (m[0] in palettes or (m[0]==2 and m[1] in (3,4,15))))
    wall_h = baseline-start
    trim_h = r.get("trim",4)
    roof_rows = start-trim_h-top
    depth = r.get("depth",2*round(roof_rows*math.sqrt(2)/2))
    depth = max(12,depth)
    front, back, ridge = 2, 2-depth, 2-depth/2
    rise = r.get("rise",max(5,round(roof_rows*.3)))
    margin = r.get("wall_margin",2)
    roof_x = r.get("roof_x",0)
    roof_w = r.get("roof_width",w-2*roof_x)
    side = s.material(r.get("wall_art",[margin,start,4,wall_h]))
    trim = s.material(r.get("trim_art",[0,start-trim_h,w,trim_h]))
    roof = s.material(r.get("roof_art",[roof_x,top,roof_w,roof_rows]))
    rear_roof = s.material(r.get("rear_roof_art",r.get("roof_art",[roof_x,top,roof_w,max(1,roof_rows//3)])))
    floor = s.material(r["foundation_art"]) if "foundation_art" in r else side
    wall_back, wall_front = back+2, -3
    s.solid("Side and back walls",[margin,0,(wall_back+wall_front)/2],[w-2*margin,wall_h,wall_front-wall_back],[side]*6)
    # The facade occupies the same width as the side/back wall envelope.
    # Sprite edge columns include already-projected side artwork; extruding
    # the full drawing made wings at the front of an otherwise inset cuboid.
    # Cropping preserves native pixel size, like the accepted Oldale starter.
    facade=[margin,start,w-2*margin,wall_h]
    holes=[]
    for x,y,rw,rh in r.get("recesses",[]):
        x0,y0=max(x,margin),max(y,start)
        x1,y1=min(x+rw,w-margin),min(y+rh,baseline)
        if x1>x0 and y1>y0:holes.append([x0,y0,x1-x0,y1-y0])
    s.relief("Facade backing",facade,-3,-2,ybase=0)
    s.relief("Facade and frames",facade,-2,0,lambda x,y:not any(in_rect(x,y,q) for q in holes),ybase=0)
    for i,q in enumerate(holes):
        s.relief(f"Recess {i+1}",q,-2,-1,ybase=baseline-q[1]-q[3])
    s.solid("Eaves",[roof_x,wall_h,ridge],[roof_w,trim_h,depth],[trim]*6)
    roof_base=wall_h+trim_h
    if r["kind"]=="flat":
        s.solid("Flat roof",[roof_x,roof_base,ridge],[roof_w,2,depth],[trim]*4+[roof,floor])
    elif r["kind"]=="hipped":
        # One-pixel courses give a real four-sided stepped roof, including hips.
        hip=r.get("hip",12)
        emblem=[]
        badge=r.get("badge")
        if badge:
            # Stamp native source rows onto the steps instead of rotating a
            # two-pixel slab. Centre sampling a tilted slab drops/repeats logo
            # rows and leaves a rectangular plaque sticking through the roof.
            badge_z=round(front-(depth/2-2)*r.get("badge_along",.48)-badge[3]/2)
        for level in range(rise):
            inset_x=math.floor(hip*level/rise)
            inset_z=math.floor((depth/2-2)*level/rise)
            s.solid(f"Roof course {level+1}",[roof_x+inset_x,roof_base+level,ridge],
                    [roof_w-2*inset_x,1,depth-2*inset_z],[roof]*6)
            faces=s.p["parts"][-1]["surfaces"]
            for face in faces:face["offset"]=[inset_x,inset_z]
            faces[0]["offset"]=[inset_x,depth-inset_z-1]
            if badge:
                # Only the exposed front strip of this course needs a stamp.
                next_z=front-math.floor((depth/2-2)*(level+1)/rise)
                x0=max(roof_x+inset_x,badge[0]);x1=min(roof_x+roof_w-inset_x,badge[0]+badge[2])
                z0=max(next_z,badge_z);z1=min(front-inset_z,badge_z+badge[3])
                if x1>x0 and z1>z0:
                    region=[x0,badge[1]+z0-badge_z,x1-x0,z1-z0]
                    front_art=[x0,region[1]+region[3]-1,region[2],1]
                    back_art=[x0,region[1],region[2],1]
                    s.solid(f"Roof emblem rows {region[1]-badge[1]+1}-{region[1]-badge[1]+region[3]}",
                            [x0,roof_base+level,(z0+z1)/2],[x1-x0,1,z1-z0],
                            [front_art,back_art,roof,roof,region,roof])
                    emblem.append(s.p["parts"].pop())
        s.p["parts"][0:0]=emblem
    else:
        run=depth/2
        s.solid("Front roof",[roof_x,roof_base,ridge+run/2],[roof_w,rise,run],
                [trim,side,side,side,roof,trim],"wedge",axis="z",direction=-1)
        s.solid("Back roof",[roof_x,roof_base,ridge-run/2],[roof_w,rise,run],
                [side,trim,side,side,rear_roof,trim],"wedge",axis="z",direction=1)
        ridge_art=s.material(r.get("ridge_art",[roof_x,top+max(0,roof_rows//3-2),roof_w,2]))
        s.solid("Ridge",[roof_x,roof_base+rise-1,ridge],[roof_w,2,2],[ridge_art]*6)
    if "badge" in r and r["kind"]!="hipped":
        q=r["badge"]
        angle=-math.atan2(depth/2-2,rise)
        along=r.get("badge_along",.48)
        cy=roof_base+rise*along
        cz=front-(depth/2-2)*along
        pos=[q[0],cy-q[3]*math.cos(angle)/2+1,cz-q[3]*math.sin(angle)/2+1]
        before=len(s.p["parts"])
        s.relief("Roof emblem",q,-1,1,angles=[math.degrees(angle),0,0],pos=pos)
        if len(s.p["parts"])>before:
            s.p["parts"].insert(0,s.p["parts"].pop())
    for i,v in enumerate(r.get("roof_details",[])):
        material=s.material(v["art"])
        s.solid(f"Roof fixture {i+1}",[v["x"],roof_base+rise-1,ridge+v.get("z",0)],v["size"],[material]*6)


def foliage(s, r):
    w,h=s.w,s.h
    profile=r["profile"]
    if profile=="broad_tree":
        # Authored canopy envelope excludes the surrounding forest shadow and
        # fragments of neighbouring crowns painted into the same metatiles.
        spans=[(9,23),(8,24),(8,24),(7,25),(6,26),(5,27),(6,26),(5,27),
               (4,28),(3,29),(4,28),(3,29),(3,29),(2,30),(3,29),(2,30),
               (1,31),(2,30),(2,30),(3,29),(5,27),(6,26),(6,26),(7,25),(7,25)]
        trunk=[12,24,8,6]
        def obj(x,y,m):
            return m[0]==2 and ((y<len(spans) and spans[y][0]<=x<spans[y][1] and m[1] in (1,2,3,4)) or
                   (in_rect(x,y,trunk) and m[1] in (6,8)))
        baseline=30
    elif profile=="slender_tree":
        trunk=[4,24,8,6]
        def obj(x,y,m):
            return m[0]==2 and ((y<25 and m[1] in (1,2,3,4)) or (in_rect(x,y,trunk) and m[1] in (6,8)))
        baseline=30
    else:
        trunk=None
        bounds=r.get("object_rect",[0,0,w,h-1])
        def obj(x,y,m):
            return m[0]==r.get("palette",2) and m[1] in r.get("indices",[1,2,3,4]) and in_rect(x,y,bounds)
        baseline=r.get("baseline",bounds[1]+bounds[3])
    if profile in ("broad_tree", "slender_tree"):
        # These tree tiles include clipped neighbouring crowns/trunks and a
        # forest shadow apron above as well as below the trunk. They are source
        # occlusion, not floor art. Keep the authored object silhouette intact;
        # the shadow/occlusion role reveals local ground beneath the leftovers.
        s.mark(obj, lambda x,y,m: (m[0]==2 and m[1] in (1,2,3,4,6,8,15)) or m[0] in r.get("occluder_palettes", []))
    else:
        s.mark(obj,lambda x,y,m: y>h*.65 and m[0]==r.get("shadow_palette",2) and m[1] in r.get("shadow_indices",[3,4,15]))
    # Coherent horizontal cross-sections are revolved through depth. Palette
    # brightness never sets depth; the source silhouette and recipe do.
    bands=collections.defaultdict(list)
    for y in range(h):
        xs=[x for x in range(w) if s.obj[y*w+x]]
        if not xs:continue
        center=(min(xs)+max(xs)+1)/2
        radius=(max(xs)-min(xs)+1)/2
        for x in xs:
            half=max(1,round(math.sqrt(max(0,radius*radius-(x+.5-center)**2))*r.get("depth_ratio",.85)))
            wood=bool(trunk and in_rect(x,y,trunk) and s.meta[y*w+x][1] in (6,8))
            bands[(wood,half)].append((x,y))
    # Round props stand on the centre of the source's bottom map cell. They
    # expand around that ground anchor, rather than behind a facade plane.
    center_z=r.get("center_z",0)
    edge_art=s.material(r.get("edge_art",[max(1,w//4),3,max(2,w//2),min(h-5,20)]))
    for (wood,half),points in sorted(bands.items(),reverse=True):
        pixels=set(points)
        label='Trunk' if wood else 'Stone' if r["kind"]=="rock" else 'Canopy'
        s.relief(f"{label} depth {half*2}px",[0,0,w,baseline],center_z-half,center_z+half,
                 lambda x,y,pixels=pixels:(x,y) in pixels,ybase=0)
        if not wood and profile != "bush":
            s.p["parts"][-1]["side_art"]=dict(region=edge_art)

    if trunk:
        # Several forest variants paint shadow over the lowest trunk row.
        # Continue the last visible wood cross-section down to the ground,
        # repeating its native texels instead of leaving a one-voxel air gap.
        wood_rows=[y for (wood,half),points in bands.items() if wood for x,y in points]
        if wood_rows:
            last=max(wood_rows)
            for (wood,half),points in sorted(bands.items(),reverse=True):
                if not wood:continue
                foot={point for point in points if point[1]==last}
                if not foot:continue
                for floor_y in range(baseline-last-1):
                    s.relief(f"Trunk foot {floor_y+1} depth {half*2}px",[0,last,w,1],center_z-half,center_z+half,
                             lambda x,y,foot=foot:(x,y) in foot,ybase=floor_y)

    if profile=="broad_tree":
        # The source unit repeats every 32px, but its pointed crown extends
        # five pixels into the preceding row (General Grass_TreeLeft/Right).
        # Complete that shape for every tree, including crowns hidden behind
        # the preceding trunk. Native canopy texels provide the inferred art.
        cap_bands=collections.defaultdict(set)
        for y,radius in enumerate((1,2,3,5,6)):
            for x in range(16-radius,16+radius):
                half=max(1,round(math.sqrt(radius*radius-(x+.5-16)**2)*r.get("depth_ratio",.85)))
                cap_bands[half].add((x,y))
        for half,points in sorted(cap_bands.items(),reverse=True):
            s.relief(f"Crown depth {half*2}px",[10,0,12,5],center_z-half,center_z+half,
                     lambda x,y,points=points:(x,y) in points,ybase=baseline)
            s.p["parts"][-1]["side_art"]=dict(region=edge_art)


def tree_ground(s, r):
    """Clear source-only crown overhangs; retain grass, paths and other artwork."""
    assert r["crown_side"] in ("left", "right") and s.w==s.h==16
    def crown(x,y):
        if not 11 <= y < 16:return False
        radius=(1,2,3,5,6)[y-11]
        return x>=16-radius if r["crown_side"]=="left" else x<radius
    s.mark(lambda x,y,m:False,
           lambda x,y,m:crown(x,y) and m[0]==2 and m[1] in (1,2,3,4))
    assert sum(s.shadow)==17, f"{s.p['name']}: incomplete canonical crown; inspect this source"


def grass(s, r):
    """Four upright voxel clumps; the elevated sprite does not measure height.

    The source supplies leaf art, pale highlights and retained ground pixels. A
    buried root layer joins the clumps below the recovered ground. The patch
    bends with authored land; gaps between blades expose the source floor.
    Tree-crown variants share ownership with their former cleanup recipe.
    """
    assert s.w == s.h == 16
    peak = r.get("blade_height_px", 12)
    assert type(peak) is int and 6 <= peak <= 24, "Grass blade height must be 6..24 whole source pixels"
    if "crown_side" in r:
        tree_ground(s,r)
    crown = list(s.shadow)
    s.mark(lambda x,y,m:m[0]==2 and m[1] in (4,12,15) and not crown[y*16+x],
           lambda x,y,m:crown[y*16+x])
    s.p["follow_ground"] = True
    def swatch(index):
        at = next(i for i,m in enumerate(s.meta) if m == (2,index) and s.obj[i])
        return [at%16,at//16,1,1]
    leaf, shade, highlight = (swatch(i) for i in (15,4,12))
    s.solid("Buried roots",[0,-2,0],[16,1,16],[shade]*6)
    # The sprite mixes projected height with ground depth. Author blade height
    # separately; keep the footprint, one-pixel steps and native texel scale.
    blades = [(-2,-2,3),(0,-2,5),(2,0,4),(-2,1,4),(0,2,5),(0,0,6)]
    for qy in range(2):
        for qx in range(2):
            patch=s.material([qx*8,qy*8,8,8])
            # A low stepped leaf base keeps each tuft coherent while leaving
            # almost half the tile open to pale ground, including its perimeter.
            s.solid(f"Tuft base {qx},{qy}:wide",[qx*8+1,0,-4+qy*8],[6,1,4],[patch]*6)
            s.solid(f"Tuft base {qx},{qy}:deep",[qx*8+2,0,-4+qy*8],[4,1,6],[patch]*6)
            for i,(dx,dz,relative_height) in enumerate(blades):
                height = round(relative_height * peak / 6)
                x,z=qx*8+3+dx,-4+qy*8+dz
                # Keep source leaf texture on the sides, with green shoulders
                # and sparse pale tips instead of shadow texels on every face.
                cap = highlight if i%3 == 1 else leaf
                s.solid(f"Grass blade {qx},{qy}:{i}",[x,-1,z],[2,height,2],
                        [patch,patch,patch,patch,leaf,shade])
                s.solid(f"Grass tip {qx},{qy}:{i}",[x, height-1,z],[1,1,1],
                        [patch,patch,patch,patch,cap,shade])


def sign(s,r):
    w,h=s.w,s.h
    panel=r.get("panel",[0,0,w,min(13,h)])
    baseline=r.get("baseline",h)
    palettes=set(r.get("object_palettes",range(16)))
    s.mark(lambda x,y,m: m[0] in palettes and (in_rect(x,y,panel) or any(in_rect(x,y,q) for q in r.get("legs",[[0,13,3,h-13],[w-3,13,3,h-13]]))),
           lambda x,y,m:y>=baseline and m[0] in palettes)
    s.relief("Sign face",panel,-3,0,ybase=baseline-panel[1]-panel[3])
    for i,q in enumerate(r.get("legs",[[0,13,3,h-13],[w-3,13,3,h-13]])):
        s.relief(f"Post {i+1}",q,-3,0,ybase=baseline-q[1]-q[3])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recipes",type=Path,default=ROOT/"recipes/voxel-world-recipes.json")
    parser.add_argument("--out",type=Path,default=ROOT/"mod-assets/voxel-world-v6.json")
    parser.add_argument("--evidence",type=Path,default=ROOT/"build/voxel-world-source")
    parser.add_argument("--experimental-foliage",action="store_true",
                        help="Include unapproved grass models for local art comparisons.")
    args=parser.parse_args()
    data=json.loads(args.recipes.read_text())
    catalog=json.loads((CATALOG/"catalog.json").read_text())
    indexed={a["id"]:a for a in catalog["assets"]}
    args.evidence.mkdir(parents=True,exist_ok=True)
    # Keep the house the user accepted byte-for-byte at the pattern level.
    patterns=copy.deepcopy(json.loads((ROOT/"mod-assets/voxel-house-v6.json").read_text())["patterns"])
    def matcher(p):return json.dumps({k:p[k] for k in ("w","extent","ids","mask","tiles")},sort_keys=True)
    seen={matcher(p):p["id"] for p in patterns}
    ledger=[]
    for recipe in data["recipes"]:
        if recipe["kind"]=="grass" and not args.experimental_foliage:
            if "crown_side" not in recipe:
                continue
            recipe={**recipe,"kind":"tree_ground",
                    "name":"Tree crown cleanup - "+recipe["crown_side"]}
        s=Source(indexed[recipe["asset"]],recipe)
        key=matcher(s.p)
        if key in seen:
            ledger.append(dict(name=recipe["name"],asset=recipe["asset"],reused=seen[key]))
            continue
        if recipe["kind"] in ("gable","flat","hipped"):building(s,recipe)
        elif recipe["kind"] in ("foliage","rock"):foliage(s,recipe)
        elif recipe["kind"]=="sign":sign(s,recipe)
        elif recipe["kind"]=="tree_ground":tree_ground(s,recipe)
        elif recipe["kind"]=="grass":grass(s,recipe)
        else:raise ValueError(recipe["kind"])
        p=s.finish(args.evidence)
        patterns.append(p);seen[key]=p["id"]
        ledger.append(dict(id=p["id"],name=p["name"],asset=recipe["asset"],kind=recipe["kind"],
            parts=len(p["parts"]),source=p["source"],object_pixels=sum(s.obj),shadow_pixels=sum(s.shadow)))
        print(f"{p['name']}: {len(p['parts'])} editable parts",flush=True)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(dict(version=6,patterns=patterns),indent=2)+"\n")
    report=dict(pack=str(args.out),sha256=hashlib.sha256(args.out.read_bytes()).hexdigest(),
        patterns=len(patterns),recipes=ledger,geometry_checks="pending",visual_review="pending",
        assumptions=data.get("assumptions",[]))
    (args.evidence/"recipes.json").write_text(json.dumps(report,indent=2)+"\n")
    print(f"Wrote {len(patterns)} families to {args.out}")


if __name__=="__main__":main()
