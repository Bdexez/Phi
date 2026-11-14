#pragma once

namespace phi::detail {

[[noreturn]] void assertFail(const char* condition, const char* file, int line,
                             const char* message);

}  // namespace phi::detail

/// Checks an invariant. Failing an assert is a programming error: the engine
/// logs the location and aborts. Enabled in every build type unless
/// PHI_DISABLE_ASSERTS is defined.
///
///   PHI_ASSERT(index < size);
///   PHI_ASSERT(ptr != nullptr, "device must be created first");
#if defined(PHI_DISABLE_ASSERTS)
#define PHI_ASSERT(condition, ...) ((void)sizeof(condition))
#else
#define PHI_ASSERT(condition, ...)                                                     \
    do {                                                                                \
        if (!(condition)) [[unlikely]] {                                                \
            ::phi::detail::assertFail(#condition, __FILE__, __LINE__, "" __VA_ARGS__); \
        }                                                                               \
    } while (false)
#endif
