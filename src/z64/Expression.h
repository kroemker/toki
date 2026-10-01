#pragma once

#include "z64/Diagnostic.h"
#include "z64/Token.h"
#include "z64/Types.h"
#include "z64/Value.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace z64 {

class MacroTable;

struct EvalContext {
    const MacroTable* macros = nullptr;

    /// Names never expanded before evaluation. `ARRAY_COUNT` and `TEX_LEN` are
    /// always added to this: their real definitions are written with `sizeof`,
    /// which no evaluator here can answer, so they are handled as builtins.
    std::function<bool(std::string_view)> keep;

    /// Identifiers that are not macros at all: the enum constants generated
    /// from the ordinal tables, and the gbi constants compiled into the binary.
    std::function<std::optional<Value>(std::string_view)> resolve;

    /// Declared element counts, for `ARRAY_COUNT`.
    std::function<std::optional<usize>(std::string_view)> arrayLength;

    /// C says an identifier left over in an `#if` is zero. Data expressions
    /// want the opposite, so the caller chooses. Either way it is reported.
    bool unknownIsZero = true;
};

/// Evaluates one constant expression. A malformed expression is the error; an
/// identifier that nothing resolves is a diagnostic and a zero, so one unknown
/// name in a scene does not take the whole scene down with it.
[[nodiscard]] std::expected<Value, Diagnostic> evaluate(std::span<const Token> tokens,
                                                        const EvalContext& context,
                                                        std::vector<Diagnostic>& diagnostics);

} // namespace z64
