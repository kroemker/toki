#include "n64/CommandDecoder.h"

#include "base/Types.h"
#include "n64/CombineMode.h"
#include "n64/Opcode.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

using base::u32;

namespace {

[[nodiscard]] u32 findField(const n64::DecodedCommand& command, const std::string_view name) {
    const auto found = std::ranges::find(command.fields, name, &n64::CommandField::name);
    REQUIRE(found != command.fields.end());
    return found->value;
}

} // namespace

TEST_CASE("geometry commands decode into the slots and addresses they name", "[n64][decode]") {
    const n64::DecodedCommand vertex = n64::decodeCommand(0x0100'3006, 0x0600'0020);
    REQUIRE(vertex.name == "gsSPVertex");
    REQUIRE(vertex.kind == n64::CommandKind::Geometry);
    REQUIRE(findField(vertex, "count") == 3);
    REQUIRE(findField(vertex, "first slot") == 0);
    REQUIRE(findField(vertex, "address") == 0x0600'0020);

    const n64::DecodedCommand triangles = n64::decodeCommand(0x0600'0204, 0x0006'080A);
    REQUIRE(triangles.name == "gsSP2Triangles");
    REQUIRE(findField(triangles, "b") == 1);
    REQUIRE(findField(triangles, "f") == 5);
}

TEST_CASE("commands written with one opcode are told apart by their words", "[n64][decode]") {
    REQUIRE(n64::decodeCommand(0xDE00'0000, 0x0600'1000).name == "gsSPDisplayList");
    REQUIRE(n64::decodeCommand(0xDE01'0000, 0x0600'1000).name == "gsSPBranchList");
    REQUIRE(n64::decodeCommand(0xD9FF'FFFF, 0x0000'0400).name == "gsSPSetGeometryMode");
    REQUIRE(n64::decodeCommand(0xD9FF'FBFF, 0x0000'0000).name == "gsSPClearGeometryMode");
    REQUIRE(n64::decodeCommand(0xD900'0000, 0x0000'0005).name == "gsSPLoadGeometryMode");

    const n64::DecodedCommand segment = n64::decodeCommand(0xDB06'0018, 0x0012'3400);
    REQUIRE(segment.name == "gsSPSegment");
    REQUIRE(findField(segment, "segment") == 6);

    const n64::DecodedCommand matrix = n64::decodeCommand(0xDA38'0003, 0x0100'0000);
    REQUIRE(matrix.name == "gsSPMatrix");
    REQUIRE(findField(matrix, "flags") == 0x02);

    const n64::DecodedCommand renderMode = n64::decodeCommand(0xE200'001C, 0x0055'2078);
    REQUIRE(renderMode.name == "gsDPSetRenderMode");
    REQUIRE(renderMode.fields.front().shift == 3);
    REQUIRE(renderMode.fields.front().kind == n64::FieldKind::OtherModeLow);
    REQUIRE(n64::decodeCommand(0xE300'0A01, 0x0000'0000).name == "gsDPSetCycleType");
}

TEST_CASE("texture and material commands decode their fields", "[n64][decode]") {
    const n64::DecodedCommand tile = n64::decodeCommand(0xF570'1000, 0x0709'5250);
    REQUIRE(tile.name == "gsDPSetTile");
    REQUIRE(findField(tile, "format") == 3);
    REQUIRE(findField(tile, "size") == 2);
    REQUIRE(findField(tile, "line") == 8);
    REQUIRE(findField(tile, "tile") == 7);
    REQUIRE(findField(tile, "s mask") == 5);

    const n64::DecodedCommand combine = n64::decodeCommand(0xFCFF'FFFF, 0xFFFE'793C);
    REQUIRE(combine.name == "gsDPSetCombineMode");
    REQUIRE(combine.fields.size() == 16);
    REQUIRE(combine.fields[11].name == "color d");
    REQUIRE(combine.fields[11].shift == 1);
    REQUIRE(combine.fields[11].value == static_cast<u32>(n64::CombinerInput::Shade));
    REQUIRE(n64::getCombinerInputName(n64::CombinerInput::Shade) == "SHADE");

    REQUIRE(n64::decodeCommand(0xFB00'0000, 0xFF80'40FF).name == "gsDPSetEnvColor");
    REQUIRE(n64::decodeCommand(0xE700'0000, 0).kind == n64::CommandKind::Sync);
}

TEST_CASE("every F3DEX2 command is named and says what it does", "[n64][decode]") {
    for (u32 opcode = 0; opcode < 0x100; ++opcode) {
        const n64::DecodedCommand command = n64::decodeCommand(opcode << 24, 0);
        INFO(opcode);
        CHECK_FALSE(command.description.empty());
        for (const n64::CommandField& field : command.fields) {
            INFO(field.name);
            CHECK_FALSE(field.description.empty());
        }
        const bool isF3dex2 = opcode <= 0x07 || opcode >= 0xD3;
        CHECK((command.name == "unknown command") != isF3dex2);
    }
    for (const u32 bit :
         {n64::geometry::ZBuffer, n64::geometry::Shade, n64::geometry::CullBack, n64::geometry::Fog,
          n64::geometry::Lighting, n64::geometry::SmoothShading}) {
        CHECK_FALSE(n64::getGeometryModeDescription(bit).empty());
    }
}
