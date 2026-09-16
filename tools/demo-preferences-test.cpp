// SPDX-License-Identifier: GPL-3.0-or-later
// demo-preferences-test.cpp — contract test for src/dev/preferences.cpp: strict
// loading, atomic replacement, and never replacing a file it rejected.
//
// Usage: demo-preferences-test <scratch directory>
// The directory is created when missing and must start empty.

#include "dev/preferences.h"
#include "vr/json_scan.h"

#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using rubyvr::dev::load_preferences;
using rubyvr::dev::Preferences;
using rubyvr::dev::save_preferences;

namespace {
int passed = 0, failed = 0;

void check(bool ok, const std::string& what, const std::string& detail = {}) {
    if (ok) { ++passed; return; }
    ++failed;
    std::printf("FAIL: %s%s%s\n", what.c_str(), detail.empty() ? "" : " -- ", detail.c_str());
}

Preferences make(int mode, int speed, bool relative, float yaw, float pitch, float distance,
                 const std::string& checkpoint = {}) {
    Preferences p;
    p.camera_mode = mode;
    p.mouse_speed = speed;
    p.camera_relative = relative;
    p.yaw = yaw;
    p.pitch = pitch;
    p.distance = distance;
    p.checkpoint = checkpoint;
    return p;
}

// A version 1 document with extra members, e.g. doc("\"yaw\":1").
std::string doc(const std::string& members) {
    return members.empty() ? std::string("{\"version\":1}") : "{\"version\":1," + members + "}";
}

void write_text(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::size_t entries(const fs::path& dir) {
    return static_cast<std::size_t>(std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
}

// platform_io names temporaries "<destination>.tmp.<pid>.<seq>".
int leftovers(const fs::path& dir) {
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.path().filename().string().find(".tmp.") != std::string::npos) ++count;
    return count;
}

fs::path fresh(const fs::path& scratch, const char* name) {
    const fs::path dir = scratch / name;
    fs::create_directories(dir);
    return dir;
}

const std::vector<std::string> format_keys = {
    "version", "camera_mode", "mouse_speed", "camera_relative", "yaw", "pitch", "distance", "checkpoint"};

bool exact_keys(const fs::path& file) {
    vr::json::Value root;
    return vr::json::parse_file(file.string().c_str(), &root) && root.keys == format_keys;
}

// A rejected file loads as defaults with a reason, and a later save must leave
// it byte-for-byte alone.
void expect_rejected(const fs::path& file, const std::string& text, const std::string& what,
                     const char* mention = nullptr) {
    write_text(file, text);
    const auto load = load_preferences(file);
    check(!load.writable && load.value == Preferences{} && !load.message.empty(), "rejects " + what, load.message);
    if (mention)
        check(load.message.find(mention) != std::string::npos,
              "diagnostic for " + what + " mentions '" + mention + "'", load.message);
    std::string error;
    const bool saved = save_preferences(file, make(1, 2, false, 1, 1, 50), &error);
    check(!saved && !error.empty() && read_text(file) == text, "save keeps " + what, error);
}

void expect_accepted(const fs::path& file, const std::string& text, const Preferences& expected,
                     const std::string& what) {
    write_text(file, text);
    const auto load = load_preferences(file);
    check(load.writable && load.value == expected, "accepts " + what, load.message);
}

void test_round_trip(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "round-trip");
    const fs::path file = dir / "preferences.json";
    const float two_pi = 2 * std::numbers::pi_v<float>;
    const std::vector<Preferences> cases = {
        make(0, 0, false, 0.123456791f, 0.15f, 1),
        make(1, 2, true, -two_pi, std::nextafter(1.5f, 0.0f), 200, "Boss 3_try-2"),
        make(2, 1, true, two_pi, -1.35f, 199.99f, std::string(48, 'z')),
        make(2, 0, false, std::nextafter(-3.0f, 0.0f), 1.0e-7f, 12.3456789f, "a"),
        make(0, 1, true, 0, 0.9f, 12),
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const std::string label =
            "round trip " + std::to_string(i) + " (camera mode " + std::to_string(cases[i].camera_mode) + ")";
        std::string error;
        check(save_preferences(file, cases[i], &error), label + " saves", error);
        const auto load = load_preferences(file);
        check(load.writable && load.value == cases[i], label + " loads back exactly", load.message);
        check(exact_keys(file), label + " writes exactly the version 1 keys");
    }
    check(entries(dir) == 1 && leftovers(dir) == 0, "round trips leave only the preferences file");
}

void test_missing(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "missing");
    const fs::path file = dir / "preferences.json";
    const auto load = load_preferences(file);
    check(load.writable && load.value == Preferences{}, "missing file loads defaults and stays writable", load.message);

