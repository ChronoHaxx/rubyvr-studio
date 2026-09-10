# Try the standalone build guide on a clean Windows machine

Work package **RV-018** · M11 Release · help wanted, good first issue, area: docs, status: ready

## Problem

The first standalone extraction builds on the development machine; a contributor needs instructions that work without its private folders or cached inputs.

## Bounded contribution

Follow docs/building.md from a fresh checkout on another Windows environment. Record the exact missing package/path/error and propose a concise guide fix. Do not install the full native game stack for an editor-only build.

## Acceptance

- [ ] Record OS/toolchain/Python versions and the source revision.
- [ ] Build both executables and run the documented source preparation.
- [ ] Open Studio, edit/save/reopen a scratch model and run the relevant checks.
- [ ] Distinguish an absent OpenGL driver from build failures; scrub personal paths from the report.

## Where to start

`docs/building.md`, `tools/build.sh`, `tools/prepare-assets.py`, `tools/run-studio.sh`.

## Validation and evidence

A short setup log and actual application result. No new automated test is required for a documentation-only correction.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-018 -->
