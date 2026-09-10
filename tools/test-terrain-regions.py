"""Source-free regional terrain tests; no game data, image assets or GL needed."""
import unittest
import copy

from terrain_regions import origins, solve
from terrain_water import select_water


def region(w=6, h=4, value=16, fixed=True):
    return dict(width=w, height=h, seed=lambda x,y:value,
                fixed=[(0,0,[value]*4)] if fixed else [], connections=[], cliffs=[])


def connect(maps, a, b, direction, offset):
    reverse={'up':'down','down':'up','left':'right','right':'left'}
    maps[a]['connections'].append(dict(map=b,direction=direction,offset=offset))
    maps[b]['connections'].append(dict(map=a,direction=reverse[direction],offset=-offset))


class TerrainRegions(unittest.TestCase):
    def assert_continuous(self, maps, cells):
        for name,m in maps.items():
            cuts={(a,x,y) for a,x,y,_,_ in m['cliffs']}
            for (x,y),c in cells[name].items():
                if x+1<m['width'] and ('e',x,y) not in cuts:
                    self.assertEqual((c[1],c[3]),(cells[name][x+1,y][0],cells[name][x+1,y][2]))
                if y+1<m['height'] and ('s',x,y) not in cuts:
                    self.assertEqual(c[2:],cells[name][x,y+1][:2])

    def test_offset_connections_and_integer_rounding(self):
        # Different anchors make a real grade. The partly overlapping boundary
        # must agree after rounding, from either map chosen as the origin.
        maps={'a':region(w=3,h=5,value=9),'b':region(w=4,h=3,value=17)}
        maps['b']['fixed']=[(3,2,[17]*4)]
        connect(maps,'a','b','right',2)
        a,report=solve(maps,'a'); b,_=solve(maps,'b')
        self.assertEqual(a,b)
        self.assertEqual(report['shared_map_edges'],3)
        self.assertEqual(report['origins']['b'],[3,2])
        for y in range(3):
            self.assertEqual((a['a'][2,y+2][1],a['a'][2,y+2][3]),
                             (a['b'][0,y][0],a['b'][0,y][2]))
        self.assert_continuous(maps,a)
        self.assertTrue(all(type(v) is int and 9<=v<=17 for c in a.values() for p in c.values() for v in p))

    def test_north_south_offset(self):
        maps={'a':region(5,3),'b':region(3,4)}
        connect(maps,'a','b','up',1)
        cells,report=solve(maps,'a')
        self.assertEqual(report['origins']['b'],[1,-4])
        self.assertEqual(report['shared_map_edges'],3)
        self.assertTrue(all(p==[16]*4 for c in cells.values() for p in c.values()))

    def test_short_ledge_has_full_interior_drop_and_walkable_ends(self):
        maps={'a':region(fixed=False)}
        maps['a']['cliffs']=[('s',x,1,16,8) for x in range(1,5)]
        cells,_=solve(maps,'a');c=cells['a']
        self.assertEqual(c[2,1][2:],[16,16]);self.assertEqual(c[2,2][:2],[8,8])
        self.assertEqual(c[1,1][2],c[1,2][0])
        self.assertEqual(c[4,1][3],c[4,2][1])
        self.assert_continuous(maps,cells)

    def test_reversed_elbow_has_one_continuous_cliff(self):
        maps={'a':region(w=7,h=5,fixed=False)}
        maps['a']['cliffs']=[('s',x,1,24,16) for x in (1,2)]+[('e',2,2,16,24)]+[
            ('s',x,2,24,16) for x in (3,4,5)]
        cells,_=solve(maps,'a');c=cells['a']
        self.assertEqual([c[2,2][1],c[2,2][3]],[16,16])
        self.assertEqual([c[3,2][0],c[3,2][2]],[24,24])
        self.assert_continuous(maps,cells)

    def test_conflicting_shared_anchors_rejected(self):
        maps={'a':region(1,1,8),'b':region(1,1,16)}
        connect(maps,'a','b','right',0)
        with self.assertRaisesRegex(ValueError,'Contradictory'): solve(maps,'a')

    def test_unanchored_and_isolated_cliff_component_rejected(self):
        with self.assertRaisesRegex(ValueError,'Unanchored'): solve({'a':region(fixed=False)},'a')
        m=region();m['cliffs']=[('s',x,1,16,8) for x in range(6)];m['pin_cliffs']=False
        with self.assertRaisesRegex(ValueError,'Unanchored'): solve({'a':m},'a')

    def test_invalid_map_graphs_rejected(self):
        with self.assertRaisesRegex(ValueError,'disconnected'): origins({'a':region(),'b':region()},'a')
        maps={'a':region(),'b':region()};connect(maps,'a','b','right',10)
        with self.assertRaisesRegex(ValueError,'no shared edge'): solve(maps,'a')
        maps={'a':region(),'b':region()};connect(maps,'a','b','right',0)
        maps['b']['connections'][0]['offset']=1
        with self.assertRaisesRegex(ValueError,'Inconsistent'): origins(maps,'a')
        maps={'a':region(),'b':region(),'c':region()}
        connect(maps,'a','b','right',0);connect(maps,'a','c','right',1)
        with self.assertRaisesRegex(ValueError,'Overlapping'): origins(maps,'a')

    def test_malformed_authoring_is_rejected(self):
        mutations=[dict(width=0),dict(width=2.5),dict(height=101),dict(seed=lambda x,y:-1),
                   dict(fixed=[(0,0,[257]*4)]),dict(fixed=[(9,0,[1]*4)]),dict(fixed=[(0,0,[1])]),
                   dict(cliffs=[('e',1.5,1,8,0)]),dict(cliffs=[('s',1,1,8.5,0)]),
                   dict(cliffs=[('s',1,3,8,0)]),dict(cliffs=[('s',1,1,8,0)]*2)]
        for change in mutations:
            with self.subTest(change=change):
                m=region();m.update(change)
                with self.assertRaises(ValueError): solve({'a':m},'a')
        with self.assertRaisesRegex(ValueError,'anchor'): solve({'a':region()},'missing')
        maps={'a':region(),'b':region()};connect(maps,'a','b','right',.5)
        with self.assertRaisesRegex(ValueError,'offset'): solve(maps,'a')


