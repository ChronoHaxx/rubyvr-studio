# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless indoor scope, matching and persistence contract; optional local pack."""
from pathlib import Path
import argparse
import os
import shutil
import subprocess

root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--pack',type=Path)
p.add_argument('--sanitize',action='store_true')
args=p.parse_args()
compiler=os.environ.get('CXX') or ('C:/msys64/mingw64/bin/g++.exe' if os.name=='nt' and Path('C:/msys64/mingw64/bin/g++.exe').exists() else shutil.which('g++'))
if not compiler:p.error('Set CXX to a C++20 compiler')
out=root/'build'/('indoor-scene-test.exe' if os.name=='nt' else 'indoor-scene-test')
out.parent.mkdir(exist_ok=True)
flags=['-std=c++20','-Wall','-Wextra','-Werror','-Wno-error=misleading-indentation','-pedantic','-ffunction-sections','-fdata-sections']
flags+=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else ['-O2']
sources=['tools/indoor-scene-test.cpp','src/vr/overrides.cpp','src/vr/json_scan.cpp','src/vr/cutout.cpp','src/vr/tileset.cpp',
         'src/vr/part_geometry.cpp','src/vr/terrain.cpp','src/studio/pattern_io.cpp','src/studio/platform_io.cpp']
env=dict(os.environ,PATH=str(Path(compiler).resolve().parent)+os.pathsep+os.environ.get('PATH',''))
for key in ('DISPLAY','WAYLAND_DISPLAY'):env.pop(key,None)
subprocess.run([compiler,*flags,'-Isrc/vr','-Isrc/studio',*sources,'-Wl,--gc-sections','-o',str(out)],cwd=root,env=env,check=True)
subprocess.run([str(out),*([str(args.pack.resolve())] if args.pack else [])],cwd=root,env=env,check=True)
