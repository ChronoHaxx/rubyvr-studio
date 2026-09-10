# Design a verified user-ROM source adapter for standalone authoring

Work package **RV-015** · M11 Release · help wanted, area: tooling, status: design

## Problem

The current editor reads a local pinned pokeruby checkout. A release needs a clear supported-input flow without distributing game data.

## Bounded contribution

Specify and prototype a local read-only source adapter for one supported ROM revision. Verify input identity before decoding maps/tiles/palettes and reuse the snapshot/mesher contracts. Do not download or bundle ROMs/BIOS.

## Acceptance

- [ ] Reject unsupported inputs with useful errors and no output clobbering.
- [ ] Record parser/schema provenance and exact supported input identity.
- [ ] Keep decoded output local and omitted from logs/CI artifacts.
- [ ] Demonstrate source parity locally and document unimplemented animation/variant cases.

## Where to start

`src/studio/decomp_source.*`, `snapshot_build.*`, `src/vr/ruby_world.h`, `docs/asset-policy.md`.

## Validation and evidence

Original malformed/size-boundary fixtures for CI; local supported-input comparison with reported hashes/counts, no ROM or decoded-data upload.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-015 -->
