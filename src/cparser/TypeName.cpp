#include "TypeName.h"

#include "cparser/Dialect.h"
#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

namespace {

constexpr std::array<std::string_view, 6> CombinedWords{"signed", "unsigned", "char",
                                                        "short",  "int",      "long"};

bool isQualifier(const std::string_view word) {
    return word == "const" || word == "volatile";
}

bool contains(const std::vector<std::string_view>& words, const std::string_view word) {
    return std::ranges::find(words, word) != words.end();
}

std::optional<ScalarType> findWord(const Dialect& dialect, const std::string_view word) {
    const ScalarType* type = findType(dialect, word);
    return type == nullptr ? std::nullopt : std::optional<ScalarType>(*type);
}

std::optional<ScalarType> combineWords(const Dialect& dialect,
                                       const std::vector<std::string_view>& words) {
    for (const std::string_view word : words) {
        if (std::ranges::find(CombinedWords, word) == CombinedWords.end()) {
            return std::nullopt;
        }
    }
    const auto longs = std::ranges::count(words, std::string_view("long"));
    std::optional<ScalarType> type;
    if (contains(words, "char")) {
        type = findWord(dialect, "char");
    } else if (contains(words, "short")) {
        type = findWord(dialect, "short");
    } else if (longs == 1) {
        type = findWord(dialect, "long");
    } else {
        type = findWord(dialect, "int");
    }
    if (!type) {
        return std::nullopt;
    }
    if (longs >= 2) {
        type->size = 8;
    }
    if (contains(words, "unsigned")) {
        type->isSigned = false;
    } else if (contains(words, "signed")) {
        type->isSigned = true;
    }
    return type;
}

} // namespace

std::optional<TypeName> findTypeName(const std::span<const Token> tokens, const usize open,
                                     const Dialect& dialect) {
    if (open >= tokens.size() || !isPunctuator(tokens[open], "(")) {
        return std::nullopt;
    }
    usize at = open + 1;
    std::vector<std::string_view> words;
    bool isAggregate = false;
    for (; at < tokens.size() && tokens[at].kind == TokenKind::Identifier; ++at) {
        const std::string_view word = tokens[at].text;
        if (isQualifier(word)) {
            continue;
        }
        if (findAggregate(dialect, word) != nullptr) {
            isAggregate = true;
        } else if (findType(dialect, word) == nullptr) {
            return std::nullopt;
        }
        words.push_back(word);
    }
    if (words.empty()) {
        return std::nullopt;
    }
    TypeName name;
    for (; at < tokens.size() && isPunctuator(tokens[at], "*"); ++at) {
        ++name.pointerDepth;
    }
    if (at >= tokens.size() || !isPunctuator(tokens[at], ")")) {
        return std::nullopt;
    }
    name.end = at + 1;

    if (words.size() == 1) {
        const ScalarType* type =
            isAggregate ? findAggregate(dialect, words.front()) : findType(dialect, words.front());
        name.type = *type;
        name.isAggregate = isAggregate;
        return name;
    }
    if (isAggregate) {
        return std::nullopt;
    }
    const std::optional<ScalarType> combined = combineWords(dialect, words);
    if (!combined) {
        return std::nullopt;
    }
    name.type = *combined;
    return name;
}

} // namespace cparser
