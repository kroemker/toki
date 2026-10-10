#pragma once

#include "base/Diagnostic.h"
#include "cparser/TypeTable.h"

#include <vector>

namespace cparser {

struct EvalContext;
struct TranslationUnit;

struct TypeParseResult {
    TypeTable types;
    std::vector<base::Diagnostic> diagnostics;
};

/// Reads every typedef, struct, union and enum definition at file scope out of
/// a preprocessed unit, the headers it reached included, and lays each type
/// out for the target: scalars at their size and alignment from the dialect,
/// pointers at four bytes, structs padded the way MIPS GCC pads them and
/// bitfields packed from the most significant bit. Functions, prototypes and
/// objects are stepped over.
///
/// The unit should be read with `PreprocessOptions::expandMacros`, since
/// headers spell attributes, bounds and whole enumerations through macros, and
/// only expansion as the text is read sees each macro as it was defined there.
/// Array bounds and enumerator values are evaluated in `context`'s dialect,
/// with the enumerators read so far resolvable. Each enumeration is also kept
/// whole, in order, by its tag and by every typedef that names it. A definition that cannot be read
/// is a diagnostic naming where, and leaves the rest standing.
[[nodiscard]] TypeParseResult parseTypes(const TranslationUnit& unit, const EvalContext& context);

} // namespace cparser
