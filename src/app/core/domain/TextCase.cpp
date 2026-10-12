#include "TextCase.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace toki::core {

std::string makeUpperCase(const std::string_view text) {
    std::string upper(text);
    std::ranges::transform(upper, upper.begin(), [](const char letter) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    });
    return upper;
}

std::string makeLowerCase(const std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](const char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return lower;
}

bool isCIdentifier(const std::string_view name) {
    return !name.empty() && std::isdigit(static_cast<unsigned char>(name.front())) == 0 &&
           std::ranges::all_of(name, [](const char letter) {
               return std::isalnum(static_cast<unsigned char>(letter)) != 0 || letter == '_';
           });
}

} // namespace toki::core
