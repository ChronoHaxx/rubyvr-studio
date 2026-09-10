# Native Linux and WSL build, capture and launchers

Work package **RV-014** · M10 Performance and reliability · area: build · complete in [PR #22](https://github.com/ChronoHaxx/rubyvr-studio/pull/22)

## Scope

The previous CMake build and file/capture helpers required Windows. The user
prioritized native WSL so tools can build/test without PowerShell, replacing
the earlier requirement to maintain the PowerShell workflow.

## Acceptance

- [x] Build and run native Linux batch and GUI executables through Bash.
- [x] Preserve serialization, atomic replacement/refusal and stderr restoration.
- [x] Run original headless fixtures and existing editor/connected/streaming checks.
- [x] Verify fresh/resume/presets before retiring PowerShell launchers.
- [x] Record toolchain, actual GUI evidence and unsupported runtime pieces.
- [x] Run source-only Linux CI without fetching game assets.

The merge is verified and both hosted runs pass, including the
[PR check](https://github.com/ChronoHaxx/rubyvr-studio/actions/runs/34542217204).
See [native evidence](../native-wsl.md) and
[build/run commands](../building.md). Full performance/headset acceptance and
M2 geography are separate work.

## Boundaries

CMake, platform file/capture helpers, GUI file protection and Bash/Python
entrypoints. Production terrain geometry, foundations and connected ownership
remain unchanged. Retained Windows branches do not imply a newly tested build.

<!-- rubyvr-work-package:RV-014 -->
