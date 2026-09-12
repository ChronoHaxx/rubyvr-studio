# Instructions for AI-assisted contributions

Read README.md, CONTRIBUTING.md and the selected issue before changing code.
Use docs/architecture.md and docs/verification.md for component boundaries and
checks. The human contributor owns the PR and its claims.

- Work on one bounded issue. Preserve unrelated changes and authored assets.
- When updating a PR, update its description, human test steps and relevant
  documentation to match the delivered revision and the commands given in chat.
  Give the maintainer one primary launch/reopen workflow; put alternative
  developer setup in a clearly labelled collapsed section. Read back the
  published PR description before saying it is updated. A new commit alone
  does not complete the handoff.
- Beside the primary human test instructions, list known visible limitations
  in plain language, with their roadmap milestone and whether this PR fixes
  them. Do not bury camera/control mismatches, missing scenery or temporary
  menu behavior behind a general "prototype" label or a link. Preserve dated
  post-merge defect reports separately from the original acceptance result.
- The maintainer prefers short visual acceptance evidence and delegates routine
  automated and desktop visual review to the contributor or agent. Run the checks,
  inspect the actual captures, and lead with a 10–30 second GIF/video, a concise
  verdict and remaining defects. Keep detailed logs behind a link. For backend
  work, show actual test/report results; do not imply unrelated footage proves
  it. Every PR also needs a short, revision-specific human functional checklist:
  exact launch/setup, numbered actions, expected results, and unchecked boxes.
  Keep automated/agent results separate from human results. Do not tick the
  human boxes or merge until the maintainer reports their hands-on result for
  that revision. Record failures and retest relevant changed behavior after a
  fix. For noninteractive changes, specify the small observable human check
  instead of inventing a GUI journey. Never infer human or headset acceptance
  from CI, an agent check, a GIF, or an earlier revision.
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
- Before implementing a new feature, inspect relevant existing implementations:
  our own code, Gen2Recomped's actual Emerald path, the DramaticShapes companion
  mod, and the locally extracted Dramatic Shape APK Lua modules where applicable.
  Start with prior audits/cached files; verify relevant versions instead of
  restarting broad research. Trace the actual behavior and integration hooks,
  not only README features or demo appearance. Briefly record the exact source
  files/version, what can be reused or adapted, and what Ruby's native runtime
  still needs. Carry those findings into the implementation and acceptance
  checks; do not merely list reference links and independently reinvent it.
  Keep this pass proportionate to the feature and continue when no suitable
  reference exists. Reference access alone does not grant copying permission;
  retain applicable notices for permitted reuse.
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
