// SPDX-License-Identifier: GPL-3.0-or-later
// preferences.cpp — the demo runner's viewer preferences file.
//
// Loading is strict: a file that is not exactly understood yields defaults and
// writable=false, so an invalid, newer or unsafe file is reported instead of
// being silently replaced by the next save. Saving goes through platform_io's
// sibling temporary and atomic replace; it never truncates in place.

#include "preferences.h"

#include "session.h"
#include "../studio/platform_io.h"
#include "../vr/json_scan.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <numbers>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace rubyvr::dev {
namespace fs = std::filesystem;
namespace io = ::studio::platform_io;
using ::vr::json::Value;

namespace {
constexpr int format_version = 1;
constexpr std::size_t max_bytes = 8192;
constexpr int max_depth = 8;         // flat format, with room for nested optional additions
constexpr double number_limit = 1e6; // beyond every range; keeps int/float conversion defined
constexpr float two_pi = 2 * std::numbers::pi_v<float>;

// The one definition of valid preferences, shared by load and save. The range
// comparisons are negated so NaN fails them.
std::string invalid_reason(const Preferences& p) {
    if (p.camera_mode < 0 || p.camera_mode > 2) return "camera_mode must be 0, 1 or 2";
    if (p.mouse_speed < 0 || p.mouse_speed > 2) return "mouse_speed must be 0, 1 or 2";
    if (!(std::fabs(p.yaw) <= two_pi)) return "yaw must be finite and within [-2pi, 2pi]";
    const float min_pitch = p.camera_mode == 2 ? -1.35f : 0.15f;
    if (!(p.pitch >= min_pitch && p.pitch <= 1.5f))
        return p.camera_mode == 2 ? "pitch must be within [-1.35, 1.5] in camera mode 2"
                                  : "pitch must be within [0.15, 1.5] in camera modes 0 and 1";
    if (!(p.distance >= 1 && p.distance <= 200)) return "distance must be within [1, 200]";
    if (!p.checkpoint.empty() && !Session::valid_name(p.checkpoint))
        return "checkpoint must be empty or a valid checkpoint name";
    return {};
}

// Nine significant digits always read back as the same float, and to_chars,
// unlike printf, ignores the locale's decimal separator.
std::string number(float value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value,
                                      std::chars_format::general, 9);
    return std::string(buffer, result.ptr);
}

std::string serialize(const Preferences& p) {
    // valid_name admits only letters, digits, space, '-' and '_', none of which
    // need escaping in a JSON string.
    return "{\n  \"version\": " + std::to_string(format_version) +
           ",\n  \"camera_mode\": " + std::to_string(p.camera_mode) +
           ",\n  \"mouse_speed\": " + std::to_string(p.mouse_speed) +
           ",\n  \"camera_relative\": " + (p.camera_relative ? "true" : "false") +
           ",\n  \"yaw\": " + number(p.yaw) +
           ",\n  \"pitch\": " + number(p.pitch) +
           ",\n  \"distance\": " + number(p.distance) +
           ",\n  \"checkpoint\": \"" + p.checkpoint + "\"\n}\n";
}

