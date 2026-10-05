#pragma once

#include "base/Diagnostic.h"
#include "cparser/Expression.h"
#include "cparser/Token.h"
#include "cparser/Types.h"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace cparser {

struct TranslationUnit;

/// One file-scope object in decomp C. The initializer is kept as the tokens it
/// was written as rather than a value tree: a room's vertex arrays are read
/// once by the memory image and a scene's `SceneCmd[]` is read by something
/// else entirely, and neither wants the other's interpretation.
struct Declaration {
    std::string_view name;
    std::string_view type;
    usize pointerDepth = 0;
    bool isArray = false;
    bool isExtern = false;
    bool hasInitializer = false;

    /// The declared element count, or the counted one when the source wrote
    /// `[]`. Nothing when neither was available, which includes `[]` whose
    /// element is an embedded file: its count is the file's, not its tokens'.
    std::optional<usize> arrayLength;

    /// Whether `arrayLength` is the one the source wrote, rather than a count
    /// of the initializer's elements, which brace elision can make too high.
    bool hasDeclaredLength = false;

    /// The length of every dimension after the first, outermost first, as
    /// `Gfx sSetupDL[SETUPDL_MAX][6]` has one of 6.
    std::vector<usize> innerLengths;

    /// The interior of the outermost braces, or the whole expression when the
    /// initializer is not a brace list.
    std::span<const Token> initializer;

    /// Whether the source wrote the braces, rather than a macro that may
    /// expand to them, as `Mtx m = gdSPDefMtx(...)` does.
    bool isBraced = false;

    base::SourceLocation location;
};

struct ParseResult {
    std::vector<Declaration> declarations;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads every file-scope object out of a preprocessed unit. Typedefs, struct
/// definitions and functions are stepped over: asset C has none, and the
/// headers that get spliced in are full of them.
///
/// Array bounds are evaluated against the unit's own macros, whatever
/// `context.macros` says: a bound can only mean what the unit defined.
[[nodiscard]] ParseResult parseDeclarations(const TranslationUnit& unit,
                                            const EvalContext& context);

} // namespace cparser