    const fs::path orphan = dir / "absent" / "preferences.json";
    const auto orphan_load = load_preferences(orphan);
    check(orphan_load.writable && orphan_load.value == Preferences{}, "missing folder loads defaults", orphan_load.message);
    std::string error;
    check(!save_preferences(orphan, Preferences{}, &error) && !error.empty(), "save refuses a missing folder", error);
    check(!fs::exists(dir / "absent"), "save does not create the missing folder");
    check(!save_preferences(orphan, Preferences{}), "save reports failure without an error sink");

    write_text(dir / "plain", "not a folder");
    error.clear();
    check(!save_preferences(dir / "plain" / "preferences.json", Preferences{}, &error) && !error.empty(),
          "save fails when the folder is a file", error);
    check(read_text(dir / "plain") == "not a folder", "failed save leaves that file alone");

    expect_accepted(file, doc(""), Preferences{}, "a version-only file as defaults");
    expect_accepted(file, doc("\"camera_mode\":2,\"distance\":50"), make(2, 1, true, 0, 0.9f, 50),
                    "a partial file, defaulting the missing fields");
    expect_accepted(file, "\xEF\xBB\xBF" + doc("\"mouse_speed\":2"), make(0, 2, true, 0, 0.9f, 12),
                    "a UTF-8 byte order mark");
    expect_accepted(file, doc("\"camera_mode\":2,\"pitch\":-1.35"), make(2, 1, true, 0, -1.35f, 12),
                    "the lowest camera mode 2 pitch");
    check(leftovers(dir) == 0, "failed saves leave no temporaries");
}

