# Instructions for AI-assisted contributions

Read README.md, CONTRIBUTING.md and the selected issue before changing code.
Use docs/architecture.md and docs/verification.md for component boundaries and
checks. The human contributor owns the PR and its claims.

- Work on one bounded issue. Preserve unrelated changes and authored assets.
- The maintainer prefers short visual acceptance evidence and delegates routine
  desktop functional/visual review to the contributor or agent. Run the checks,
  inspect the actual captures, and lead with a 10–30 second GIF/video, a concise
  verdict and remaining defects. Keep detailed logs behind a link. For backend
  work, show actual test/report results; do not imply unrelated footage proves
  it. Do not assign the maintainer a manual test checklist as the default handoff
  or infer human usability/headset acceptance from agent checks.
- Keep the active roadmap milestone visible. When the user reports another
  defect or idea, name its existing milestone/work package and record it there.
  Continue the active milestone unless the report blocks it or the user
  explicitly reprioritizes. A report or question alone is not a request to
  abandon the roadmap. This is the maintainer's requested working preference.
- Use docs/roadmap.md as the single completion tracker. Do not maintain a second
  roadmap in the research workspace. Update the relevant checklist, work-package
  status and evidence with each change; keep implementation in an open PR
  unchecked and marked In review. When a merge is reported, verify it and tick
  only the delivered scope, preserving outstanding visual/live/headset acceptance.
- Inspect source and reproduce the problem; do not invent API behavior or
  terrain heights from color/elevation values.
- Use the shared production mesher for previews and evidence. Capture actual
  application output; generated mockups are not validation screenshots.
- Preserve source palette/texel identity, native pixel scale, object/ground/
  shadow ownership, exact persistence, undo and input ownership.
- Do not copy code from source-available/restricted projects without permission.
  Record exact sources and notices for any adaptation.
- Never commit ROMs, BIOS, saves, extracted art, snapshots, generated ROM code,
  credentials or local caches. Generated packs remain local.
- Run relevant checks and report failures and skipped checks. Do not weaken a
  check or update a frozen hash merely to make it pass.
- Distinguish editor, live-game, strict-static and headset evidence. Do not
  claim completed gameplay or FOSS licensing for the external runtime stack.
- Do not publish a release, post to other repositories or merge your own work
  unless the maintainer explicitly asks for that action.

Useful commands are documented in docs/building.md. Start with
`python tools/check-repo.py`, then build and run the relevant graphics checks.
