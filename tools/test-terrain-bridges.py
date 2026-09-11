# SPDX-License-Identifier: GPL-3.0-or-later
"""Source-free contract checks for the bridge compiler; no game assets needed."""
import copy
import unittest

from terrain_bridges import apply_bridge, bridge_guard


def fixture():
    blocks = [0x3001, 0x3002, 0x1003, 0x3001]
    definitions = {i: dict(id=i, attr=i, entries=[i] * 8) for i in range(1, 6)}
    cells = [dict(x=7+i%2, y=7+i//2, expected=packed, underlay=-1,
                  surfaces=[dict(layer=packed >> 12, height=16, thickness=16,
                                 kind='ground', top=-1, side=5)]) for i, packed in enumerate(blocks)]
    recipe = dict(name='synthetic boardwalk', water_cells=[[0, 1]], deck_cells=[[0, 0], [1, 0]],
                  deck_height=16, water_height=8, thickness=4, water_layer=1, water_tile=4, side_tile=5)
    recipe['guard'] = bridge_guard(2, 2, blocks, definitions, recipe)
    return blocks, definitions, cells, recipe


class Bridges(unittest.TestCase):
    def test_stack_and_independent_ownership(self):
        blocks, definitions, cells, recipe = fixture()
        before = copy.deepcopy((blocks, definitions, cells, recipe))
        result, report = apply_bridge(2, 2, blocks, definitions, cells, recipe)
        self.assertEqual(result[0]['surfaces'], [
            dict(layer=1, height=8, thickness=0, kind='water', top=4, side=-1),
            dict(layer=3, height=16, thickness=4, kind='deck', top=1, side=5)])
        self.assertEqual(result[1]['surfaces'][1]['top'], 2)
        self.assertEqual(result[0]['underlay'], 4)
        self.assertEqual(result[2]['surfaces'], [dict(layer=1, height=8, thickness=0, kind='water', top=3, side=-1)])
        self.assertEqual(result[2]['underlay'], -1)
        self.assertEqual(result[3], cells[3])
        self.assertEqual([(c['x'], c['y'], c['expected']) for c in result],
                         [(7, 7, 0x3001), (8, 7, 0x3002), (7, 8, 0x1003), (8, 8, 0x3001)])
        self.assertEqual((report['deck_cells'], report['water_cells'], report['clearance']), (2, 1, 4))
        result[3]['surfaces'][0]['height'] = 99
        result[0]['surfaces'][0]['top'] = 99
        self.assertEqual((blocks, definitions, cells, recipe), before)

    def test_guard_is_order_independent_and_binds_labels(self):
        blocks, definitions, cells, recipe = fixture()
        recipe['deck_cells'].reverse()
        self.assertEqual(bridge_guard(2, 2, blocks, dict(reversed(list(definitions.items()))), recipe), recipe['guard'])
        recipe['water_cells'], recipe['deck_cells'] = recipe['deck_cells'], recipe['water_cells']
        self.assertNotEqual(bridge_guard(2, 2, blocks, definitions, recipe), recipe['guard'])

    def test_source_and_material_changes_are_refused(self):
        for change in ('packed', 'attribute', 'entry', 'water_material', 'side_material', 'missing_definition', 'missing_guard'):
            with self.subTest(change=change):
                blocks, definitions, cells, recipe = fixture()
                if change == 'packed': blocks[0] ^= 0x400
                if change == 'attribute': definitions[2]['attr'] ^= 1
                if change == 'entry': definitions[1]['entries'][7] ^= 1
                if change == 'water_material': definitions[4]['entries'][0] ^= 1
                if change == 'side_material': definitions[5]['entries'][3] ^= 1
                if change == 'missing_definition': del definitions[2]
                if change == 'missing_guard': del recipe['guard']
                before = copy.deepcopy(cells)
                with self.assertRaises(ValueError): apply_bridge(2, 2, blocks, definitions, cells, recipe)
                self.assertEqual(cells, before)

    def test_geometry_and_numeric_refusals(self):
        for key, value in [('water_height', 16), ('water_height', -1), ('thickness', 8),
                           ('thickness', 0), ('deck_height', 257), ('deck_height', True),
                           ('water_height', 8.0), ('water_layer', 3), ('water_layer', 0),
                           ('water_layer', 15), ('water_tile', 1023), ('side_tile', False)]:
            with self.subTest(key=key, value=value):
                blocks, definitions, cells, recipe = fixture()
                recipe[key] = value
                with self.assertRaises(ValueError): apply_bridge(2, 2, blocks, definitions, cells, recipe)

    def test_selection_refusals(self):
        for value in ([], [[0, 0], [0, 0]], [[0, 1]], [[2, 0]], [[-1, 0]], [[True, 0]], [[0.0, 0]], [[0]]):
            with self.subTest(value=value):
                blocks, definitions, cells, recipe = fixture()
                recipe['deck_cells'] = value
                with self.assertRaises(ValueError): bridge_guard(2, 2, blocks, definitions, recipe)

    def test_preexisting_terrain_and_late_failure_are_atomic(self):
        for change in ('missing', 'duplicate', 'expected', 'height', 'grade', 'stack', 'material', 'bool'):
            with self.subTest(change=change):
                blocks, definitions, cells, recipe = fixture()
                last = cells[2]
                if change == 'missing': cells.pop(2)
                if change == 'duplicate': cells.append(copy.deepcopy(last))
                if change == 'expected': last['expected'] ^= 1
                if change == 'height': last['surfaces'][0].update(height=17, thickness=17)
                if change == 'grade': last['surfaces'][0]['rise_z'] = 1
                if change == 'stack': last['surfaces'] *= 2
                if change == 'material': last['surfaces'][0]['top'] = 4
                if change == 'bool': last['surfaces'][0]['rise_x'] = False
                before = copy.deepcopy(cells)
                with self.assertRaises(ValueError): apply_bridge(2, 2, blocks, definitions, cells, recipe)
                self.assertEqual(cells, before)

    def test_undefined_and_ambiguous_deck_layers(self):
        for packed in (1023, 0x33ff, 1, 0xf001, 0x1001, True, 65536):
            with self.subTest(packed=packed):
                blocks, definitions, cells, recipe = fixture()
                blocks[0] = packed
                cells[0]['expected'] = packed
                cells[0]['surfaces'][0]['layer'] = packed >> 12
                with self.assertRaises(ValueError):
                    recipe['guard'] = bridge_guard(2, 2, blocks, definitions, recipe)
                    apply_bridge(2, 2, blocks, definitions, cells, recipe)


if __name__ == '__main__':
    unittest.main()
