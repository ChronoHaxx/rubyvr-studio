# Licensing: GPLv3-or-later

Copyright (c) 2026 ChronoHaxx and RubyVR Studio contributors.

Original code, tools, procedural recipes, documentation and accepted original
models/assets are licensed under the **GNU General Public License, version 3
or (at your option) any later version** (`GPL-3.0-or-later`), except where
separately noted. The [full licence](../LICENSE) is the unmodified standard
GPLv3 text. This grant has no additional noncommercial or no-sales restriction.

## What this means for contributors and users

- You may use, modify, copy and redistribute the covered work, commercially
  or noncommercially, subject to the GPL.
- Distributed covered derivatives must preserve GPL freedoms. When conveying
  binaries, provide their complete corresponding source by a method allowed
  by GPLv3 section 6, retaining required notices and identifying changes.
- Selling copies is allowed. Recipients may modify and redistribute their
  copies, including for free; sellers cannot take those GPL rights away.
- Source generally must be available to recipients as the GPL requires, not
  automatically posted on a public website or submitted back to this project.
  Some source-offer methods have wider obligations. Private changes need not
  be published. Merely running a modified program as a hosted service without
  conveying it does not trigger GPL source-sharing requirements.
- GPL permissions do not guarantee that resale will be unprofitable or that
  improvements will return to this repository. They preserve recipients'
  freedoms when the covered software is distributed.

Read the [GNU FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#GPLRequireSourcePostedPublic)
and the [licence sections on distribution](https://www.gnu.org/licenses/gpl-3.0.html#section6)
for the actual obligations. This guide does not add licence restrictions.

An independently authored output is not automatically GPL merely because the
editor produced it. Output containing or derived from covered material may
have different obligations. For original models/assets covered here, the
preferred editable form is their source; game-derived art remains separate.

## Earlier versions and the repository restart

The previous repository remains a private maintainer archive. Historical
PR/action links point there; the source, roadmap and captured verification
evidence remain in this public repository. The GPL switch uses the existing
repository and does not rewrite its history or revoke earlier grants:

- Previous public main at `532fc64cf7b5258b499525566994276015d92c6b` used MIT.
  Its [MIT notice](../LICENSES/RubyVR-Studio-MIT-legacy.txt) is retained.
- The previous public proposal at `835268dc719d6dd493319778c352661f885b82af`
  offered original contributions under GPLv3-or-later. Its
  [GPL text](../LICENSES/RubyVR-Studio-GPL-3.0-legacy.txt) is retained.
- The fresh repository's initial commit
  `947fe47a32a758172bf7f70fa54ceb06c1a31ec2` used custom Noncommercial and
  No-Sales terms. Its [historical notice](../LICENSES/RubyVR-Studio-Noncommercial-NoSales-1.0-legacy.txt)
  is retained for that version; it is not an extra condition on this GPL grant.
- Retaining old notices does not offer future contributions under those old
  terms. Separately licensed ImGui and DRAMALESS_SHAPE portions keep their MIT
  permissions and notices. See [third-party notices](../THIRD_PARTY_NOTICES.md)
  and [contribution terms](../CONTRIBUTING.md#licence-for-contributions).

The historical custom licence was adapted from
[PolyForm Noncommercial 1.0.0](https://github.com/polyformproject/polyform-licenses/blob/76a278c402bc43b8d2b561da140b0f3e17263015/PolyForm-Noncommercial-1.0.0.md)
under its [licence-text adaptation permission](https://github.com/polyformproject/polyform-licenses/blob/76a278c402bc43b8d2b561da140b0f3e17263015/README.md#license),
with its original name and URL removed from the changed licence. That
provenance does not make the current licence a modified GPL or PolyForm licence.

## Separate native game integration

The standalone editor does not link the game runner or `gbarecomp`. The
external framework has its own Noncommercial terms and clarification; no
redistribution permission has been established for the pinned runner base.
Noncommercial restrictions cannot simply be attached to a GPL-covered
combined work. Distribution of that combination needs compatible upstream
permission or an appropriate exception from the relevant rightsholders.
This GPL adoption grants no such exception or rights in upstream work.

[RV-007 / M5](issues/007-native-integration.md) retains that integration task.
A separate file, native-plugin interface or dynamic link is not, by itself,
proof of GPL compatibility or upstream permission. This does not block the
independent editor and its M2 terrain/map-loading work. The full native game
stack is not represented as FOSS or ready for public redistribution.

## Game material and names

The code licence does not grant rights in ROMs, BIOS files, game assets or
third-party trademarks. The [asset policy](asset-policy.md) remains in force.
Changing the code licence is not permission from game rightsholders.

GPL adoption does not register a trademark or establish exclusive ownership
of a name. It is not a guarantee of a successful infringement claim or store
takedown. Any trademark rights and lawful descriptive uses are separate from
the copyright permissions in this licence; this guide adds no branding
restriction to the GPL grant.
