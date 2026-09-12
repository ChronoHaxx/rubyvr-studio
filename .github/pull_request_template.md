## Quick acceptance result

Short actual-application GIF/video for a visible change, or compact test/report
output for backend work. State the verdict and remaining defects in a few lines.
The contributor/agent runs and inspects the automated/visual checks. Keep these
results separate from the human checks below. Label scripted/edited timing and
baseline footage. CI passing does not mean the maintainer has tried the change.

## Problem and result

Related issue / RV work package:

Describe the concrete trigger and resulting behavior. Include actual before/after
views for a visual change; distinguish geometry from texture changes.

## Automated and agent validation

Commands run, results and checks not run. State editor, batch, live-game and
headset evidence separately. Include build/source/recipe identity where relevant.

## Human functional check — required before merge

Tested commit/build: **not yet tested**

**Known visible limitations in this build:** list each relevant symptom, its
roadmap milestone/work package, and whether this PR fixes it or leaves it open.
Put this beside the launch/test instructions, even when the roadmap already
contains the issue. Use concrete behavior, not just "prototype" or "polish".

Provide exact setup and launch commands, then a short numbered sequence for this
revision. Each unchecked box must contain the action and its expected result.
Cover the changed behavior, one adjacent ordinary workflow, and relevant failure
or recovery paths. Include restart/save/reopen only when the change affects them.
For noninteractive work, use a small observable check appropriate to the change.
Do not replace the sequence with a request to read the diff or run all tests.
Give the maintainer one primary launch/reopen workflow matching the chat handoff.
Collapse alternative developer setup. Keep these steps and relevant docs current
when pushing updates, then read back the published PR description.

1. [ ] **Action:** … **Expected:** …
2. [ ] **Action:** … **Expected:** …

Human verdict / remaining defects: **pending**

Only record a pass after the maintainer reports the result for this revision.
Keep failed steps and their symptoms; fixes need a relevant retest on the new
revision. Do not merge with these checks pending, even when CI/agent checks pass.

## Sources and limitations

Credit adapted code/techniques with exact source and licence. Explain remaining
defects or assumptions. AI tools are welcome; describe their role if helpful.

- [ ] I reviewed the submitted change and its evidence.
- [ ] Roadmap/work-package progress and evidence are updated, or no implementation status changed.
- [ ] No ROM/BIOS/save/snapshot/extracted game data or private information is included.
- [ ] Relevant persistence, undo, source-pixel and input-ownership contracts remain intact.
