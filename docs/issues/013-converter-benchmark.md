# Benchmark one external sprite converter on the same house and tree

Work package **RV-013** · M3 Manual authoring · help wanted, area: research, status: ready

## Problem

External converters were source-inspected, not installed or benchmarked. There is no evidence for an automatic-conversion speed claim.

## Bounded contribution

Choose one tool in docs/references.md, verify its exact revision/licence, and run a small local benchmark. Record setup, conversion, correction and import/editing time using the same fixtures as the manual workflow.

## Acceptance

- [ ] Separate installation time from repeated authoring time.
- [ ] Inspect all sides, native pixel scale, roof/ground ownership and editability.
- [ ] Record limitations/failures as well as successful output.
- [ ] Submit reproducible steps and limited illustrative evidence, not extracted game assets or unlicensed tool code.

## Where to start

`docs/references.md`, `docs/reference-parity.md`, `docs/asset-policy.md`.

## Validation and evidence

A measured report with exact settings and actual results; do not infer a state-of-the-art ranking from two examples.

Dependencies: RV-004.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-013 -->
