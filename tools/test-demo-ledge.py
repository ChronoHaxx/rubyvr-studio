"""Source-free geometry intent for the bounded Route 101 correction."""
import importlib.util
import json
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('seam',Path(__file__).with_name('terrain-seam-fixture.py'))
seam=importlib.util.module_from_spec(spec)
spec.loader.exec_module(seam)

class LedgeContact(unittest.TestCase):
    def test_takeoff_is_half_cell_from_the_visible_drop(self):
        c=seam.route_corners()
        # Inspected native barrier rows, not values derived from route_cliffs().
        for xs,barrier,high,low in ((range(3,5),7,16,8),(range(7,10),6,16,8),(range(9,11),13,8,0)):
            for x in xs:
                # Walk-around grading can slope the approach near an endpoint;
                # the two lip corners, not the whole tile, define the drop.
                self.assertEqual(c[x,barrier-1][2:],[high]*2)
                self.assertEqual(c[x,barrier][:2],[low]*2)
                cut=('s',x,barrier-1)
                self.assertIn(cut,seam.route_cliffs())
                self.assertEqual(cut[2]+1-(barrier-.5),.5)
                self.assertEqual(c[x,barrier][2:],c[x,barrier+1][:2])

    def test_no_cracks_and_same_town_boundaries(self):
        c=seam.route_corners()
        for y in range(20):
            for x in range(20):
                if x<19 and ('e',x,y) not in seam.route_cliffs():
                    self.assertEqual(c[x,y][1::2],c[x+1,y][0::2])
                if y<19 and ('s',x,y) not in seam.route_cliffs():
                    self.assertEqual(c[x,y][2:],c[x,y+1][:2])
        for x in range(20):
            self.assertEqual(c[x,0][:2],[16,16])
            self.assertEqual(c[x,19][2:],[0,0])

    def test_elbow_is_connected_and_open_endpoints_taper(self):
        c=seam.route_corners()
        self.assertEqual(c[5,6][1::2],[16,16])
        self.assertEqual(c[6,6][0::2],[8,8])
        for x,y,k,nx,ny,nk in ((2,6,2,2,7,0),(10,5,3,10,6,1),(8,12,2,8,13,0),(11,12,3,11,13,1)):
            self.assertEqual(c[x,y][k],c[nx,ny][nk])
        self.assertEqual(c,seam.route_corners())

    def test_recipe_cuts_match_profile_without_moving_art_guards(self):
        recipe=json.loads((Path(__file__).resolve().parents[1]/'recipes/terrain-regions.json').read_text())
        route=next(m for m in recipe['maps'] if m['name']=='Route101')
        cuts=set()
        for stroke in route['cliffs']:
            axis=stroke['axis'];x,y=stroke['start']
            cuts.update((axis,x+i*(axis=='s'),y+i*(axis=='e')) for i in range(stroke['length']))
        self.assertEqual(cuts,seam.route_cliffs())
        guards={(g['start'][0]+i,g['start'][1]):value for g in route['guards'] for i,value in enumerate(g['ids'])}
        self.assertEqual(set(guards),seam.route_ledge_cells())
        self.assertEqual([guards[x,13] for x in range(8,12)],[213,135,135,214])
        self.assertNotIn((9,12),guards) # Geometry moved; source IDs did not.

if __name__=='__main__':unittest.main()
