// SPDX-License-Identifier: GPL-3.0-or-later
// portability_test.cpp — original fixtures for the native file/capture boundary.
//
// WHAT THIS PROVES, and what it deliberately does not:
//
//   * The real studio::pattern_io::write and studio::overrides::load round-trip
//     v5, v6 and v7 documents through the portable temporary + atomic replace
//     path — nothing here re-implements the writer.
//   * A rejected document (unsupported version, invalid pattern) and a failed
//     publication both leave the previous destination byte-identical and leave
//     no temporary behind.
//   * studio::capture_stderr restores the caller's own stderr redirection, so
//     the caller can keep logging to its redirected stream after the capture.
//   * Paths with spaces work throughout, and platform_io::same_file recognises
//     relative/symlink aliases while keeping Linux case variants distinct.
//
// It does NOT prove rendering, a GUI journey or an OpenXR runtime. It needs no
// display, no source art and no ROM; DISPLAY may be unset (see docs/native-wsl.md).

#include "capture.h"
#include "pattern_io.h"
#include "platform_io.h"
#include "terrain.h"

#include "overrides.h"
#include "ruby_world.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using vr::overrides::OverrideSet;
using vr::overrides::Pattern;

struct Checks {
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    bool check(bool ok, const char* name) {
        if (ok) {
            ++passed;
            std::printf("[portability] PASS %s\n", name);
        } else {
            ++failed;
            std::fprintf(stderr, "[portability] FAIL %s\n", name);
        }
        return ok;
    }

    void skip(const char* name, const char* reason) {
        ++skipped;
        std::printf("[portability] SKIP %s: %s\n", name, reason);
    }
};

std::string read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void write_bytes(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Any "<name>.tmp.<pid>.<seq>" the writer may have abandoned.
std::vector<std::string> temporary_files(const std::string& directory) {
    std::vector<std::string> found;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(directory, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().filename().string().find(".tmp.") != std::string::npos)
            found.push_back(it->path().string());
    }
    return found;
}

// A minimal but complete v7 world. The same shape foundation_test.cpp uses, so
// a failure here is about file plumbing rather than an invented document.
// Terrain surfaces only live on primary cells (x>=7, y>=7, eight cells of
// border for connection padding), and a map must be at least 16x15.
vr::world::Snapshot synthetic_snapshot() {
    using namespace vr;
    world::Snapshot s;
    s.valid = true;
    s.width = 19;
    s.height = 18;
    s.layout_ptr = 0x20525456;
    s.map_group = 0;
    s.map_number = 1;
    s.identity_source = world::Snapshot::IdentitySource::SourceTable;
    s.grid.assign(size_t(s.width) * s.height, world::kGridUndefined);
    for (int y = 7; y < 11; ++y)
        for (int x = 7; x < 11; ++x) s.grid[size_t(y) * s.width + x] = 0x3001;
    s.metatiles.resize(8192);
    s.attributes.resize(1024);
    s.vram_tiles.resize(32768);
    s.bg_palette.resize(256);
    for (int id = 1; id <= 4; ++id)
        for (int k = 0; k < 4; ++k) s.metatiles[size_t(id) * 8 + k] = uint16_t(id);
    return s;
}

bool build_pattern(const vr::world::Snapshot& s, const char* name, Pattern* out) {
    if (!studio::pattern_io::from_cells(s, {{7, 7}, {8, 7}, {7, 8}}, name, out))
        return false;
    out->source.room = "MAP_PORTABLE";
    return true;
}

// `with_terrain` gives the v7 document a real map to persist; v5/v6 must carry
// no terrain at all (write() refuses that combination).
bool build_set(const vr::world::Snapshot& s, uint32_t version, const char* name,
               bool with_terrain, OverrideSet* out) {
    Pattern p;
    if (!build_pattern(s, name, &p)) return false;
    out->version = version;
    out->patterns = {p};
    out->terrain.clear();
    if (!with_terrain) return true;
    vr::overrides::TerrainMap map;
    map.group = s.map_group;
    map.number = s.map_number;
    map.width = s.width;
    map.height = s.height;
    for (int id : {1, 2, 3}) vr::terrain::guard_tile(s, id, &map);
    for (int y = 7; y < 9; ++y)
        for (int x = 7; x < 9; ++x) {
            vr::overrides::TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.expected = s.cell(x, y);
            cell.underlay = 1;
            vr::overrides::TerrainSurface ground;
            ground.layer = 3;
            ground.height = ground.thickness = 32 + (x - 7) * 4;
            ground.rise_x = 4;
            ground.rise_z = -4;
            ground.top = 1;
            ground.side = 2;
            ground.side_offset = 8;
            cell.surfaces = {ground};
            map.cells.push_back(cell);
        }
    out->terrain = {map};
    return true;
}

