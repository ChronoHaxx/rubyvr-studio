# Contributor work packages

These are source-controlled definitions for the
[published contribution backlog](https://github.com/ChronoHaxx/rubyvr-studio/issues).
Stable RV identifiers are work-package IDs; GitHub assigns separate issue
numbers. The catalog includes labels and all twelve
[roadmap milestones](https://github.com/ChronoHaxx/rubyvr-studio/milestones).
No due dates or automatic assignment are imposed.

The [roadmap and progress tracker](../roadmap.md) is the canonical M0–M11
checklist. These documents hold each work package's scope and acceptance.
Their Markdown status and hosted issue state require an explicit update when
work lands; the publisher does not synchronize existing issues automatically.

For a first contribution, choose a small scenery review, a manual-workflow
report or a clean-machine setup check. Advanced terrain/runtime work needs the
documented contract and dependencies; it is not labeled good first issue.
Comment on a published issue with the exact slice you intend to take before
starting a large change. Maintainers should split broad implementation into
reviewable PRs and keep evidence up to date.

| ID | Work package | Milestone | Readiness |
|---|---|---|---|
| RV-001 | [Review one starter building or prop from all sides](001-scenery-review.md) | M0 | ready |
| RV-002 | [Track source families and placement review without losing prior evidence](002-coverage-ledger.md) | M1 | inventories, browser, native paths and treatment/ownership rules merged; remaining source/runtime audits pending |
| RV-003 | [Add the first versioned authored terrain surface and shared height query](003-terrain-contract.md) | M2 | terrain/explorer/loading/base/bridge/query component merged; consumers/geography pending |
| RV-004 | [Time a held-out house and tree authoring workflow against the demo checklist](004-manual-workflow.md) | M3 | ready |
| RV-005 | [Add part hide, solo and lock for crowded models](005-part-visibility.md) | M3 | design |
| RV-006 | [Add centered depth and geometry mirroring with intentional face art](006-symmetry-depth.md) | M3 | design |
| RV-007 | [Define a supported public RubySapphireRecomp integration boundary](007-native-integration.md) | M5 | identity/actors/dev/camera/border and connected native scenery merged; PR #31 connected and PR #32 bounded play-session human sequences passed; broader loop/public integration pending |
| RV-008 | [Version map identity in source-built and live snapshots](008-map-identity.md) | M5 | source identity/snapshot v2 and bounded live identity/invalidation merged; wider captures pending |
| RV-009 | [Preview starter-pack updates while preserving personal edits](009-pack-updates.md) | M3 | design |
| RV-010 | [Capture one animated actor sequence with correct source identity](010-actor-capture.md) | M6 | player facing and ordinary NPC views/viewport repair merged; distant actors and broader profiles open |
| RV-011 | [Route field, dialogue, menu and battle transitions without stale scenery](011-scene-routing.md) | M7 | desktop play session merged in PR #32; full-game/VR coverage pending |
| RV-012 | [Add a deterministic sky and time-of-day preview to Studio](012-sky-day-preview.md) | M8 | complete: editor-only preview merged in PR #19; issue #12 closed; runtime M8 remains open |
| RV-013 | [Benchmark one external sprite converter on the same house and tree](013-converter-benchmark.md) | M3 | ready |
| RV-014 | [Inventory Windows dependencies and prove a Linux batch build boundary](014-portability.md) | M10 | design |
| RV-015 | [Design a verified user-ROM source adapter for standalone authoring](015-rom-source-adapter.md) | M11 | design |
| RV-016 | [Add role-aware bulk mask actions with a preview](016-mask-shortcuts.md) | M3 | design |
| RV-017 | [Add one voxel primitive justified by an unmodeled scenery family](017-needed-primitives.md) | M3 | design |
| RV-018 | [Try the standalone build guide on a clean Windows machine](018-clean-setup.md) | M11 | ready |

Every draft includes scope, acceptance, source paths, validation and a compact
AI-assisted contribution instruction. See [CONTRIBUTING](../../CONTRIBUTING.md)
and [AI contributions](../ai-contributions.md).

## Maintainer setup

`python tools/publish-issues.py --repo OWNER/REPO` previews the local plan
without network access. After explicitly choosing the public repository and
reviewing the content, adding `--apply` creates missing labels, milestones and
issues through authenticated GitHub CLI. Stable markers prevent duplicates on
rerun. Existing issues are preserved; changing a draft does not silently edit
an already published conversation. The tool does not create a repository or
post to upstream projects.

A GitHub Projects board can use the same milestones and labels with columns
Backlog, Ready, In progress, In review and Done. The initial contribution flow
works through Issues/Milestones; a hosted Projects board is not yet configured.
