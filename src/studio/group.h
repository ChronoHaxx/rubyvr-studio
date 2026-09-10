#pragma once

#include <deque>
#include <string>
#include "overrides.h"

namespace studio {

// All undoable authoring state. No GL resources, derived object indices or
// meshes enter history. A source origin belongs to the active match instance;
// Pattern::source records the definition's persisted editing provenance.
struct EditorState {
    vr::overrides::OverrideSet working;
    vr::overrides::Pattern draft, draft_base;
    bool has_draft = false;
    int draft_slot = -1;
    int sel_x = -1, sel_y = -1;
    int origin_x = 0, origin_y = 0;
    int base_origin_x = 0, base_origin_y = 0;
    std::string map_id = "MAP_ROUTE101";
    std::string selected_part;
    bool operator==(const EditorState&) const = default;
};

class Document {
public:
    EditorState state;
    vr::overrides::OverrideSet saved;

    // Call once at gesture release, not for every brush cell or slider tick.
    void record(const EditorState& before);
    bool undo();
    bool redo();
    void clear_history();
    size_t undo_count() const { return past_.size(); }
    size_t redo_count() const { return future_.size(); }
    bool draft_dirty() const { return state.has_draft && state.draft != state.draft_base; }
    bool unsaved() const { return state.working != saved; }
    std::string new_id();

private:
    std::deque<EditorState> past_, future_;
    uint64_t next_id_ = 1;
};

// Invoked by the GUI's existing --selftest; returns count, or -1 on failure.
int document_selftest(const vr::world::Snapshot& snapshot, const char* output);

} // namespace studio
