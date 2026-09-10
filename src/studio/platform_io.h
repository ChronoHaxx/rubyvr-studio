// SPDX-License-Identifier: GPL-3.0-or-later
// platform_io.h — the small set of operating-system services the studio needs.
//
// WHY THIS FILE EXISTS. The editor used to call _dup/_dup2/_getpid in
// capture.cpp, CreateFileA/_open_osfhandle/_commit/MoveFileExA in
// pattern_io.cpp, and _fullpath/_stricmp in gui.cpp. Every one of those has a
// direct equivalent on the other platform, so the choice is not *whether* the
// behaviour can be portable but which of the two nearly-identical spellings
// each call site picks. Spreading the choice over three call sites is how a
// persistence guarantee quietly becomes two different guarantees, so the
// platform branch lives here once.
//
// The persistence contract this file exists to keep, in order:
//   1. The temporary is created exclusively in the destination's own directory,
//      so the publication step can never cross a filesystem boundary.
//   2. The bytes are flushed and synchronised before publication, so a crash
//      after the rename cannot leave a complete-looking but empty file.
//   3. The destination is only ever replaced atomically. There is no unlink,
//      no truncate-in-place and no "write beside it and copy over it".
//   4. On failure the destination keeps its previous bytes and the caller still
//      owns the temporary, so it can be removed and reported.

#pragma once

#include <cstdio>
#include <string>

namespace studio {
namespace platform_io {

// Process id, used to keep scratch and temporary names unique between the two
// executables and concurrent runs. Positive; 0 only if the OS refuses.
long process_id();

// Absolute, lexically normalised path. Relative input is resolved against the
// current directory and "." / ".." are folded. Existing symlinks are resolved
// too, so the result names the same file a user would reach through any alias.
// Returns `path` unchanged when it cannot be resolved, so callers can still
// print something useful instead of an empty string.
std::string absolute_path(const std::string& path);

// Whether two names refer to the same file, for the editor's input protection.
//
//   Windows: case-insensitive comparison of the absolute paths. A case or
//   separator variant is the same file there, and treating it as different is
//   the bug the guard exists to prevent.
//   POSIX: identical inode when both names exist (so symlinks and hard links
//   are recognised), otherwise the same canonical name compared
//   case-sensitively. /tmp/a.json and /tmp/A.json are different files and must
//   stay different.
bool same_file(const std::string& a, const std::string& b);

// Create "<destination>.tmp.<pid>.<seq>" exclusively (no truncating an existing
// file, no following a planted symlink) and return it as a binary stream. On
// success *out_path names the file. On failure returns nullptr and creates
// nothing.
//
// The temporary sits beside the destination, which is what makes the rename in
// replace_file() atomic.
std::FILE* create_sibling_temporary(const char* destination, std::string* out_path);

// Flush stdio buffers and ask the OS to put the file's bytes on the device.
// False if either step fails; the caller then refuses to publish.
bool sync_file(std::FILE* f);

// Atomically publish `temporary` as `destination`, replacing any previous file.
//
// False means nothing was published: the destination still holds its previous
// bytes and the caller still owns `temporary`. A same-directory rename cannot
// partially succeed, so there is deliberately no separate "unlink old first"
// path in this file.
bool replace_file(const char* temporary, const char* destination);

// Make the rename itself durable, best effort. POSIX fsyncs the containing
// directory; Windows returns true because replace_file() already asks for
// write-through.
//
// LIMITATION, stated rather than hidden: not every filesystem supports
// directory fsync (some network/fuse mounts return EINVAL) and a directory sync
// failure is deliberately not fatal — the file bytes were already synchronised
// and the rename is atomic with respect to other processes. What is NOT
// promised is that the new name survives an immediate power loss on a
// filesystem that refuses the directory sync. See docs/native-wsl.md.
bool sync_parent_directory(const char* path);

// Remove a file, reporting whether it is gone afterwards. Used for temporary
// cleanup only; never for the destination.
bool remove_file(const char* path);

}  // namespace platform_io
}  // namespace studio
