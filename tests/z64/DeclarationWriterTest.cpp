#include "z64/DeclarationWriter.h"

#include "base/Types.h"
#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeName.h"
#include "cparser/TypeParser.h"
#include "z64/DataEncoder.h"
#include "z64/DecompDialect.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using base::u8;
using base::usize;

namespace {

constexpr std::string_view Types =
    "typedef short s16;\n"
    "typedef signed char s8;\n"
    "typedef unsigned char u8;\n"
    "typedef unsigned short u16;\n"
    "typedef unsigned int u32;\n"
    "typedef unsigned long long u64;\n"
    "typedef unsigned int uintptr_t;\n"
    "typedef float f32;\n"
    "typedef struct { s16 x, y, z; } Vec3s;\n"
    "typedef struct { Vec3s center; s16 radius; void* opa; void* xlu; } Entry;\n"
    "typedef struct { u32 flag : 1; s8 kind : 7; u8 rest; } Bits;\n"
    "typedef union { struct { u32 w0, w1; } words; long long int force; } Gfx;\n"
    "typedef struct { uintptr_t start; uintptr_t end; } RomFile;\n"
    "typedef struct { Entry* first; f32 scale; } Holder;\n"
    "#define NULL ((void*)0)\n";

/// Symbols the data may point at: a display list, one entry, and a ROM symbol.
struct Shapes {
    [[nodiscard]] std::optional<cparser::SymbolAddress>
    findAddress(const std::string_view name) const {
        if (name == "sDL") {
            return cparser::SymbolAddress{.name = "sDL", .elementSize = 8};
        }
        if (name == "sEntry") {
            return cparser::SymbolAddress{.name = "sEntry", .elementSize = 16};
        }
        if (name == "_roomSegmentRomStart") {
            return cparser::SymbolAddress{.name = "_roomSegmentRomStart", .elementSize = 1};
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::optional<z64::SymbolShape> findShape(const std::string_view name) {
        if (name == "sDL") {
            return z64::SymbolShape{.type = "Gfx", .isArray = true, .elementSize = 8};
        }
        if (name == "sEntry") {
            return z64::SymbolShape{.type = "Entry", .isArray = false, .elementSize = 16};
        }
        if (name == "_roomSegmentRomStart") {
            return z64::SymbolShape{.type = "u8", .isArray = true, .elementSize = 1};
        }
        return std::nullopt;
    }
};

/// The types `Types` defines, and the last declaration of a source encoded
/// against them, as the symbol a memory image would hold.
class Harness {
public:
    Harness()
        : typesUnit(cparser::preprocessText(
              base, "types.h", std::string(Types),
              cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(),
                                         .expandMacros = true})),
          types(cparser::parseTypes(typesUnit,
                                    cparser::EvalContext{.dialect = &z64::getDecompDialect()})) {}

    z64::SymbolData encode(const std::string& source) {
        units.push_back(std::make_unique<cparser::TranslationUnit>(
            cparser::preprocessText(typesUnit.macros, "data.c", source, {})));
        const cparser::TranslationUnit& unit = *units.back();
        const cparser::ParseResult parsed = cparser::parseDeclarations(
            unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
        REQUIRE_FALSE(parsed.declarations.empty());
        const cparser::Declaration& declaration = parsed.declarations.back();
        const cparser::CType* type = findType(declaration.type);
        REQUIRE(type != nullptr);
        for (usize i = 0; i < declaration.pointerDepth; ++i) {
            type = types.types.getPointer(type);
        }
        if (declaration.isArray) {
            for (auto inner = declaration.innerLengths.rbegin();
                 inner != declaration.innerLengths.rend(); ++inner) {
                type = types.types.getArray(type, *inner);
            }
            type = types.types.getArray(
                type, declaration.hasDeclaredLength ? declaration.arrayLength : std::nullopt);
        }
        z64::DataEncoder encoder(
            z64::DataContext{.macros = &unit.macros,
                             .types = &types.types,
                             .findSymbol = [this](const std::string_view name) {
                                 return shapes.findAddress(name);
                             }});
        z64::EncodedData data =
            encoder.encode(*type, declaration.initializer, declaration.isBraced);
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

    [[nodiscard]] const cparser::CType* findType(const std::string_view name) const {
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

    [[nodiscard]] z64::DeclarationWriter makeWriter() const {
        return z64::DeclarationWriter(z64::DeclarationContext{
            .findType = [this](const std::string_view name) { return findType(name); },
            .findSymbol = &Shapes::findShape});
    }

    /// Writes the symbol `source` declares and encodes what was written,
    /// which must come out as the same bytes and relocations.
    std::string writeAgain(const std::string& source) {
        const z64::SymbolData symbol = encode(source);
        const z64::WrittenText written = makeWriter().writeDefinition(symbol);
        REQUIRE(written.diagnostics.empty());
        const z64::SymbolData again = encode(written.text);
        REQUIRE(again.bytes == symbol.bytes);
        REQUIRE(again.relocations == symbol.relocations);
        REQUIRE(again.innerLengths == symbol.innerLengths);
        return written.text;
    }

private:
    cparser::MacroTable base;
    cparser::TranslationUnit typesUnit;
    mutable cparser::TypeParseResult types;
    Shapes shapes;
    std::vector<std::unique_ptr<cparser::TranslationUnit>> units;
};

} // namespace

TEST_CASE("a struct is written back member by member, its addresses by their symbols",
          "[z64][writer]") {
    Harness harness;

    const std::string written =
        harness.writeAgain("Entry e = { { 1, -2, 3 }, 4, &sDL[2], NULL };\n");

    REQUIRE(written == "Entry e = {\n"
                       "    { 1, -2, 3 },\n"
                       "    4,\n"
                       "    &sDL[2],\n"
                       "    NULL,\n"
                       "};\n");
}

TEST_CASE("an array of structs is written an element a line", "[z64][writer]") {
    Harness harness;

    const std::string written = harness.writeAgain(
        "Entry list[] = { { { 1, 2, 3 }, 4, sDL, &sEntry }, { { 5, 6, 7 }, 8, NULL, NULL } };\n");

    REQUIRE(written == "Entry list[2] = {\n"
                       "    { { 1, 2, 3 }, 4, sDL, &sEntry },\n"
                       "    { { 5, 6, 7 }, 8, NULL, NULL },\n"
                       "};\n");
}

TEST_CASE("integers are hex when unsigned and decimal when signed, many to a line",
          "[z64][writer]") {
    Harness harness;

    REQUIRE(harness.writeAgain("u16 table[] = { 1, 0xFFFF, 0x1234 };\n") ==
            "u16 table[3] = {\n    0x0001, 0xFFFF, 0x1234,\n};\n");
    REQUIRE(harness.writeAgain("s16 table[] = { -1, 2, -32768 };\n") ==
            "s16 table[3] = {\n    -1, 2, -32768,\n};\n");
    REQUIRE(harness.writeAgain("u64 words[] = { 0x0123456789ABCDEF, 1, 2, 3, 4 };\n") ==
            "u64 words[5] = {\n"
            "    0x0123456789ABCDEF, 0x0000000000000001, 0x0000000000000002, 0x0000000000000003,\n"
            "    0x0000000000000004,\n"
            "};\n");
}

TEST_CASE("bitfields, unions, reals and 2D arrays read back as they were", "[z64][writer]") {
    Harness harness;

    REQUIRE(harness.writeAgain("Bits b = { 1, -3, 9 };\n") ==
            "Bits b = {\n    0x1,\n    -3,\n    0x09,\n};\n");
    REQUIRE(harness.writeAgain("Gfx dl[] = { { 0xDE000000, (uintptr_t)sDL } };\n") ==
            "Gfx dl[1] = {\n    { 0xDE000000, (uintptr_t)sDL },\n};\n");
    REQUIRE(harness.writeAgain("Holder h = { &sEntry, 0.1f };\n") ==
            "Holder h = {\n    &sEntry,\n    0.1f,\n};\n");
    REQUIRE(harness.writeAgain("f32 values[] = { 1.0f, -2.5f, 3e-30f };\n") ==
            "f32 values[3] = {\n    1.0f, -2.5f, 3e-30f,\n};\n");
    REQUIRE(harness.writeAgain("s16 pairs[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };\n") ==
            "s16 pairs[2][3] = {\n    { 1, 2, 3 },\n    { 4, 5, 6 },\n};\n");
}

TEST_CASE("an address the constant does not land on an element is cast through its bytes",
          "[z64][writer]") {
    Harness harness;

    REQUIRE(harness.writeAgain("RomFile rooms[] = { { (uintptr_t)_roomSegmentRomStart, "
                               "(uintptr_t)_roomSegmentRomStart + 4 } };\n") ==
            "RomFile rooms[1] = {\n"
            "    { (uintptr_t)_roomSegmentRomStart, (uintptr_t)&_roomSegmentRomStart[4] },\n"
            "};\n");
    REQUIRE(harness.writeAgain("void* odd[] = { (void*)((uintptr_t)sDL + 3), sDL };\n") ==
            "void* odd[2] = {\n    (void*)((uintptr_t)sDL + 0x3), sDL,\n};\n");
}

TEST_CASE("an extern names the type and length a definition has", "[z64][writer]") {
    Harness harness;
    const z64::DeclarationWriter writer = harness.makeWriter();

    REQUIRE(writer.writeExtern(harness.encode("Entry* list[] = { &sEntry, NULL };\n")) ==
            "extern Entry* list[2];\n");
    REQUIRE(writer.writeExtern(harness.encode("s16 pairs[2][3] = { 0 };\n")) ==
            "extern s16 pairs[2][3];\n");
    REQUIRE(writer.writeExtern(harness.encode("Holder h = { 0 };\n")) == "extern Holder h;\n");
}

TEST_CASE("a symbol built from an embedded file is written as its include", "[z64][writer]") {
    Harness harness;
    z64::SymbolData texture = harness.encode("u64 tex[] = { 1, 2 };\n");
    texture.embedded = z64::EmbeddedSource{.spelling = "assets/scenes/a/tex.rgba16.inc.c",
                                           .path = "tex.rgba16.png"};

    REQUIRE(harness.makeWriter().writeDefinition(texture).text ==
            "u64 tex[2] = {\n#include \"assets/scenes/a/tex.rgba16.inc.c\"\n};\n");
}

TEST_CASE("a type no header defines is not written", "[z64][writer]") {
    Harness harness;
    z64::SymbolData symbol = harness.encode("u8 data[] = { 1 };\n");
    symbol.type = "Missing";

    const z64::WrittenText written = harness.makeWriter().writeDefinition(symbol);

    REQUIRE(written.text.empty());
    REQUIRE(written.diagnostics.size() == 1);
}
