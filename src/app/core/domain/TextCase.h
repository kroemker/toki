#pragma once

#include <string>
#include <string_view>

namespace toki::core {

/// `text` with its ASCII letters in upper case, as an enum name is made of a
/// file's: `ocean_test` is `OCEAN_TEST`.
[[nodiscard]] std::string makeUpperCase(std::string_view text);

/// `text` with its ASCII letters in lower case.
[[nodiscard]] std::string makeLowerCase(std::string_view text);

/// Whether `name` may name something in C: letters, digits and `_`, not
/// starting with a digit.
[[nodiscard]] bool isCIdentifier(std::string_view name);

} // namespace toki::core