// ── Save / replace / readback ────────────────────────────────────────────────

void persistence_checks(Checks& c, const std::string& root,
                        const vr::world::Snapshot& s) {
    const std::string directory = root + "/patterns with spaces";
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (!c.check(!ec, "fixture directory with spaces is created")) return;
    const std::string destination = directory + "/personal override.json";

    OverrideSet v5, v6, v7;
    if (!c.check(build_set(s, vr::overrides::kVersion, "portable v5", false, &v5) &&
                     build_set(s, vr::overrides::kVoxelVersion, "portable v6", false, &v6) &&
                     build_set(s, vr::overrides::kTerrainVersion, "portable v7", true, &v7),
                 "fixtures build through the production pattern derivation"))
        return;

    // One save per supported version, read back with the production loader.
    struct Versioned {
        uint32_t version;
        const OverrideSet* set;
        const char* check_name;
    };
    const Versioned versions[] = {
        {vr::overrides::kVersion, &v5, "v5 save/readback round trip"},
        {vr::overrides::kVoxelVersion, &v6, "v6 save/readback round trip"},
        {vr::overrides::kTerrainVersion, &v7, "v7 save/readback round trip"},
    };
    for (const Versioned& entry : versions) {
        OverrideSet loaded;
        const bool ok = studio::pattern_io::write(destination.c_str(), *entry.set) &&
                        vr::overrides::load(destination.c_str(), &loaded) &&
                        loaded == *entry.set && loaded.version == entry.version;
        c.check(ok, entry.check_name);
    }

    // The paths-with-spaces assertion is the file that just round-tripped.
    c.check(fs::exists(destination) &&
                read_bytes(destination).find("patterns") != std::string::npos,
            "paths with spaces persist and read back");

    // Replace: the previous bytes must actually change, and the new document
    // must be the one the loader sees.
    const std::string before = read_bytes(destination);
    OverrideSet replacement;
    if (!build_set(s, vr::overrides::kTerrainVersion, "replacement v7", true,
                   &replacement))
        return;
    replacement.patterns[0].apply.roof_rows = 3;
    OverrideSet loaded;
    const bool replaced = studio::pattern_io::write(destination.c_str(), replacement) &&
                          vr::overrides::load(destination.c_str(), &loaded) &&
                          loaded == replacement;
    c.check(replaced && read_bytes(destination) != before,
            "replace publishes the new document over the old bytes");
    c.check(read_bytes(destination).rfind("\xEF\xBB\xBF", 0) != 0,
            "written document starts without a byte order mark");

    // Rejected documents: validation refuses before anything is published.
    OverrideSet unsupported = replacement;
    unsupported.version = 4;
    const std::string prior = read_bytes(destination);
    c.check(!studio::pattern_io::write(destination.c_str(), unsupported) &&
                read_bytes(destination) == prior,
            "rejected version keeps the previous destination bytes");

    OverrideSet invalid = replacement;
    invalid.version = vr::overrides::kVoxelVersion;
    invalid.terrain.clear();
    invalid.patterns[0].follow_ground = true;  // needs a voxel model; refused
    c.check(!studio::pattern_io::write(destination.c_str(), invalid) &&
                read_bytes(destination) == prior,
            "rejected pattern keeps the previous destination bytes");

    // A publication failure after a fully written temporary: a directory cannot
    // be replaced by a file, so rename/MoveFileEx fails after validation.
    const std::string occupied = directory + "/occupied destination";
    fs::create_directories(occupied, ec);
    OverrideSet second;
    build_set(s, vr::overrides::kTerrainVersion, "publication failure", true, &second);
    const bool published =
        studio::pattern_io::write(occupied.c_str(), second);
    c.check(!published && fs::is_directory(occupied),
            "failed publication leaves the destination untouched");
}