void test_invalid_documents(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "invalid");
    const fs::path file = dir / "preferences.json";
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"", "an empty file"},
        {" \r\n\t", "a whitespace-only file"},
        {"[]", "an array at the top level"},
        {"\"text\"", "a string at the top level"},
        {"{}", "a missing version"},
        {"{\"camera_mode\":1}", "fields without a version"},
        {"{\"version\":\"1\"}", "a string version"},
        {"{\"version\":0}", "version 0"},
        {"{\"version\":1.5}", "a fractional version"},
        {doc("\"camera_mode\":3"), "camera_mode 3"},
        {doc("\"camera_mode\":-1"), "camera_mode -1"},
        {doc("\"camera_mode\":1.5"), "a fractional camera_mode"},
        {doc("\"camera_mode\":true"), "a boolean camera_mode"},
        {doc("\"camera_mode\":null"), "a null camera_mode"},
        {doc("\"camera_mode\":0x1"), "a hexadecimal camera_mode"},
        {doc("\"camera_mode\":+1"), "a leading plus sign"},
        {doc("\"camera_mode\":01"), "a leading zero"},
        {doc("\"pitch\":.5"), "a missing integer part"},
        {doc("\"pitch\":1."), "a missing fraction"},
        {doc("\"pitch\":1e+"), "a missing exponent"},
        {doc("\"unknown\":\"raw\nnewline\""), "an unescaped control character"},
        {doc("\"mouse_speed\":3"), "mouse_speed 3"},
        {doc("\"mouse_speed\":1e300"), "a huge mouse_speed"},
        {doc("\"camera_relative\":1"), "a numeric camera_relative"},
        {doc("\"camera_relative\":\"true\""), "a string camera_relative"},
        {doc("\"yaw\":\"0\""), "a string yaw"},
        {doc("\"yaw\":6.3"), "yaw above 2pi"},
        {doc("\"yaw\":-6.3"), "yaw below -2pi"},
        {doc("\"yaw\":1e999"), "an overflowing yaw"},
        {doc("\"yaw\":NaN"), "a NaN yaw"},
        {doc("\"yaw\":-Infinity"), "an infinite yaw"},
        {doc("\"yaw\":inf"), "an inf yaw"},
        {doc("\"pitch\":0.1"), "pitch below the camera mode 0 range"},
        {doc("\"camera_mode\":1,\"pitch\":-0.5"), "negative pitch in camera mode 1"},
        {doc("\"camera_mode\":2,\"pitch\":-1.4"), "pitch below the camera mode 2 range"},
        {doc("\"pitch\":1.6"), "pitch above 1.5"},
        {doc("\"distance\":0.5"), "distance below 1"},
        {doc("\"distance\":200.5"), "distance above 200"},
        {doc("\"distance\":[12]"), "an array distance"},
        {doc("\"checkpoint\":\"..\\\\outside\""), "a backslash traversal checkpoint"},
        {doc("\"checkpoint\":\"sub/name\""), "a checkpoint with a separator"},
        {doc("\"checkpoint\":\"C:\\\\name\""), "a drive-qualified checkpoint"},
        {doc("\"checkpoint\":\"CON\""), "a reserved checkpoint name"},
        {doc("\"checkpoint\":\" padded\""), "a checkpoint with a leading space"},
        {doc("\"checkpoint\":\"" + std::string(49, 'a') + "\""), "a 49-character checkpoint"},
        {doc("\"checkpoint\":\"a\\u0000b\""), "a checkpoint with an embedded NUL"},
        {doc("\"checkpoint\":7"), "a numeric checkpoint"},
        {doc("\"checkpoint\":null"), "a null checkpoint"},
        {"{\"version\":1,\"version\":1}", "a duplicate version"},
        {doc("\"note\":1,\"note\":1"), "a duplicate unknown key"},
        {doc("") + "x", "trailing garbage"},
        {doc("") + doc(""), "two objects"},
        {"{\"version\":1}}", "an extra closing brace"},
        {"{\"version\":1,}", "a trailing comma"},
        {"{\"version\" 1}", "a missing colon"},
        {"{version:1}", "an unquoted key"},
    };
    for (const auto& [text, what] : cases) expect_rejected(file, text, what);

    expect_rejected(file, "{\"version\":2,\"camera_mode\":1,\"new_setting\":true}", "a future version", "version");
    expect_rejected(file, doc("\"camera_mode\":1,\"camera_mode\":2"), "a duplicate camera_mode", "duplicate");
    expect_rejected(file, doc("\"camera_mode\":\"2\""), "a string camera_mode", "camera_mode");
    expect_rejected(file, doc("\"checkpoint\":\"../outside\""), "a traversal checkpoint", "checkpoint");
    check(leftovers(dir) == 0, "rejected documents leave no temporaries");
}

void test_truncation(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "truncated");
    const fs::path file = dir / "preferences.json";
    std::string error;
    check(save_preferences(file, make(2, 2, false, 1.25f, -0.5f, 33.5f, "Checkpoint 1"), &error),
          "truncation source saves", error);
    const std::string full = read_text(file);
    const std::size_t close = full.rfind('}');
    if (close == std::string::npos) {
        check(false, "saved file ends its object", full);
        return;
    }
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < close; ++n) {
        write_text(file, full.substr(0, n));
        const auto load = load_preferences(file);
        if (load.writable || load.value != Preferences{}) ++accepted;
    }
    check(accepted == 0, "all " + std::to_string(close) + " truncated prefixes of a saved file are rejected");
    expect_rejected(file, full.substr(0, full.size() / 2), "a file truncated halfway", "truncated");
}

