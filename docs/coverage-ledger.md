# Coverage ledger (M1 / RV-002)

The ledger answers which source groups and placements have authored models,
which remain unresolved, and what evidence supports each review. It does not
convert a building, approve its hidden walls or implement terrain heights.
The [source-state inventory](dynamic-inventory.md) adds map events, connections,
script changes, animation declarations and native mutation references.
The gym defects and remaining Sootopolis scenery are recorded in
[known defects](known-scenery-defects.json) for M4; terraces and shore heights
belong to M2. The [roadmap](roadmap.md) remains the work-order reference.

## Use it

After the documented build/source setup, regenerate the catalog and starter
pack with `python tools/prepare-assets.py`. Catalog v2 retains every repeated
flat tile's coordinate; older exports have only a representative and a count.
The existing eight oversized proposals remain explicit gaps, with all fragments
retained. `prepare-assets.py` recognizes that known incomplete-export condition.

```powershell
python tools/coverage-ledger.py sync
python tools/coverage-ledger.py report --out build/coverage/remaining-work.md
python tools/coverage-ledger.py report --map MAP_SOOTOPOLIS_CITY --out build/coverage/sootopolis.md
python tools/coverage-ledger.py show model:asset-544bfd83afef3991
python tools/coverage-ledger.py report --map MAP_SOOTOPOLIS_CITY --category script_tile_change --out build/coverage/sootopolis-states.md
python tools/coverage-ledger.py report --map MAP_PETALBURG_CITY_GYM --category native_mutation_trace --out build/coverage/native-petalburg.md
python tools/coverage-ledger.py report --milestone M1 --disposition unresolved --out build/coverage/source-triage.md
```

`sync` runs fresh native matching against the selected pack, then merges into
`build/coverage/ledger.sqlite`. `--db` before the command selects another ledger.
The catalog's source receipt must still match the source inputs and batch
binary. A mismatch requires regenerating the catalog; stale exports cannot
silently certify current coverage. Reports link to the local source drawings.
`sync` also scans source states and verifies their separate source receipt before
updating the database. `python tools/inventory-dynamics.py` runs that source-only
scan independently. Both inventories keep their own review dependencies.
Use `report --json --out build/coverage/remaining-work.json` for all records.

Record a review only after inspecting the named item:

```powershell
python tools/coverage-ledger.py review model:asset-544bfd83afef3991 --stage visual --result rejected --evidence "local-review/sootopolis-gym.png" --note "Entrance needs forward projection; side and rear materials need revision."
```

Stages `visual`, `live` and `headset` accept `accepted`, `rejected` or `blocked`.
Each requires an evidence reference and note. `disposition` is a separate stage
with `model`, `terrain`, `intentional_flat`, `animated_effect`, `runtime_state`
or `unresolved`.
None of the review stages is promoted by a model match, passing topology check,
catalog count or review of another entry. Evidence paths/references should be
durable; the ledger retains the reference, not a copy of the evidence.

Reports and Studio share [intended-treatment rules](coverage-dispositions.md),
including the reason, evidence and milestone owning the next action. These are
defaults for the recorded inventory, not new reviews. A valid recorded
disposition overrides a rule; a stale one stays unresolved until reconsidered.
`report --milestone` and `--disposition` filter the effective result, and `show`
includes its explanation alongside the unchanged history. Reporting does not
write the database. Source families do not inherit an occurrence's intent, and
family decisions do not silently replace placement decisions.

## Browse static reviews in Studio

After the first successful `sync`, `tools/run-studio.ps1` exports a read-only
view of the ledger before opening the editor. Click **Review…**, find a map,
choose **No model**, **Unresolved**, **Unreviewed** or **Failed**, select an item,
then **OPEN IN MAP**. The source rectangle is outlined and the scene camera
frames that occurrence. Unapplied edits use the normal Apply/Discard/Cancel
guard. Browsing and filtering do not write models or review decisions.

Flat/ground drawings and connection-padding copies start hidden; their toggles
include them explicitly. **No model** includes partial coverage. **Failed**
includes blocked/conflicting placements, rejected reviews and open related
model/family defects. Related approval never approves a placement. The details
show the placement's own visual/live/headset results and the source of each
related note; stale reviews retain their earlier result and invalidation reason.
Map-wide issues are separate from individual placement failures.

```powershell
python tools/coverage-ledger.py export-studio
build/rubyvr_studio.exe --check-review-index build/coverage/studio/index.json
python tools/test-studio-review.py
```

`export-studio --pack <audited-pack> --out <index.json>` supports another pack;
use `--db` before the command for another ledger. Direct GUI launches accept
`--review-index <index.json>`. The launcher exports the default audited starter
pack; personal artwork can differ and is explicitly marked historical in the
browser. Re-exporting reads recorded evidence; it does not rerun a source or
renderer audit. Run a fresh `sync` after changes to those inputs. Source changes
with unchanged map dimensions are not independently detected by the browser.

