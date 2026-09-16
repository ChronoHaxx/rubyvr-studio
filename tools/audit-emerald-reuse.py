# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare local Gen 3 inputs without executing a mod or opening a window.

Reports candidate metatile correspondences, NOT accepted models. Palette RGB,
animation, collision in placements and multi-cell ownership need later review.
No downloads, generated game data, or modifications to either input tree.
"""
import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image


CASES = [
    ('primary/general', 'general', 'petalburg'),
    ('secondary/petalburg', 'general', 'petalburg'),
    ('primary/building', 'building', 'brendans_mays_house'),
    ('secondary/brendans_mays_house', 'building', 'brendans_mays_house'),
    ('secondary/pokemon_center', 'building', 'pokemon_center'),
]
PINS = {'primary/general': [245, 468, 469, 476, 477],
        'secondary/petalburg': [578, 579, 586, 587],
        'secondary/brendans_mays_house': [568, 569, 570, 571, 572, 576, 577, 578, 582, 586, 605, 691],
        'secondary/pokemon_center': [640, 641, 648, 649, 656, 657]}


def words(data):
    if len(data) % 2:
        raise ValueError('Truncated 16-bit source data')
    return struct.unpack('<' + 'H' * (len(data) // 2), data)


def signature(entries, attr, sheets):
    """Eight separate 8px layers, preserving flips, palette slots and index 0."""
    result = bytearray(struct.pack('<H', attr))
    for entry in entries:
        tile = entry & 1023
        sheet = sheets[tile // 512]
        tile %= 512
        sx, sy = tile % (sheet.width // 8) * 8, tile // (sheet.width // 8) * 8
        if sy + 8 > sheet.height:
            raise ValueError('Tile outside supplied indexed sheet')
        result.append(entry >> 12)
        for y in range(8):
            for x in range(8):
                px = sx + (7 - x if entry & 1024 else x)
                py = sy + (7 - y if entry & 2048 else y)
                value = sheet.getpixel((px, py))
                if not 0 <= value < 16:
                    raise ValueError('Source pixel outside the GBA 4-bit index range')
                result.append(value)
    return bytes(result)


def load(root, folder, primary, secondary):
    base = root / 'data/tilesets'
    sheets = []
    files = [base / folder / n for n in ('metatiles.bin', 'metatile_attributes.bin')]
    for sub in (f'primary/{primary}', f'secondary/{secondary}'):
        path = base / sub / 'tiles.png'
        files.append(path)
        with Image.open(path) as image:
            if image.mode != 'P' or image.width % 8 or image.height % 8:
                raise ValueError(f'Expected indexed 8px tile sheet: {path}')
            sheets.append(image.copy())
    entries, attrs = (words(p.read_bytes()) for p in files[:2])
    if len(entries) != len(attrs) * 8:
        raise ValueError(f'Metatile/attribute count mismatch: {folder}')
    rows, rejected = {}, {}
    offset = 512 if folder.startswith('secondary/') else 0
    for index, attr in enumerate(attrs):
        row = entries[index * 8:index * 8 + 8]
        try:
            sig = signature(row, attr, sheets)
        except ValueError as error:
            rejected[index + offset] = str(error)
            sig = None
        rows[index + offset] = (row, attr, sig)
    hashes = {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in files}
    return rows, rejected, hashes


def compare(ruby, emerald):
    candidates = defaultdict(list)
    for key, (_, _, sig) in ruby.items():
        if sig is not None:
            candidates[sig].append(key)
    same_definition, same_id, moved, absent, unresolved = [], [], {}, [], []
    for key, (row, attr, sig) in emerald.items():
        if key in ruby and ruby[key][:2] == (row, attr):
            same_definition.append(key)
        if sig is None:
            unresolved.append(key)
        elif key in ruby and ruby[key][2] == sig:
            same_id.append(key)
        elif candidates[sig]:
            moved[key] = candidates[sig]
        else:
            absent.append(key)
    return {'ruby_rows': len(ruby), 'emerald_rows': len(emerald),
            'same_id_definition_and_attribute': same_definition,
            'same_id_indexed_layers_and_attribute': same_id,
            'different_id_candidates': moved, 'no_indexed_candidate': absent,
            'unresolved_emerald_pixels': unresolved}


def selftest():
    a = Image.new('P', (16, 8), 0)
    for y in range(8):
        for x in range(8):
            a.putpixel((x, y), x + 1)
            a.putpixel((x + 8, y), 8 - x)
    original = signature((0,) * 8, 0, [a, a])
    flipped = signature((1 | 1024,) * 8, 0, [a, a])
    assert original == flipped, 'A relocated, horizontally flipped tile must match'
    assert original[3:11] == bytes(range(1, 9)), 'Decoded row must retain pixel order'
    assert original != signature((1,) * 8, 0, [a, a]), 'Ignored flips hide a mismatch'
    assert original != signature((0,) * 8, 1, [a, a]), 'Attribute changes must reject reuse'
    assert original != signature((0x1000,) * 8, 0, [a, a]), 'Palette slots are part of identity'
    changed = a.copy()
    changed.putpixel((0, 0), 9)
    changed_sig = signature((0,) * 8, 0, [changed, a])
    result = compare({7: ((0,) * 8, 0, original)},
                     {7: ((0,) * 8, 0, changed_sig), 9: ((1 | 1024,) * 8, 0, flipped)})
    assert result['same_id_definition_and_attribute'] == [7]
    assert result['same_id_indexed_layers_and_attribute'] == []
    assert result['different_id_candidates'] == {9: [7]}
    assert result['no_indexed_candidate'] == [7]
    for operation in (lambda: words(b'\0'), lambda: signature((400,) * 8, 0, [a, a])):
        try:
            operation()
        except ValueError:
            pass
        else:
            raise AssertionError('Truncated or missing source data must be rejected')
    print('PASS: pixel order, flips, attribute/palette guards, changed-art rejection, ID remapping and missing-input rejection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ruby', type=Path)
    parser.add_argument('--emerald', type=Path)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return
    if not args.ruby or not args.emerald or not args.out:
        parser.error('--ruby, --emerald and --out are required unless using --selftest')
    if any(args.out.resolve().is_relative_to(root.resolve()) for root in (args.ruby, args.emerald)):
        parser.error('--out must be outside both source input trees')
    report = {'schema': 1, 'scope': 'indexed-layer candidate mapping only; not model or visual acceptance',
              'ruby_inputs': {}, 'emerald_inputs': {}, 'cases': {}}
    for folder, primary, secondary in CASES:
        ruby, ruby_bad, ruby_hashes = load(args.ruby, folder, primary, secondary)
        emerald, emerald_bad, emerald_hashes = load(args.emerald, folder, primary, secondary)
        result = compare(ruby, emerald)
        result['rejected_ruby_pixels'] = ruby_bad
        result['rejected_emerald_pixels'] = emerald_bad
        result['sample_pins'] = {key: ('same_id' if key in result['same_id_indexed_layers_and_attribute']
            else result['different_id_candidates'].get(key, 'unresolved' if key in emerald_bad else 'no_candidate'))
            for key in PINS.get(folder, [])}
        report['cases'][folder] = result
        report['ruby_inputs'].update(ruby_hashes)
        report['emerald_inputs'].update(emerald_hashes)
        print(f"{folder}: {len(result['same_id_indexed_layers_and_attribute'])}/{len(emerald)} same-ID candidates; "
              f"{len(result['different_id_candidates'])} remapped; {len(result['no_indexed_candidate'])} unmatched; "
              f"{len(emerald_bad)} unresolved")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
