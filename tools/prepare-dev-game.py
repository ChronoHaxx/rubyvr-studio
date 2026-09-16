#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare an isolated LOCAL Windows demo from your existing private runner and inputs.

Does not compile, download, publish or establish permission to redistribute a runner.
ROM/BIOS stay at their original paths. Checkpoints and an optional save are copied.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

spec = importlib.util.spec_from_file_location('dev_launcher', Path(__file__).with_name('run-dev-game.py'))
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

CONFIG = '''# Local Ruby USA revision 1 runner configuration. Inputs are supplied by the launcher.
[game]
name = "Pokemon Ruby Version"
short_name = "ruby"
default_region = "usa"
[program]
name = "Pokemon Ruby Version (USA)"
id = "ruby_usa"
load_address = 0x08000000
size = 0x01000000
entry_pc = 0x08000000
[identity]
sha1 = "610b96a9c9a7d03d2bafb655e7560ccff1a6d894"
[bios]
sha1 = "300c20df6731a33952ded8c436f7f186d25d3492"
[save]
type = "flash1m"
[runtime]
window_title = "RubyVR local demo"
'''


def dependencies(binary, directories, objdump):
    """Follow PE imports; copy non-system dependencies, refuse unresolved imports."""
    folders = [binary.parent] + list(directories)
    system = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32'
    found = {}
    queue = [binary]
    while queue:
        current = queue.pop()
        output = subprocess.check_output([str(objdump), '-p', str(current)], text=True, errors='replace')
        if 'file format pei-x86-64' not in output:
            raise ValueError(f'Expected a Windows x64 PE executable/DLL: {current.name}')
        for name in re.findall(r'DLL Name:\s*(\S+)', output):
            key = name.lower()
            if key in found or key.startswith(('api-ms-win-', 'ext-ms-win-')):
                continue
            if (system / name).is_file():
                continue
            path = next((d / name for d in folders if launcher.regular(d / name)), None)
            if path is None:
                raise ValueError(f'Missing runtime dependency {name}; supply its folder with --dll-dir.')
            found[key] = path
            queue.append(path)
    return list(found.values())


def prepare(args):
    output = args.output.absolute()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise ValueError('Output must be a new or empty directory. Existing sessions and saves are never replaced.')
    if output.is_symlink() or (hasattr(output, 'is_junction') and output.is_junction()):
        raise ValueError('Output must not be a link.')
    for path, kind in [(args.runner, 'runner'), (args.pack, 'scenery pack'), (args.rom, 'ROM'), (args.bios, 'BIOS')]:
        if not launcher.regular(path):
            raise ValueError(f'Missing or unsafe {kind}: {path}')
    for path, sha, size, label in [(args.rom, launcher.ROM_SHA1, 0x1000000, 'Ruby USA revision 1 ROM'),
                                    (args.bios, launcher.BIOS_SHA1, 0x4000, 'GBA BIOS')]:
        if path.stat().st_size != size or launcher.digest(path, 'sha1') != sha:
            raise ValueError('Wrong ' + label + '; no session files created.')
    pack = json.loads(args.pack.read_text(encoding='utf-8'))
    if not isinstance(pack, dict) or not isinstance(pack.get('patterns'), list) or not pack['patterns']:
        raise ValueError('Scenery pack has no patterns; generate the current local demo pack first.')
    if not re.fullmatch(r'[0-9a-f]{40}', args.source_commit):
        raise ValueError('--source-commit must name the exact 40-character Studio revision used to build the runner.')
    if not args.checkpoints.is_dir() or args.checkpoints.is_symlink():
        raise ValueError('Checkpoint input must be a regular directory.')
    states = sorted(args.checkpoints.glob('*.state'))
    if len(states) > 64 or any(not launcher.regular(s) or not launcher.checkpoint_name(s.stem) for s in states):
        raise ValueError('Invalid checkpoint names/files or more than 64 checkpoints.')
    if len({s.stem.casefold() for s in states}) != len(states):
        raise ValueError('Checkpoint names differ only by case; rename them first.')
    if not launcher.checkpoint_name(args.default_checkpoint) or args.default_checkpoint not in [s.stem for s in states]:
        raise ValueError('--default-checkpoint must name one of the supplied checkpoints.')
    if args.save and (not launcher.regular(args.save) or args.save.stat().st_size not in (0x20000, 0x20010)):
        raise ValueError('Optional save must be a regular 128KB Ruby flash save (with optional RTC trailer).')
    dlls = dependencies(args.runner, args.dll_dir, args.objdump)
    # Check every input before creating anything. A failed copy never publishes
    # handoff.json, so a half-prepared directory cannot be launched.
    output.mkdir(parents=True, exist_ok=True)
    entries = []
    def copy(source, name=None):
        target = output / (name or source.name)
        with source.open('rb') as src, target.open('xb') as dst:
            shutil.copyfileobj(src, dst)
        entries.append(dict(name=target.name, sha256=launcher.digest(target)))
        return target.name
    binary_hash = launcher.digest(args.runner)
    exe = copy(args.runner, 'RubyRecomp-' + binary_hash[:12] + '.exe')
    pack_name = copy(args.pack, 'review-pack.json')
    for dll in dlls:
        copy(dll)
    config = output / 'demo-game.toml'
    config.write_text(CONFIG, encoding='utf-8', newline='\n')
    entries.append(dict(name=config.name, sha256=launcher.digest(config)))
    states_dir = output / 'checkpoints'
    states_dir.mkdir()
    imported = []
    for state in states:
        shutil.copy2(state, states_dir / state.name)
        imported.append(dict(name=state.name, sha256=launcher.digest(states_dir / state.name)))
    if args.save:
        shutil.copy2(args.save, output / 'test-session.sav')
    for filename in ['run-dev-game.ps1', 'run-dev-game.py']:
        shutil.copy2(Path(__file__).with_name(filename), output / filename)
    manifest = dict(version=2, source_commit=args.source_commit, executable=exe, pack=pack_name,
                    config=config.name, default_checkpoint=args.default_checkpoint, files=entries,
                    inputs=dict(rom=str(args.rom.resolve()), bios=str(args.bios.resolve())),
                    imported_checkpoints=imported,
                    scope='Local private runner only. Public distribution/build route remains unresolved.')
    temp = output / 'handoff.pending.json'
    temp.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    os.replace(temp, output / 'handoff.json')
    launcher.inspect_session(output)
    return dict(session=str(output), files=len(entries), checkpoints=len(states), executable=exe,
                launcher=str(output / 'run-dev-game.ps1'), source_commit=args.source_commit)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--pack', required=True, type=Path)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--bios', required=True, type=Path)
    parser.add_argument('--checkpoints', required=True, type=Path)
    parser.add_argument('--default-checkpoint', default='Oldale Center ready')
    parser.add_argument('--save', type=Path)
    parser.add_argument('--dll-dir', action='append', type=Path, default=[])
    parser.add_argument('--objdump', type=Path, default=Path(shutil.which('objdump') or 'C:/msys64/mingw64/bin/objdump.exe'))
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(prepare(args), indent=2))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print('Could not prepare local demo: ' + str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
