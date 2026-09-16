#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Local-session setup and launch contracts using synthetic bytes; no game/display."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


prepare = module('prepare-dev-game')
run = prepare.launcher


class DemoRunner(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='demo runner ', dir=ROOT / 'build')
        self.root = Path(self.temp.name)
        self.inputs = self.root / 'own inputs'
        self.inputs.mkdir()
        for name, data in [('test.exe', b'synthetic runner'), ('libstdc++-6.dll', b'synthetic dependency'),
                           ('pack.json', b'{"patterns":[{"synthetic":true}]}'),
                           ('test.gba', bytes(0x1000000)), ('bios.bin', bytes(0x4000)), ('own.sav', bytes(0x20000))]:
            (self.inputs / name).write_bytes(data)
        states = self.inputs / 'states'
        states.mkdir()
        (states / 'Start here.state').write_bytes(b'synthetic checkpoint')
        (states / 'User moment.state').write_bytes(b'another synthetic checkpoint')
        self.args = argparse.Namespace(output=self.root / 'new session', runner=self.inputs / 'test.exe',
            source_commit='a' * 40, pack=self.inputs / 'pack.json', rom=self.inputs / 'test.gba',
            bios=self.inputs / 'bios.bin', checkpoints=states, default_checkpoint='Start here',
            save=self.inputs / 'own.sav', dll_dir=[], objdump=Path('unused'))
        self.rom_patch = patch.object(run, 'ROM_SHA1', run.digest(self.args.rom, 'sha1'))
        self.bios_patch = patch.object(run, 'BIOS_SHA1', run.digest(self.args.bios, 'sha1'))
        self.deps_patch = patch.object(prepare, 'dependencies', return_value=[self.inputs / 'libstdc++-6.dll'])
        for p in (self.rom_patch, self.bios_patch, self.deps_patch):
            p.start(); self.addCleanup(p.stop)
        self.addCleanup(self.temp.cleanup)
        prepare.prepare(self.args)
        self.session = self.args.output

    def manifest(self, change):
        path = self.session / 'handoff.json'
        value = json.loads(path.read_text(encoding='utf-8'))
        change(value)
        path.write_text(json.dumps(value), encoding='utf-8')

    def prefs(self, text):
        (self.session / 'preferences.json').write_text(text, encoding='utf-8')

    def test_isolation_and_relocation(self):
        self.assertFalse((self.session / 'test.gba').exists())
        self.assertFalse((self.session / 'bios.bin').exists())
        (self.session / 'test-session.sav').write_bytes(b'new progress')
        self.assertEqual(self.args.save.read_bytes(), bytes(0x20000))
        moved = self.root / 'moved directory with spaces'
        self.session.rename(moved)
        plan = run.inspect_session(moved)
        self.assertEqual(plan['cwd'], moved)
        self.assertIn(str(moved / 'checkpoints/Start here.state'), plan['args'])
        self.assertIn(str(self.args.rom), plan['args'])
        self.assertNotIn('msys64', plan['env']['PATH'])
        self.assertEqual((moved / 'test-session.sav').read_bytes(), b'new progress')

    def test_remembered_checkpoint_and_explicit_override(self):
        self.prefs('{"version":1,"checkpoint":"User moment"}')
        self.assertEqual(run.inspect_session(self.session)['checkpoint'], 'User moment')
        self.assertEqual(run.inspect_session(self.session, 'Start here')['checkpoint'], 'Start here')
        self.assertNotIn('--load-state', run.inspect_session(self.session, fresh=True)['args'])

    def test_invalid_remembered_checkpoint_is_preserved(self):
        for text in ['{', '[]', 'null', '{"version":99,"checkpoint":"User moment"}',
                     '{"version":1,"checkpoint":"../../escape"}', '{"version":1,"checkpoint":"Gone"}']:
            with self.subTest(text=text):
                self.prefs(text)
                plan = run.inspect_session(self.session)
                self.assertEqual(plan['checkpoint'], 'Start here')
                self.assertTrue(plan['warnings'])
                self.assertEqual((self.session / 'preferences.json').read_text(), text)

    def test_missing_and_changed_runtime_dependency(self):
        dll = self.session / 'libstdc++-6.dll'
        dll.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            run.inspect_session(self.session)
        dll.unlink()
        with self.assertRaisesRegex(ValueError, 'missing'):
            run.inspect_session(self.session)

    def test_wrong_and_missing_own_inputs(self):
        self.args.rom.write_bytes(b'wrong revision')
        with self.assertRaisesRegex(ValueError, 'Wrong ROM'):
            run.inspect_session(self.session)
        self.args.rom.unlink()
        with self.assertRaisesRegex(ValueError, 'missing'):
            run.inspect_session(self.session)

    def test_manifest_cannot_escape_or_skip_hashes(self):
        original = (self.session / 'handoff.json').read_bytes()
        changes = [lambda m: m['files'].append(dict(name='../test.exe', sha256='a'*64)),
                   lambda m: m['files'].append(m['files'][0]),
                   lambda m: m.update(config='../elsewhere.toml'),
                   lambda m: m.update(executable='elsewhere.exe'),
                   lambda m: m.update(version=True),
                   lambda m: m.update(files=m['files'][1:])]
        for i, change in enumerate(changes):
            with self.subTest(case=i):
                (self.session / 'handoff.json').write_bytes(original)
                self.manifest(change)
                with self.assertRaises(ValueError):
                    run.inspect_session(self.session)
        (self.session / 'handoff.json').write_text('[]')
        with self.assertRaisesRegex(ValueError, 'JSON object'):
            run.inspect_session(self.session)

    def test_existing_output_and_wrong_inputs_do_not_overwrite(self):
        before = (self.session / 'handoff.json').read_bytes()
        with self.assertRaisesRegex(ValueError, 'new or empty'):
            prepare.prepare(self.args)
        self.assertEqual((self.session / 'handoff.json').read_bytes(), before)
        self.args.output = self.root / 'not created'
        self.args.bios.write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError, 'Wrong GBA BIOS'):
            prepare.prepare(self.args)
        self.assertFalse(self.args.output.exists())

    def test_duplicate_or_reserved_checkpoint_names(self):
        for name in ['CON', 'Lpt2', '..', 'bad/name', ' trailing ', 'x'*49]:
            self.assertFalse(run.checkpoint_name(name))
        self.assertTrue(run.checkpoint_name('My town - 02'))

    def test_child_environment_is_isolated(self):
        with patch.dict(os.environ, {'RUBYVR_FREE_WALK_PROBE':'1', 'GBARECOMP_INPUT_REPLAY':'somewhere', 'GBARECOMP_SELFHEAL_RECOMPILE':'1'}):
            env = run.inspect_session(self.session)['env']
        self.assertNotIn('RUBYVR_FREE_WALK_PROBE', env)
        self.assertNotIn('GBARECOMP_INPUT_REPLAY', env)
        self.assertEqual(env['GBARECOMP_SELFHEAL_RECOMPILE'], '0')
        self.assertEqual(env['RUBYVR_DEMO_HOME'], '1')

    def test_lock_refuses_concurrent_runner_and_releases(self):
        with run.session_lock(self.session):
            with self.assertRaisesRegex(ValueError, 'already running'):
                with run.session_lock(self.session):
                    self.fail('second session acquired the same save')
        with run.session_lock(self.session):
            pass

    def test_rejected_prepare_does_not_change_imported_save_or_state(self):
        before = {p.name: run.digest(p) for p in self.args.checkpoints.iterdir()}
        self.args.output = self.root / 'invalid checkpoint output'
        self.args.default_checkpoint = 'missing'
        with self.assertRaisesRegex(ValueError, 'default-checkpoint'):
            prepare.prepare(self.args)
        self.assertEqual(before, {p.name: run.digest(p) for p in self.args.checkpoints.iterdir()})
        self.assertFalse(self.args.output.exists())


if __name__ == '__main__':
    (ROOT / 'build').mkdir(exist_ok=True)
    unittest.main(verbosity=2)
