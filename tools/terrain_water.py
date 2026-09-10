"""Explicit level-water constraints for the original regional terrain compiler.

Behavior identifies the selected source material, never its physical height.
The authored scope, material membership and adjacent shore cells are guarded.
"""
import hashlib
import json


def select_water(width, height, blocks, attributes, regions):
    """Return water heights, flat water/shore pins and review summaries.

    Each author-selected region has bounds [x,y,w,h], behavior IDs, height in
    pixels, shore width (0..2 cells), and a SHA-256 guard from inspected source.
    Shore pins include diagonal neighbours so mixed shoreline texels stay flat.
    """
    if len(blocks)!=width*height:
        raise ValueError('Water source dimensions do not match')
    water, pins, summaries, names = {}, {}, [], set()
    for r in regions:
        if not isinstance(r.get('name'),str) or not r['name'] or r['name'] in names:
            raise ValueError('Water regions need distinct names')
        names.add(r['name'])
        bounds=r.get('bounds',[])
        if len(bounds)!=4 or any(type(v) is not int for v in bounds):
            raise ValueError('Water bounds must be four integers')
        x,y,w,h=bounds
        if not (0<=x<x+w<=width and 0<=y<y+h<=height):
            raise ValueError('Water bounds outside source map')
        level,shore=r.get('height'),r.get('shore')
        if type(level) is not int or not 0<=level<=256 or type(shore) is not int or not 0<=shore<=2:
            raise ValueError('Water height/shore width outside authoring limits')
        behaviors=r.get('behaviors',[])
        if not behaviors or any(type(v) is not int or not 0<=v<=255 for v in behaviors) or len(set(behaviors))!=len(behaviors):
            raise ValueError('Invalid water behavior selection')
        selected=set()
        for cy in range(y,y+h):
            for cx in range(x,x+w):
                packed=blocks[cy*width+cx]
                attr=attributes.get(packed&1023)
                if attr is None: raise ValueError('Missing water source attributes')
                if attr&255 in behaviors: selected.add((cx,cy))
        if not selected: raise ValueError(f"{r['name']}: empty water selection")
        fringe={(cx+dx,cy+dy) for cx,cy in selected for dy in range(-shore,shore+1)
                for dx in range(-shore,shore+1) if 0<=cx+dx<width and 0<=cy+dy<height}
        # Guard the full authored selection bounds plus any shore outside them:
        # changed membership or shore cell identity needs review before compiling.
        guarded=fringe|{(cx,cy) for cy in range(y,y+h) for cx in range(x,x+w)}
        rows=[(cx,cy,blocks[cy*width+cx],attributes.get(blocks[cy*width+cx]&1023))
              for cx,cy in sorted(guarded,key=lambda p:(p[1],p[0]))]
        if any(row[3] is None for row in rows): raise ValueError('Missing shoreline source attributes')
        digest=hashlib.sha256(json.dumps(rows,separators=(',',':')).encode()).hexdigest()
        if r.get('guard')!=digest:
            raise ValueError(f"{r['name']}: water/shore source guard changed (observed {digest}); inspect the source before updating the recipe")
        for p in sorted(fringe,key=lambda p:(p[1],p[0])):
            if p in pins and pins[p]!=level: raise ValueError('Conflicting water/shore heights')
            pins[p]=level
        for p in sorted(selected,key=lambda p:(p[1],p[0])):
            if p in water: raise ValueError('Overlapping water selections')
            water[p]=level
        summaries.append(dict(name=r['name'],height_pixels=level,water_cells=len(selected),
                              shore_cells=len(fringe-selected),guard=digest))
    return water,pins,summaries
