#pragma once

#include "cparser/ScalarType.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace cparser {

struct Dialect;

/// A type name as a cast or `sizeof` spells it between parentheses.
struct TypeName {
    ScalarType type;
    usize pointerDepth = 0;

    /// One of the dialect's aggregates, or a type the caller sizes, which
    /// only `sizeof` and a pointer cast may name.
    bool isAggregate = false;

    /// What `sizeof` answers for it, pointers aside.
    usize size = 0;

    /// The index just past the closing parenthesis.
    usize end = 0;
};

/// The scalar type a run of type words names: `unsigned int`, `long long`,
/// `signed char`, or one of the dialect's own names such as `u32`. Nothing for
/// words that do not combine into one.
[[nodiscard]] std::optional<ScalarType> combineScalarWords(const Dialect& dialect,
                                                           std::span<const std::string_view> words);

/// Reads the type name in the parentheses opening at `open`: type words such
/// as `unsigned int`, `long long`, `u32` or `const Vtx`, then any `*`s.
/// Nothing when the parentheses hold anything else, which is most likely an
/// expression. A word the dialect does not know is a type when
/// `findTypeSize` sizes it, as a struct read from a header is.
[[nodiscard]] std::optional<TypeName>
findTypeName(std::span<const Token> tokens, usize open, const Dialect& dialect,
             const std::function<std::optional<usize>(std::string_view)>& findTypeSize = {});

} // namespace cparser
