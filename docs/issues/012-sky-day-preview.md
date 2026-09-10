# Add a deterministic sky and time-of-day preview to Studio

Work package **RV-012** · M8 Environment · area: rendering · **complete (editor-only scope)**

## Problem

Originally the editor had a clear color and fixed face shading, without an inspectable day/night environment.

**Merged, 2026-09-08:** the bounded editor preview landed in
[PR #19](https://github.com/ChronoHaxx/rubyvr-studio-archive-20260910/pull/19) and
[issue #12](https://github.com/ChronoHaxx/rubyvr-studio/issues/12) is closed.
See [recorded validation](../environment-verification.md) and
[source reuse/provenance](../reuse-review.md). The original issue statement and
acceptance scope below are retained; the full
[M8 checklist](../roadmap.md#m8-sky-lighting-time-and-weather) remains open.

## Bounded contribution

Add an optional deterministic preview for dawn/noon/dusk/night with a simple sky and restrained lighting. Keep it editor-only initially, default neutral, and separate it from guest time/weather policy.

## Acceptance

- [x] Time/lighting controls can be reset to neutral.
- [x] Native art, dark outlines and UI remain readable at all four times in the recorded editor views.
- [x] No procedural environment setting changes persisted model geometry or source palette indices.
- [x] Report draw/rebuild cost and document that runtime clock/weather integration is pending.

## Where to start

`src/studio/gui.cpp`, `src/vr/diorama.*`, `gl_loader.*`, roadmap M8.

## Validation and evidence

Actual renders at four deterministic times and neutral, same camera/source; verify saved pack and geometry hashes remain unchanged.

Dependencies: none beyond the documented local build/source setup.

AI-assisted contributions are welcome. Read `AGENTS.md`, `CONTRIBUTING.md` and
the relevant architecture/format code first. Reproduce the issue, keep one
reviewable change, and report what ran and what did not. Do not weaken checks,
invent visual/headset evidence or upload game data. The human submitting the PR
owns its correctness and provenance.

<!-- rubyvr-work-package:RV-012 -->
