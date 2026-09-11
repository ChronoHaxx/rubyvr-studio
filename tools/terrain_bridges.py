# SPDX-License-Identifier: GPL-3.0-or-later
"""Guarded, explicitly authored Water + Deck selections for v7 terrain.

Coordinates in recipes are source-body cells; generated terrain uses the
snapshot's seven-cell padding. Gameplay layers select surfaces, never heights.
This compiler refuses to overwrite previously sculpted or stacked terrain.
"""
import copy
import hashlib
import json
import re


def integer(value, lo, hi, label):
    if type(value) is not int or not lo <= value <= hi:
        raise ValueError(f'{label}: expected an integer in {lo}..{hi}')
    return value


def _selection(recipe, label, width, height):
    points = recipe.get(label)
    if not isinstance(points, (list, tuple)) or not points:
        raise ValueError(f'{label}: select at least one source cell')
    selected = set()
    for point in points:
        if not isinstance(point, (list, tuple)) or len(point) != 2:
            raise ValueError(f'{label}: coordinates must be [x, y]')
        x = integer(point[0], 0, width - 1, label + ' x')
        y = integer(point[1], 0, height - 1, label + ' y')
        if (x, y) in selected:
            raise ValueError(f'{label}: duplicate source cell {x},{y}')
        selected.add((x, y))
    return sorted(selected, key=lambda p: (p[1], p[0]))


def _source(width, height, blocks, definitions, recipe):
    integer(width, 1, 1009, 'width')
    integer(height, 1, 1010, 'height')
    if (width + 15) * (height + 14) > 10240:
        raise ValueError('Source dimensions exceed the native snapshot limit')
    if not isinstance(blocks, (list, tuple)) or len(blocks) != width * height:
        raise ValueError('Source dimensions do not match packed cells')
    if not isinstance(recipe, dict) or not isinstance(definitions, dict):
        raise ValueError('Recipe and source definitions must be dictionaries')
    water = _selection(recipe, 'water_cells', width, height)
    deck = _selection(recipe, 'deck_cells', width, height)
    if set(water) & set(deck):
        raise ValueError('Water and deck selections overlap')
    used = {integer(recipe.get(k), 0, 1022, k) for k in ('water_tile', 'side_tile')}
    rows = []
    for label, selected in (('water', water), ('deck', deck)):
        values = []
        for x, y in selected:
            packed = integer(blocks[y * width + x], 0, 65535, f'{label} source {x},{y}')
            if packed & 1023 == 1023:
                raise ValueError(f'{label} source {x},{y}: undefined metatile')
            used.add(packed & 1023)
            values.append([x, y, packed])
        rows.append([label, values])
    tiles = []
    # A bool key can alias an integer key in Python; reject it explicitly.
    if any(type(key) is not int for key in definitions):
        raise ValueError('Source definition keys must be integer metatile IDs')
    for tile in sorted(used):
        definition = definitions.get(tile)
        if not isinstance(definition, dict):
            raise ValueError(f'Missing source definition for metatile {tile}')
        if integer(definition.get('id'), 0, 1022, 'definition id') != tile:
            raise ValueError(f'Mismatched definition ID for metatile {tile}')
        attr = integer(definition.get('attr'), 0, 65535, 'attribute word')
        entries = definition.get('entries')
        if not isinstance(entries, (list, tuple)) or len(entries) != 8:
            raise ValueError(f'Metatile {tile} needs eight tile-entry words')
        tiles.append([tile, attr, [integer(e, 0, 65535, 'tile-entry word') for e in entries]])
    data = [width, height, rows, tiles, recipe['water_tile'], recipe['side_tile']]
    digest = hashlib.sha256(json.dumps(data, separators=(',', ':')).encode()).hexdigest()
    return water, deck, digest


def bridge_guard(width, height, blocks, definitions, recipe):
    """Fingerprint reviewed membership, packed source cells and all materials."""
    return _source(width, height, blocks, definitions, recipe)[2]


