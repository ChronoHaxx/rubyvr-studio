#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile/run the production play-screen widgets without taking desktop input.

Requires SDL2/OpenGL and a usable GL driver, but no game assets. Linux may use
SDL_VIDEODRIVER=offscreen with Mesa; a visible window is never created.
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
out = root / 'build/demo-panel-test'
out.mkdir(parents=True, exist_ok=True)
cxx = os.environ.get('CXX') or ('C:/msys64/mingw64/bin/g++.exe' if os.name == 'nt' else 'c++')
env = os.environ.copy()
env['PATH'] = str(Path(shutil.which(cxx) or cxx).parent) + os.pathsep + env.get('PATH', '')
pkg = shutil.which('pkg-config', path=env['PATH'])
if not pkg:
    raise SystemExit('pkg-config and SDL2 development files are required.')
flags = shlex.split(subprocess.check_output([pkg, '--cflags', '--libs', 'sdl2'], env=env, text=True))
flags = [f for f in flags if f not in ('-Dmain=SDL_main', '-lSDL2main', '-lmingw32', '-mwindows')]
imgui = root / 'third_party/imgui'
sources = [root / 'tools/demo-panel-test.cpp'] + [imgui / n for n in
    ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp',
     'backends/imgui_impl_sdl2.cpp','backends/imgui_impl_opengl3.cpp']]
exe = out / ('demo-panel-test.exe' if os.name == 'nt' else 'demo-panel-test')
subprocess.run([cxx, '-std=c++20', '-DSDL_MAIN_HANDLED', '-I'+str(imgui), '-I'+str(imgui/'backends'),
                *map(str, sources), *flags, '-lopengl32' if os.name == 'nt' else '-lGL', '-o', str(exe)],
               env=env, check=True)
raise SystemExit(subprocess.run([str(exe)], cwd=out, env=env, timeout=30).returncode)
