#pragma once

#include "app/message/Types.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace toki::message {

/// The Shift-JIS bytes of each character, as Python's `shift_jis` codec
/// encodes it, which `tools/msgenc.py` encodes Japanese messages with. Toki
/// ships the table (`assets/shift_jis.txt`), since nothing in its
/// dependencies converts to Shift-JIS on every platform. ASCII is itself.
class ShiftJis {
public:
    /// Reads the table: a line for each character, its code point and then
    /// its bytes, both in hex, and `#` comments.
    [[nodiscard]] static std::expected<ShiftJis, std::string> parse(std::string_view contents);

    [[nodiscard]] static std::expected<ShiftJis, std::string>
    read(const std::filesystem::path& file);

    /// The bytes `codePoint` encodes to, one or two as a number (`0x82A0`).
    [[nodiscard]] std::optional<u16> findCode(u32 codePoint) const;

    /// The character `code` decodes to, the reverse of `findCode`.
    [[nodiscard]] std::optional<u32> findCodePoint(u16 code) const;

private:
    std::unordered_map<u32, u16> codes;
    std::unordered_map<u16, u32> codePoints;
};

} // namespace toki::message
