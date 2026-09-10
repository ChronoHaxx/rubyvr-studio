# Time a held-out house and tree authoring workflow against the demo checklist

Work package **RV-004** · M3 Manual authoring · help wanted, good first issue, area: usability, status: ready

## Problem

Core controls exist, but a matching layout and automated tests do not tell us whether a person can work quickly and intuitively.

## Bounded contribution

Follow `docs/reference-parity.md` on one previously unauthored house and one tree. Record time spent on source selection, masking, body/roof/detail editing, hidden surfaces and save/reopen. Report the three biggest obstacles, with precise reproduction.

## Acceptance

- [ ] Use the current standalone build and identify the fixtures.
- [ ] Record actual human time and corrections; do not compare against the edited demo duration as a benchmark.
- [ ] Exercise undo and reopening in a new process.
- [ ] Separate missing functionality from hard-to-find controls and visual-quality defects.

## Where to start

`docs/reference-parity.md`, `docs/authoring.md`, `src/studio/gui.cpp`, `src/studio/gui_voxel.inl`.

## Validation and evidence

A short recording or screenshots of actual application actions plus a timing table. No code or automated test is required for the initial usability report.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-004 -->
