#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate and run a prepared LOCAL RubyVR session. No downloads or build steps."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROM_SHA1 = '610b96a9c9a7d03d2bafb655e7560ccff1a6d894'
BIOS_SHA1 = '300c20df6731a33952ded8c436f7f186d25d3492'


def digest(path, algorithm='sha256'):
    h = hashlib.new(algorithm)
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def regular(path):
    return path.is_file() and not path.is_symlink() and not (hasattr(path, 'is_junction') and path.is_junction())


def checkpoint_name(name):
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9 _-]{1,48}', name) or name.strip() != name:
        return False
    return name.lower() not in {'con', 'prn', 'aux', 'nul'} and not re.fullmatch(r'(com|lpt)[1-9]', name.lower())


def local_file(root, name):
    if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_+.-]{0,120}', name):
        raise ValueError('Invalid prepared filename; rebuild this session.')
    path = root / name
    if not regular(path):
        raise ValueError(f'Prepared file missing or not a regular file: {name}')
    return path


def inspect_session(root, checkpoint='', fresh=False):
    root = Path(root).absolute()
    if root.is_symlink() or (hasattr(root, 'is_junction') and root.is_junction()):
        raise ValueError('The session directory must not be a link.')
    manifest_path = local_file(root, 'handoff.json')
    if manifest_path.stat().st_size > 128 * 1024:
        raise ValueError('Prepared manifest is too large.')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    if not isinstance(manifest, dict):
        raise ValueError('Prepared manifest must be a JSON object.')
    version = manifest.get('version', 1)
    if type(version) is not int or version not in (1, 2):
        raise ValueError('Unsupported prepared-session version.')
    exe = manifest.get('executable', 'RubyRecomp.exe')
    if not re.fullmatch(r'RubyRecomp(?:-[0-9a-f]{12})?\.exe', exe):
        raise ValueError('Invalid prepared executable name.')
    files = manifest.get('files', [])
    if not isinstance(files, list) or not 1 <= len(files) <= 128:
        raise ValueError('Prepared file list is missing or invalid.')
    verified = set()
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get('name'), str) or not isinstance(entry.get('sha256'), str):
            raise ValueError('Invalid prepared file entry.')
        name = entry['name']
        if name.casefold() in verified:
            raise ValueError('Duplicate prepared filename: ' + name)
        path = local_file(root, name)
        if digest(path) != entry['sha256']:
            raise ValueError(f'Prepared file changed: {name}. Prepare the session again.')
        verified.add(name.casefold())
    pack = manifest.get('pack', 'review-pack.json')
    required = [exe, pack]
    if version == 2:
        required.append(manifest['config'])
    if any(not isinstance(name, str) or name.casefold() not in verified for name in required):
        raise ValueError('Executable, scenery pack and config must be hash-verified.')
    warnings = []
    selected = checkpoint or manifest.get('default_checkpoint', 'NPC views')
    preferences = root / 'preferences.json'
    if not checkpoint and not fresh and preferences.exists():
        try:
            if not regular(preferences) or preferences.stat().st_size > 8192:
                raise ValueError('unsafe settings file')
            prefs = json.loads(preferences.read_text(encoding='utf-8'))
            if not isinstance(prefs, dict):
                raise ValueError('settings must be a JSON object')
            if type(prefs.get('version')) is not int or prefs['version'] != 1:
                raise ValueError('unsupported settings version')
            remembered = prefs.get('checkpoint', '')
            if remembered:
                if not checkpoint_name(remembered) or not regular(root / 'checkpoints' / (remembered + '.state')):
                    raise ValueError('last checkpoint is unavailable')
                selected = remembered
        except (ValueError, OSError, KeyError, TypeError, RecursionError):
            warnings.append('Last checkpoint could not be restored; opening the prepared starting situation. Settings file kept.')
    states = root / 'checkpoints'
    if states.is_symlink() or (hasattr(states, 'is_junction') and states.is_junction()):
        raise ValueError('Checkpoint directory must not be a link.')
    if not fresh and (not checkpoint_name(selected) or not regular(states / (selected + '.state'))):
        raise ValueError(f'Checkpoint missing or invalid: {selected}. Choose a prepared checkpoint or use -Fresh.')
    save = root / 'test-session.sav'
    if save.is_symlink() or (save.exists() and not regular(save)):
        raise ValueError('The test save must be a regular file.')
    args = [str(root / exe), '--no-launcher', '--window', '--scale', '4', '--volume', '0']
    if version == 2:
        inputs = manifest['inputs']
        for key, sha, size in [('rom', ROM_SHA1, 0x1000000), ('bios', BIOS_SHA1, 0x4000)]:
            path = Path(inputs[key])
            if not path.is_absolute() or not regular(path):
                raise ValueError(f'Your {key.upper()} is missing: {path}. Re-prepare with its current location.')
            if path.stat().st_size != size or digest(path, 'sha1') != sha:
                raise ValueError(f'Wrong {key.upper()}. This runner needs Ruby USA revision 1 and the matching GBA BIOS; see docs/demo-runner.md.')
            args.extend(['--' + key, str(path)])
        config = root / manifest['config']
        cwd = root
    else:
        cwd = Path(manifest['game_directory'])
        config = cwd / 'variants/ruby/game.toml'
        if not config.is_file():
            raise ValueError('Legacy game config is missing. Re-prepare this local session with tools/prepare-dev-game.py.')
        warnings.append('Legacy session still needs its source checkout and MSYS2 DLLs; new prepared sessions keep runtime DLLs beside the executable.')
    args += ['--config', str(config), '--save', str(save)]
    if not fresh:
        args += ['--load-state', str(states / (selected + '.state'))]
    env = {k: v for k, v in os.environ.items() if not k.startswith(('RUBYVR_', 'GBARECOMP_'))}
    if version == 2:
        windows = os.environ.get('SystemRoot', 'C:/Windows')
        env['PATH'] = os.pathsep.join([str(root), str(Path(windows) / 'System32'), windows])
    else:
        env['PATH'] = 'C:/msys64/mingw64/bin' + os.pathsep + env.get('PATH', '')
    env.update(RUBYVR_DEV_DIR=str(states), RUBYVR_DEV_START_CHECKPOINT='' if fresh else selected,
               RUBYVR_DEMO_HOME='1', RUBYVR_VIEWER='1', RUBYVR_WORLD_DEBUG='1',
               RUBYVR_BUILD_MODE='diorama', RUBYVR_OVERRIDES=str(root / pack),
               GBARECOMP_PRESENT_IN_PLACE='1', GBARECOMP_SELFHEAL_RECOMPILE='0',
               GBARECOMP_COVERAGE_JSON=str(root / 'coverage.json'), GBARECOMP_MISS_FRAG=str(root / 'misses.toml.frag'))
    return dict(manifest=manifest, root=root, args=args, env=env, cwd=cwd,
                checkpoint='' if fresh else selected, warnings=warnings)


