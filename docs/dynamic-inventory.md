# Source-state inventory (M1)

The adapter turns source events, script changes and animation declarations into
queryable coverage entries. It reads the local `pokeruby` checkout; it does not
run that source or change the editor's renderer. A discovered command is not
proof that its branch executes, that its output is implemented, or that it has
passed visual, live or headset review.

## Run and inspect

```powershell
python tools/inventory-dynamics.py
python tools/coverage-ledger.py sync
python tools/coverage-ledger.py report --map MAP_SOOTOPOLIS_CITY --category script_tile_change --out build/coverage/sootopolis-states.md
python tools/coverage-ledger.py report --category warp --json --out build/coverage/warps.json
python tools/coverage-ledger.py show state:warp:MAP_OLDALE_TOWN:0
```

The first command is a source-only scan; it needs no batch executable or graphics
driver. `sync` repeats that scan, verifies it against the exact catalog map set,
runs the existing native placement audit and merges both inventories in one
SQLite transaction. `build/coverage/source-states.json` keeps the source receipt
and complete discovered records. Generated data and reports remain local.

Use `--source` for another checkout. This is a parser for the inspected pokeruby
source conventions, not a universal ROM-hack or other-generation adapter.
Malformed/missing required files and duplicate map/layout identities fail the
scan. Unknown command names and unresolved references become review work.

## Records and meaning

| Record | What is retained | What remains unverified |
|---|---|---|
| Map environment | Source type, layout, weather, flash, music and other map metadata | Runtime overrides and presentation |
| Connections and warps | Every source entry, target map/index, connection direction/offset | Actual transitions and saved dynamic destinations |
| Object, coordinate and background events | Source index, position, layer, graphics, scripts, flags and trigger data | Spawn conditions, actor animation and event execution |
| Script blocks and changes | Source label, commands, conditional branches, tile/weather/actor changes, movement and native calls | Branch reachability, variable values and execution order |
| Graphics/effect/behavior definitions | Symbol, integer expression, aliases and variable IDs | Unique images and actual use of each definition |
| Animation/sprite declarations | Frame/timing expressions, loops, dimensions and pointer tables | Rendered frames, affine/subsprite composition and runtime timing |
| Tileset animation | Source frame paths and the callback for each used tileset; explicit null callbacks | Playback order, rate and affected map texels |
| Native state references | Lexical calls to selected tile, weather, warp, object and field-effect boundaries | Indirect calls, arbitrary memory writes and procedural state combinations |
| Native mutation traces | Script target, helper/callback witness to each reached boundary, source locations and unresolved calls | Whether a branch or scheduled callback actually runs, and writes outside the selected APIs |

Map event coordinates are **unpadded layout cells**. Static catalog placements
use **backup-map cells**. These are named separately in reports; no automatic
seven-cell offset is applied to an event. Elevation remains a gameplay-layer
value, not a physical height. Object-local IDs use the source event array's
one-based order; warp references use its zero-based order.

`MAP_DYNAMIC` intentionally takes its destination from saved warp data. Its
source warp index is ignored by that path. The scanner retains those records
as requiring runtime resolution, rather than inventing a target or reporting
a missing map. Variable object graphics likewise stay unresolved until the
guest state is known. `COORD_EVENT_WEATHER_*` and `WEATHER_*` are separate ID
namespaces; equal numbers do not establish equivalent weather.

Script includes and symbol references conservatively associate shared scripts
with maps. The association is at source-file granularity and can include unused
branches. Both conditional source branches are retained, including version-specific
alternatives, for both preprocessor and assembler conditionals. Numeric evaluation permits only literals, aliases and a small set
of integer operators. Calls, unknown names and unsupported expressions remain
symbolic; no source expression is executed.

## Native mutation audit

The continuation links registered `special`/`specialvar` targets and literal
`callnative` targets to their C definitions. It follows direct helper calls and
literal callback arguments at `CreateTask`, `SetMainCallback2`,
`SetVBlankCallback`, `SetHBlankCallback` and `SetupNativeScript`. Callback edges
are labeled **deferred candidates**. They do not establish execution order or
prove that the callback will run.

```powershell
python tools/coverage-ledger.py report --map MAP_PETALBURG_CITY_GYM --category native_mutation_trace --out build/coverage/native-petalburg.md
python tools/coverage-ledger.py show state:native_trace:PetalburgGymSlideOpenDoors
python tools/coverage-ledger.py report --category native_mutation_trace --json --out build/coverage/native-paths.json
```

For example, the inspected source links Petalburg's door special through its
registered task and door-tile helper to the metatile update API. JSON retains
one shortest witness per reached mutation call site, the original script
references and every encountered unresolved reference. Markdown limits the
display to eight mutation sites per target and `--limit` targets; it says when
more are available. Map filters use the existing conservative script-file
association, which can include unused shared routines.

Same-file static definitions bind locally; conditional/duplicate definitions
remain ambiguous candidates. Cycles terminate without discarding the other
candidate definitions. Pointer/table calls, callback stores, nonliteral
callback arguments, function-like macros and missing bodies remain explicit
unresolved references. Unregistered specials do not gain a valid dispatch just
because a similarly named C function exists.

This is a lexical adapter for inspected C conventions, without preprocessing
or a full C type system. Assembly, header-inline definitions, arbitrary memory
writes and unrecognized indirection still require audit. A trace with zero
listed mutations, even with zero unresolved calls, is **not** evidence that the
routine cannot change state. All traces start unresolved and pending runtime
review. [Recorded results and limits](native-audit-acceptance.md).

## Identity and review retention

Events use map ID, event type and source-array index. Script records use relative
path, label, conditional-definition occurrence and command occurrence. Source
declarations use path and symbol. Inserting a blank line does not change those
identities; changing a source file can still invalidate its dependent reviews.
Reordering indexed events requires another review, rather than transferring an
approval to an object with a different source position or condition.

The inventory fingerprints source bytes in `data`, `src`, `include` and
`graphics`. Each record depends on its own relevant files and shared constants/
macros. Script dependencies include transitively referenced script files. Native
code dependencies invalidate conservatively across records which rely on native
behaviour; actor and tileset-animation declarations include their shared art/
table dependencies. Changes to this inventory adapter or the batch renderer
invalidate dynamic reviews.
The adapter fingerprint covers both `dynamic_inventory.py` and `native_trace.py`;
trace identity uses the target symbol, independently of source line numbers.
Unrelated model recipe changes do not claim or approve a source state.

The source is checked again before ledger updates. Removed entries are archived
with their reviews. The existing review history and three independent approval
stages are reused; all new source states start `pending_runtime` with an
`unresolved` disposition. Parsing success never creates an accepted review.
Legacy single-byte C comments are decoded without replacing bytes; affected
files are named in the receipt, and their original bytes remain the hash input.

## Remaining M1 work

The scanner covers identifiable source declarations and call sites, not every
reachable whole-game state. Review dispositions, unresolved native references,
procedural/direct-memory mutations, UI/battle routing and runtime state
combinations still need investigation. The ledger retains the remaining broad
inventory scopes alongside concrete records. M2 terrain and M3 authoring remain
separate implementation work; no editor reference package is needed for this scan.

`python tools/test-dynamic-inventory.py` and `python tools/test-native-trace.py`
run original synthetic fixtures without game assets or a GPU. The existing
static ledger tests also remain in CI.
