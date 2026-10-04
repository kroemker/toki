#pragma once

#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <optional>
#include <span>

namespace cparser {

struct Dialect;

/// A type name as a cast or `sizeof` spells it between parentheses.
struct TypeName {
    ScalarType type;
    usize pointerDepth = 0;

    /// One of the dialect's aggregates, which only `sizeof` and a pointer cast
    /// may name.
    bool isAggregate = false;

    /// The index just past the closing parenthesis.
    usize end = 0;
};

/// Reads the type name in the parentheses opening at `open`: type words such
/// as `unsigned int`, `long long`, `u32` or `const Vtx`, then any `*`s.
/// Nothing when the parentheses hold anything else, which is most likely an
/// expression.
[[nodiscard]] std::optional<TypeName> findTypeName(std::span<const Token> tokens, usize open,
                                                   const Dialect& dialect);

} // namespace cparser
