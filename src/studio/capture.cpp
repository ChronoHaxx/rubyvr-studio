// capture.cpp — see capture.h.

#include "capture.h"

#include <io.h>
#include <fcntl.h>
#include <process.h>

#include <cstdio>

namespace studio {

std::string capture_stderr(void (*fn)(void*), void* ctx) {
    std::fflush(stderr);

    // dup/dup2 rather than freopen: restoring with freopen("CON") is
    // Windows-only and throws away any redirection the caller set up, so a
    // piped run would lose its own log. Saving and restoring the descriptor
    // keeps whatever was there.
    const int saved = _dup(_fileno(stderr));
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
    std::snprintf(scratch, sizeof(scratch), "rubyvr_capture_%d.tmp", _getpid());

    std::FILE* tmp = std::fopen(scratch, "wb+");
    if (!tmp) { _close(saved); fn(ctx); return {}; }

    std::fflush(stderr);
    _dup2(_fileno(tmp), _fileno(stderr));

    fn(ctx);

    std::fflush(stderr);
    _dup2(saved, _fileno(stderr));
    _close(saved);

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
