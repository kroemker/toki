#include "TypeName.h"

#include "cparser/Dialect.h"
#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <algorithm>
#include <array>
#include <functional>
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

bool contains(const std::span<const std::string_view> words, const std::string_view word) {
    return std::ranges::find(words, word) != words.end();
}

std::optional<ScalarType> findWord(const Dialect& dialect, const std::string_view word) {
    const ScalarType* type = findType(dialect, word);
    return type == nullptr ? std::nullopt : std::optional<ScalarType>(*type);
}

} // namespace

std::optional<ScalarType> combineScalarWords(const Dialect& dialect,
                                             const std::span<const std::string_view> words) {
    for (const std::string_view word : words) {
        if (std::ranges::find(CombinedWords, word) == CombinedWords.end() && word != "double" &&
            (words.size() > 1 || findType(dialect, word) == nullptr)) {
            return std::nullopt;
        }
    }
    if (words.size() == 1 &&
        std::ranges::find(CombinedWords, words.front()) == CombinedWords.end()) {
        return findWord(dialect, words.front());
    }
    if (contains(words, "double")) {
        return findWord(dialect, "double");
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

namespace {

struct TypeWords {
    std::vector<std::string_view> words;
    bool isAggregate = false;
    std::optional<usize> sized;
    usize end = 0;
};

std::optional<TypeWords>
readTypeWords(const std::span<const Token> tokens, usize at, const Dialect& dialect,
              const std::function<std::optional<usize>(std::string_view)>& findTypeSize) {
    TypeWords read;
    for (; at < tokens.size() && tokens[at].kind == TokenKind::Identifier; ++at) {
        const std::string_view word = tokens[at].text;
        if (isQualifier(word)) {
            continue;
        }
        if (findAggregate(dialect, word) != nullptr) {
            read.isAggregate = true;
        } else if (findType(dialect, word) == nullptr) {
            read.sized = findTypeSize ? findTypeSize(word) : std::nullopt;
            if (!read.sized) {
                return std::nullopt;
            }
            read.isAggregate = true;
        }
        read.words.push_back(word);
    }
    if (read.words.empty()) {
        return std::nullopt;
    }
    read.end = at;
    return read;
}

std::optional<TypeName> makeTypeName(const TypeWords& read, const Dialect& dialect, TypeName name) {
    if (read.words.size() == 1) {
        if (read.sized) {
            name.type = ScalarType{.name = read.words.front(), .size = 0};
            name.size = *read.sized;
        } else {
            const ScalarType* type = read.isAggregate ? findAggregate(dialect, read.words.front())
                                                      : findType(dialect, read.words.front());
            name.type = *type;
            name.size = type->size;
        }
        name.isAggregate = read.isAggregate;
        return name;
    }
    if (read.isAggregate) {
        return std::nullopt;
    }
    const std::optional<ScalarType> combined = combineScalarWords(dialect, read.words);
    if (!combined) {
        return std::nullopt;
    }
    name.type = *combined;
    name.size = combined->size;
    return name;
}

} // namespace

std::optional<TypeName>
findTypeName(const std::span<const Token> tokens, const usize open, const Dialect& dialect,
             const std::function<std::optional<usize>(std::string_view)>& findTypeSize) {
    if (open >= tokens.size() || !isPunctuator(tokens[open], "(")) {
        return std::nullopt;
    }
    const std::optional<TypeWords> read = readTypeWords(tokens, open + 1, dialect, findTypeSize);
    if (!read) {
        return std::nullopt;
    }
    TypeName name;
    usize at = read->end;
    for (; at < tokens.size() && isPunctuator(tokens[at], "*"); ++at) {
        ++name.pointerDepth;
    }
    if (at >= tokens.size() || !isPunctuator(tokens[at], ")")) {
        return std::nullopt;
    }
    name.end = at + 1;
    return makeTypeName(*read, dialect, name);
}

} // namespace cparser
