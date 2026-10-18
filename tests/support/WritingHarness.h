#pragma once

#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeParser.h"
#include "z64/DataEncoder.h"
#include "z64/DeclarationWriter.h"
#include "z64/SymbolData.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace testing {

/// A symbol the data written may point at.
struct PointedSymbol {
    std::string name;
    z64::SymbolShape shape;
};

/// The types and macros a header text defines, the symbols data may point at,
/// and declarations encoded against them as a memory image would hold them,
/// for writing them back and reading what was written again.
class WritingHarness {
public:
    WritingHarness(std::string_view header, std::vector<PointedSymbol> symbols);

    /// The last declaration `source` holds, encoded.
    [[nodiscard]] z64::SymbolData encode(const std::string& source);

    [[nodiscard]] const cparser::CType* findType(std::string_view name) const;

    [[nodiscard]] const cparser::TypeTable& getTypes() const {
        return types.types;
    }

    [[nodiscard]] z64::DataContext makeDataContext() const;

    /// A writer with the element writers given, checked by encoding what they
    /// write.
    [[nodiscard]] z64::DeclarationWriter
    makeWriter(std::map<std::string, z64::ElementWriter, std::less<>> elementWriters = {}) const;

    /// Writes the symbol `source` declares and encodes what was written, which
    /// must come out as the same bytes and relocations.
    std::string writeAgain(const std::string& source, const z64::DeclarationWriter& writer);

    std::string writeAgain(const std::string& source) {
        return writeAgain(source, makeWriter());
    }

private:
    cparser::MacroTable base;
    cparser::TranslationUnit headerUnit;
    mutable cparser::TypeParseResult types;
    std::vector<PointedSymbol> symbols;
    std::vector<std::unique_ptr<cparser::TranslationUnit>> units;
};

} // namespace testing
