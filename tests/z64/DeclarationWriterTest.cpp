#include "z64/DeclarationWriter.h"

#include "support/WritingHarness.h"
#include "z64/SymbolData.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

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

/// Types, and symbols the data may point at: a display list, one entry, and a
/// ROM symbol.
class Harness : public testing::WritingHarness {
public:
    Harness()
        : WritingHarness(Types, {{"sDL", {.type = "Gfx", .isArray = true, .elementSize = 8}},
                                 {"sEntry", {.type = "Entry", .isArray = false, .elementSize = 16}},
                                 {"_roomSegmentRomStart",
                                  {.type = "u8", .isArray = true, .elementSize = 1}}}) {}
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
