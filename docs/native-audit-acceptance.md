# M1 native audit acceptance

**PASS for source auditing. The ledger now connects native script targets to
tile-changing helpers, including scheduled task candidates.**

| Actual source report | Result |
|---|---|
| Petalburg Gym doors | Special → registered door task → tile helper; two tile-update sites |
| Mauville Gym switches | Two direct tile-update sites; symbolic tile macros remain unresolved |
| Sootopolis Gym cracked ice | One direct tile-update site; runtime conditions remain unverified |
| Full scan | 394 maps; 374 target audits, 46 with mutation paths, 296 with unresolved references; counts overlap |
| Verification | All 45 synthetic tests pass, including the native review reader |
| Persistence | Prior entry IDs and both rejection payloads retained; authored pack unchanged |

The two earlier gym rejections are marked stale because the batch renderer
changed since their recorded review. Their evidence and open defects survive.
The catalog refresh has zero failed maps and retains the eight known oversized
proposals with all 22 fragments; its incomplete-export exit status remains 1.

**M1 remains open:** classification, unresolved native/memory writes, reachable
state combinations and UI/battle coverage. A source path is a candidate, and
finding none does not establish absence of a mutation. This is backend
acceptance, not a rendered change or a live-game test.

[Recorded counts and identities](native-audit-evidence-2026-09-08.json) ·
[Reproduce and inspect](dynamic-inventory.md#native-mutation-audit) ·
[M1 tracker](roadmap.md#m1-coverage-ledger)