#ifdef _WIN32
// MinGW's std::filesystem reports a symlink as the file it points to, so ask
// Windows whether the entry itself is a link.
bool reparse_point(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
#else
bool reparse_point(const fs::path&) { return false; }
#endif

bool missing(const fs::path& path) {
    std::error_code ec;
    return fs::symlink_status(path, ec).type() == fs::file_type::not_found;
}

// Reads one byte past the limit, so an oversized file is caught without
// trusting the size its directory entry reports.
std::string read_bounded(const fs::path& path, std::string* bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "it cannot be opened";
    bytes->assign(max_bytes + 1, '\0');
    in.read(bytes->data(), static_cast<std::streamsize>(bytes->size()));
    if (in.bad()) return "it cannot be read";
    bytes->resize(static_cast<std::size_t>(in.gcount()));
    if (bytes->size() > max_bytes) return "it is larger than 8192 bytes";
    return {};
}

// json_scan recurses once per nesting level and hands anything it does not
// recognise to strtod. This non-recursive pass bounds that recursion before the
// parser runs, and rejects what strict JSON does not allow there: NaN,
// Infinity, hexadecimal numbers and data after the object.
std::string structure_problem(std::string_view text) {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3); // json_scan skips a BOM too
    int depth = 0;
    bool in_string = false, escaped = false, closed = false, empty = true;
    for (std::size_t at=0; at<text.size(); ++at) {
        const char c=text[at];
        if (in_string) {
            if(static_cast<unsigned char>(c)<0x20)return "its string contains an unescaped control character";
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (closed) return "it has data after the JSON object";
        empty = false;
        // strtod accepts +1, .5, 01 and 1.; JSON does not. Bound each number's
        // syntax here so the shared generated-data parser cannot admit them.
        const auto digit=[](char n){return n>='0' && n<='9';};
        if(digit(c) || c=='-' || c=='+' || c=='.') {
            std::size_t end=at;
            if(text[end]=='-')++end;
            if(end==text.size())return "its JSON number is incomplete";
            if(text[end]=='0')++end;
            else if(text[end]>='1' && text[end]<='9')while(end<text.size() && digit(text[end]))++end;
            else return "it has an invalid JSON number";
            if(end<text.size() && text[end]=='.') {
                const auto begin=++end;while(end<text.size() && digit(text[end]))++end;
                if(end==begin)return "its JSON fraction is incomplete";
            }
            if(end<text.size() && (text[end]=='e' || text[end]=='E')) {
                ++end;if(end<text.size() && (text[end]=='+' || text[end]=='-'))++end;
                const auto begin=end;while(end<text.size() && digit(text[end]))++end;
                if(end==begin)return "its JSON exponent is incomplete";
            }
            if(end<text.size() && std::string_view(",]} \t\r\n").find(text[end])==std::string_view::npos)
                return "it has an invalid JSON number ending";
            at=end-1;continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max_depth) return "its JSON nesting is too deep";
        } else if (c == '}' || c == ']') {
            if (--depth < 0) return "its JSON brackets are unbalanced";
            closed = depth == 0;
        } else if (std::string_view("0123456789+-.eEtrufalsn,:").find(c) == std::string_view::npos) {
            return "it contains a character JSON does not allow there";
        }
    }
    if (empty) return "it is empty";
    if (in_string || depth > 0) return "it is truncated";
    return {};
}

// Reads known keys with exact types; absent keys keep their defaults. The first
// problem wins and later reads do nothing.
struct FieldReader {
    const Value& root;
    std::string problem;

    const Value* member(const char* key, Value::Type type, const char* expected) {
        const Value* value = problem.empty() ? root.find(key) : nullptr;
        if (value && value->type != type) {
            problem = std::string(key) + " must be " + expected;
            return nullptr;
        }
        return value;
    }
    void read(const char* key, int* out) {
        const Value* value = member(key, Value::Type::Number, "an integer");
        if (!value) return;
        if (value->number != std::trunc(value->number)) problem = std::string(key) + " must be an integer";
        else if (!(std::fabs(value->number) <= number_limit)) problem = std::string(key) + " is out of range";
        else *out = static_cast<int>(value->number);
    }
    void read(const char* key, float* out) {
        const Value* value = member(key, Value::Type::Number, "a number");
        if (!value) return;
        if (!std::isfinite(value->number)) problem = std::string(key) + " must be finite";
        else if (!(std::fabs(value->number) <= number_limit)) problem = std::string(key) + " is out of range";
        else *out = static_cast<float>(value->number);
    }
    void read(const char* key, bool* out) {
        if (const Value* value = member(key, Value::Type::Bool, "true or false")) *out = value->boolean;
    }
    void read(const char* key, std::string* out) {
        if (const Value* value = member(key, Value::Type::String, "a string")) *out = value->string;
    }
};

