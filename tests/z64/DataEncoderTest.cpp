#include "z64/DataEncoder.h"

#include "base/Types.h"
#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Parser.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeParser.h"
#include "z64/DecompDialect.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using base::u8;

namespace {

constexpr std::string_view Types = "typedef short s16;\n"
                                   "typedef unsigned char u8;\n"
                                   "typedef unsigned int u32;\n"
                                   "typedef float f32;\n"
                                   "typedef struct { s16 x, y, z; } Vec3s;\n"
                                   "typedef struct { Vec3s center; s16 radius; void* opa; "
                                   "void* xlu; } Entry;\n"
                                   "typedef struct { u32 flag : 1; u32 kind : 7; u8 rest; } Bits;\n"
                                   "typedef struct { u8 low : 4; u8 : 2; u8 high : 2; } Gap;\n"
                                   "typedef struct Undefined Undefined;\n"
                                   "typedef struct { u32 w0, w1; } Gfx;\n"
                                   "#define NULL ((void*)0)\n"
                                   "#define ENTRY(x, dl) { { x, x, x }, x, dl, NULL }\n";

std::optional<cparser::SymbolAddress> findSymbol(const std::string_view name) {
    if (name == "sDL") {
        return cparser::SymbolAddress{.address = 0, .elementSize = 8};
    }
    return std::nullopt;
}

/// The last declaration of `source`, encoded against the types and macros
/// `Types` defines.
struct Encoding {
    explicit Encoding(const std::string& source)
        : typesUnit(cparser::preprocessText(
              base, "types.h", std::string(Types),
              cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(),
                                         .expandMacros = true})),
          types(cparser::parseTypes(typesUnit,
                                    cparser::EvalContext{.dialect = &z64::getDecompDialect()})),
          unit(cparser::preprocessText(typesUnit.macros, "data.c", source, {})) {
        const cparser::ParseResult parsed = cparser::parseDeclarations(
            unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});
        REQUIRE_FALSE(parsed.declarations.empty());
        const cparser::Declaration& declaration = parsed.declarations.back();
        const cparser::CType* type = types.types.findTypedef(declaration.type);
        REQUIRE(type != nullptr);
        if (declaration.isArray) {
            for (auto inner = declaration.innerLengths.rbegin();
                 inner != declaration.innerLengths.rend(); ++inner) {
                type = types.types.getArray(type, *inner);
            }
            type = types.types.getArray(
                type, declaration.hasDeclaredLength ? declaration.arrayLength : std::nullopt);
        }
        z64::DataEncoder encoder(z64::DataContext{
            .macros = &unit.macros, .types = &types.types, .findSymbol = &findSymbol});
        data = encoder.encode(*type, declaration.initializer, declaration.isBraced);
    }

    cparser::MacroTable base;
    cparser::TranslationUnit typesUnit;
    cparser::TypeParseResult types;
    cparser::TranslationUnit unit;
    z64::EncodedData data;
};

} // namespace

TEST_CASE("a struct encodes padded and big-endian, with its addresses as relocations",
          "[z64][data]") {
    const Encoding encoding("Entry e = { { 1, 2, 3 }, 4, sDL + 2, NULL };\n");

    REQUIRE(encoding.data.diagnostics.empty());
    REQUIRE(encoding.data.bytes ==
            std::vector<u8>{0, 1, 0, 2, 0, 3, 0, 4, 0, 0, 0, 0x10, 0, 0, 0, 0});
    REQUIRE(encoding.data.relocations.size() == 1);
    REQUIRE(encoding.data.relocations.front().offset == 8);
    REQUIRE(encoding.data.relocations.front().symbol == "sDL");
}

TEST_CASE("braces may be left out, and an array takes its length from them", "[z64][data]") {
    const Encoding encoding("Entry list[] = { 1, 2, 3, 4, 0, 0, ENTRY(5, sDL) };\n");

    REQUIRE(encoding.data.diagnostics.empty());
    REQUIRE(encoding.data.bytes.size() == 32);
    REQUIRE(encoding.data.bytes[17] == 5);
    REQUIRE(encoding.data.relocations.front().offset == 24);
}

