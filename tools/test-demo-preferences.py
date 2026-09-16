#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build and run tools/demo-preferences-test.cpp.

Compiles the preference store together with the sources it links against,
then runs the test in a fresh scratch directory under build/. The compiler is
$CXX when set, otherwise c++ or g++ from PATH, otherwise MSYS2's MinGW g++ on
Windows.
"""

import argparse
import tempfile
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "demo-preferences-test"
SOURCES = [
    "tools/demo-preferences-test.cpp",
    "src/dev/preferences.cpp",
    "src/dev/session.cpp",
    "src/vr/json_scan.cpp",
    "src/studio/platform_io.cpp",
]
MSYS2_GXX = "C:/msys64/mingw64/bin/g++.exe"


def find_compiler():
    requested = os.environ.get("CXX")
    if requested:
        found = shutil.which(requested)
        if not found:
            sys.exit(f"demo-preferences: CXX={requested} was not found")
        return Path(found)
    candidates = ["c++", "g++"] + ([MSYS2_GXX] if os.name == "nt" else [])
    for candidate in candidates:
        found = shutil.which(candidate)
        if found:
            return Path(found)
    sys.exit("demo-preferences: no C++ compiler found; set CXX")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    compiler = find_compiler()
    env = os.environ.copy()
    # MinGW's g++ and the programs it builds load runtime DLLs from its own
    # directory, so that directory has to be on PATH for both steps.
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")

    OUT.mkdir(parents=True, exist_ok=True)
    program = OUT / ("demo-preferences-test.exe" if os.name == "nt" else "demo-preferences-test")
    build = [str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "src"),
             *(str(ROOT / source) for source in SOURCES),
             "-o", str(program)]
    if args.sanitize:
        build[1:1] = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    print("demo-preferences: building with", compiler, flush=True)
    if subprocess.run(build, env=env).returncode != 0:
        print("demo-preferences: build failed")
        return 1

    with tempfile.TemporaryDirectory(prefix="scratch-", dir=OUT) as scratch:
        result = subprocess.run([str(program), scratch], env=env)
    if result.returncode != 0:
        print(f"demo-preferences: test exited with {result.returncode}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
