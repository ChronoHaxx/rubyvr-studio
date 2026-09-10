# Code, assets and publication

The public contribution format is original code, documentation, procedural
recipes and review notes. A contributor must have the right to submit their
work under the project's licence. Generated game-derived data remains local.

## Suitable contributions

- Original editor/renderer changes with their relevant checks.
- Recipe parameters and original procedural operations, without embedded
  extracted sprites, palette arrays or copied game maps.
- Source identifiers, coordinates, defects and reproducible review steps.
- Documentation and small actual-application screenshots/GIFs for explaining a
  feature or defect, subject to maintainer review. These are illustrative media,
  not a redistribution licence for the game artwork depicted.
- Compatible third-party code with exact provenance and required notices.

## Keep local

Do not upload ROMs, BIOS files, saves/save states, memory/snapshot dumps,
generated ROM code, ripped audio, extracted sprite/tile sheets, full map exports
or generated voxel packs containing source masks/indices. Do not include local
paths with personal information, access tokens or private community transcripts.
Do not paste these inputs into public issues or AI prompts hosted as issue text.

`third_party/pokeruby/`, `build/` and `mod-assets/` are ignored for this reason.
The documented source checkout is a local input, not a vendored asset release.
The initial source extraction used an explicit file allowlist rather than the
private development repository's history. `tools/check-repo.py --publication`
checks the proposed Git file set; it is a guardrail, not legal clearance.

An automatically generated pack and a compact recipe are different artifacts.
The generated pack includes source material needed for matching/rendering and
may therefore be unsuitable for redistribution even when the recipe is original.
Submit the recipe plus instructions to regenerate it locally. User-authored
personal packs should not be overwritten by regeneration or updater changes.

## Before a release

The standalone source can be reviewed separately from the game stack. A runnable
native game bundle still needs an established integration licence, a verified
user-supplied-input flow, notices for shipped libraries and an artifact audit.
No ROM/BIOS distribution is part of the release plan. No emulator save or full
game extraction should become a CI artifact.

Our Noncommercial and No-Sales terms do not override upstream permissions or
supply rights to the pinned game base. A sales ban does not guarantee protection
against game-rightsholder claims. Distribution still requires compatible rights,
as described in
[the licensing guide](licensing.md#separate-native-game-integration). The project is unaffiliated with
Nintendo, Game Freak and The Pokémon Company. Maintainers should resolve
specific ownership or permission questions before including disputed material;
contributors should describe a concern without reposting the material itself.
