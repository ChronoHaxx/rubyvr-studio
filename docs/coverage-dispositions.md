# Intended treatment and work ownership

Every reported entry has a treatment, reason, evidence reference and milestone
owning its next action. The policy uses the recorded inventory and authored
roles; it does not inspect current disk/game state or write review approvals.
Reports and Studio exports record the policy version/hash. Run `sync` to refresh
source evidence, then export again to refresh Studio's snapshot.

| Treatment | Meaning |
|---|---|
| `model` | Authored solid or complete source coverage by known authored solids; review every occurrence |
| `intentional_flat` | Explicit source-only mask, or recorded flat-detail intent; surrounding terrain may still need height |
| `terrain` | Recorded terrain intent; the policy does not infer it from a flat/mass label or numeric elevation |
| `animated_effect` | Known field actor/effect or inspected field/battle sprite declaration; source IDs and declarations are not counts of unique rendered images |
| `runtime_state` | Scene, script, behavior or environment control/configuration with no standalone drawing; execution and presentation still need verification |
| `unresolved` | Missing, partial, mixed or unknown roles, source-family context, native audit gaps and retained export gaps |

Complete source coverage is a member-cell test, not a per-pixel/artistic review.
Mixed solid/ground-mask coverage remains unresolved. Unknown or archived models
cannot supply a role. Field and battle declarations route separately to M6 and
M7 based on inspected source paths; other sprite declarations stay unresolved.
Variable graphics and saved warp targets keep their unresolved runtime values,
even where their general presentation/control role is known. Explicit null
tileset callbacks are configuration records, not proof that no other code
changes the tiles. Native traces stay unresolved even when no mutation is found.

The milestone names a work area, not an assigned person or a completion claim.
M1 owns unresolved source-role investigation; M2 owns terrain/behavior constraints
and oversized exports; M4 owns authored scenery review; M5 owns live scene state;
M6 owns field actors/effects; M7 owns battle/UI presentation; M8 owns weather/time.
Full scene/family aggregates are not automatically treated as a single model.

```powershell
python tools/coverage-ledger.py report --milestone M1 --disposition unresolved --out build/coverage/source-triage.md
python tools/coverage-ledger.py report --milestone M7 --category actor_animation --out build/coverage/battle-animations.md
python tools/coverage-ledger.py report --map MAP_OLDALE_TOWN --disposition model --json --out build/coverage/oldale-model-intent.json
python tools/coverage-ledger.py show state:native_trace:PetalburgGymSlideOpenDoors
python tools/coverage-ledger.py export-studio
```

Valid recorded dispositions take precedence, retaining their evidence and note.
Stale decisions are shown as unresolved with the previous result and invalidation
reason; a rule cannot silently replace them. Review stages remain independent.
No decision propagates from a family/model review to its placements. Defaults
are recomputed from the recorded snapshot; the database and authored pack are
not rewritten by reports or exports. Policy changes therefore do not fabricate
new history or change a recorded decision. Full classification detail stays in
the CLI JSON; Studio includes the treatment, reason and next action in its notes.

[Current acceptance](acceptance.md) · [M1 tracker](roadmap.md#m1-coverage-ledger)
