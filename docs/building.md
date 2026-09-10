# Building and trying the editor

The supported workflow is native Linux on Ubuntu 22.04 or WSL2. Studio and
its batch tool are ELF executables. The editor needs WSLg or another working
SDL/OpenGL display. Headless checks need no display. PowerShell launchers are
retired; the native game/OpenXR integration remains separate.

## Install and build

In an Ubuntu/WSL Bash terminal, from the repository:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build pkg-config libsdl2-dev zlib1g-dev libgl1-mesa-dev libopenxr-dev python3-venv git
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
bash tools/build.sh --jobs 4
```

Keeping the repository and source assets in the Linux filesystem avoids slow
cross-filesystem reads of thousands of files. The default output is
`build-linux/`; `--batch-only` omits the GUI. `--build-dir DIR` selects another
folder, relative to the repository. Set `RUBYVR_BUILD_DIR` to use that folder
in launchers and portable tests. Existing Windows build outputs stay intact.
Dear ImGui is vendored with its MIT notice.

## Local source assets

The editor reads map data and indexed PNGs from a pinned local pokeruby
checkout. It does not compile that project's gameplay C or download a ROM.

```bash
git clone https://github.com/pret/pokeruby.git third_party/pokeruby
git -C third_party/pokeruby checkout 63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1
python3 tools/prepare-assets.py
bash tools/run-studio.sh --fresh
```

An existing local checkout can be used at `third_party/pokeruby`. Asset
preparation accepts only the known eight oversized terrain proposals and their
22 retained fragments; it does not mark terrain complete. Catalogs and model
packs remain in ignored local folders. Never commit game assets, ROMs or saves.

## Explore or keep editing

```bash
bash tools/run-studio.sh
bash tools/run-studio.sh --terrain-regions --connected
bash tools/run-studio.sh --terrain-regions --map MAP_ROUTE102
bash tools/run-studio.sh --terrain-example
```

The first command resumes `build/my-scenery.json` when it exists. `--fresh`
creates a new personal output. `--overrides FILE` opens a read-only template;
`--out FILE` chooses the Save destination. A resume session uses a temporary
input baseline so the personal output remains writable. Paths with spaces work.

The connected example includes Littleroot, Oldale, Petalburg and Routes
101/102/103. Explore area in DIORAMA opens the same view. Hold RMB to look;
WASD flies, Q/E changes height, Shift speeds up, and Escape releases flight.
This is a scenery explorer; full geography and playable gameplay remain open.

For the Review browser, run `python3 tools/coverage-ledger.py sync` after
preparing assets. The launcher exports recorded review queues before opening
Studio; stale review data warns without blocking launch.

## Verify locally

```bash
python3 tools/check-repo.py
python3 tools/test-native-portability.py
python3 tools/test-bash-launcher.py
python3 tools/test-editor.py
python3 tools/test-connected-studio.py
python3 tools/test-camera-map-streaming.py
```

The last three use local source assets and hidden real SDL windows. They verify
editor saving/controls, frozen geometry, connected ownership and streaming.
See [native evidence and limits](native-wsl.md) and [verification](verification.md).
Historical rendering scripts beyond these portable suites retain their original
platform assumptions.
