"""Source-free geometry intent for the bounded Route 101 correction."""
import importlib.util
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('seam',Path(__file__).with_name('terrain-seam-fixture.py'))
seam=importlib.util.module_from_spec(spec)
spec.loader.exec_module(seam)

class LedgeContact(unittest.TestCase):
    def test_barrier_is_descent_not_flat_grass(self):
        c=seam.route_corners()
        self.assertEqual(c[8,5],[16]*4)
        self.assertEqual(c[8,6],[16,16,8,8])
        self.assertEqual(c[8,7],[8]*4)
        self.assertTrue(c[9,13][0]>c[9,13][2])
        self.assertTrue(c[9,13][1]>c[9,13][3])

    def test_no_cracks_and_same_town_boundaries(self):
        c=seam.route_corners()
        for y in range(20):
            for x in range(20):
                if x<19:self.assertEqual(c[x,y][1::2],c[x+1,y][0::2])
                if y<19:self.assertEqual(c[x,y][2:],c[x,y+1][:2])
        for x in range(20):
            self.assertEqual(c[x,0][:2],[16,16])
            self.assertEqual(c[x,19][2:],[0,0])

if __name__=='__main__':unittest.main()
