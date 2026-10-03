#pragma once

#include "cparser/Token.h"
#include "cparser/Types.h"

#include <optional>
#include <span>
#include <vector>

namespace cparser {

/// Splits a brace body into its top-level, comma-separated elements. A trailing
/// comma produces no empty element, because decomp C writes one on almost every
/// list.
[[nodiscard]] std::vector<std::span<const Token>> splitElements(std::span<const Token> body);

/// The interior of an element that is itself a brace list, as `{ 0, 0 }` is
/// inside an `ActorEntry` list. Nothing for an element that is an expression.
[[nodiscard]] std::optional<std::span<const Token>> getBraceBody(std::span<const Token> element);

[[nodiscard]] usize countElements(std::span<const Token> body);

} // namespace cparser
