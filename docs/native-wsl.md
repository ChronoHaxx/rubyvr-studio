# Native Linux and WSL support

RV-014 is merged in [PR #22](https://github.com/ChronoHaxx/rubyvr-studio/pull/22),
with local editor acceptance and hosted Linux source/build checks passing.
Bash builds and launches the batch tool and SDL editor as native Linux ELF
executables. No Windows executable or PowerShell bridge is involved.

[Watch the 12-second native editor check](acceptance.md): connected exploration,
part selection and a saved session, captured from the production GUI on WSLg.

## Verified scope

| Check | Result |
|---|---|
| Fresh build from outside the repository | GUI and batch compile with GCC; output stays in the requested repository build folder |
| Headless production geometry | 197 terrain, 23 foundation and 69 connected checks pass with DISPLAY unset |
| Portable file/capture operations | 29 original native checks: v5/v6/v7 round trips, replacement/refusal, cleanup, spaces, aliases and stderr restoration |
| Bash launchers | 22 original cases including fresh/resume, exact arguments, failure statuses, capture options and build-directory resolution |
| Existing editor suite | GUI selftest, four layouts, cameras, voxel editing and part selection pass; all eight frozen hashes unchanged |
| Connected exploration | Existing suite passes; 32,000 atlas pixels match and input/source documents remain unchanged |
| Camera map loading | Authored travel, return, cancellation and source-floor checks pass |
| Local asset preparation | Full 394-map catalog and starter generation complete; known oversized terrain proposals remain explicit |
| Linux CI | Original source fixtures, native batch checks and GUI compilation pass in the [PR run](https://github.com/ChronoHaxx/rubyvr-studio/actions/runs/34542217204) |

The graphics suites exercise the production renderer on WSLg with local
pokeruby data. Scripted desktop checks do not establish human usability, VR
comfort or complete game acceptance. CI does not fetch or publish game assets.

## Environment and commands

Verified on Ubuntu 22.04.5/WSL2, kernel 5.15.167.4-microsoft-standard-WSL2,
GCC 11.4.0, CMake 3.22.1, Ninja 1.10.1, SDL2 2.0.20, zlib 1.2.11 and
Python 3.10.12 with Pillow/PyYAML and OpenGL/OpenXR development headers.
OpenXR headers supply shared pose/FOV types, not a working Linux runtime.

[Build and run instructions](building.md):

```bash
bash tools/build.sh --jobs 4
python3 tools/prepare-assets.py
bash tools/run-studio.sh --terrain-regions --connected
```

For headless workers:

```bash
bash tools/build.sh --batch-only --jobs 4
unset DISPLAY
build-linux/rubyvr_studio --test-terrain build-linux/terrain-test
build-linux/rubyvr_studio --test-foundation build-linux/foundation-test
bash tools/run-batch.sh --test-connected
python3 tools/test-native-portability.py
python3 tools/test-bash-launcher.py
RUBYVR_REVIEW_BATCH="$PWD/build-linux/rubyvr_studio" python3 tools/test-coverage-review.py
```

The coverage-ledger, dynamic-inventory, native-trace, coverage-disposition and
terrain-regions Python fixture suites also pass headlessly. GUI verification
is separate and requires a display and local source data.

## Persistence and capture

Pattern saves create an exclusive temporary beside the destination, flush and
synchronize its bytes, close it, validate it through the production reader and
atomically rename it over the destination. Failed validation or replacement
leaves the previous destination intact. Temporaries are cleaned up.

The containing directory is synchronized after a successful POSIX rename on a
best-effort basis. Filesystems that reject directory fsync do not provide the
same power-loss durability for the directory entry; a completed publication is
not falsely reported as rolled back. Formats and ordinary atomic-save behavior
remain unchanged.

Stderr capture restores the caller's original descriptor, including an existing
file or pipe redirection. Its failure path still runs the callback with the
caller's stderr. File protection recognizes Linux filesystem aliases and keeps
distinct case-sensitive filenames distinct.

## Remaining boundaries

- A physical WSL right-mouse spin was reported after merge. The
  [mouse follow-up](wsl-mouse-look.md) merged in PR #24 after all four maintainer
  checks passed on 2026-09-11. The original hidden tests skipped the OS grab;
  their evidence and the later defect remain recorded separately.
- Linux OpenXR/live-game integration is unsupported and was not exercised.
- Windows branches remain in shared code, but PowerShell scripts and the
  Windows-only CI workflow are retired. This does not certify a new Windows
  build; existing binaries and personal assets stay untouched.
- Source assets on a Windows-mounted filesystem work but are slower to inventory.
- Historical evidence scripts beyond the documented portable suites retain
  their original platform assumptions.

Review corrected build-directory resolution and an inappropriate source-audit
exception. Existing Python wrappers now select native binaries while retaining
their geometry and input assertions.
