// SPDX-License-Identifier: GPL-3.0-or-later
#include "session.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace rubyvr::dev {
namespace fs = std::filesystem;
namespace {
std::string folded(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}
bool Session::valid_name(const std::string& name) {
    if (name.empty() || name.size() > 48 || name.front() == ' ' || name.back() == ' ')
        return false;
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_')) return false;
    const std::string n = folded(name);
    if (n == "con" || n == "prn" || n == "aux" || n == "nul") return false;
    if (n.size() == 4 && (n.starts_with("com") || n.starts_with("lpt")) &&
        n[3] >= '1' && n[3] <= '9') return false;
    return true;
}
Session::Session(fs::path directory) : directory_(fs::absolute(std::move(directory))) {
    fs::create_directories(directory_);
    if (fs::is_symlink(fs::symlink_status(directory_)))
        throw std::runtime_error("Checkpoint directory must not be a symlink");
    directory_ = fs::canonical(directory_);
    refresh();
    message_ = "Test session: checkpoints stay separate from normal saves";
}
void Session::refresh() {
    const std::string old = selected_ < static_cast<int>(names_.size()) ? names_[selected_] : "";
    names_.clear();
    for (const auto& entry : fs::directory_iterator(directory_)) {
        if (entry.path().extension() != ".state" ||
            !fs::is_regular_file(entry.symlink_status())) continue;
        const auto name = entry.path().stem().string();
        if (valid_name(name)) names_.push_back(name);
    }
    std::sort(names_.begin(), names_.end());
    if (names_.size() > capacity) names_.resize(capacity);
    auto it = std::find(names_.begin(), names_.end(), old);
    select(it == names_.end() ? 0 : static_cast<int>(it - names_.begin()));
}
void Session::select(int index) {
    selected_ = std::clamp(index, 0, std::max(0, static_cast<int>(names_.size()) - 1));
}
bool Session::save_new(const std::string& name) {
    if (busy()) return false;
    if (!valid_name(name)) { message_ = "Use 1-48 letters, numbers, spaces, - or _"; return false; }
    refresh();
    if (names_.size() >= capacity) { message_ = "Checkpoint limit reached (64)"; return false; }
    for (const auto& existing : names_)
        if (folded(existing) == folded(name)) { message_ = "Name already exists; choose a new name"; return false; }
    destination_ = directory_ / (name + ".state");
    if (fs::exists(fs::symlink_status(destination_))) { message_ = "Destination already exists"; return false; }
    const auto staging = directory_ / (name + ".pending");
    if (fs::exists(fs::symlink_status(staging))) { message_ = "Unfinished capture exists; choose another name"; return false; }
    active_ = Request{Operation::save, staging};
    dispatched_ = false;
    message_ = "Saving " + name;
    return true;
}
bool Session::load_selected() {
    if (busy() || names_.empty()) return false;
    const auto path = directory_ / (names_[selected_] + ".state");
    if (!fs::is_regular_file(fs::symlink_status(path))) {
        message_ = "Checkpoint missing or not a regular file";
        return false;
    }
    active_ = Request{Operation::load, path};
    dispatched_ = false;
    message_ = "Loading " + names_[selected_];
    return true;
}
std::optional<Request> Session::take_request() {
    if (!pending()) return {};
    dispatched_ = true;
    return active_;
}
void Session::complete(bool success, const std::string& error) {
    if (!active_ || !dispatched_) return;
    const auto request = *active_;
    std::error_code ec;
    if (request.operation == Operation::save) {
        // Atomically publish without replacing another checkpoint, including
        // one created by another process since the name was checked.
        if (success) {
            fs::create_hard_link(request.path, destination_, ec);
            success = !ec;
        }
        std::error_code ignored;
        fs::remove(request.path, ignored);
    }
    active_.reset();
    const std::string name = request.operation == Operation::save ? destination_.stem().string() : request.path.stem().string();
    message_ = success ? (request.operation == Operation::save ? "Saved " : "Loaded ") + name
                       : "Failed: " + (ec ? ec.message() : error);
    refresh();
    const auto it = std::find(names_.begin(), names_.end(), name);
    if (success && it != names_.end()) select(static_cast<int>(it - names_.begin()));
}
void Transport::set_speed(int value) { if (value >= 0 && value <= 64) speed_ = value; }
}
