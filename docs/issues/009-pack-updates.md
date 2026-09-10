# Preview starter-pack updates while preserving personal edits

Work package **RV-009** · M3 Manual authoring · help wanted, area: editor, status: design

## Problem

Regenerated starters and a user-edited pack can diverge. Replacing the personal file would lose manual work.

## Bounded contribution

Implement a dry-run three-way comparison using a prior starter, new starter and user document. Report unchanged/new/edited/conflicting definitions by stable identity. Start with a review report before adding an Apply UI.

## Acceptance

- [ ] Do not overwrite any input, even when paths alias.
- [ ] Preserve user-only definitions and source selections.
- [ ] Flag incompatible schema/source changes and per-definition conflicts.
- [ ] Emit an explicit proposed result plus conflict report only on request.

## Where to start

`src/studio/pattern_io.*`, `src/vr/overrides.*`, `tools/run-studio.sh`, `recipes/`.

## Validation and evidence

Small original fixtures covering untouched, user-only, upstream-only, concurrent and identity-change cases; prove input hashes are unchanged.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-009 -->
