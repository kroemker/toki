#include "cparser/TypeParser.h"

#include "cparser/CType.h"
#include "cparser/Expression.h"
#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "cparser/TypeTable.h"
#include "cparser/Types.h"
#include "z64/DecompDialect.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Parsed {
    explicit Parsed(std::string source)
        : unit(cparser::preprocessText(
              base, "types.h", std::move(source),
              cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(),
                                         .expandMacros = true})),
          result(cparser::parseTypes(unit,
                                     cparser::EvalContext{.dialect = &z64::getDecompDialect()})) {}

    [[nodiscard]] const cparser::CType& require(const std::string_view name) const {
        const cparser::CType* type = result.types.findTypedef(name);
        REQUIRE(type != nullptr);
        return *type;
    }

    cparser::MacroTable base;
    cparser::TranslationUnit unit;
    cparser::TypeParseResult result;
};

const cparser::Field& findField(const cparser::CType& type, const std::string_view name) {
    const auto found = std::ranges::find(type.fields, name, &cparser::Field::name);
    REQUIRE(found != type.fields.end());
    return *found;
}

} // namespace

TEST_CASE("a struct is padded the way MIPS GCC pads it", "[cparser][types]") {
    const Parsed parsed("typedef unsigned char u8;\n"
                        "typedef short s16;\n"
                        "typedef struct Vec3s { s16 x, y, z; } Vec3s;\n"
                        "typedef struct Entry {\n"
                        "    /* 0x00 */ Vec3s center;\n"
                        "    /* 0x06 */ s16 radius;\n"
                        "    /* 0x08 */ void* opa;\n"
                        "    /* 0x0C */ u8 flag;\n"
                        "    /* 0x10 */ long long wide;\n"
                        "} Entry;\n");

    const cparser::CType& entry = parsed.require("Entry");
    REQUIRE(entry.size == 0x18);
    REQUIRE(entry.alignment == 8);
    REQUIRE(findField(entry, "radius").offset == 0x06);
    REQUIRE(findField(entry, "opa").offset == 0x08);
    REQUIRE(findField(entry, "flag").offset == 0x0C);
    REQUIRE(findField(entry, "wide").offset == 0x10);
    REQUIRE(parsed.require("Vec3s").size == 6);
}

TEST_CASE("arrays, pointers and unions take their real sizes", "[cparser][types]") {
    const Parsed parsed("#define COUNT 3\n"
                        "typedef unsigned int u32;\n"
                        "typedef struct { u32 w0, w1; } Gwords;\n"
                        "typedef union { Gwords words; long long force; } Gfx;\n"
                        "typedef struct { Gfx* list[COUNT]; void (*callback)(int); } Holder;\n"
                        "typedef u32 Table[4][2];\n");

    REQUIRE(parsed.require("Gfx").size == 8);
    REQUIRE(parsed.require("Gfx").kind == cparser::TypeKind::Union);
    REQUIRE(parsed.require("Holder").size == 16);
    REQUIRE(findField(parsed.require("Holder"), "callback").offset == 12);
    REQUIRE(parsed.require("Table").size == 32);
}

TEST_CASE("bitfields pack from the most significant bit", "[cparser][types]") {
    const Parsed parsed("typedef struct {\n"
                        "    unsigned int a : 4;\n"
                        "    unsigned int b : 30;\n"
                        "    unsigned char c;\n"
                        "} Bits;\n");

    const cparser::CType& bits = parsed.require("Bits");
    REQUIRE(findField(bits, "a").offset == 0);
    REQUIRE(findField(bits, "a").bitOffset == 0);
    REQUIRE(findField(bits, "b").offset == 4);
    REQUIRE(findField(bits, "b").bitOffset == 0);
    REQUIRE(findField(bits, "c").offset == 8);
    REQUIRE(bits.size == 12);
}

TEST_CASE("enumerators count up and may use each other", "[cparser][types]") {
    const Parsed parsed("typedef enum { FIRST, SECOND = 5, THIRD, LAST = THIRD * 2 } Kind;\n");

    REQUIRE(parsed.result.types.findEnumerator("FIRST") == std::optional<cparser::s64>(0));
    REQUIRE(parsed.result.types.findEnumerator("THIRD") == std::optional<cparser::s64>(6));
    REQUIRE(parsed.result.types.findEnumerator("LAST") == std::optional<cparser::s64>(12));
    REQUIRE(parsed.require("Kind").size == 4);
}

