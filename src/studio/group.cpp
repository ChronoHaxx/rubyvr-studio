#include "group.h"

namespace studio {

void Document::record(const EditorState& before) {
    if (before == state) return;
    past_.push_back(before);
    if (past_.size() > 128) past_.pop_front();
    future_.clear();
}

bool Document::undo() {
    if (past_.empty()) return false;
    future_.push_back(state);
    state = std::move(past_.back());
    past_.pop_back();
    return true;
}

bool Document::redo() {
    if (future_.empty()) return false;
    past_.push_back(state);
    state = std::move(future_.back());
    future_.pop_back();
    return true;
}

void Document::clear_history() { past_.clear(); future_.clear(); }

std::string Document::new_id() {
    for (;;) {
        const std::string candidate = "group-" + std::to_string(next_id_++);
        bool used = state.has_draft && state.draft.id == candidate;
        for (const auto& p : state.working.patterns) used = used || p.id == candidate;
        if (!used) return candidate;
    }
}

} // namespace studio