The exporter verifies pack/catalog identity, protects its inputs, keeps a SQLite
read transaction, writes
deterministic per-map files and publishes the index last. A failed export
preserves the previous index. The native reader validates identities, bounds,
row counts and duplicate placements before replacing the displayed map. It
loads one map at a time; list filtering is cached between input changes. These
files stay under ignored `build/coverage/studio/` and contain no sprite images.
The view covers 159,937 source placements, 5,832 model/floor-mask instances and
8 retained export gaps across 394 maps; these overlapping units are not a
completion percentage. Source-state candidates and review writing remain in
the ledger CLI for this bounded M1 continuation.

## Identity and retention

The version-1 SQLite schema has entries, review history, defect records and input
metadata. Entries include maps, source families, exact source placements,
authored definitions, authored instances, source states, export gaps and explicit
pending inventory categories. Model availability and intended disposition are separate
from the three review stages.

- Map IDs come from the source adapter. Family IDs retain the catalog's exact
  structural/art identity. Placement and instance IDs combine map, backup-map
  coordinates and family/model identity. Unchanged input keeps the same IDs.
- Source fingerprints cover membership, tile definitions, source-image bytes,
  indexed graphics/palette inputs and placement context. Recipe fingerprints
  cover the matched definitions and batch binary. Shared tileset or binary
  changes invalidate conservatively, even when a visible result might coincide.
- An unchanged merge keeps reviews. Source or recipe/renderer changes mark
  affected review records stale and retain the old result, evidence and reason.
  A recipe-only change preserves the intended disposition. A source change
  requires reconsidering that disposition.
- Removed entries remain archived with their reviews. A content change that
  produces a new catalog family creates an unreviewed entry and retires the old
  identity; reviews are never guessed across different drawings. A returning
  archived entry requires another review.
- Duplicate IDs/coordinates, incomplete flat coordinates, invalid fragments,
  unknown maps and contradictory accepted cell claims abort import. Database
  changes use one transaction so a failed merge preserves the previous ledger.

“Modeled” means source member cells have authored coverage. A source group can
contain several buildings, trees and terrain; partial coverage remains partial.
Unclaimed/partial/mixed drawings stay unresolved and are **not automatically missing models**.
Complete member-cell coverage with one known authored role gets that role as its
default intent, without establishing pixel ownership, artistic correctness or
visual approval. A `flat` segmentation label by itself never means intentional flatness.
Model instances inside the main map, instances in connection padding and source
groups crossing a boundary are reported separately. These are different units;
their counts must not be added into a completion percentage.

## Scope and verification

The local run covers 394 maps, 4,787 source families, 159,937 exact static source
placements, 72 authored definitions and 5,832 authored instances including
floor masks and padding. Eight oversized source groups retain all 22 fragments.
The merged source-state baseline adds 27,927 records across the same 394 maps,
for 198,961 present entries. The native-path continuation adds 374 target audits,
bringing the tested inventory to 28,301 source records and 199,335 present ledger
entries. Forty-six targets have paths to a selected mutation API; 296 have
unresolved calls/macros/stores (these counts overlap). [Acceptance evidence](native-audit-acceptance.md)
records the tested scope; [M1 status](roadmap.md#m1-coverage-ledger) tracks its merge.
Four **pending inventory scopes** remain. The old warps/connections placeholder is archived because concrete
records now describe every source entry, including 41 saved-destination warps.
Neither the record count nor a pending-scope count is a count of reachable
runtime combinations or a completion percentage.

`python tools/test-coverage-ledger.py` uses original synthetic inputs without
game data, graphics drivers or network access. It checks unchanged merges,
source/recipe/renderer invalidation, independent stages, removal/return,
duplicates, partial coverage, exact flat coordinates, padding and fragments.
The same check and `python tools/test-dynamic-inventory.py` run in CI. The latter
checks symbolic/conditional source, saved warp destinations, exact event
coordinates, shared script dependencies, native references, retained review
history and malformed inputs. Native catalog generation and the local 394-map merge
provide separate real-source evidence; database/report outputs stay ignored.
`python tools/test-native-trace.py` adds ten original helper/callback, ambiguity,
unresolved-reference and review-retention checks. Source witnesses appear in CLI
reports and JSON; Studio's review browser continues to show static placements.
`python tools/test-coverage-disposition.py` adds twelve original treatment,
source-domain, manual-override, stale-intent, filter, read-only and CLI/Studio
consistency and concurrent-sync checks. [Current acceptance](acceptance.md) records the real result.

RV-002's static and source-state inventories are implemented. M1 still needs
classification/review decisions and audits of indirect/native changes, reachable
state combinations and complete UI/battle/gameplay presentation. See the source
adapter's [record definitions and limits](dynamic-inventory.md). Runtime and
headset evidence remain work to perform; parsing a source command never supplies it.
