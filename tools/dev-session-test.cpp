// SPDX-License-Identifier: GPL-3.0-or-later
#include "dev/session.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace rubyvr::dev;
namespace fs = std::filesystem;
int checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
std::string read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path root(argv[1]);
    Session s(root);
    check(!s.load_selected(), "empty load refused");
    for (const char* bad : {"", "../outside", "a/b", "a\\b", "a:b", ".", "CON", "nul", "LPT1", " a", "a ", "hidden.state"})
        check(!s.save_new(bad), "unsafe name refused");
    check(s.save_new("Before trainer"), "named capture queued");
    check(!s.save_new("Another"), "no overlapping captures");
    auto request = s.take_request();
    check(request && request->operation == Operation::save, "save request");
    check(!s.take_request(), "request dispatched once");
    std::ofstream(request->path) << "guest snapshot A";
    s.complete(true);
    check(s.names() == std::vector<std::string>{"Before trainer"}, "catalog updated");
    check(read(root / "Before trainer.state") == "guest snapshot A", "capture published intact");
    check(!fs::exists(request->path), "staging cleaned");
    check(!s.save_new("before TRAINER"), "case insensitive duplicate refused");
    check(s.save_new("Other place"), "second independent capture");
    request = s.take_request();
    std::ofstream(request->path) << "guest snapshot B";
    // Another process claimed the name after queuing. Publication must fail
    // without replacing its file, even on a case-sensitive Linux filesystem.
    std::ofstream(root / "Other place.state") << "existing";
    s.complete(true);
    check(read(root / "Other place.state") == "existing", "concurrent destination protected");
    check(s.message().starts_with("Failed:"), "publication failure visible");
    check(s.save_new("Broken capture"), "failed capture queued");
    request = s.take_request();
    std::ofstream(request->path) << "partial";
    s.complete(false, "storage unavailable");
    check(!fs::exists(root / "Broken capture.state") && !fs::exists(request->path), "failed capture not offered");
    check(s.message() == "Failed: storage unavailable", "backend error retained");
    Session reopened(root);
    check(reopened.names().size() == 2, "catalog survives restart");
    reopened.select(0);
    check(reopened.load_selected(), "existing checkpoint can load");
    request = reopened.take_request();
    check(request->operation == Operation::load && read(request->path) == "guest snapshot A", "exact selected bytes loaded");
    reopened.complete(false, "wrong ROM");
    check(read(request->path) == "guest snapshot A", "failed load never mutates source");
    fs::remove(request->path);
    check(!reopened.load_selected(), "missing file refused before backend");
    Transport t;
    check(t.speed() == 1 && !t.paused(), "normal defaults");
    t.pause(true); t.next_frame(); check(t.paused(), "pause persists");
    t.step(); check(!t.paused(), "step releases pause");
    t.next_frame(); check(t.paused(), "step stops after one boundary");
    t.pause(false); t.next_frame(); check(!t.paused(), "resume clears stepping");
    t.set_speed(64); check(t.speed() == 64, "64x accepted");
    t.set_speed(0); check(t.speed() == 0, "uncapped accepted");
    t.set_speed(-1); t.set_speed(65); check(t.speed() == 0, "invalid speed rejected");
    std::cout << "PASS: " << checks << " developer checkpoint/transport checks\n";
}
