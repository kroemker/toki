#pragma once

#include "z64/Diagnostic.h"
#include "z64/Token.h"
#include "z64/Types.h"

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace z64 {

class MacroTable;

struct ExpandOptions {
    const MacroTable* macros = nullptr;

    /// Names left alone however they are defined. The `gs*` macros go here:
    /// they are dispatched by name to the gbi emitter, and expanding them would
    /// throw away the one thing that identifies the command.
    std::function<bool(std::string_view)> keep;

    usize maxDepth = 64;
};

/// Expands macro uses in a token range, leaving everything else untouched.
/// Body tokens come from wherever their macro was defined, so the result
/// borrows from the input's files and from the files behind `macros`.
[[nodiscard]] std::vector<Token> expand(std::span<const Token> tokens, const ExpandOptions& options,
                                        std::vector<Diagnostic>& diagnostics);

} // namespace z64
