#!/usr/bin/env python3
"""Camera input and Ruby gate checks, without SDL/OpenGL/game assets/display."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX') or shutil.which('c++') or shutil.which('g++')
if not compiler and Path('C:/msys64/mingw64/bin/g++.exe').exists():
    compiler = 'C:/msys64/mingw64/bin/g++.exe'
if not compiler:
    raise SystemExit('A C++20 compiler is required (set CXX).')
work = root / 'build/camera-input-tests'
work.mkdir(parents=True, exist_ok=True)
exe = work / ('check.exe' if os.name == 'nt' else 'check')
args = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
        '-I'+str(root/'src/vr'), '-I'+str(root/'integration/runtime'),
        str(root/'tools/camera-input-test.cpp'), str(root/'src/vr/camera_input.cpp'),
        str(root/'integration/runtime/live_scene.cpp'), '-o', str(exe)]
if '--sanitize' in sys.argv:
    args[2:3] = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
env = dict(os.environ, PATH=str(Path(compiler).resolve().parent)+os.pathsep+os.environ['PATH'])
env.pop('DISPLAY', None)
env.pop('WAYLAND_DISPLAY', None)
subprocess.run(args, check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
