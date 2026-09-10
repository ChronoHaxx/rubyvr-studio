# Define a supported public RubySapphireRecomp integration boundary

Work package **RV-007** · M5 Live integration · help wanted, area: runtime, status: design

## Problem

The editor is independent, but our native runtime prototype still relies on local runner/frame-sink changes. It is not an installable public mod.

## Bounded contribution

Document a compatible public upstream revision and required API contract. Establish the runner redistribution/integration permission before publishing runner code. Evaluate the existing .gbamod/compiled-plugin API, then propose one small adapter PR rather than copying the engine.

## Acceptance

- [ ] Record exact upstream licence/API references and unresolved permission questions.
- [ ] Establish compatible distribution rights for RubyVR's GPLv3-or-later
  source, the framework's Noncommercial terms and the pinned game runner.
  Resolve any required permission or exception with the relevant rightsholders;
  this licence change grants no linking exception. A plugin boundary alone is
  not permission. See [licensing](../licensing.md#separate-native-game-integration).
- [ ] Describe snapshot lifetime, clock ownership, original-frame delivery and renderer lifecycle.
- [ ] Provide a reproducible source-only integration plan using user-supplied game inputs.
- [ ] Clearly distinguish implemented adapter work from missing live/full-game/headset validation.

## Where to start

`integration/README.md`, `integration/runtime/`, `src/vr/ruby_world.h`, `THIRD_PARTY_NOTICES.md`.

## Validation and evidence

Start with source/API evidence and an original synthetic producer/consumer fixture. Any later runner proof must record the exact public revision and inputs without distributing those inputs.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-007 -->