// Unknown keys are never read, so they cannot switch anything on.
std::string decode(const Value& root, Preferences* out) {
    if (!root.is_object()) return "its top level is not a JSON object";
    for (std::size_t i = 0; i < root.keys.size(); ++i)
        for (std::size_t j = i + 1; j < root.keys.size(); ++j)
            if (root.keys[i] == root.keys[j]) return "it has a duplicate key";
    const Value* version = root.find("version");
    if (!version) return "its version is missing";
    if (version->type != Value::Type::Number || version->number != format_version)
        return "its version is not supported (this build reads version 1)";

    Preferences p;
    FieldReader fields{root, {}};
    fields.read("camera_mode", &p.camera_mode);
    fields.read("mouse_speed", &p.mouse_speed);
    fields.read("camera_relative", &p.camera_relative);
    fields.read("yaw", &p.yaw);
    fields.read("pitch", &p.pitch);
    fields.read("distance", &p.distance);
    fields.read("checkpoint", &p.checkpoint);
    if (!fields.problem.empty()) return fields.problem;
    if (std::string why = invalid_reason(p); !why.empty()) return why;
    *out = std::move(p);
    return {};
}

// Why the existing file at `path` cannot be used, or empty with *out filled in.
std::string parse_existing(const fs::path& path, Preferences* out) {
    try {
        std::error_code ec;
        const fs::file_status status = fs::symlink_status(path, ec);
        if (ec) return "its status cannot be read (" + ec.message() + ")";
        if (fs::is_symlink(status) || reparse_point(path)) return "it is a symlink";
        if (!fs::is_regular_file(status)) return "it is not a regular file";

        std::string bytes;
        if (std::string why = read_bounded(path, &bytes); !why.empty()) return why;
        if (std::string why = structure_problem(bytes); !why.empty()) return why;
        Value root;
        // Parse the exact bytes whose size/depth were checked, not a second read.
        if (!::vr::json::parse_text(bytes, &root)) return "it is not valid JSON";
        return decode(root, out);
    } catch (const std::exception& e) {
        return std::string("it cannot be read (") + e.what() + ")";
    }
}
}

PreferenceLoad load_preferences(const fs::path& path) {
    PreferenceLoad result;
    if (missing(path)) {
        result.message = "No preferences file yet; using defaults";
        return result;
    }
    if (const std::string why = parse_existing(path, &result.value); !why.empty()) {
        result.value = {};
        result.writable = false;
        result.message = "Preferences file ignored because " + why +
                         "; using defaults and leaving the file unchanged";
    } else {
        result.message = "Loaded preferences";
    }
    return result;
}

bool save_preferences(const fs::path& path, const Preferences& value, std::string* error) {
    const auto fail = [error](const std::string& why) {
        if (error) *error = "Preferences not saved: " + why;
        return false;
    };
    try {
        if (std::string why = invalid_reason(value); !why.empty()) return fail(why);
        const fs::path folder = path.has_parent_path() ? path.parent_path() : fs::path(".");
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) return fail("its folder does not exist");
        // Only a missing or fully understood file may be replaced; anything else
        // may be newer or hand-edited, and stays for the user to resolve.
        Preferences existing;
        if (std::string why = missing(path) ? "" : parse_existing(path, &existing); !why.empty())
            return fail("the existing file is kept because " + why);

        const std::string text = serialize(value);
        const std::string target = path.string();
        std::string temporary;
        std::FILE* file = io::create_sibling_temporary(target.c_str(), &temporary);
        if (!file) return fail("a temporary file cannot be created beside it");
        bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size() &&
                       io::sync_file(file);
        written = std::fclose(file) == 0 && written;
        if (!written || !io::replace_file(temporary.c_str(), target.c_str())) {
            io::remove_file(temporary.c_str());
            return fail(written ? "the file cannot be replaced" : "the temporary file cannot be written");
        }
        // Best effort by platform_io's contract: the bytes are already synced and
        // the rename is atomic.
        io::sync_parent_directory(target.c_str());
        return true;
    } catch (const std::exception& e) {
        return fail(e.what());
    }
}
}
