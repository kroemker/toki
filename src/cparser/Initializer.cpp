#include "Initializer.h"

#include "cparser/Token.h"
#include "cparser/Types.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

namespace {

[[nodiscard]] int getNestingDelta(const Token& token) {
    if (token.kind != TokenKind::Punctuator || token.text.size() != 1) {
        return 0;
    }
    switch (token.text.front()) {
    case '(':
    case '[':
    case '{':
        return 1;
    case ')':
    case ']':
    case '}':
        return -1;
    default:
        return 0;
    }
}

} // namespace

std::vector<std::span<const Token>> splitElements(std::span<const Token> body) {
    std::vector<std::span<const Token>> elements;
    int nesting = 0;
    usize begin = 0;

    for (usize cursor = 0; cursor < body.size(); ++cursor) {
        nesting += getNestingDelta(body[cursor]);
        if (nesting != 0 || !isPunctuator(body[cursor], ",")) {
            continue;
        }
        elements.push_back(body.subspan(begin, cursor - begin));
        begin = cursor + 1;
    }

    if (begin < body.size()) {
        elements.push_back(body.subspan(begin));
    }
    return elements;
}

std::optional<std::span<const Token>> getBraceBody(std::span<const Token> element) {
    if (element.size() < 2 || !isPunctuator(element.front(), "{") ||
        !isPunctuator(element.back(), "}")) {
        return std::nullopt;
    }
    return element.subspan(1, element.size() - 2);
}

usize countElements(std::span<const Token> body) {
    return splitElements(body).size();
}

} // namespace cparser
