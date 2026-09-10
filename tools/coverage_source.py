"""Fingerprint local catalog inputs without copying game data into the ledger."""
import hashlib
import json
from pathlib import Path


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def file_hash(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_receipt(root, binary):
    root = Path(root).resolve()
    files = {}
    for folder in ("data/maps", "data/layouts", "data/tilesets", "graphics/tilesets", "include/constants"):
        for path in sorted((root / folder).rglob("*")):
            if path.is_file():
                files[path.relative_to(root).as_posix()] = file_hash(path)
    for relative in ("src/data/graphics.c",):
        path = root / relative
        if path.is_file():
            files[relative] = file_hash(path)
    if not files:
        raise ValueError("No source inputs found; supply the local pokeruby checkout")
    return dict(version=1, source_hash=digest(files), files=files, batch_sha256=file_hash(binary))
