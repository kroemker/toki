#include "support/WritingHarness.h"

#include "base/Diagnostic.h"
#include "base/Types.h"
#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/Parser.h"
#include "cparser/TypeName.h"
#include "z64/DecompDialect.h"
#include "z64/EncodingCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace testing {

WritingHarness::WritingHarness(const std::string_view header, std::vector<PointedSymbol> symbols)
    : headerUnit(cparser::preprocessText(
          base, "header.h", std::string(header),
          cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(), .expandMacros = true})),
      types(cparser::parseTypes(headerUnit,
                                cparser::EvalContext{.dialect = &z64::getDecompDialect()})),
      symbols(std::move(symbols)) {}

const cparser::CType* WritingHarness::findType(const std::string_view name) const {
    if (const cparser::CType* type = types.types.findTypedef(name)) {
        return type;
    }
    const std::array<std::string_view, 1> words{name};
    if (const std::optional<cparser::ScalarType> scalar =
            cparser::combineScalarWords(z64::getDecompDialect(), words)) {
        return types.types.getScalar(*scalar);
    }
    return nullptr;
}

z64::DataContext WritingHarness::makeDataContext() const {
    return z64::DataContext{
        .macros = &headerUnit.macros,
        .types = &types.types,
        .findSymbol = [this](const std::string_view name) -> std::optional<cparser::SymbolAddress> {
            const auto found = std::ranges::find(symbols, name, &PointedSymbol::name);
            if (found == symbols.end()) {
                return std::nullopt;
            }
            return cparser::SymbolAddress{.name = found->name,
                                          .elementSize = found->shape.elementSize};
        }};
}

z64::SymbolData WritingHarness::encode(const std::string& source) {
    units.push_back(std::make_unique<cparser::TranslationUnit>(
        cparser::preprocessText(headerUnit.macros, "data.c", source, {})));
    const cparser::TranslationUnit& unit = *units.back();
    const cparser::ParseResult parsed =
        cparser::parseDeclarations(unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
    REQUIRE_FALSE(parsed.declarations.empty());
    const cparser::Declaration& declaration = parsed.declarations.back();
    const cparser::CType* type = findType(declaration.type);
    REQUIRE(type != nullptr);
    for (base::usize i = 0; i < declaration.pointerDepth; ++i) {
        type = types.types.getPointer(type);
    }
    if (declaration.isArray) {
        for (auto inner = declaration.innerLengths.rbegin();
             inner != declaration.innerLengths.rend(); ++inner) {
            type = types.types.getArray(type, *inner);
        }
        type = types.types.getArray(type, declaration.hasDeclaredLength ? declaration.arrayLength
                                                                        : std::nullopt);
    }
    z64::DataContext context = makeDataContext();
    context.macros = &unit.macros;
    z64::DataEncoder encoder(std::move(context));
    z64::EncodedData data = encoder.encode(*type, declaration.initializer, declaration.isBraced);
    for (const base::Diagnostic& diagnostic : data.diagnostics) {
        UNSCOPED_INFO(diagnostic.message);
    }
    REQUIRE(data.diagnostics.empty());
    return z64::SymbolData{
        .name = std::string(declaration.name),
        .type = std::string(declaration.type),
        .isArray = declaration.isArray,
        .pointerDepth = declaration.pointerDepth,
        .innerLengths = declaration.innerLengths,
        .bytes = std::move(data.bytes),
        .relocations = std::move(data.relocations),
    };
}

z64::DeclarationWriter WritingHarness::makeWriter(
    std::map<std::string, z64::ElementWriter, std::less<>> elementWriters) const {
    return z64::DeclarationWriter(z64::DeclarationContext{
        .findType = [this](const std::string_view name) { return findType(name); },
        .findSymbol = [this](const std::string_view name) -> std::optional<z64::SymbolShape> {
            const auto found = std::ranges::find(symbols, name, &PointedSymbol::name);
            return found == symbols.end() ? std::nullopt : std::optional(found->shape);
        },
        .elementWriters = std::move(elementWriters),
        .isExact = z64::makeEncodingCheck(makeDataContext())});
}

std::string WritingHarness::writeAgain(const std::string& source,
                                       const z64::DeclarationWriter& writer) {
    const z64::SymbolData symbol = encode(source);
    const z64::WrittenText written = writer.writeDefinition(symbol);
    REQUIRE(written.diagnostics.empty());
    INFO(written.text);
    const z64::SymbolData again = encode(written.text);
    REQUIRE(again.bytes == symbol.bytes);
    REQUIRE(again.relocations == symbol.relocations);
    REQUIRE(again.innerLengths == symbol.innerLengths);
    return written.text;
}

} // namespace testing
