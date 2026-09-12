#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless presentation contract: native C++ on Windows or Linux."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
compiler = os.environ.get('CXX') or shutil.which('g++')
if not compiler and os.name == 'nt':
    compiler = 'C:/msys64/mingw64/bin/g++.exe'
if not compiler:
    parser.error('Set CXX to a C++20 compiler.')
out = root / 'build' / ('live-presentation-test.exe' if os.name == 'nt' else 'live-presentation-test')
out.parent.mkdir(exist_ok=True)
flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic']
flags += (['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
          if args.sanitize else ['-O2'])
env = os.environ.copy()
env['PATH'] = str(Path(compiler).resolve().parent) + os.pathsep + env.get('PATH', '')
for key in ['DISPLAY', 'WAYLAND_DISPLAY']:
    env.pop(key, None)
subprocess.run([compiler, *flags, '-Isrc/vr', '-Iintegration/runtime',
                'tools/live-presentation-test.cpp', 'integration/runtime/live_presentation.cpp',
                'integration/runtime/live_scene.cpp', '-o', str(out)], cwd=root, env=env, check=True)
subprocess.run([str(out)], cwd=root, env=env, check=True)
