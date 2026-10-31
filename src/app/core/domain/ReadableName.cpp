#include "ReadableName.h"

#include "app/core/Types.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace toki::core {

namespace {

[[nodiscard]] bool isUpper(const char letter) {
    return std::isupper(static_cast<unsigned char>(letter)) != 0;
}

[[nodiscard]] bool isLower(const char letter) {
    return std::islower(static_cast<unsigned char>(letter)) != 0;
}

constexpr std::array CapitalWords{std::string_view("ID"),   std::string_view("BGM"),
                                  std::string_view("SFX"),  std::string_view("DL"),
                                  std::string_view("MM"),   std::string_view("OOT"),
                                  std::string_view("RGBA"), std::string_view("NPC"),
                                  std::string_view("HUD"),  std::string_view("UI")};

[[nodiscard]] bool isConstant(const std::string_view name) {
    return std::ranges::any_of(name, [](const char letter) { return isUpper(letter); }) &&
           std::ranges::all_of(name, [](const char letter) {
               return isUpper(letter) || letter == '_' ||
                      std::isdigit(static_cast<unsigned char>(letter)) != 0;
           });
}

[[nodiscard]] std::vector<std::string_view> splitWords(std::string_view name) {
    std::vector<std::string_view> words;
    while (!name.empty()) {
        const usize end = std::min(name.find('_'), name.size());
        if (end > 0) {
            words.push_back(name.substr(0, end));
        }
        name.remove_prefix(std::min(end + 1, name.size()));
    }
    return words;
}

[[nodiscard]] std::string joinWords(const std::span<const std::string_view> words) {
    std::string label;
    for (const std::string_view word : words) {
        if (!label.empty()) {
            label += ' ';
        }
        if (std::ranges::contains(CapitalWords, word) ||
            std::isdigit(static_cast<unsigned char>(word.front())) != 0) {
            label += word;
            continue;
        }
        label += word.front();
        for (const char letter : word.substr(1)) {
            label += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
        }
    }
    return label;
}

/// How many words every constant of `words` starts with alike, leaving each at
/// least one.
[[nodiscard]] usize countSharedWords(const std::span<const std::vector<std::string_view>> words) {
    const auto shortest = std::ranges::min_element(words, {}, &std::vector<std::string_view>::size);
    usize shared = 0;
    while (shared + 1 < shortest->size() &&
           std::ranges::all_of(words, [&](const std::vector<std::string_view>& other) {
               return other[shared] == words.front()[shared];
           })) {
        ++shared;
    }
    return shared;
}

} // namespace

std::vector<std::string> makeConstantLabels(const std::span<const std::string_view> names) {
    std::vector<std::vector<std::string_view>> constants;
    for (const std::string_view name : names) {
        if (isConstant(name)) {
            constants.push_back(splitWords(name));
        }
    }
    const usize shared = constants.size() > 1 ? countSharedWords(constants) : 0;
    std::vector<std::string> labels;
    labels.reserve(names.size());
    for (const std::string_view name : names) {
        if (!isConstant(name)) {
            labels.emplace_back(name);
            continue;
        }
        const std::vector<std::string_view> words = splitWords(name);
        labels.push_back(joinWords(std::span(words).subspan(shared)));
    }
    return labels;
}

std::vector<std::string> makeConstantLabels(const std::span<const std::string> names) {
    const std::vector<std::string_view> views(names.begin(), names.end());
    return makeConstantLabels(std::span<const std::string_view>(views));
}

std::string makeConstantLabel(std::string_view name, const std::string_view prefix) {
    if (name.starts_with(prefix) && name.size() > prefix.size()) {
        name.remove_prefix(prefix.size());
    }
    if (!isConstant(name)) {
        return std::string(name);
    }
    const std::vector<std::string_view> words = splitWords(name);
    return joinWords(words);
}

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

} // namespace toki::core
