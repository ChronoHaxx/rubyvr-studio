# Third-party notices and source provenance

The project uses the [RubyVR Studio Noncommercial and No-Sales License 1.0](LICENSE)
for original work offered under those terms. Separately licensed third-party
portions retain their original permissions and notices. Earlier public MIT
and GPL grants remain valid; their notices are retained in `LICENSES/`.
See [licensing scope and the repository restart](docs/licensing.md).
These terms do not grant rights to external runtimes, game data or trademarks.

## Included third-party material

| Material | Included form | Notice |
|---|---|---|
| Dear ImGui 1.91.9b | Selected source files under `third_party/imgui`, including SDL2/OpenGL backends | [MIT, copyright Omar Cornut](third_party/imgui/LICENSE.txt) |
| DRAMALESS_SHAPE techniques | C++ adaptations of structural/inference rules, sky-band drawing and phase palettes/tints, with inline references; no Lua asset/runtime distribution | [MIT, copyright Stahltier and contributors](LICENSES/DRAMALESS_SHAPE-MIT.txt) |

ImGui was imported from recomp-ui revision
`76392fb215652a94f5b3b113806b9492731e2807`. The inspected DRAMALESS source is
revision `5ccc0e25417bd8b16cefd50cf08f61c44223e3d4`. Initial file hashes and
relative origins are in [SOURCE_ORIGIN.json](SOURCE_ORIGIN.json).

The environment adaptation uses `lib/Sky.lua` and `lib/DayNight.lua` from that
same revision. The [reuse review](docs/reuse-review.md) records the exact files
and the separate, source-only Quest/OpenXR inspection; no Quest code is bundled.

## External build dependencies

SDL2 ([zlib licence](https://www.libsdl.org/license.php)),
zlib ([licence](https://zlib.net/zlib_license.html)) and
OpenXR headers ([Khronos SDK licence](https://github.com/KhronosGroup/OpenXR-SDK/blob/main/LICENSE))
are provided by the user's development environment, not vendored here.
The editor links SDL2, zlib and the platform OpenGL library. It uses OpenXR
types without linking the loader. Python tools use
[Pillow](https://github.com/python-pillow/Pillow/blob/main/LICENSE) and
[PyYAML](https://github.com/yaml/pyyaml/blob/main/LICENSE).
Any future binary release must include the applicable notices for libraries it
actually redistributes; this source-only preparation is not that binary bundle.

## Game integration sources outside this repository

- [RubySapphireRecomp](https://github.com/mstan/RubySapphireRecomp), upstream
  base `4d49909cbc6dccd3fbb0087cb68347ffbe55ce5d`, local development revision
  `dad4c68251aa3adde77be47884b17870a151f429` plus working changes. No licence
  file was found in the pinned base. Its game runner source/history is not
  republished in this standalone repository.
- [gbarecomp](https://github.com/mstan/gbarecomp), upstream base
  `a1de406b179addf10a534369b64f449e5fe28c4a`, local frame-sink revision
  `13cab0418106e86708cfd10b817379fe2318b201`. It uses
  [PolyForm Noncommercial 1.0.0 with an upstream clarification](https://github.com/mstan/gbarecomp/blob/main/LICENSE).
  It is not linked into the standalone editor. Its terms and clarification
  must be assessed for the actual integration; our licence does not grant
  upstream permissions. We do not advertise the full native game stack as FOSS.
- [pret/pokeruby](https://github.com/pret/pokeruby), source-data revision
  `63a8cbf0016b351a4e68f7036fa0b77e23d2f2c1`. Contributors supply their own
  local checkout. Its source art and map data are not included here and are
  not relicensed by RubyVR.

`integration/runtime/` contains our added adapter/presentation files, not a
copy of the upstream game runner or recompilation framework. Reusing these
files in a runnable game still requires compatible rights and the separate
upstream integration. See [integration notes](integration/README.md).

Screenshots/GIFs document actual development output. Pokémon art, names and
trademarks remain the property of their respective owners. The code licence
does not turn screenshots into a freely licensed game-asset library. See the
[asset policy](docs/asset-policy.md) before proposing media or a release.

For any future imported code, add the source URL, exact revision, applicable
licence and retained notice in the same PR. A public repository alone is not
evidence of permission to redistribute it.
