#pragma once

#include <span>
#include <string_view>

namespace toki::core {

/// Whether a setting described by `texts` (its label, help, category) is one a
/// search for `query` should show: every word of the query appears in one of
/// the texts, ignoring case, in any order. An empty query matches everything.
[[nodiscard]] bool matchesSearch(std::span<const std::string_view> texts, std::string_view query);

} // namespace toki::core
