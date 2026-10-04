#pragma once

#include <string>
#include <string_view>

namespace cparser {

/// The bytes the text between a literal's quotes stands for: simple escapes
/// such as `\n` and `\"`, up to three octal digits, and `\x` with every hex
/// digit after it, as C reads them. An unknown escape stands for its
/// character, as GCC reads it.
[[nodiscard]] std::string decodeEscapes(std::string_view body);

} // namespace cparser
