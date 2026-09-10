# Building and trying the editor

The supported development setup is Windows x64, PowerShell and MSYS2 mingw64.
The standalone CMake build compiles our editor and the shared production mesher
without compiling or linking the game runtime. New editor UX work lives here;
the native integration remains a separate prototype.

## Dependencies

Install [MSYS2](https://www.msys2.org/) and these packages in its terminal:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib mingw-w64-x86_64-openxr-sdk
```

Install Python 3.10+ and Git. From PowerShell in this repository:

```powershell
python -m pip install -r requirements.txt
.\tools\build.ps1
```

`-Mingw <path>` supports a different mingw64 installation. The build script copies
the executables' MinGW runtime DLLs beside them, including SDL2 and transitive
dependencies. Re-run it if an older build reports a missing DLL. The compiler uses
your normal writable temporary directory; do not point TEMP at Windows system
directories. Dear ImGui 1.91.9b is vendored with its MIT notice.

## Local source assets

The current editor reads map data and indexed PNGs from a local pokeruby
checkout. It does not compile that project's gameplay C. This dependency is
source data for the editor, not an independently licensed game asset pack.

```powershell
git clone https://github.com/pret/pokeruby.git third_party/pokeruby
git -C third_party/pokeruby checkout 63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1
python .\tools\prepare-assets.py
.\tools\run-studio.ps1 -Fresh
```

The catalog reports eight oversized terrain proposals and retains their source
and 22 fragments. `prepare-assets.py` permits that known incomplete-export
condition after validating the catalog; it does not mark terrain complete.

The generated catalog, PNGs and model packs stay under ignored local paths.
Do not commit them. A future verified-ROM source adapter is a separate work
package. The playable native game requires user-provided supported ROM/BIOS
inputs and the upstream runtime; it is not built by these commands.

## Commands for everyday work

```powershell
python .\tools\check-repo.py
python .\tools\test-editor.py
python .\tools\test-studio-guided.py
python .\tools\review-voxel-world.py
python .\tools\test-voxel-emblems.py
```

Graphics checks need a working OpenGL driver, even when their SDL window is
hidden. See [verification](verification.md) for what each command proves.
Use `-Map MAP_RUSTBORO_CITY` with the launcher to inspect another town.

For the local connected-terrain example, use
`./tools/run-studio.ps1 -TerrainRegions`. It opens Oldale with a new personal
output and leaves the default starter unchanged. Add `-Map MAP_ROUTE102` or
`-Map MAP_ROUTE103` to inspect the authored ledges. [Scope and limitations](terrain-regions.md).

Add `-Connected` to fly across the six-map authored area, including Littleroot,
Oldale, Petalburg and Routes 101/102/103: `./tools/run-studio.ps1 -TerrainRegions -Connected`. You can also
click **Explore area** in DIORAMA. [Controls and preview limits](connected-scene.md).

For the **Review…** browser, run `python tools/coverage-ledger.py sync` after
preparing assets. The launcher then exports the recorded static review queues
automatically. [Browser usage and snapshot limits](coverage-ledger.md#browse-static-reviews-in-studio).
