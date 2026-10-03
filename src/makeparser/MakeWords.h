#pragma once

#include "makeparser/Types.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace makeparser {

[[nodiscard]] std::string_view trimLeft(std::string_view text);

[[nodiscard]] std::string_view trim(std::string_view text);

/// The index of the bracket closing the `(` or `{` at `open`, counting only
/// brackets of the same kind as make does, or `npos`.
[[nodiscard]] usize findClose(std::string_view text, usize open);

/// The first `wanted` outside any bracket, or `npos`.
[[nodiscard]] usize findTopLevel(std::string_view text, char wanted);

/// Splits on make's whitespace: spaces, tabs and newlines.
[[nodiscard]] std::vector<std::string_view> splitWords(std::string_view text);

[[nodiscard]] std::string joinWords(const std::vector<std::string>& words);

/// `$(strip)`: leading and trailing whitespace gone, inner runs collapsed.
[[nodiscard]] std::string stripWords(std::string_view text);

/// Matches a word against a pattern whose first `%` stands for any run of
/// characters, returning what the `%` matched. A pattern without `%` matches
/// only itself, and then the stem is empty.
[[nodiscard]] std::optional<std::string_view> matchPattern(std::string_view pattern,
                                                           std::string_view word);

/// `$(patsubst)` for one word: the replacement with its `%` swapped for the
/// stem, or the word unchanged when the pattern does not match.
[[nodiscard]] std::string substitutePattern(std::string_view pattern, std::string_view replacement,
                                            std::string_view word);

/// Applies one of make's pure text functions, `subst` through `addsuffix`, to
/// arguments that are already expanded. Nothing when `name` is not one.
[[nodiscard]] std::optional<std::string> applyTextFunction(std::string_view name,
                                                           std::span<const std::string> arguments);

} // namespace makeparser