void unwritable_directory_check(Checks& c, const std::string& root,
                                const vr::world::Snapshot& s) {
    const char* name = "unwritable directory keeps the previous bytes";
#ifdef _WIN32
    c.skip(name, "Windows directory ACLs are not exercised by read-only bits");
#else
    if (::geteuid() == 0) {
        c.skip(name, "running as root: a read-only directory is still writable");
        return;
    }
    const std::string directory = root + "/read only destination";
    std::error_code ec;
    fs::create_directories(directory, ec);
    const std::string destination = directory + "/personal.json";
    OverrideSet first;
    if (!build_set(s, vr::overrides::kTerrainVersion, "before", true, &first)) return;
    if (!c.check(studio::pattern_io::write(destination.c_str(), first),
                 "writable destination accepts the fixture"))
        return;
    const std::string before = read_bytes(destination);

    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    OverrideSet second;
    build_set(s, vr::overrides::kTerrainVersion, "after", true, &second);
    const bool wrote = studio::pattern_io::write(destination.c_str(), second);
    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, ec);

    c.check(!wrote && read_bytes(destination) == before, name);
#endif
}

// ── Cleanup ──────────────────────────────────────────────────────────────────

void cleanup_checks(Checks& c, const std::string& root) {
    c.check(temporary_files(root).empty(),
            "no temporary file survives an attempted write");
}

// ── Capture ──────────────────────────────────────────────────────────────────

struct CaptureContext {
    const char* line = nullptr;
    bool called = false;
};

void capture_callback(void* raw) {
    auto* ctx = static_cast<CaptureContext*>(raw);
    ctx->called = true;
    std::fprintf(stderr, "[portability] captured mesher-style line\n");
    std::fflush(stderr);
    (void)ctx->line;
}

int save_descriptor(int fd) {
#ifdef _WIN32
    return _dup(fd);
#else
    return ::dup(fd);
#endif
}

void restore_descriptor(int from, int to) {
#ifdef _WIN32
    _dup2(from, to);
    _close(from);
#else
    ::dup2(from, to);
    ::close(from);
#endif
}

int descriptor_of(std::FILE* f) {
#ifdef _WIN32
    return _fileno(f);
#else
    return ::fileno(f);
#endif
}

// The caller's own redirection is the fixture: the GUI and the batch tool both
// redirect stderr to a log, and the whole point of the descriptor dance is that
// after capture_stderr() returns, that log keeps receiving output.
void capture_checks(Checks& c, const std::string& root, const fs::path& home) {
    const std::string directory = root + "/capture working directory";
    std::error_code ec;
    fs::create_directories(directory, ec);
    const std::string log_path = root + "/caller stderr.log";

    fs::current_path(directory, ec);
    if (!c.check(!ec, "capture fixture directory is usable")) return;

    const int saved = save_descriptor(descriptor_of(stderr));
    std::FILE* log = std::freopen(log_path.c_str(), "wb", stderr);
    if (!c.check(saved >= 0 && log != nullptr,
                 "caller redirects stderr to its own log")) {
        if (saved >= 0) restore_descriptor(saved, descriptor_of(stderr));
        fs::current_path(home, ec);
        return;
    }

    CaptureContext ctx;
    const std::string captured = studio::capture_stderr(capture_callback, &ctx);
    c.check(ctx.called, "capture still runs the callback");
    c.check(captured.find("captured mesher-style line") != std::string::npos,
            "capture returns the redirected stderr text");

    // The behaviour the contract exists for: log again AFTER the capture.
    std::fprintf(stderr, "[portability] caller log continues after capture\n");
    std::fflush(stderr);
    restore_descriptor(saved, descriptor_of(stderr));
    std::clearerr(stderr);

    const std::string log_text = read_bytes(log_path);
    c.check(log_text.find("caller log continues after capture") != std::string::npos,
            "writing after capture reaches the caller's redirected stderr");
    c.check(log_text.find("captured mesher-style line") == std::string::npos,
            "captured lines do not leak into the caller's log");
    c.check(fs::is_empty(directory, ec) || temporary_files(directory).empty(),
            "capture removes its scratch file");

    // The documented failure path: an unwritable working directory makes the
    // scratch file impossible. The callback must still run and the caller's log
    // must still receive its output — losing the log, never the work.
#ifndef _WIN32
    if (::geteuid() != 0) {
        fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec,
                        fs::perm_options::replace, ec);
        const int saved_again = save_descriptor(descriptor_of(stderr));
        std::FILE* log_again = std::freopen(log_path.c_str(), "wb", stderr);
        if (saved_again >= 0 && log_again) {
            CaptureContext failed_ctx;
            const std::string failed =
                studio::capture_stderr(capture_callback, &failed_ctx);
            std::fflush(stderr);
            restore_descriptor(saved_again, descriptor_of(stderr));
            std::clearerr(stderr);
            c.check(failed.empty() && failed_ctx.called &&
                        read_bytes(log_path).find("captured mesher-style line") !=
                            std::string::npos,
                    "capture failure loses the log but keeps the work");
        } else {
            if (saved_again >= 0) restore_descriptor(saved_again, descriptor_of(stderr));
        }
        fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, ec);
    }
