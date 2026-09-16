// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <string>

namespace rubyvr::dev {
// Host-only viewer settings for the demo runner, stored as flat versioned JSON.
// Deliberately holds no guest or save bytes, credentials, or transient debug
// state (pause, speed, noclip, mouse capture): a preferences file restores how
// the runner looks, never what it was doing.
//
// Valid values, all finite: camera_mode and mouse_speed 0..2; yaw within
// [-2pi, 2pi]; pitch within [0.15, 1.5], or [-1.35, 1.5] in camera mode 2;
// distance within [1, 200]; checkpoint empty or a Session::valid_name.
struct Preferences {
    int camera_mode = 0;
    int mouse_speed = 1;
    bool camera_relative = true;
    float yaw = 0, pitch = 0.9f, distance = 12;
    std::string checkpoint;
    bool operator==(const Preferences&) const = default;
};

struct PreferenceLoad {
    Preferences value;
    bool writable = true; // false: the existing file was rejected and saving will not replace it
    std::string message;
};

// A missing file loads as defaults. An unsafe, oversized, malformed, newer or
// invalid file also loads as defaults, with writable=false and the reason in
// message.
PreferenceLoad load_preferences(const std::filesystem::path& path);

// Atomically replaces a missing or valid file in an existing directory. Invalid
// preferences, a rejected existing file or an I/O failure leave any previous
// file untouched and put the reason in *error.
bool save_preferences(const std::filesystem::path& path, const Preferences& value,
                      std::string* error = nullptr);
}