class LevelWater(unittest.TestCase):
    def setUp(self):
        self.width,self.height=6,5
        self.blocks=[1]*30
        for x,y in ((2,1),(2,2),(3,2)): self.blocks[y*6+x]=2
        self.attributes={1:0,2:16}
        # Frozen guard for this original synthetic pool and its dry shore.
        self.recipe=dict(name='pool',bounds=[1,0,4,4],behaviors=[16],height=12,shore=1,
                         guard='46d730a3adbc35acbadafbc193ac5fbb0bbe86c737b5a528f8be6363a8e6dd8e')

    def select(self, recipes=None):
        return select_water(self.width,self.height,self.blocks,self.attributes,
                            recipes if recipes is not None else [self.recipe])

    def test_concave_pool_and_shore_stay_level_against_graded_land(self):
        water,pins,report=self.select()
        self.assertEqual(set(water),{(2,1),(2,2),(3,2)})
        self.assertNotIn((1,0),water);self.assertEqual(pins[1,0],12)
        m=region(6,5,value=24,fixed=False)
        # The last cell touches the shore at one corner, so its near edge must
        # begin at 12 while the free far edge may rise to 24.
        m['fixed']=[(x,y,[v]*4) for (x,y),v in pins.items()]+[(5,4,[12,12,24,24])]
        cells,_=solve({'pool':m},'pool')
        for p in pins: self.assertEqual(cells['pool'][p],[12]*4)
        self.assertEqual(cells['pool'][5,4],[12,12,24,24])
        self.assertEqual(report[0]['water_cells'],3)
        self.assertTrue(any(len(set(v))>1 for v in cells['pool'].values()))
        TerrainRegions.assert_continuous(self,{'pool':m},cells)

    def test_source_behavior_does_not_choose_height(self):
        self.recipe['height']=7
        water,pins,_=self.select()
        self.assertEqual(set(water.values()),{7})
        self.assertEqual(set(pins.values()),{7})

    def test_changed_membership_shore_or_attributes_rejected(self):
        original=self.blocks.copy()
        for x,y in ((2,1),(1,0),(5,4)):
            self.blocks=original.copy();self.blocks[y*6+x]=2 if self.blocks[y*6+x]==1 else 1
            # (5,4) is outside the guarded selection and shore: unrelated edits
            # must not invalidate this water body.
            if (x,y)==(5,4): self.select()
            else:
                with self.assertRaisesRegex(ValueError,'source guard changed'): self.select()
        self.blocks=original
        self.attributes[2]=16|256
        with self.assertRaisesRegex(ValueError,'source guard changed'): self.select()

    def test_overlaps_and_contradictory_levels_rejected(self):
        second=copy.deepcopy(self.recipe);second['name']='other-pool'
        with self.assertRaisesRegex(ValueError,'Overlapping'): self.select([self.recipe,second])
        second['height']=8
        with self.assertRaisesRegex(ValueError,'Conflicting'): self.select([self.recipe,second])
        with self.assertRaisesRegex(ValueError,'distinct'): self.select([self.recipe,self.recipe])

    def test_invalid_scope_and_empty_water_rejected(self):
        changes=[dict(bounds=[-1,0,4,4]),dict(bounds=[1,0,9,4]),dict(bounds=[1.5,0,4,4]),
                 dict(bounds=[1,0,4]),dict(height=-1),dict(height=257),dict(height=1.2),
                 dict(shore=3),dict(shore=-1),dict(behaviors=[]),dict(behaviors=[16,16]),
                 dict(behaviors=[300]),dict(behaviors=[15])]
        for change in changes:
            with self.subTest(change=change):
                r=dict(self.recipe,**change)
                with self.assertRaises(ValueError): self.select([r])
        self.assertEqual(self.select([]),({},{},[]))


if __name__=='__main__': unittest.main(verbosity=2)
