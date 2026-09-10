"""Inventory source states without a ROM, renderer run or automatic approval."""
import argparse
import json
from pathlib import Path
import sys

from dynamic_inventory import inventory, verify_source

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "third_party/pokeruby")
    parser.add_argument("--out", type=Path, default=ROOT / "build/coverage/source-states.json")
    args = parser.parse_args()
    manifest = inventory(args.source)
    verify_source(args.source, manifest)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.out.with_suffix(args.out.suffix + ".tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary.replace(args.out)
    print(json.dumps(dict(maps=len(manifest["map_ids"]), states=len(manifest["records"]),
                          counts=manifest["counts"], native_audit=manifest['native_audit'], unknown_commands=manifest["unknown_commands"],
                          output=str(args.out)), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as exc:
        print(f"inventory-dynamics: {exc}", file=sys.stderr)
        raise SystemExit(1)
