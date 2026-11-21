#pragma once

#include <phi/core/Types.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace phi::test_support {

/// Calls `run` with `text` (a scene file) once for every value in it
/// replaced by each hostile number, one value at a time: random mutations
/// rarely land exactly on these, in exactly the field that minds.
template <class Run>
void everyValueMadeHostile(const std::string& text, Run&& run) {
    usize lineStart = 0;
    while (lineStart < text.size()) {
        const usize lineEnd = std::min(text.find('\n', lineStart), text.size());
        const usize equals = text.find(" = ", lineStart);
        if (equals != std::string::npos && equals < lineEnd) {
            for (const char* hostile : {"nan", "inf", "-inf", "1e38", "-1e38", "0", "-1", "4000000000"}) {
                std::string file = text;
                file.replace(equals + 3, lineEnd - (equals + 3), hostile);
                run(std::string_view(file));
            }
        }
        lineStart = lineEnd + 1;
    }
}

}  // namespace phi::test_support
