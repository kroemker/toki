#include "z64/GbiNames.h"

#include "cparser/MacroTable.h"
#include "cparser/Preprocessor.h"
#include "n64/CommandDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

namespace {

constexpr const char* Gbi = "#define G_ZBUFFER 0x00000001\n"
                            "#define G_SHADE 0x00000004\n"
                            "#define G_CULL_BACK 0x00000400\n"
                            "#define G_MDSFT_CYCLETYPE 20\n"
                            "#define G_CYC_1CYCLE (0 << G_MDSFT_CYCLETYPE)\n"
                            "#define G_CYC_2CYCLE (1 << G_MDSFT_CYCLETYPE)\n"
                            "#define G_IM_FMT_CI 2\n"
                            "#define G_IM_SIZ_4b 0\n"
                            "#define G_IM_SIZ_4b_BYTES 0\n"
                            "#define G_TX_MIRROR 0x1\n"
                            "#define G_TX_CLAMP 0x2\n"
                            "#define G_TX_RENDERTILE 0\n"
                            "#define G_MTX_PROJECTION 0x04\n"
                            "#define G_MTX_LOAD 0x02\n"
                            "#define G_MTX_PUSH 0x01\n"
                            "#define G_CC_SHADE 0, 0, 0, SHADE, 0, 0, 0, SHADE\n"
                            "#define G_CC_PASS2 0, 0, 0, COMBINED, 0, 0, 0, COMBINED\n"
                            "#define G_RM_OPA 0x0C080000\n"
                            "#define G_RM_OPA2 0x03020000\n";

} // namespace

TEST_CASE("decoded values read as gbi.h names them", "[z64][gbi]") {
    const cparser::MacroTable base;
    const cparser::TranslationUnit unit = cparser::preprocessText(base, "gbi.h", Gbi, {});
    const z64::GbiNames names(unit.macros);
    const z64::FindAddressName findAddressName =
        [](const base::u32 address) -> std::optional<std::string> {
        return address == 0x0600'0000 ? std::optional<std::string>("gTestDL") : std::nullopt;
    };

    REQUIRE(names.makeCommandText(n64::decodeCommand(0xD9FF'FFFF, 0x0000'0405), findAddressName) ==
            "gsSPSetGeometryMode(G_ZBUFFER | G_SHADE | G_CULL_BACK)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xE300'0A01, 0x0010'0000), findAddressName) ==
            "gsDPSetCycleType(G_CYC_2CYCLE)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xDE00'0000, 0x0600'0000), findAddressName) ==
            "gsSPDisplayList(gTestDL)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xDA38'0007, 0x0100'0000), findAddressName) ==
            "gsSPMatrix(0x01000000, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xE200'001C, 0x0F0A'0000), findAddressName) ==
            "gsDPSetRenderMode(G_RM_OPA, G_RM_OPA2)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xFCFF'FFFF, 0xFFFE'793C), findAddressName) ==
            "gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)");
    REQUIRE(names.makeCommandText(n64::decodeCommand(0xF540'0200, 0x0000'0100), findAddressName) ==
            "gsDPSetTile(G_IM_FMT_CI, G_IM_SIZ_4b, 1, 0x0, G_TX_RENDERTILE, 0, G_TX_NOMIRROR | "
            "G_TX_WRAP, 0, 0, G_TX_MIRROR | G_TX_WRAP, 0, 0)");
}