#endif

    fs::current_path(home, ec);
}

// ── Path identity (the GUI input-protection rule) ────────────────────────────

void path_identity_checks(Checks& c, const std::string& root, const fs::path& home) {
    const std::string target = root + "/alias target.json";
    write_bytes(target, "same file\n");
    std::error_code ec;
    fs::current_path(root, ec);

    c.check(studio::platform_io::same_file("alias target.json", target),
            "relative and absolute names of one file compare equal");
    c.check(studio::platform_io::same_file("./alias target.json", target),
            "a path with a . segment compares equal");
    c.check(!studio::platform_io::same_file(target, root + "/other.json"),
            "different paths do not compare equal");

#ifndef _WIN32
    fs::create_symlink("alias target.json", root + "/alias link.json", ec);
    c.check(!ec && studio::platform_io::same_file(root + "/alias link.json", target),
            "a symlink alias compares equal");

    write_bytes(root + "/alias Target.json", "different file\n");
    c.check(!studio::platform_io::same_file(root + "/alias Target.json", target),
            "Linux case variants are different files");
    c.check(!studio::platform_io::same_file("alias target.json", "alias\\target.json"),
            "a backslash is a filename character, not a separator");
#else
    // On Windows the canonical guard folds case and separators, which is the
    // old _fullpath + _stricmp behaviour preserved.
    c.check(studio::platform_io::same_file(target, root + "\\ALIAS TARGET.JSON"),
            "Windows case and separator variants compare equal");
#endif

    fs::current_path(home, ec);
}

}  // namespace

int portability_selftest(const char* workdir) {
    if (!workdir || !*workdir) {
        std::fprintf(stderr, "[portability] a working directory is required\n");
        return 2;
    }
    std::error_code ec;
    const fs::path home = fs::current_path(ec);
    const std::string root = fs::absolute(fs::path(workdir), ec).string();
    fs::create_directories(root, ec);
    if (ec) {
        std::fprintf(stderr, "[portability] cannot create %s\n", root.c_str());
        return 2;
    }

    Checks checks;
    const vr::world::Snapshot snapshot = synthetic_snapshot();
    persistence_checks(checks, root, snapshot);
    unwritable_directory_check(checks, root, snapshot);
    capture_checks(checks, root, home);
    cleanup_checks(checks, root);
    path_identity_checks(checks, root, home);
    cleanup_checks(checks, root);

    fs::current_path(home, ec);
    if (checks.failed) {
        std::fprintf(stderr, "[portability] FAIL: %d of %d checks failed\n",
                     checks.failed, checks.passed + checks.failed);
        return 1;
    }
    std::printf("[portability] PASS: %d checks, %d skipped; native save/replace/"
                "readback, refusal, capture restoration and path aliases\n",
                checks.passed, checks.skipped);
    return 0;
}
