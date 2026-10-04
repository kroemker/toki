#pragma once

#include "base/Diagnostic.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

class MacroTable;
class Spellings;

struct ExpandOptions {
    const MacroTable* macros = nullptr;

    /// Names left alone however they are defined. The `gs*` macros go here:
    /// they are dispatched by name to the gbi emitter, and expanding them would
    /// throw away the one thing that identifies the command.
    std::function<bool(std::string_view)> keep;

    /// Where `#` and `##` keep the text of the tokens they make. Without it
    /// both are reported and skipped, since their tokens would have nothing
    /// to view.
    Spellings* spellings = nullptr;

    /// Replacements made before expansion gives up with an error, so a
    /// macro set that never terminates cannot hang a read.
    usize maxExpansions = 1 << 16;
};

/// Expands macro uses in a token range, leaving everything else untouched.
/// A replacement is rescanned together with the tokens that follow it, so
/// `#define F G` then `F(1)` reaches `G(1)`, and every token carries the set
/// of macros it came out of, so no macro expands inside its own replacement.
/// Arguments are expanded before they are substituted, and `...` collects
/// the trailing ones into `__VA_ARGS__`. `#` stringifies an argument and
/// `##` pastes two tokens into one, both on the argument as written, and GNU's
/// `, ## __VA_ARGS__` drops the comma when there are no trailing arguments.
/// Body tokens come from wherever their macro was defined, so the result
/// borrows from the input's files and from the files behind `macros`.
[[nodiscard]] std::vector<Token> expand(std::span<const Token> tokens, const ExpandOptions& options,
                                        std::vector<base::Diagnostic>& diagnostics);

} // namespace cparser
