#!/usr/bin/env python3
"""Compile and exercise host-only checkpoint/transport contracts; no game or GUI."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('c++') or shutil.which('g++')
if not compiler and Path('C:/msys64/mingw64/bin/g++.exe').exists():
    compiler = 'C:/msys64/mingw64/bin/g++.exe'
if not compiler:
    raise SystemExit('A C++20 compiler is required (set CXX).')
work = ROOT / 'build' / 'dev-component-tests'
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='rubyvr-dev-', dir=work) as directory:
    directory = Path(directory)
    exe = directory / ('check.exe' if os.name == 'nt' else 'check')
    subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT / 'src'), str(ROOT / 'tools/dev-session-test.cpp'),
                    str(ROOT / 'src/dev/session.cpp'), '-o', str(exe)], check=True)
    env = dict(os.environ, PATH=str(Path(compiler).resolve().parent)+os.pathsep+os.environ['PATH'])
    subprocess.run([str(exe), str(directory / 'session with spaces')], check=True, env=env)
