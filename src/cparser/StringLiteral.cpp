#include "StringLiteral.h"

#include "cparser/Types.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace cparser {

namespace {

constexpr usize MaxOctalDigits = 3;
constexpr u32 OctalBase = 8;
constexpr u32 HexBase = 16;
constexpr u32 DecimalDigits = 10;

std::optional<u32> findDigit(const char character, const u32 base) {
    std::optional<u32> digit;
    if (character >= '0' && character <= '9') {
        digit = static_cast<u32>(character - '0');
    } else if (character >= 'a' && character <= 'f') {
        digit = static_cast<u32>(character - 'a') + DecimalDigits;
    } else if (character >= 'A' && character <= 'F') {
        digit = static_cast<u32>(character - 'A') + DecimalDigits;
    }
    return digit && *digit < base ? digit : std::nullopt;
}

char findSimpleEscape(const char escaped) {
    switch (escaped) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'v':
        return '\v';
    case 'e':
        return '\x1b';
    default:
        return escaped;
    }
}

} // namespace

std::string decodeEscapes(const std::string_view body) {
    std::string text;
    for (usize at = 0; at < body.size(); ++at) {
        if (body[at] != '\\' || at + 1 >= body.size()) {
            text += body[at];
            continue;
        }
        ++at;
        u32 code = 0;
        if (findDigit(body[at], OctalBase)) {
            const usize end = std::min(body.size(), at + MaxOctalDigits);
            for (; at < end && findDigit(body[at], OctalBase); ++at) {
                code = (code * OctalBase) + *findDigit(body[at], OctalBase);
            }
            --at;
            text += static_cast<char>(code);
        } else if (body[at] == 'x') {
            for (; at + 1 < body.size() && findDigit(body[at + 1], HexBase); ++at) {
                code = (code * HexBase) + *findDigit(body[at + 1], HexBase);
            }
            text += static_cast<char>(code);
        } else {
            text += findSimpleEscape(body[at]);
        }
    }
    return text;
}

} // namespace cparser