void test_limits(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "limits");
    const fs::path file = dir / "preferences.json";
    const std::string head = "{\"version\":1";
    expect_accepted(file, head + std::string(8192 - head.size() - 1, ' ') + "}", Preferences{},
                    "a file of exactly 8192 bytes");
    expect_rejected(file, head + std::string(8192 - head.size(), ' ') + "}", "a file of 8193 bytes", "8192");
    expect_rejected(file, doc("\"pad\":\"" + std::string(1 << 20, 'x') + "\""), "a 1 MiB file", "8192");

    const std::string future = "{\"version\":1,\"future\":";
    expect_accepted(file, future + "{\"list\":[[1],[2]]}}", Preferences{}, "a shallow nested unknown value");
    expect_rejected(file, future + std::string(64, '[') + std::string(64, ']') + "}", "64 levels of nesting", "nest");
    const std::string deep = future + std::string(4000, '[') + std::string(4000, ']') + "}";
    check(deep.size() <= 8192, "deep nesting case fits inside the size limit");
    expect_rejected(file, deep, "4000 levels of nesting", "nest");
    expect_rejected(file, std::string(8192, '['), "8192 unclosed brackets", "nest");
}

void test_unsafe_paths(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "unsafe");
    const fs::path folder = dir / "folder.json";
    fs::create_directory(folder);
    const auto load = load_preferences(folder);
    check(!load.writable && load.value == Preferences{} && !load.message.empty(),
          "a folder at the preferences path is rejected", load.message);
    std::string error;
    check(!save_preferences(folder, Preferences{}, &error) && !error.empty() && fs::is_directory(folder),
          "save refuses to replace a folder", error);

    const fs::path target = dir / "target.json";
    const fs::path link = dir / "link.json";
    check(save_preferences(target, make(1, 0, true, 0.5f, 1, 20), &error), "symlink target saves", error);
    const std::string before = read_text(target);
    std::error_code ec;
    fs::create_symlink(target.filename(), link, ec);
    if (ec) {
        std::printf("SKIP: symlink checks; cannot create a symlink here (%s)\n", ec.message().c_str());
        return;
    }
    const auto via_link = load_preferences(link);
    check(!via_link.writable && via_link.value == Preferences{}, "a symlinked preferences file is rejected",
          via_link.message);
    error.clear();
    check(!save_preferences(link, Preferences{}, &error) && !error.empty(), "save refuses to replace a symlink", error);
    check(fs::is_symlink(fs::symlink_status(link)) && read_text(target) == before,
          "the symlink and its target are unchanged");
    check(leftovers(dir) == 0, "unsafe paths leave no temporaries");
}

void test_protected(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "protected");
    const fs::path file = dir / "preferences.json";
    const Preferences good = make(2, 1, false, -1, -1, 30, "Keep");
    std::string error;
    check(save_preferences(file, good, &error), "protected file saves", error);
    const std::string before = read_text(file);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const std::vector<std::pair<Preferences, std::string>> invalid = {
        {make(3, 1, true, 0, 0.9f, 12), "camera mode 3"},
        {make(-1, 1, true, 0, 0.9f, 12), "camera mode -1"},
        {make(0, 3, true, 0, 0.9f, 12), "mouse speed 3"},
        {make(0, 1, true, nan, 0.9f, 12), "a NaN yaw"},
        {make(0, 1, true, -inf, 0.9f, 12), "an infinite yaw"},
        {make(0, 1, true, 6.3f, 0.9f, 12), "yaw beyond 2pi"},
        {make(0, 1, true, 0, -1, 12), "negative pitch in camera mode 0"},
        {make(1, 1, true, 0, 0.1f, 12), "low pitch in camera mode 1"},
        {make(2, 1, true, 0, -1.4f, 12), "pitch below the camera mode 2 range"},
        {make(2, 1, true, 0, nan, 12), "a NaN pitch"},
        {make(0, 1, true, 0, 0.9f, 0.5f), "distance below 1"},
        {make(0, 1, true, 0, 0.9f, inf), "an infinite distance"},
        {make(0, 1, true, 0, 0.9f, 12, "../escape"), "a traversal checkpoint"},
        {make(0, 1, true, 0, 0.9f, 12, "nul"), "a reserved checkpoint"},
    };
    for (const auto& [prefs, what] : invalid) {
        error.clear();
        check(!save_preferences(file, prefs, &error) && !error.empty() && read_text(file) == before,
              "save rejects " + what + " and keeps the previous file", error);
        check(!save_preferences(dir / "never.json", prefs) && !fs::exists(dir / "never.json"),
              "save rejects " + what + " without creating a file");
    }
    const auto load = load_preferences(file);
    check(load.writable && load.value == good, "the previous preferences still load", load.message);
    check(leftovers(dir) == 0, "rejected saves leave no temporaries");
}

