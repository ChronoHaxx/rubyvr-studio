# Inventory Windows dependencies and prove a Linux batch build boundary

Work package **RV-014** · M10 Performance and reliability · help wanted, area: build, status: design

## Problem

The standalone CMake build currently depends on Windows capture/file APIs. Advertising cross-platform support would be premature.

## Bounded contribution

Identify Windows-only calls and isolate the minimum needed for the batch tool. Prove a Linux batch build using synthetic or locally supplied data before attempting a full GUI/OpenXR port.

## Acceptance

- [ ] Keep the supported Windows build passing.
- [ ] Do not remove existing capture/persistence guarantees to compile.
- [ ] Record dependency/compiler versions and unsupported GUI/runtime pieces.
- [ ] Use portable original fixtures in CI without bundling source art.

## Where to start

`CMakeLists.txt`, `src/studio/capture.cpp`, `src/vr/world_io.cpp`, `src/studio/png_write.cpp`, `src/vr/gl_loader.*`.

## Validation and evidence

Windows build plus a named Linux toolchain build and targeted file/capture round trips. No claim that all platforms work from compilation alone.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-014 -->
