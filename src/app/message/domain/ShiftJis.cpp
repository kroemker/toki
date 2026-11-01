#include "ShiftJis.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <fstream>
#include <iterator>
#include <system_error>

namespace toki::message {

namespace {

constexpr u32 AsciiEnd = 0x80;

[[nodiscard]] std::optional<u32> parseHex(const std::string_view text) {
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (error != std::errc() || end != text.data() + text.size() || text.empty()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::expected<ShiftJis, std::string> ShiftJis::parse(const std::string_view contents) {
    ShiftJis table;
    usize start = 0;
    usize number = 0;
    while (start < contents.size()) {
        const usize end = std::min(contents.find('\n', start), contents.size());
        std::string_view line = contents.substr(start, end - start);
        start = end + 1;
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const usize space = line.find(' ');
        const std::optional<u32> codePoint =
            space == std::string_view::npos ? std::nullopt : parseHex(line.substr(0, space));
        const std::optional<u32> code =
            space == std::string_view::npos ? std::nullopt : parseHex(line.substr(space + 1));
        if (!codePoint || !code || *code > 0xFFFF) {
            return std::unexpected(
                std::format("line {} is not a code point and its bytes: `{}`", number, line));
        }
        table.codes.emplace(*codePoint, static_cast<u16>(*code));
        table.codePoints.emplace(static_cast<u16>(*code), *codePoint);
    }
    return table;
}

std::expected<ShiftJis, std::string> ShiftJis::read(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return std::unexpected(std::format("{} could not be opened", file.string()));
    }
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    std::expected<ShiftJis, std::string> table = parse(contents);
    if (!table) {
        return std::unexpected(std::format("{}: {}", file.string(), table.error()));
    }
    return table;
}

std::optional<u16> ShiftJis::findCode(const u32 codePoint) const {
    if (codePoint < AsciiEnd) {
        return static_cast<u16>(codePoint);
    }
    const auto found = codes.find(codePoint);
    return found != codes.end() ? std::optional(found->second) : std::nullopt;
}

std::optional<u32> ShiftJis::findCodePoint(const u16 code) const {
    if (code < AsciiEnd) {
        return code;
    }
    const auto found = codePoints.find(code);
    return found != codePoints.end() ? std::optional(found->second) : std::nullopt;
}

} // namespace toki::message