void test_repeated_replacement(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "repeated");
    const fs::path file = dir / "preferences.json";
    const int rounds = 40;
    int mismatches = 0;
    for (int i = 0; i < rounds; ++i) {
        const int mode = i % 3;
        const float pitch = mode == 2 ? -1.3f + 0.07f * i : 0.2f + 0.03f * i;
        const Preferences p = make(mode, (i / 3) % 3, i % 2 == 0, -6 + 0.3f * i, pitch, 1 + 4.9f * i,
                                   i % 4 ? "Run " + std::to_string(i) : "");
        std::string error;
        const bool saved = save_preferences(file, p, &error);
        const auto load = load_preferences(file);
        if (!saved || !load.writable || load.value != p) {
            ++mismatches;
            std::printf("  replacement %d: %s %s\n", i, error.c_str(), load.message.c_str());
        }
    }
    check(mismatches == 0, std::to_string(rounds) + " consecutive replacements each load back exactly");
    check(entries(dir) == 1 && leftovers(dir) == 0, "replacements leave no temporary files");
}

void test_unknown_fields(const fs::path& scratch) {
    const fs::path dir = fresh(scratch, "unknown");
    const fs::path file = dir / "preferences.json";
    const std::string text = doc(
        "\"camera_mode\":1,\"paused\":true,\"speed\":0,\"noclip\":true,\"mouse_capture\":true,"
        "\"debug\":{\"camera_mode\":2,\"noclip\":true},\"save_state\":\"AAECAwQF\","
        "\"password\":\"hunter2\",\"future_list\":[1,\"two\",null,{\"yaw\":3}]");
    const Preferences expected = make(1, 1, true, 0, 0.9f, 12);
    expect_accepted(file, text, expected, "unknown fields without applying them");

    std::string error;
    check(save_preferences(file, load_preferences(file).value, &error), "a file with unknown fields is replaceable", error);
    const std::string saved = read_text(file);
    for (const char* key : {"\"paused\"", "\"speed\"", "\"noclip\"", "\"mouse_capture\"", "\"debug\"",
                            "\"save_state\"", "\"password\"", "hunter2", "\"future_list\""})
        check(saved.find(key) == std::string::npos, std::string("the saved file drops ") + key);
    check(exact_keys(file) && load_preferences(file).value == expected,
          "the saved file holds only the version 1 keys");
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: demo-preferences-test <scratch directory>\n");
        return 2;
    }
    const fs::path scratch = argv[1];
    try {
        fs::create_directories(scratch);
        if (!fs::is_empty(scratch)) {
            std::fprintf(stderr, "scratch directory must be empty: %s\n", argv[1]);
            return 2;
        }
        test_round_trip(scratch);
        test_missing(scratch);
        test_invalid_documents(scratch);
        test_truncation(scratch);
        test_limits(scratch);
        test_unsafe_paths(scratch);
        test_protected(scratch);
        test_repeated_replacement(scratch);
        test_unknown_fields(scratch);
    } catch (const std::exception& e) {
        check(false, "unexpected exception", e.what());
    }
    if (failed) std::printf("FAIL: %d of %d preference checks failed\n", failed, passed + failed);
    else std::printf("PASS: %d preference checks\n", passed);
    return failed ? 1 : 0;
}
