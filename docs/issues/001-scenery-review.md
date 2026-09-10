# Review one starter building or prop from all sides

Work package **RV-001** · M0 Common scenery · help wanted, good first issue, area: art, status: ready

## Problem

Starter generation and closed-mesh checks do not establish good proportions, source ownership or correct hidden surfaces.

## Bounded contribution

Choose one named definition from the recipe file and say which one in the issue. Inspect source, front/back/sides/roof, a neutral angle and its ground contact in two compatible placements. Submit one recipe correction or a specific reproducible defect report.

## Acceptance

- [ ] Record the definition, source location and pack/build identity.
- [ ] Show native-scale source and actual textured/neutral views; no facade doors repeated around the building.
- [ ] Check terrain contact, shadows, complete logos and save/reopen.
- [ ] Submit recipe changes and review notes, not the generated pack or extracted source sheets.

## Where to start

`recipes/voxel-world-recipes.json`, `tools/build-voxel-world.py`, `tools/review-voxel-world.py`; see `docs/authoring.md`.

## Validation and evidence

Regenerate locally and run the pack review for geometry/persistence/matches. For emblem changes also run `tools/test-voxel-emblems.py`. Record human visual findings separately.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-001 -->
