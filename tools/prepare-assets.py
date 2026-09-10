"""Generate local source catalog and editable starters; no downloads or uploads."""
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    if not (ROOT/'third_party/pokeruby/data/maps/map_groups.json').is_file():
        raise SystemExit('Provide the pinned pokeruby checkout described in docs/building.md first.')
    (ROOT/'build').mkdir(exist_ok=True)
    (ROOT/'mod-assets').mkdir(exist_ok=True)
    result = subprocess.run([sys.executable, str(ROOT/'tools/catalog-sprites.py')], cwd=ROOT)
    if result.returncode != 0:
        path = ROOT/'build/sprite-catalog/catalog.json'
        catalog = json.loads(path.read_text()) if path.exists() else {}
        # The source exporter deliberately returns 1 for the recorded oversized
        # terrain proposals. Accept only that known condition, never any error.
        failures = catalog.get('unexportable_details', [])
        known_incomplete = (
            result.returncode == 1
            and catalog.get('map_count') == 394
            and catalog.get('failed_maps') == 0
            and catalog.get('source_load_complete') is True
            and catalog.get('unexportable_proposals') == 8
            and len(failures) == 8
            and all(row.get('fragments_complete') is True for row in failures)
            and sum(len(row.get('fragments', [])) for row in failures) == 22
        )
        if not known_incomplete:
            raise SystemExit(result.returncode or 1)
        print('Eight oversized terrain proposals remain unresolved; their source/fragments are retained.')
    for command in (
        [sys.executable, str(ROOT/'tools/build-voxel-house.py')],
        [sys.executable, str(ROOT/'tools/build-voxel-world.py'), '--recipes', str(ROOT/'recipes/voxel-world-recipes.json')],
    ):
        subprocess.run(command, cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
