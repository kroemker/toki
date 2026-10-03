#include "MakeWords.h"

#include "makeparser/Types.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace makeparser {

namespace {

[[nodiscard]] bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

using Arguments = std::span<const std::string>;

[[nodiscard]] std::string_view argumentAt(Arguments arguments, usize index) {
    return index < arguments.size() ? std::string_view(arguments[index]) : std::string_view();
}

[[nodiscard]] std::string eachWord(std::string_view list, auto transform) {
    std::vector<std::string> words;
    for (const std::string_view word : splitWords(list)) {
        words.push_back(transform(word));
    }
    return joinWords(words);
}

[[nodiscard]] std::string substitute(Arguments arguments) {
    const std::string_view from = argumentAt(arguments, 0);
    const std::string_view to = argumentAt(arguments, 1);
    std::string text(argumentAt(arguments, 2));
    if (from.empty()) {
        return text;
    }
    for (usize found = text.find(from); found != std::string::npos;
         found = text.find(from, found + to.size())) {
        text.replace(found, from.size(), to);
    }
    return text;
}

[[nodiscard]] std::string substitutePatterns(Arguments arguments) {
    return eachWord(argumentAt(arguments, 2), [&](std::string_view word) {
        return substitutePattern(argumentAt(arguments, 0), argumentAt(arguments, 1), word);
    });
}

[[nodiscard]] std::string findString(Arguments arguments) {
    const std::string_view wanted = argumentAt(arguments, 0);
    return argumentAt(arguments, 1).contains(wanted) ? std::string(wanted) : std::string();
}

[[nodiscard]] std::string filterWords(Arguments arguments, bool keepMatches) {
    const std::vector<std::string_view> patterns = splitWords(argumentAt(arguments, 0));
    std::vector<std::string> kept;
    for (const std::string_view word : splitWords(argumentAt(arguments, 1))) {
        const bool matches = std::ranges::any_of(patterns, [word](std::string_view pattern) {
            return matchPattern(pattern, word).has_value();
        });
        if (matches == keepMatches) {
            kept.emplace_back(word);
        }
    }
    return joinWords(kept);
}

[[nodiscard]] std::string filterIn(Arguments arguments) {
    return filterWords(arguments, true);
}

[[nodiscard]] std::string filterOut(Arguments arguments) {
    return filterWords(arguments, false);
}

[[nodiscard]] std::string sortWords(Arguments arguments) {
    std::vector<std::string> words;
    for (const std::string_view word : splitWords(argumentAt(arguments, 0))) {
        words.emplace_back(word);
    }
    std::ranges::sort(words);
    words.erase(std::ranges::unique(words).begin(), words.end());
    return joinWords(words);
}

[[nodiscard]] std::string nthWord(Arguments arguments) {
    const std::string number = stripWords(argumentAt(arguments, 0));
    usize index = 0;
    std::from_chars(number.data(), number.data() + number.size(), index);
    const std::vector<std::string_view> list = splitWords(argumentAt(arguments, 1));
    return index > 0 && index <= list.size() ? std::string(list[index - 1]) : std::string();
}

[[nodiscard]] std::string countWords(Arguments arguments) {
    return std::to_string(splitWords(argumentAt(arguments, 0)).size());
}

[[nodiscard]] std::string firstOfWords(Arguments arguments) {
    const std::vector<std::string_view> list = splitWords(argumentAt(arguments, 0));
    return list.empty() ? std::string() : std::string(list.front());
}

[[nodiscard]] std::string lastOfWords(Arguments arguments) {
    const std::vector<std::string_view> list = splitWords(argumentAt(arguments, 0));
    return list.empty() ? std::string() : std::string(list.back());
}

[[nodiscard]] std::string directoryParts(Arguments arguments) {
    return eachWord(argumentAt(arguments, 0), [](std::string_view word) {
        const usize slash = word.rfind('/');
        return slash == std::string_view::npos ? std::string("./")
                                               : std::string(word.substr(0, slash + 1));
    });
}

[[nodiscard]] std::string fileParts(Arguments arguments) {
    return eachWord(argumentAt(arguments, 0), [](std::string_view word) {
        const usize slash = word.rfind('/');
        return std::string(slash == std::string_view::npos ? word : word.substr(slash + 1));
    });
}

[[nodiscard]] std::string addPrefix(Arguments arguments) {
    return eachWord(argumentAt(arguments, 1), [&](std::string_view word) {
        return std::string(argumentAt(arguments, 0)) + std::string(word);
    });
}

[[nodiscard]] std::string addSuffix(Arguments arguments) {
    return eachWord(argumentAt(arguments, 1), [&](std::string_view word) {
        return std::string(word) + std::string(argumentAt(arguments, 0));
    });
}

[[nodiscard]] std::string stripArgument(Arguments arguments) {
    return stripWords(argumentAt(arguments, 0));
}

struct TextFunction {
    std::string_view name;
    std::string (*apply)(Arguments);
};

constexpr std::array TextFunctions = {
    TextFunction{"subst", &substitute},       TextFunction{"patsubst", &substitutePatterns},
    TextFunction{"findstring", &findString},  TextFunction{"filter", &filterIn},
    TextFunction{"filter-out", &filterOut},   TextFunction{"sort", &sortWords},
    TextFunction{"word", &nthWord},           TextFunction{"words", &countWords},
    TextFunction{"firstword", &firstOfWords}, TextFunction{"lastword", &lastOfWords},
    TextFunction{"dir", &directoryParts},     TextFunction{"notdir", &fileParts},
    TextFunction{"addprefix", &addPrefix},    TextFunction{"addsuffix", &addSuffix},
    TextFunction{"strip", &stripArgument},
};

} // namespace

