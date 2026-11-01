#include "Charmap.h"

#include <charconv>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace toki::message {

namespace {

void appendUtf8(std::string& out, const u32 codePoint) {
    if (codePoint < 0x80) {
        out += static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

class CharmapParser {
public:
    explicit CharmapParser(const std::string_view contents) : contents(contents) {}

    [[nodiscard]] std::expected<Charmap, std::string> parse();

private:
    void skipSpace();
    [[nodiscard]] bool consume(char expected);
    [[nodiscard]] std::expected<std::string, std::string> readString();
    [[nodiscard]] std::expected<u16, std::string> readInteger();
    [[nodiscard]] std::string makeError(std::string_view what) const;

    [[nodiscard]] bool isAtEnd() const {
        return cursor >= contents.size();
    }

    std::string_view contents;
    usize cursor = 0;
};

void CharmapParser::skipSpace() {
    while (!isAtEnd()) {
        const char character = contents[cursor];
        if (character == '#') {
            while (!isAtEnd() && contents[cursor] != '\n') {
                ++cursor;
            }
        } else if (character == ' ' || character == '\t' || character == '\r' ||
                   character == '\n') {
            ++cursor;
        } else {
            return;
        }
    }
}

bool CharmapParser::consume(const char expected) {
    skipSpace();
    if (isAtEnd() || contents[cursor] != expected) {
        return false;
    }
    ++cursor;
    return true;
}

std::string CharmapParser::makeError(const std::string_view what) const {
    usize line = 1;
    for (usize index = 0; index < cursor && index < contents.size(); ++index) {
        if (contents[index] == '\n') {
            ++line;
        }
    }
    return std::format("line {}: {}", line, what);
}

std::expected<std::string, std::string> CharmapParser::readString() {
    skipSpace();
    if (isAtEnd() || (contents[cursor] != '\'' && contents[cursor] != '"')) {
        return std::unexpected(makeError("a quoted string was expected"));
    }
    const char quote = contents[cursor++];
    std::string text;
    while (!isAtEnd() && contents[cursor] != quote) {
        const char character = contents[cursor++];
        if (character != '\\') {
            text += character;
            continue;
        }
        if (isAtEnd()) {
            break;
        }
        const char escape = contents[cursor++];
        switch (escape) {
        case 'n':
            text += '\n';
            break;
        case 't':
            text += '\t';
            break;
        case 'r':
            text += '\r';
            break;
        case 'x':
        case 'u':
        case 'U': {
            const usize digits = escape == 'x' ? 2 : escape == 'u' ? 4 : 8;
            u32 codePoint = 0;
            const std::string_view hex = contents.substr(cursor, digits);
            const auto [end, error] =
                std::from_chars(hex.data(), hex.data() + hex.size(), codePoint, 16);
            if (error != std::errc() || end != hex.data() + hex.size() || hex.size() != digits) {
                return std::unexpected(
                    makeError(std::format("`\\{}` needs {} hex digits", escape, digits)));
            }
            cursor += digits;
            appendUtf8(text, codePoint);
            break;
        }
        default:
            text += escape;
            break;
        }
    }
    if (!consume(quote)) {
        return std::unexpected(makeError("a string is never closed"));
    }
    return text;
}

std::expected<u16, std::string> CharmapParser::readInteger() {
    skipSpace();
    std::string_view text = contents.substr(cursor);
    int base = 10;
    usize prefix = 0;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        base = 16;
        prefix = 2;
    }
    u32 value = 0;
    const char* const begin = text.data() + prefix;
    const auto [end, error] = std::from_chars(begin, text.data() + text.size(), value, base);
    if (error != std::errc() || end == begin || value > 0xFFFF) {
        return std::unexpected(makeError("a character code from 0 to 0xFFFF was expected"));
    }
    cursor += static_cast<usize>(end - text.data());
    return static_cast<u16>(value);
}

std::expected<Charmap, std::string> CharmapParser::parse() {
    if (!consume('{')) {
        return std::unexpected(makeError("the charmap does not start with `{`"));
    }
    Charmap charmap;
    while (!consume('}')) {
        std::expected<std::string, std::string> text = readString();
        if (!text) {
            return std::unexpected(text.error());
        }
        if (!consume(':')) {
            return std::unexpected(makeError("`:` was expected after a sequence"));
        }
        const std::expected<u16, std::string> code = readInteger();
        if (!code) {
            return std::unexpected(code.error());
        }
        charmap.entries.push_back(CharmapEntry{.text = std::move(*text), .code = *code});
        if (!consume(',')) {
            if (!consume('}')) {
                return std::unexpected(makeError("`,` or `}` was expected"));
            }
            break;
        }
    }
    return charmap;
}

} // namespace

const CharmapEntry* Charmap::findMatch(const std::string_view text, const usize offset) const {
    for (const CharmapEntry& entry : entries) {
        if (!entry.text.empty() && text.substr(offset).starts_with(entry.text)) {
            return &entry;
        }
    }
    return nullptr;
}

std::optional<u16> Charmap::findCode(const std::string_view text) const {
    for (const CharmapEntry& entry : entries) {
        if (entry.text == text) {
            return entry.code;
        }
    }
    return std::nullopt;
}

std::expected<Charmap, std::string> parseCharmap(const std::string_view contents) {
    return CharmapParser(contents).parse();
}

} // namespace toki::message
