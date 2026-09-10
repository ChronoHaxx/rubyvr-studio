# Track source families and placement review without losing prior evidence

Work package **RV-002** · M1 Coverage ledger · area: tooling · **in progress**

## Problem

The original catalog inventoried 394 maps but could not answer which individual families and placements were visually accepted, live-tested or still unresolved.

**Progress, 2026-09-09:** static coverage is merged through
[PR #21](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/21) and
[PR #20](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/20).
The source-state continuation is **merged** in
[PR #22](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/22).
The merged browser, [PR #23](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/23),
adds a Studio browser for static review queues and navigation to exact source
placements. [Watch the acceptance check](../acceptance.md). The continuation
**merged in [PR #24](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/24)** traces script-native helpers and registered callback candidates to
mutation APIs, with unresolved references retained for the remaining M1 audit.
It records 374 target audits and passes 45 synthetic checks across the four
coverage suites. [Short results](../native-audit-acceptance.md).
See [usage, schema and limits](../coverage-ledger.md) and the canonical
[M1 checklist](../roadmap.md#m1-coverage-ledger). Source discovery does not
enumerate reachable runtime combinations or approve their presentation.
Intended-treatment rules, evidence/reasons, next-action ownership and CLI filters
are **merged in [PR #25](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/25)**. They use authored roles and known source types, preserve
recorded decisions and leave ambiguous roles explicit. [Usage](../coverage-dispositions.md).
Unresolved source/native references and review decisions remain
under M1; the hosted issue remains open for that remaining work.

## Bounded contribution

Implement a stable review-ledger schema and deterministic merge command for the existing catalog. Begin with static map/family/placement entries; leave dynamic states explicitly pending. Include a small original synthetic fixture.

## Acceptance

- [x] Keep modeled, visual, live and headset states separate.
- [x] Retain reviews when source/recipe dependencies match; invalidate changed inputs with an explanation.
- [x] Preserve all eight oversized proposals and distinguish map-body from padding coordinates.
- [x] Generate a useful remaining-work report without claiming a whole-game percentage.

## Where to start

`src/studio/asset_catalog.*`, `tools/catalog-sprites.py`, `tools/review-voxel-world.py`, `docs/roadmap.md` M1.

## Validation and evidence

Use synthetic fixtures for unchanged, changed, removed and duplicated source entries, plus a local 394-map run. Share counts and identifiers rather than extracted assets.

Dependencies: none beyond the documented local build/source setup.

## Source-state continuation

The first five items below are implemented, tested and merged in PR #22.

- [x] Inventory every map's events, connections and environment with exact source locations.
- [x] Retain saved-destination warps, variable graphics, aliases and separate weather ID namespaces.
- [x] Inventory script blocks and changes, generated movement commands, animation declarations,
  tileset frames/callbacks, behavior constants and native mutation call sites.
- [x] Preserve existing static reviews and track dynamic reviews independently.
- [x] Fail a changed/incomplete input before updating the ledger; retain unknown commands for triage.
- [x] **Merged — PR #23:** Studio filters for missing/partial, unresolved, unreviewed and failed static placements; exact map navigation with unsaved-edit protection.
- [x] **Merged — [PR #24](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/24):** source witnesses for direct native helpers and literal registered callbacks, with explicit unresolved references and independent review states.
- [x] **Merged — [PR #25](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/25):** intended treatment, reasons, evidence and milestone ownership for every recorded entry; CLI filters and consistent Studio explanations, with manual decisions preserved.
- [ ] Audit reachable state combinations, indirect/native writes and complete UI/battle routing.
- [ ] Resolve intended dispositions and supply visual/live/headset evidence for each applicable entry.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-002 -->
