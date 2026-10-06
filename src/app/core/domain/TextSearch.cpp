#include "TextSearch.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace toki::core {

namespace {

[[nodiscard]] char makeLower(const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

bool containsIgnoringCase(const std::string_view text, const std::string_view wanted) {
    return wanted.empty() || !std::ranges::search(text, wanted, [](const char a, const char b) {
                                  return makeLower(a) == makeLower(b);
                              }).empty();
}

} // namespace toki::core
