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

    /// Replacements made before expansion gives up with an error, so a
    /// macro set that never terminates cannot hang a read.
    usize maxExpansions = 1 << 16;
};

/// Expands macro uses in a token range, leaving everything else untouched.
/// A replacement is rescanned together with the tokens that follow it, so
/// `#define F G` then `F(1)` reaches `G(1)`, and every token carries the set
/// of macros it came out of, so no macro expands inside its own replacement.
/// Arguments are expanded before they are substituted, and `...` collects
/// the trailing ones into `__VA_ARGS__`.
/// Body tokens come from wherever their macro was defined, so the result
/// borrows from the input's files and from the files behind `macros`.
[[nodiscard]] std::vector<Token> expand(std::span<const Token> tokens, const ExpandOptions& options,
                                        std::vector<Diagnostic>& diagnostics);

} // namespace z64
