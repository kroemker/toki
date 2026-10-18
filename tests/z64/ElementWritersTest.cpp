#include "z64/ElementWriters.h"

#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "support/WritingHarness.h"
#include "z64/GbiNames.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {

constexpr std::string_view Header =
    "typedef short s16;\n"
    "typedef unsigned char u8;\n"
    "typedef struct { short ob[3]; unsigned short flag; short tc[2]; unsigned char cn[4]; } "
    "Vtx_t;\n"
    "typedef union { Vtx_t v; long long int force_structure_alignment; } Vtx;\n"
    "typedef struct { unsigned int w0; unsigned int w1; } Gwords;\n"
    "typedef union { Gwords words; long long int force_structure_alignment; } Gfx;\n"
    "#define F3DEX_GBI_2\n"
    "#define G_VTX 0x01\n"
    "#define G_DL 0xDE\n"
    "#define G_ENDDL 0xDF\n"
    "#define G_DL_PUSH 0x00\n"
    "#define G_DL_NOPUSH 0x01\n"
    "#define _SHIFTL(v, s, w) ((unsigned int)(((unsigned int)(v) & ((0x01 << (w)) - 1)) << (s)))\n"
    "#define gsDma1p(c, s, l, p) { (_SHIFTL((c), 24, 8) | _SHIFTL((p), 16, 8) | "
    "_SHIFTL((l), 0, 16)), (unsigned int)(s) }\n"
    "#define gsSPVertex(v, n, v0) { (_SHIFTL(G_VTX, 24, 8) | _SHIFTL((n), 12, 8) | "
    "_SHIFTL((v0) + (n), 1, 7)), (unsigned int)(v) }\n"
    "#define gsSPDisplayList(dl) gsDma1p(G_DL, dl, 0, G_DL_PUSH)\n"
    "#define gsSPEndDisplayList() { _SHIFTL(G_ENDDL, 24, 8), 0 }\n";

class Harness : public testing::WritingHarness {
public:
    Harness()
        : WritingHarness(Header,
                         {{"sVtx", {.type = "Vtx", .isArray = true, .elementSize = 16}},
                          {"sOtherDL", {.type = "Gfx", .isArray = true, .elementSize = 8}}}),
          gbiUnit(cparser::preprocessText(base, "gbi.h", std::string(Header), {})),
          names(gbiUnit.macros) {}

    std::string writeGbi(const std::string& source) {
        return writeAgain(source, makeWriter({{"Vtx", z64::makeVertexWriter()},
                                              {"Gfx", z64::makeDisplayListWriter(names)}}));
    }

private:
    cparser::MacroTable base;
    cparser::TranslationUnit gbiUnit;
    z64::GbiNames names;
};

} // namespace

TEST_CASE("a vertex is written as fast64 writes it", "[z64][writer]") {
    Harness harness;

    REQUIRE(harness.writeGbi(
                "Vtx vertices[] = {\n"
                "    { { { -100, 0, 100 }, 0, { -16, 1008 }, { 0, 0x7F, 0, 0xFF } } },\n"
                "};\n") == "Vtx vertices[1] = {\n"
                           "    {{ {-100, 0, 100}, 0, {-16, 1008}, {0x00, 0x7F, 0x00, 0xFF} }},\n"
                           "};\n");
}

TEST_CASE("a display list is written a macro a command, its addresses by symbol", "[z64][writer]") {
    Harness harness;

    REQUIRE(harness.writeGbi("Gfx dl[] = {\n"
                             "    gsSPVertex(&sVtx[4], 3, 0),\n"
                             "    gsSPDisplayList(sOtherDL),\n"
                             "    { 0x12345678, 0x9ABCDEF0 },\n"
                             "    gsSPEndDisplayList(),\n"
                             "};\n") == "Gfx dl[4] = {\n"
                                        "    gsSPVertex(&sVtx[4], 3, 0),\n"
                                        "    gsSPDisplayList(sOtherDL),\n"
                                        "    { 0x12345678, 0x9ABCDEF0 },\n"
                                        "    gsSPEndDisplayList(),\n"
                                        "};\n");
}