std::optional<std::string> applyTextFunction(std::string_view name,
                                             std::span<const std::string> arguments) {
    const auto found = std::ranges::find(TextFunctions, name, &TextFunction::name);
    if (found == TextFunctions.end()) {
        return std::nullopt;
    }
    return found->apply(arguments);
}

std::string_view trimLeft(std::string_view text) {
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    return text;
}

std::string_view trim(std::string_view text) {
    text = trimLeft(text);
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

usize findClose(std::string_view text, usize open) {
    const char opening = text[open];
    const char closing = opening == '(' ? ')' : '}';
    usize depth = 0;
    for (usize at = open; at < text.size(); ++at) {
        if (text[at] == opening) {
            ++depth;
        } else if (text[at] == closing && --depth == 0) {
            return at;
        }
    }
    return std::string_view::npos;
}

usize findTopLevel(std::string_view text, char wanted) {
    usize depth = 0;
    for (usize at = 0; at < text.size(); ++at) {
        const char c = text[at];
        if (c == '(' || c == '{') {
            ++depth;
        } else if ((c == ')' || c == '}') && depth > 0) {
            --depth;
        } else if (c == wanted && depth == 0) {
            return at;
        }
    }
    return std::string_view::npos;
}

std::vector<std::string_view> splitWords(std::string_view text) {
    std::vector<std::string_view> words;
    usize cursor = 0;
    while (cursor < text.size()) {
        while (cursor < text.size() && isSpace(text[cursor])) {
            ++cursor;
        }
        const usize begin = cursor;
        while (cursor < text.size() && !isSpace(text[cursor])) {
            ++cursor;
        }
        if (cursor > begin) {
            words.push_back(text.substr(begin, cursor - begin));
        }
    }
    return words;
}

std::string joinWords(const std::vector<std::string>& words) {
    std::string joined;
    for (const std::string& word : words) {
        if (!joined.empty()) {
            joined += ' ';
        }
        joined += word;
    }
    return joined;
}

std::string stripWords(std::string_view text) {
    std::vector<std::string> words;
    for (const std::string_view word : splitWords(text)) {
        words.emplace_back(word);
    }
    return joinWords(words);
}

std::optional<std::string_view> matchPattern(std::string_view pattern, std::string_view word) {
    const usize percent = pattern.find('%');
    if (percent == std::string_view::npos) {
        return pattern == word ? std::optional<std::string_view>(std::string_view()) : std::nullopt;
    }
    const std::string_view prefix = pattern.substr(0, percent);
    const std::string_view suffix = pattern.substr(percent + 1);
    if (word.size() < prefix.size() + suffix.size() || !word.starts_with(prefix) ||
        !word.ends_with(suffix)) {
        return std::nullopt;
    }
    return word.substr(prefix.size(), word.size() - prefix.size() - suffix.size());
}

std::string substitutePattern(std::string_view pattern, std::string_view replacement,
                              std::string_view word) {
    const std::optional<std::string_view> stem = matchPattern(pattern, word);
    if (!stem) {
        return std::string(word);
    }
    const usize percent = replacement.find('%');
    if (percent == std::string_view::npos || !pattern.contains('%')) {
        return std::string(replacement);
    }
    std::string result(replacement.substr(0, percent));
    result += *stem;
    result += replacement.substr(percent + 1);
    return result;
}

} // namespace makeparser
