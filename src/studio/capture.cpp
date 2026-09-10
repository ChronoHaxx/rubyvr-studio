// capture.cpp — see capture.h.

#include "capture.h"

#include "platform_io.h"

#include <cstdio>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace studio {
namespace {

// The descriptor calls are the same operation under two spellings. Keeping
// them behind one name is what lets the reasoning below be about stderr
// redirection rather than about which CRT is in use.
#ifdef _WIN32
int duplicate_descriptor(int fd) { return _dup(fd); }
int replace_descriptor(int from, int to) { return _dup2(from, to); }
int close_descriptor(int fd) { return _close(fd); }
int descriptor_of(std::FILE* f) { return _fileno(f); }
#else
int duplicate_descriptor(int fd) { return ::dup(fd); }
int replace_descriptor(int from, int to) { return ::dup2(from, to); }
int close_descriptor(int fd) { return ::close(fd); }
int descriptor_of(std::FILE* f) { return ::fileno(f); }
#endif

}  // namespace

std::string capture_stderr(void (*fn)(void*), void* ctx) {
    if (!fn) return {};

    std::fflush(stderr);

    // dup/dup2 rather than freopen: restoring with freopen("CON") is
    // Windows-only and throws away any redirection the caller set up, so a
    // piped run would lose its own log. Saving and restoring the descriptor
    // keeps whatever was there — on POSIX that is the difference between
    // honouring `2>build/log.txt` and silently replacing it with the terminal.
    const int saved = duplicate_descriptor(descriptor_of(stderr));
    if (saved < 0) { fn(ctx); return {}; }

    // NOT std::tmpfile(). On Windows the MSVCRT implementation creates its
    // temporary in the ROOT OF THE CURRENT DRIVE, which an unelevated process
    // usually cannot write — it fails, and the failure looks like the mesher
    // printing nothing. A file beside the executable is boring and works.
    //
    // The pid is in the name because there are now TWO executables compiling
    // this, and the studio harness and the GUI are routinely run from the same
    // directory. A fixed name would have them truncating each other's capture
    // and reporting the other's [stats] line, which is precisely the kind of
    // wrong-but-plausible output this project keeps paying for.
    char scratch[64];
    std::snprintf(scratch, sizeof(scratch), "rubyvr_capture_%ld.tmp",
                  platform_io::process_id());

    std::FILE* tmp = std::fopen(scratch, "wb+");
    if (!tmp) { close_descriptor(saved); fn(ctx); return {}; }

    std::fflush(stderr);
    // The two spellings disagree about success: _dup2 returns 0, dup2 returns
    // the new descriptor. Testing against 0 would treat every successful POSIX
    // redirect as a failure and quietly run the callback with the caller's
    // stderr still attached — so the test is "negative means error" on both.
    if (replace_descriptor(descriptor_of(tmp), descriptor_of(stderr)) < 0) {
        // Losing the log is always better than losing the work: run the
        // callback with the caller's own stderr still attached and report an
        // empty capture.
        std::fclose(tmp);
        std::remove(scratch);
        close_descriptor(saved);
        fn(ctx);
        return {};
    }

    fn(ctx);

    std::fflush(stderr);
    replace_descriptor(saved, descriptor_of(stderr));
    close_descriptor(saved);

    std::fseek(tmp, 0, SEEK_END);
    const long n = std::ftell(tmp);
    std::fseek(tmp, 0, SEEK_SET);
    std::string text;
    if (n > 0) {
        text.resize(static_cast<size_t>(n));
        const size_t got = std::fread(text.data(), 1, text.size(), tmp);
        text.resize(got);
    }
    std::fclose(tmp);
    std::remove(scratch);
    return text;
}

}  // namespace studio
