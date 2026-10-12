#pragma once

#include <string_view>
#include <vector>

namespace hackeroot {

/// `text` without the spaces, tabs and carriage returns around it.
[[nodiscard]] std::string_view trimSpaces(std::string_view text);

/// The lines of `text`, without their `\n`; a text ending in one ends with an
/// empty line.
[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text);

} // namespace hackeroot