def apply_bridge(width, height, blocks, definitions, cells, recipe):
    """Return independent terrain cells and a report; never mutate the inputs."""
    water, deck, guard = _source(width, height, blocks, definitions, recipe)
    supplied = recipe.get('guard')
    if not isinstance(supplied, str) or not re.fullmatch(r'[0-9a-fA-F]{64}', supplied):
        raise ValueError('Bridge needs an existing SHA-256 source guard')
    if supplied.lower() != guard:
        raise ValueError(f'Bridge source guard changed (observed {guard}); inspect before updating')
    name = recipe.get('name')
    if not isinstance(name, str) or not name.strip():
        raise ValueError('Bridge needs a name')
    high = integer(recipe.get('deck_height'), 1, 256, 'deck height')
    low = integer(recipe.get('water_height'), 0, high - 1, 'water height')
    thickness = integer(recipe.get('thickness'), 1, high, 'deck thickness')
    if high - thickness <= low:
        raise ValueError('Deck underside must have positive clearance above water')
    layer = integer(recipe.get('water_layer'), 1, 14, 'water layer')
    if not isinstance(cells, list):
        raise ValueError('Terrain cells must be a list')
    wanted = {(x + 7, y + 7) for x, y in water + deck}
    found = {}
    for index, cell in enumerate(cells):
        if not isinstance(cell, dict):
            raise ValueError('Terrain cell must be a dictionary')
        x = integer(cell.get('x'), 7, width + 6, 'terrain x')
        y = integer(cell.get('y'), 7, height + 6, 'terrain y')
        if (x, y) not in wanted:
            continue
        if (x, y) in found:
            raise ValueError(f'Duplicate selected terrain cell {x},{y}')
        found[x, y] = index
        expected = blocks[(y - 7) * width + x - 7]
        if integer(cell.get('expected'), 0, 65535, 'expected cell') != expected:
            raise ValueError(f'Terrain source mismatch at {x},{y}')
        underlay = integer(cell.get('underlay'), -1, 1022, 'underlay')
        if underlay >= 0 and underlay not in definitions:
            raise ValueError('Missing existing underlay definition')
        surfaces = cell.get('surfaces')
        if not isinstance(surfaces, list) or len(surfaces) != 1 or not isinstance(surfaces[0], dict):
            raise ValueError(f'{x},{y}: bridge requires one flat Ground surface')
        surface = surfaces[0]
        for key, lo, hi in (('layer', 0, 15), ('height', 0, 256), ('thickness', 0, 256),
                            ('top', -1, 1022), ('side', -1, 1022), ('side_offset', 0, 15),
                            ('rise_x', -256, 256), ('rise_z', -256, 256), ('corner_delta', -512, 512)):
            value = surface.get(key, 0) if key in ('side_offset', 'rise_x', 'rise_z', 'corner_delta') else surface.get(key)
            integer(value, lo, hi, 'existing surface ' + key)
        if (surface.get('kind') != 'ground' or surface['height'] != high or
                surface['thickness'] != high or surface['layer'] != expected >> 12 or
                surface['top'] not in (-1, expected & 1023) or
                any(surface.get(k, 0) for k in ('rise_x', 'rise_z', 'corner_delta'))):
            raise ValueError(f'{x},{y}: refusing to replace nonmatching authored terrain')
    if set(found) != wanted:
        raise ValueError('A selected source cell is missing from generated terrain')
    for x, y in deck:
        original_layer = blocks[y * width + x] >> 12
        if original_layer not in range(1, 15) or original_layer == layer:
            raise ValueError(f'Deck {x},{y}: source layer is ambiguous or conflicts with water')
    # Validate everything before copying/writing, including the last selected cell.
    result = copy.deepcopy(cells)
    for selected, is_deck in ((water, False), (deck, True)):
        for x, y in selected:
            cell = result[found[x + 7, y + 7]]
            packed = blocks[y * width + x]
            cell['surfaces'] = [dict(layer=layer if is_deck else packed >> 12,
                                     height=low, thickness=0, kind='water',
                                     top=recipe['water_tile'] if is_deck else packed & 1023, side=-1)]
            if is_deck:
                cell['underlay'] = recipe['water_tile']
                cell['surfaces'].append(dict(layer=packed >> 12, height=high, thickness=thickness,
                                             kind='deck', top=packed & 1023, side=recipe['side_tile']))
    return result, dict(name=name, deck_cells=len(deck), water_cells=len(water),
                        deck_height=high, water_height=low, thickness=thickness,
                        clearance=high-thickness-low, guard=guard)
