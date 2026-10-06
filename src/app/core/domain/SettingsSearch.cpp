#include "SettingsSearch.h"

#include "app/core/Types.h"
#include "app/core/domain/TextSearch.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace toki::core {

bool matchesSearch(const std::span<const std::string_view> texts, const std::string_view query) {
    usize start = 0;
    while (start < query.size()) {
        const usize first = query.find_first_not_of(' ', start);
        if (first == std::string_view::npos) {
            break;
        }
        const usize end = std::min(query.find(' ', first), query.size());
        const std::string_view word = query.substr(first, end - first);
        const bool found = std::ranges::any_of(texts, [word](const std::string_view text) {
            return containsIgnoringCase(text, word);
        });
        if (!found) {
            return false;
        }
        start = end;
    }
    return true;
}

} // namespace toki::core
