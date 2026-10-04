#pragma once

#include "base/Diagnostic.h"
#include "cparser/Token.h"
#include "cparser/Types.h"
#include "cparser/Value.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

class MacroTable;
struct Dialect;

/// Where a symbol lives, for expressions that take an address: display lists
/// point at vertices, textures and other display lists.
struct SymbolAddress {
    s64 address = 0;

    /// The name a value derived from the address carries in `Value::symbol`,
    /// in storage that outlives the evaluation. The spelling that named the
    /// symbol when empty.
    std::string_view name;

    /// The size of one element, so `&vertices[3]` and `vertices + 3` both move
    /// by three of them.
    usize elementSize = 1;
};

struct EvalContext {
    const MacroTable* macros = nullptr;

    /// The type names casts may use and the builtins to answer. Plain C when
    /// nothing is given.
    const Dialect* dialect = nullptr;

    /// Names never expanded before evaluation. Every builtin of the dialect is
    /// kept as well, since the evaluator answers it instead.
    std::function<bool(std::string_view)> keep;

    /// Identifiers that are not macros at all: the enum constants generated
    /// from the ordinal tables, and the gbi constants compiled into the binary.
    std::function<std::optional<Value>(std::string_view)> resolve;

    /// Symbols an expression may take the address of. A symbol names its
    /// first element, as an array does in C.
    std::function<std::optional<SymbolAddress>(std::string_view)> findSymbol;

    /// The sizes of types beyond the dialect's, such as the structs a header
    /// defines, for `sizeof` and pointer casts.
    std::function<std::optional<usize>(std::string_view)> findTypeSize;

    /// Declared element counts, for builtins that count an array.
    std::function<std::optional<usize>(std::string_view)> findArrayLength;

    /// C says an identifier left over in an `#if` is zero. Data expressions
    /// want the opposite, so the caller chooses. Either way it is reported.
    bool unknownIsZero = true;
};

/// Evaluates one constant expression. A malformed expression is the error; an
/// identifier that nothing resolves is a diagnostic and a zero, so one unknown
/// name in a scene does not take the whole scene down with it.
[[nodiscard]] std::expected<Value, base::Diagnostic>
evaluate(std::span<const Token> tokens, const EvalContext& context,
         std::vector<base::Diagnostic>& diagnostics);

} // namespace cparser
