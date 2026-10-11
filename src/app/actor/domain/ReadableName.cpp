#include "ReadableName.h"

#include "app/actor/Types.h"

#include <cctype>
#include <string>
#include <string_view>

namespace toki::actor {

namespace {

[[nodiscard]] bool isUpper(const char letter) {
    return std::isupper(static_cast<unsigned char>(letter)) != 0;
}

[[nodiscard]] bool isLower(const char letter) {
    return std::islower(static_cast<unsigned char>(letter)) != 0;
}

} // namespace

std::string makeReadableName(std::string_view identifier) {
    if (identifier.size() > 1 && identifier.front() == 's' && isUpper(identifier[1])) {
        identifier.remove_prefix(1);
    }
    std::string words;
    for (usize i = 0; i < identifier.size(); ++i) {
        const char letter = identifier[i];
        if (letter == '_') {
            if (!words.empty() && words.back() != ' ') {
                words += ' ';
            }
            continue;
        }
        const bool isAfterDigit =
            i > 0 && std::isdigit(static_cast<unsigned char>(identifier[i - 1])) != 0 &&
            i + 1 < identifier.size() && isLower(identifier[i + 1]);
        const bool startsWord =
            i > 0 && isUpper(letter) && (isLower(identifier[i - 1]) || isAfterDigit);
        if (startsWord && !words.empty() && words.back() != ' ') {
            words += ' ';
        }
        const bool isFirst = words.empty() || words.back() == ' ';
        words +=
            isFirst ? static_cast<char>(std::toupper(static_cast<unsigned char>(letter))) : letter;
    }
    while (!words.empty() && words.back() == ' ') {
        words.pop_back();
    }
    return words;
}

} // namespace toki::actor
