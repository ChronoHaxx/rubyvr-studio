// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rubyvr::dev {
enum class Operation { save, load };
struct Request { Operation operation; std::filesystem::path path; };

// Host-only state. Never serialized into a guest checkpoint. All calls belong
// to the runtime thread; guest state operations execute at a dispatch boundary.
class Session {
public:
    explicit Session(std::filesystem::path directory);
    void refresh();
    bool save_new(const std::string& name);
    bool load_selected();
    std::optional<Request> take_request();
    void complete(bool success, const std::string& error = {});
    void select(int index);
    const std::vector<std::string>& names() const { return names_; }
    int selected() const { return selected_; }
    const std::string& message() const { return message_; }
    bool busy() const { return active_.has_value(); }
    bool pending() const { return active_ && !dispatched_; }
    static bool valid_name(const std::string& name);
    static constexpr int capacity = 64;
private:
    std::filesystem::path directory_, destination_;
    std::vector<std::string> names_;
    std::optional<Request> active_;
    int selected_ = 0;
    bool dispatched_ = false;
    std::string message_;
};

class Transport {
public:
    void pause(bool value) { paused_ = value; stepping_ = false; }
    void step() { paused_ = false; stepping_ = true; }
    void next_frame() { if (stepping_) { paused_ = true; stepping_ = false; } }
    bool paused() const { return paused_; }
    void set_speed(int value); // 0 = uncapped; otherwise 1..64 times normal.
    int speed() const { return speed_; }
private:
    int speed_ = 1;
    bool paused_ = false, stepping_ = false;
};
}