TEST_CASE("functions, prototypes and objects are stepped over", "[cparser][types]") {
    const Parsed parsed("typedef int s32;\n"
                        "s32 gCount;\n"
                        "void Do(s32 a, s32 b);\n"
                        "static inline s32 Twice(s32 x) { struct { int y; } z; return x * 2; }\n"
                        "typedef struct { s32 after; } After;\n");

    REQUIRE(parsed.require("After").size == 4);
    REQUIRE(parsed.result.types.findTypedef("gCount") == nullptr);
}

TEST_CASE("an alignment attribute raises a struct's alignment", "[cparser][types]") {
    const Parsed parsed("#define ALIGNED(n) __attribute__((aligned(n)))\n"
                        "typedef struct { char c; } ALIGNED(8) Aligned;\n"
                        "typedef struct Anon { struct { short a; short b; }; int c; } Anon;\n");

    REQUIRE(parsed.require("Aligned").alignment == 8);
    REQUIRE(parsed.require("Aligned").size == 8);
    REQUIRE(parsed.require("Anon").size == 8);
}

TEST_CASE("an enumeration built from an X-macro table reads as the build reads it",
          "[cparser][types]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(
        base, "types.h",
        "#define DEFINE_SCENE(name, id) id,\n"
        "typedef enum { DEFINE_SCENE(a, SCENE_A) DEFINE_SCENE(b, SCENE_B) SCENE_MAX } SceneId;\n"
        "#undef DEFINE_SCENE\n"
        "typedef long double Wide;\n",
        cparser::PreprocessOptions{.dialect = &z64::getDecompDialect(), .expandMacros = true});
    const cparser::TypeParseResult result =
        cparser::parseTypes(unit, cparser::EvalContext{.dialect = &z64::getDecompDialect()});

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.types.findEnumerator("SCENE_B") == std::optional<cparser::s64>(1));
    REQUIRE(result.types.findEnumerator("SCENE_MAX") == std::optional<cparser::s64>(2));
    REQUIRE(result.types.findTypedef("Wide")->size == 8);
}

TEST_CASE("an enumeration is kept whole, by its tag and its typedef", "[cparser][types]") {
    const Parsed parsed("typedef enum Item { ITEM_NONE, ITEM_BOW = 3, ITEM_ARROW,\n"
                        "#if 1\n"
                        "    ITEM_EXTRA,\n"
                        "#else\n"
                        "    ITEM_MISSING,\n"
                        "#endif\n"
                        "    ITEM_MAX } ItemID;\n"
                        "enum Tagged { TAGGED_A = -1 };\n"
                        "typedef enum { ANON_A } Anonymous;\n"
                        "typedef enum Item* ItemPointer;\n");

    const cparser::Enumeration* enumeration = parsed.result.types.findEnumeration("ItemID");
    REQUIRE(enumeration != nullptr);
    REQUIRE(enumeration == parsed.result.types.findEnumeration("Item"));
    REQUIRE(enumeration->location.file.filename() == "types.h");
    REQUIRE(enumeration->location.line == 1);
    const std::vector<cparser::Enumerator>* item = &enumeration->constants;
    REQUIRE(item->size() == 5);
    REQUIRE(item->at(1).name == "ITEM_BOW");
    REQUIRE(item->at(2).value == 4);
    REQUIRE(item->at(3).name == "ITEM_EXTRA");
    REQUIRE(item->back().value == 6);
    REQUIRE(parsed.result.types.findEnumeration("Tagged")->constants.front().value == -1);
    REQUIRE(parsed.result.types.findEnumeration("Anonymous")->constants.size() == 1);
    REQUIRE(parsed.result.types.listEnumerationNames().size() == 4);
    REQUIRE(parsed.result.types.findEnumeration("ItemPointer") == nullptr);
    REQUIRE(parsed.result.types.findEnumeration("ITEM_BOW") == nullptr);
}
