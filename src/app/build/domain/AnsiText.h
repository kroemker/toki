#pragma once

#include "app/build/Types.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::build {

struct Rgb {
    u8 red = 0;
    u8 green = 0;
    u8 blue = 0;

    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/// What ANSI's Select Graphic Rendition codes have set. No colour means the
/// view's own.
struct AnsiStyle {
    std::optional<Rgb> foreground;
    std::optional<Rgb> background;
    bool bold = false;
    bool italic = false;
    bool underline = false;

    friend bool operator==(const AnsiStyle&, const AnsiStyle&) = default;
};

struct AnsiSpan {
    std::string text;
    AnsiStyle style;
};

/// Splits a line of terminal output into runs of text and the style each is
/// drawn in. Colour codes cover the 16 standard colours, the 256-colour
/// palette and 24-bit colour. Any other escape sequence — cursor movement,
/// clearing — has no meaning in a log and is dropped, as are control
/// characters other than tab. Each line starts unstyled.
[[nodiscard]] std::vector<AnsiSpan> parseAnsiLine(std::string_view line);

} // namespace toki::build
