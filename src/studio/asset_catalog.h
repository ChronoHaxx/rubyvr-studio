#pragma once
#include "decomp_source.h"
#include "overrides.h"

namespace studio {
// Source inventory, not visual approval. Uses the same snapshot, segmentation,
// compositor and pattern writer as the editor. No SDL or GL context is needed.
bool write_asset_catalog(Decomp&, const char* output_directory);
// Closed-solid pack acceptance: actual shared mesh topology at its authored
// source and deterministic ownership on every map. Visual review is separate.
bool audit_asset_pack(Decomp&, const vr::overrides::OverrideSet&, const char* report_path);
}
