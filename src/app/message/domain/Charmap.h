#pragma once

#include "app/message/Types.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::message {

/// One sequence of a charmap and the character code the game draws for it.
struct CharmapEntry {
    std::string text;
    u16 code = 0;
};

/// The build's charmap, `assets/text/charmap.nes.txt`: text the encoder turns
/// into one character code, `[A]` into the A button's glyph. `tools/msgenc.py`
/// tries the entries in the order the file lists them and takes the first
/// that matches, so that order is kept.
struct Charmap {
    std::vector<CharmapEntry> entries;

    /// The first entry `text` starts with at `offset`, as the encoder finds it.
    [[nodiscard]] const CharmapEntry* findMatch(std::string_view text, usize offset) const;

    [[nodiscard]] std::optional<u16> findCode(std::string_view text) const;

    /// The entries that stand for a sign rather than a letter: a button,
    /// `[A]`, or a character past the Latin ones, `▼`.
    [[nodiscard]] std::vector<const CharmapEntry*> listSigns() const;
};

/// Reads a charmap file, a Python dictionary of strings to integers with `#`
/// comments. A malformed file is an error naming the line.
[[nodiscard]] std::expected<Charmap, std::string> parseCharmap(std::string_view contents);

} // namespace toki::message
