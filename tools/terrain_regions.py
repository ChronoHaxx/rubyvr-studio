"""Original corner constraints for explicitly authored, connected terrain.

Source connections determine adjacency, never physical height. Callers supply
heights and cliff edges; joined corners share one value even after rounding.
"""
from collections import defaultdict, deque


def origins(maps, anchor):
    """Resolve cell origins, rejecting inconsistent loops and overlapping bodies."""
    if anchor not in maps:
        raise ValueError('Unknown regional anchor')
    for name, m in maps.items():
        if any(type(m[k]) is not int or not 1 <= m[k] <= 100 for k in ('width','height')):
            raise ValueError(f'{name}: unsupported region dimensions')
        for c in m.get('connections', []):
            if type(c['offset']) is not int:
                raise ValueError(f'{name}: connection offset must be an integer')
    result = {anchor: (0, 0)}
    queue = deque([anchor])
    while queue:
        name = queue.popleft()
        m = maps[name]
        x, y = result[name]
        for c in m.get('connections', []):
            other = c['map']
            if other not in maps:
                continue
            n = maps[other]
            d, offset = c['direction'], c['offset']
            if d == 'up': point = (x+offset, y-n['height'])
            elif d == 'down': point = (x+offset, y+m['height'])
            elif d == 'left': point = (x-n['width'], y+offset)
            elif d == 'right': point = (x+m['width'], y+offset)
            else: raise ValueError(f'{name}: unsupported field connection {d}')
            if other in result and result[other] != point:
                raise ValueError(f'Inconsistent connection origin: {name} -> {other}')
            if other not in result:
                result[other] = point
                queue.append(other)
    if set(result) != set(maps):
        raise ValueError('Regional selection is disconnected')
    names = list(result)
    for i, a in enumerate(names):
        ax, ay = result[a]
        for b in names[i+1:]:
            bx, by = result[b]
            if (max(ax,bx) < min(ax+maps[a]['width'],bx+maps[b]['width']) and
                    max(ay,by) < min(ay+maps[a]['height'],by+maps[b]['height'])):
                raise ValueError(f'Overlapping map bodies: {a}, {b}')
    return result