TEST_CASE("designators jump, and what they skip is zero", "[z64][data]") {
    const Encoding encoding("Entry e = { .radius = 9, .xlu = sDL };\n");
    const Encoding indexed("s16 table[4] = { [2] = 7, 8 };\n");

    REQUIRE(encoding.data.bytes[7] == 9);
    REQUIRE(encoding.data.relocations.front().offset == 12);
    REQUIRE(indexed.data.bytes == std::vector<u8>{0, 0, 0, 0, 0, 7, 0, 8});
}

TEST_CASE("reals, strings and bitfields take their own encodings", "[z64][data]") {
    const Encoding reals("f32 values[] = { 1.0f, -2.5f };\n");
    const Encoding text("u8 name[6] = \"hi\";\n");
    const Encoding bits("Bits b = { 1, 3, 9 };\n");

    REQUIRE(reals.data.bytes == std::vector<u8>{0x3F, 0x80, 0, 0, 0xC0, 0x20, 0, 0});
    REQUIRE(text.data.bytes == std::vector<u8>{'h', 'i', 0, 0, 0, 0});
    REQUIRE(bits.data.bytes == std::vector<u8>{0x83, 9, 0, 0});
}

TEST_CASE("an address in a field too narrow for it is reported", "[z64][data]") {
    const Encoding encoding("s16 narrow = (s16)sDL;\n");

    REQUIRE_FALSE(encoding.data.diagnostics.empty());
}

TEST_CASE("an unnamed bitfield takes no initializer", "[z64][data]") {
    const Encoding encoding("Gap g = { 5, 3 };\n");

    REQUIRE(encoding.data.diagnostics.empty());
    REQUIRE(encoding.data.bytes == std::vector<u8>{0x53});
}

TEST_CASE("a string's escapes encode as C reads them", "[z64][data]") {
    const Encoding encoding(R"(u8 text[] = "\x41\101\r\"";)"
                            "\n");

    REQUIRE(encoding.data.bytes == std::vector<u8>{'A', 'A', '\r', '"', 0});
}

TEST_CASE("an object of a type never defined is reported, not encoded", "[z64][data]") {
    const Encoding encoding("Undefined u = { 1 };\n");

    REQUIRE_FALSE(encoding.data.diagnostics.empty());
    REQUIRE(encoding.data.bytes.empty());
}

TEST_CASE("an array of display lists encodes each list into its own element", "[z64][data]") {
    const Encoding encoding("Gfx lists[2][2] = {\n"
                            "    { { 0xE7000000, 0 }, { 0xDF000000, 0 } },\n"
                            "    { { 0xDF000000, 0 } },\n"
                            "};\n");

    REQUIRE(encoding.data.diagnostics.empty());
    REQUIRE(encoding.data.bytes.size() == 32);
    REQUIRE(encoding.data.bytes[0] == 0xE7);
    REQUIRE(encoding.data.bytes[8] == 0xDF);
    REQUIRE(encoding.data.bytes[16] == 0xDF);
    REQUIRE(encoding.data.bytes[24] == 0x00);
}

TEST_CASE("a display list longer than its element is cut, saying so", "[z64][data]") {
    const Encoding encoding("Gfx lists[2][1] = {\n"
                            "    { { 0xE7000000, 0 }, { 0xDF000000, 0 } },\n"
                            "    { { 0xDF000000, 0 } },\n"
                            "};\n");

    REQUIRE(encoding.data.diagnostics.size() == 1);
    REQUIRE(encoding.data.bytes.size() == 16);
    REQUIRE(encoding.data.bytes[0] == 0xE7);
    REQUIRE(encoding.data.bytes[8] == 0xDF);
}
