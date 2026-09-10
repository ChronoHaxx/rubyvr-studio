// SPDX-License-Identifier: GPL-3.0-or-later
// platform_io.cpp — see platform_io.h for the contract and the reasoning.

#include "platform_io.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace studio {
namespace platform_io {
namespace {

// Monotonic within one process, so two writes from the same run cannot collide
// even on a filesystem with coarse timestamps. The pid covers the two
// executables and concurrent runs.
std::atomic<unsigned> g_temporary_sequence{0};

#ifndef _WIN32
std::string parent_directory(const std::string& path) {
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return ".";
    return parent.string();
}
#endif

}  // namespace

long process_id() {
#ifdef _WIN32
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(::getpid());
#endif
}

std::string absolute_path(const std::string& path) {
    if (path.empty()) return path;
    std::error_code ec;
    const std::filesystem::path given(path);
    // weakly_canonical resolves every existing component (including symlinks)
    // and normalises the not-yet-existing tail, which is exactly the case for
    // an output file the editor is about to create.
    std::filesystem::path resolved = std::filesystem::weakly_canonical(given, ec);
    if (!ec) return resolved.string();
    ec.clear();
    resolved = std::filesystem::absolute(given, ec);
    if (!ec) return resolved.lexically_normal().string();
    return path;
}

bool same_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
#ifdef _WIN32
    // Windows path comparison is case-insensitive by contract, so the
    // canonical names are folded before comparing. _stricmp keeps the exact
    // behaviour the old _fullpath + _stricmp guard had, minus the _MAX_PATH
    // ceiling.
    return _stricmp(absolute_path(a).c_str(), absolute_path(b).c_str()) == 0;
#else
    // Same inode first: this is what recognises a symlink or hard link that
    // the string form would not. Comparing canonical names remains necessary
    // for a destination that does not exist yet, and is deliberately
    // case-sensitive here — Linux filenames are.
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && !ec &&
        std::filesystem::exists(b, ec) && !ec) {
        ec.clear();
        if (std::filesystem::equivalent(a, b, ec) && !ec) return true;
        if (!ec) return false;
    }
    return absolute_path(a) == absolute_path(b);
#endif
}

std::FILE* create_sibling_temporary(const char* destination,
                                    std::string* out_path) {
    if (!destination || !*destination || !out_path) return nullptr;

    // A handful of attempts rather than one: the exclusive-create is what
    // makes a collision impossible, and the sequence number makes one
    // vanishingly unlikely, but a retry loop costs nothing and removes the
    // "stale file from a killed run" failure mode entirely.
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::string candidate = std::string(destination) + ".tmp." +
                                std::to_string(process_id()) + "." +
                                std::to_string(++g_temporary_sequence);
        if (attempt) candidate += "." + std::to_string(attempt);
#ifdef _WIN32
        HANDLE handle = CreateFileA(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                continue;
            return nullptr;
        }
        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle),
                                       _O_WRONLY | _O_BINARY);
        if (fd < 0) {
            CloseHandle(handle);
            std::remove(candidate.c_str());
            return nullptr;
        }
        std::FILE* file = _fdopen(fd, "wb");
        if (!file) {
            _close(fd);
            std::remove(candidate.c_str());
            return nullptr;
        }
#else
        int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
        const int fd = ::open(candidate.c_str(), flags, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return nullptr;
        }
        std::FILE* file = ::fdopen(fd, "wb");
        if (!file) {
            ::close(fd);
            std::remove(candidate.c_str());
            return nullptr;
        }
#endif
        *out_path = std::move(candidate);
        return file;
    }
    return nullptr;
}

bool sync_file(std::FILE* f) {
    if (!f) return false;
    if (std::fflush(f) != 0) return false;
#ifdef _WIN32
    return _commit(_fileno(f)) == 0;
#else
    return ::fsync(::fileno(f)) == 0;
#endif
}

bool replace_file(const char* temporary, const char* destination) {
    if (!temporary || !destination) return false;
#ifdef _WIN32
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    // POSIX rename(2) replaces the destination atomically. When it fails,
    // nothing at the destination changed, which is the property the caller
    // depends on.
    return std::rename(temporary, destination) == 0;
#endif
}

bool sync_parent_directory(const char* path) {
#ifdef _WIN32
    (void)path;
    return true;  // MOVEFILE_WRITE_THROUGH in replace_file() covers the rename.
#else
    if (!path || !*path) return false;
    const std::string parent = parent_directory(path);
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int fd = ::open(parent.c_str(), flags);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
#endif
}

bool remove_file(const char* path) {
    if (!path || !*path) return false;
    return std::remove(path) == 0;
}

}  // namespace platform_io
}  // namespace studio
