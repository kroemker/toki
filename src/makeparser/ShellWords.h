#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace makeparser {

/// Splits a command line the way a POSIX shell would before running it:
/// whitespace separates words, single quotes are literal, and double quotes
/// and backslashes escape. The quoting itself is removed, so
/// `-DNAME='"a b"'` is the one word `-DNAME="a b"`.
[[nodiscard]] std::vector<std::string> splitShellWords(std::string_view text);

} // namespace makeparser
