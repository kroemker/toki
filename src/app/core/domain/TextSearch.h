#pragma once

#include <string_view>

namespace toki::core {

/// Whether `wanted` appears in `text`, ignoring ASCII case. Empty `wanted` is
/// found in every text.
[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view wanted);

} // namespace toki::core
