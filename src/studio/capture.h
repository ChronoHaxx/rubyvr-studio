// capture.h — run something and keep whatever it printed to stderr.
//
// WHY THIS IS SHARED RATHER THAN DUPLICATED. The mesher reports through
// stderr: the [stats] line with its geom= hash, the [object]/[mass] lines, the
// [override] match counts. That is the contract tools/uat.ps1 already parses,
// so it is the one to reuse rather than replace — and it means any tool that
// wants to SHOW what the mesher just did has to read the descriptor back.
//
// compare.cpp needs it to diff two builds in one process. The GUI needs it to
// put the current geom= hash on screen, which is the whole reason a person can
// tell at a glance that the baseline still holds. Two callers, one careful
// implementation, and the two paragraphs of Windows-specific reasoning in the
// .cpp only have to be right once.

#pragma once

#include <string>

namespace studio {

// Call `fn(ctx)` with stderr redirected, and return everything it wrote.
//
// Returns an empty string, having still called `fn`, if the redirection could
// not be set up — losing the log is always better than losing the work.
std::string capture_stderr(void (*fn)(void*), void* ctx);

}  // namespace studio
