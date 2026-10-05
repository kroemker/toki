#pragma once

#include "base/Diagnostic.h"
#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/Token.h"
#include "z64/DisplayListEncoder.h"
#include "z64/Relocation.h"
#include "z64/Types.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cparser {
class MacroTable;
class TypeTable;
} // namespace cparser

namespace z64 {

/// One object's bytes before it is laid out: its addresses are relocations,
/// with the constant added to each address already in place.
struct EncodedData {
    std::vector<u8> bytes;
    std::vector<Relocation> relocations;
    std::vector<base::Diagnostic> diagnostics;
};

struct DataContext {
    /// The file's macros layered over its prelude, the gbi's included.
    const cparser::MacroTable* macros = nullptr;

    /// The types the file's prelude defines, and its enumerators.
    const cparser::TypeTable* types = nullptr;

    /// Identifiers the prelude's types do not answer, such as the enumerators
    /// the file's own headers define for its limbs.
    std::function<std::optional<cparser::Value>(std::string_view)> resolve;

    /// The symbols the data may point at, with the size of an element of
    /// each. Their addresses are not known yet and are left at zero.
    std::function<std::optional<cparser::SymbolAddress>(std::string_view)> findSymbol;

    /// Declared element counts, for `ARRAY_COUNT`.
    std::function<std::optional<usize>(std::string_view)> findArrayLength;

    /// The bytes of the file's embedded file at an index into its unit's
    /// `embedded`, as the build would generate them.
    std::function<std::expected<std::vector<u8>, std::string>(u32 index)> encodeEmbedded;
};

/// Encodes the data a file declares, object by object, into the bytes the
/// build would compile it to. An initializer fills its type the way C fills
/// it: braces may be left out, `.field =` and `[index] =` jump, missing
/// members are zero, and an embedded file fills as many elements as it holds.
/// Scalars are big-endian at their own width, reals IEEE 754, and bitfields
/// packed where the layout put them. A `Gfx` array goes through a
/// `DisplayListEncoder`, so its commands are encoded as the gbi defines them.
///
/// One encoder serves one file: its display list encoder remembers commands
/// across the file's lists.
class DataEncoder {
public:
    explicit DataEncoder(DataContext context);

    /// `initializer` is what a declaration's braces hold when `isBraced`, or
    /// else the expression it is initialized with, whose own braces may come
    /// from a macro. An array of unknown length takes the length the
    /// initializer gives it.
    [[nodiscard]] EncodedData encode(const cparser::CType& type,
                                     std::span<const cparser::Token> initializer, bool isBraced);

private:
    [[nodiscard]] bool isDisplayList(const cparser::CType& type) const;
    [[nodiscard]] EncodedData encodeDisplayLists(const cparser::CType& type,
                                                 std::span<const cparser::Token> initializer);

    DataContext context;
    DisplayListEncoder displayLists;
};

} // namespace z64
