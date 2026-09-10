# Contributing to RubyVR Studio

We need people who enjoy making things: voxel artists, C++ and tooling
developers, testers and people who can explain a confusing workflow clearly.
You can contribute with or without AI tools. You do not need a VR headset to
work on the editor or review static models.

## Pick a bounded piece of work

Start with the [issue backlog](docs/issues/README.md). Each work package names
the problem, source entry points, acceptance criteria and evidence to return.
The [roadmap and progress tracker](docs/roadmap.md) is the single M0–M11 checklist;
work packages describe smaller contributions within it.
Use `good first issue` for genuinely small starting points. Larger runtime,
terrain and rendering tasks are marked separately. Comment on an issue before
a substantial implementation so two people do not unknowingly do the same work.

For a new idea, open a feature issue with one concrete user action and the
expected result. An asset contribution should name the source family/map and
show the object from the front, sides, back and in its placement context.

## Make and verify a change

Lead the handoff with a short actual-application GIF/video, a verdict and the
remaining defects, as in the [visual acceptance summary](docs/acceptance.md).
The contributor or agent performs and inspects routine desktop checks; the
maintainer does not need to repeat them. For backend-only changes, use concise
test/report results and label any editor footage as baseline evidence. Keep
logs and reproduction behind links. Human usability and headset evidence still
require an actual tester; no such acceptance is implied by a GIF.

1. Follow [building](docs/building.md), branch from the public repository's
   default branch, and reproduce the problem before editing.
2. Keep the change within the selected issue. Preserve existing authored files
   and the v5/v6 format meaning. Test malformed input and rollback when changing
   persistence, masks or authoring operations.
3. Run the checks relevant to the change in [verification](docs/verification.md).
   A screenshot alone cannot prove topology or persistence; a passing mesh audit
   cannot prove that the object looks right.
4. Open a pull request explaining the before/after behavior, reproduction,
   commands actually run and remaining limitations. Include real renders for
   visible changes. Mark anything you could not verify explicitly.
5. Update the relevant roadmap/work-package progress and evidence, or state that
   no implementation status changed. Work in an open PR stays unchecked and
   marked **In review**. After verifying the merge, the maintainer ticks only the
   delivered scope and reconciles the dashboard and hosted issue state. User,
   live-game and headset acceptance remain separate.

## Art contributions

Use the [authoring guide](docs/authoring.md). Start with one family, preserve
native pixel scale, inspect a complete turn, and check ground contact. Repeated
patterns are matched structurally; a similar name or hash is not sufficient.
Prefer a reviewable recipe change under `recipes/` with exact source coordinates
and assumptions. Generated packs and original game art stay local.

Do not mirror a front door onto every face just to make the building symmetric.
Review the wall/roof geometry separately from its artwork. Back and side
materials are authored assumptions; record them. User acceptance of one house
does not imply acceptance of every new building.

## Code boundaries

- `src/studio/`: editor, source adapter, saving, history and verification tools.
- `src/vr/`: shared scene representation, matching and geometry renderer.
- `integration/runtime/`: our native runner integration source, excluded from
  the standalone editor build; read its status before changing it.
- `third_party/imgui/`: pinned upstream dependency, with its original licence.

Game behavior belongs to the recompilation base. Do not replace gameplay with
hand-written approximations to make a visual test pass. Keep input ownership,
source-pixel roles and the shared mesher intact. No new renderer just for demos.

## Licence for contributions

By submitting original contributions for inclusion, you offer them under the
**RubyVR Studio Noncommercial and No-Sales License 1.0**
(`LicenseRef-RubyVR-Noncommercial-NoSales-1.0`). This covers original code,
tools, recipes, documentation and accepted original models/assets. You must
have the rights to make that offer; no copyright assignment is required.
Identify separately licensed material and retain its notices.

Noncommercial development and free sharing are welcome. Distributed modified
versions must provide source for the covered material and keep these terms
for their changes. Private changes do not have to be published. Sales and
paid access are not licensed. See [the licensing guide](docs/licensing.md),
including the earlier MIT/GPL grants and third-party/output boundaries.

Add `SPDX-License-Identifier: LicenseRef-RubyVR-Noncommercial-NoSales-1.0`
to new original source files where the format supports comments. Do not
replace a third-party file's existing licence header.

## Credit, review and conduct

Identify copied/adapted code and its exact source/licence in the PR. Public
availability alone is not a reuse licence. Preserve third-party notices. Do not
submit ROMs, BIOS images, saves, extracted sprites, personal paths/tokens or
another mod's restricted code. See [asset policy](docs/asset-policy.md).

Be specific and respectful when reviewing work or comparing projects. Discuss
technical behavior and evidence. Personal disputes and private community logs
do not belong in this repository. Maintainers may ask for a smaller PR, missing
evidence or a revision before merging. No model-generated approval substitutes
for a maintainer review.