def solve(maps, anchor):
    """Return integer NW/NE/SW/SE corners and an independent edge report.

    Each map supplies seed(x,y), fixed corner triples and optional cliffs:
    (axis, x, y, before_height, after_height). Before is west for an east
    edge, north for a south edge. Shared cliff endpoints taper to zero.
    """
    positions = origins(maps, anchor)
    if sum(m['width']*m['height'] for m in maps.values()) > 20000:
        raise ValueError('Select at most 20000 source cells per regional solve')
    nodes, parent = {}, []
    for name, m in maps.items():
        for y in range(m['height']):
            for x in range(m['width']):
                for k in range(4):
                    nodes[name,x,y,k] = len(parent)
                    parent.append(len(parent))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    def join(a,b):
        parent[root(b)] = root(a)
    pairs = {'e': ((1,0),(3,2)), 's': ((2,0),(3,1))}
    cuts = {}
    for name,m in maps.items():
        cuts[name] = {}
        for axis,x,y,before,after in m.get('cliffs',[]):
            if (axis not in pairs or any(type(v) is not int for v in (x,y,before,after)) or
                    not (0 <= before <= 256 and 0 <= after <= 256)):
                raise ValueError(f'{name}: invalid cliff')
            nx,ny = x+(axis=='e'),y+(axis=='s')
            if not (0 <= x < m['width'] and 0 <= y < m['height'] and nx < m['width'] and ny < m['height']):
                raise ValueError(f'{name}: cliff outside region')
            if (axis,x,y) in cuts[name]:
                raise ValueError(f'{name}: duplicate cliff')
            cuts[name][axis,x,y] = (before,after)
        for y in range(m['height']):
            for x in range(m['width']):
                for axis in pairs:
                    nx,ny=x+(axis=='e'),y+(axis=='s')
                    if nx >= m['width'] or ny >= m['height'] or (axis,x,y) in cuts[name]:
                        continue
                    for a,b in pairs[axis]:
                        join(nodes[name,x,y,a],nodes[name,nx,ny,b])
    # World positions are derived from the source graph. Only declared pairs
    # join: incidental contact between unrelated map rectangles is not enough.
    seams = set()
    for name,m in maps.items():
        ox,oy=positions[name]
        for c in m.get('connections',[]):
            other=c['map']
            if other not in maps: continue
            n=maps[other];px,py=positions[other]
            d=c['direction']
            span=m['width'] if d in ('up','down') else m['height']
            matched=0
            for i in range(span):
                x,y=(i,0) if d=='up' else (i,m['height']-1) if d=='down' else (0,i) if d=='left' else (m['width']-1,i)
                nx,ny=x+ox-px+(d=='right')-(d=='left'),y+oy-py+(d=='down')-(d=='up')
                if not (0<=nx<n['width'] and 0<=ny<n['height']): continue
                corners=((0,2),(1,3)) if d=='up' else ((2,0),(3,1)) if d=='down' else ((0,1),(2,3)) if d=='left' else ((1,0),(3,2))
                for a,b in corners: join(nodes[name,x,y,a],nodes[other,nx,ny,b])
                seams.add(tuple(sorted(((name,x,y,d),(other,nx,ny,{'up':'down','down':'up','left':'right','right':'left'}[d])))))
                matched+=1
            if not matched: raise ValueError(f'Connection has no shared edge: {name} -> {other}')
    seeds, adjacent, fixed = defaultdict(list), defaultdict(set), {}
    def pin(i,value):
        i=root(i)
        if type(value) is not int or not 0<=value<=256:
            raise ValueError('Anchor height must be a whole pixel in 0..256')
        if i in fixed and fixed[i]!=value:
            raise ValueError(f'Contradictory corner anchors: {fixed[i]} vs {value}')
        fixed[i]=value
    for name,m in maps.items():
        for y in range(m['height']):
            for x in range(m['width']):
                seed=m['seed'](x,y)
                if type(seed) is not int or not 0<=seed<=256:
                    raise ValueError(f'{name}: invalid seed height')
                for k in range(4): seeds[root(nodes[name,x,y,k])].append(seed)
                for a,b in ((0,1),(0,2),(1,3),(2,3)):
                    ia,ib=root(nodes[name,x,y,a]),root(nodes[name,x,y,b])
                    adjacent[ia].add(ib);adjacent[ib].add(ia)
        for x,y,corners in m.get('fixed',[]):
            if (type(x) is not int or type(y) is not int or not 0<=x<m['width'] or
                    not 0<=y<m['height'] or len(corners)!=4):
                raise ValueError(f'{name}: invalid fixed cell')
            for k,value in enumerate(corners): pin(nodes[name,x,y,k],value)
        for (axis,x,y),(before,after) in cuts[name].items():
            if not m.get('pin_cliffs',True): continue
            nx,ny=x+(axis=='e'),y+(axis=='s')
            for a,b in pairs[axis]:
                ia,ib=root(nodes[name,x,y,a]),root(nodes[name,nx,ny,b])
                # Walking around the end joins its two corners. Pinning a
                # height jump there would recreate a wall across that path.
                if ia!=ib: pin(ia,before);pin(ib,after)
    # Every connected part of the corner graph needs an authored anchor.
    unseen=set(seeds)
    while unseen:
        stack=[unseen.pop()];anchored=False
        while stack:
            i=stack.pop();anchored |= i in fixed
            for j in adjacent[i]:
                if j in unseen: unseen.remove(j);stack.append(j)
        if not anchored: raise ValueError('Unanchored terrain component')
    values={i:fixed.get(i,sum(v)/len(v)) for i,v in seeds.items()}
    free=[i for i in values if i not in fixed]
    residual=0.0
    for iteration in range(4000):
        update={i:sum(values[j] for j in sorted(adjacent[i]))/len(adjacent[i]) for i in free}
        residual=max((abs(values[i]-v) for i,v in update.items()),default=0.)
        values.update(update)
        if residual < .0001: break
    else: raise ValueError('Regional grade constraints did not converge')
    result={name:{(x,y):[round(values[root(nodes[name,x,y,k])]) for k in range(4)]
                  for y in range(m['height']) for x in range(m['width'])} for name,m in maps.items()}
    continuous=0
    for name,m in maps.items():
        for (x,y),corners in result[name].items():
            for axis in pairs:
                nx,ny=x+(axis=='e'),y+(axis=='s')
                if nx>=m['width'] or ny>=m['height'] or (axis,x,y) in cuts[name]: continue
                if any(corners[a]!=result[name][nx,ny][b] for a,b in pairs[axis]):
                    raise AssertionError('Non-cliff edge lost continuity')
                continuous+=1
    return result,dict(origins={n:list(p) for n,p in positions.items()},shared_map_edges=len(seams),
        continuous_internal_edges=continuous,iterations=iteration+1,residual=residual)
