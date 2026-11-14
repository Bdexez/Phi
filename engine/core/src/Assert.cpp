#include <phi/core/Assert.hpp>

#include <phi/core/Log.hpp>

#include <cstdio>
#include <cstdlib>

namespace phi::detail {

void assertFail(const char* condition, const char* file, int line, const char* message) {
    // Write straight to stderr as well: the logger may be filtered or redirected.
    std::fprintf(stderr, "ASSERTION FAILED: %s\n  at %s:%d\n  %s\n", condition, file, line,
                 message);
    std::fflush(stderr);
    log::error("assert", "{} ({}:{}) {}", condition, file, line, message);
    std::abort();
}

}  // namespace phi::detail
