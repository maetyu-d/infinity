#pragma once
#include <string_view>

namespace infinity {
// Existing saves carry the original brand prefix on otherwise identical records.
inline bool matchesSaveTag(std::string_view tag, std::string_view expected) {
    constexpr std::string_view current = "INFINITY_";
    constexpr std::string_view legacy = "INFINITE_";
    return tag == expected || (tag.starts_with(legacy) && expected.starts_with(current) &&
                               tag.substr(legacy.size()) == expected.substr(current.size()));
}
} // namespace infinity