@contextlib.contextmanager
def session_lock(root):
    lock_path = root / 'session.lock'
    if lock_path.is_symlink():
        raise ValueError('Session lock must not be a link.')
    with lock_path.open('a+b') as lock:
        lock.seek(0, 2)
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise ValueError('This session is already running. Close its game window before reopening.') from exc
        try:
            yield
        finally:
            lock.seek(0)
            if os.name == 'nt':
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=Path, required=True)
    parser.add_argument('--checkpoint', default='')
    parser.add_argument('--fresh', action='store_true', help='Boot to the title screen using the isolated in-game save')
    parser.add_argument('--check', action='store_true', help='Verify inputs without launching')
    args = parser.parse_args()
    try:
        plan = inspect_session(args.session, args.checkpoint, args.fresh)
        print('RubyVR local demo:', plan['manifest']['source_commit'], flush=True)
        print('Starting:', plan['checkpoint'] or 'title screen', flush=True)
        for warning in plan['warnings']:
            print('Note:', warning, flush=True)
        if args.check:
            print('PASS: prepared files, dependencies and inputs verified (not a gameplay check)')
            return 0
        if os.name != 'nt':
            raise ValueError('This prepared live-game runner requires Windows. The standalone editor supports Linux/WSL.')
        print('Play & test opens in the viewer. Pick a camera or situation, then Continue playing.\n'
              'Save a new named checkpoint to reopen that moment next time. Existing saves are kept.', flush=True)
        with session_lock(plan['root']):
            with (plan['root'] / 'last-run.out.log').open('w', encoding='utf-8') as out, \
                    (plan['root'] / 'last-run.err.log').open('w', encoding='utf-8') as err:
                child = subprocess.Popen(plan['args'], cwd=plan['cwd'], env=plan['env'], stdout=out, stderr=err,
                                         creationflags=subprocess.CREATE_NO_WINDOW)
                while True:
                    try:
                        code = child.wait()
                        break
                    except KeyboardInterrupt:
                        # Keep the save lock until the child has actually exited,
                        # including repeated Ctrl+C while the game is still open.
                        print('Close the RubyVR game window to finish this session safely.', flush=True)
        if code:
            raise ValueError(f'Game exited with {code}. Details: {plan["root"] / "last-run.err.log"}')
        return 0
    except (OSError, ValueError, KeyError, TypeError, RecursionError) as exc:
        print('RubyVR could not start: ' + str(exc), file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
