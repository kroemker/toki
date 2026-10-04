#include "n64/Rdp.h"

#include "base/Types.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/Memory.h"
#include "n64/OtherMode.h"
#include "n64/RspResult.h"
#include "n64/TileDescriptor.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <utility>
#include <vector>

using base::u16;
using base::u32;
using base::u8;

namespace {

n64::RspOutput makeCommand(const u32 word0, const u32 word1) {
    return n64::RdpCommand{.word0 = word0, .word1 = word1, .address = 0};
}

n64::RspOutput makeTriangle() {
    return n64::Triangle{};
}

n64::RdpResult runOutputs(std::vector<n64::RspOutput> outputs, const n64::Memory& memory = {}) {
    n64::RspResult rspResult;
    rspResult.outputs = std::move(outputs);
    return n64::runRdp(rspResult, memory);
}

} // namespace

TEST_CASE("primitives drawn under the same state share it", "[n64][rdp]") {
    const n64::RdpResult result = runOutputs({
        makeCommand(0xFA00'0000, 0x1122'3344),
        makeTriangle(),
        makeTriangle(),
        makeCommand(0xFA00'0000, 0x5566'7788),
        makeTriangle(),
        makeCommand(0xFA00'0000, 0x5566'7788),
        makeTriangle(),
    });

    REQUIRE(result.draws.size() == 4);
    REQUIRE(result.states.size() == 2);
    REQUIRE(result.draws[1].state == 0);
    REQUIRE(result.draws[3].state == 1);
    REQUIRE(result.states[0].primitive == n64::Color{0x11, 0x22, 0x33, 0x44});
}

TEST_CASE("texture memory is copied only after a load changed it", "[n64][rdp]") {
    const std::array<u8, 16> texels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    n64::Memory memory;
    REQUIRE(memory.map(0x1000, texels));

    const n64::RdpResult result = runOutputs(
        {
            makeCommand(0xFD10'0003, 0x1000),
            makeCommand(0xF510'0000, 0x0700'0000),
            makeCommand(0xF300'0000, 0x0700'3000),
            makeTriangle(),
            makeCommand(0xF500'0200, 0x0000'0000),
            makeTriangle(),
            makeCommand(0xF300'0000, 0x0700'3000),
            makeTriangle(),
        },
        memory);

    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.textureMemories.size() == 2);
    REQUIRE(result.textureMemories[0][0] == 1);
    REQUIRE(result.states.size() == 3);
    REQUIRE(result.states[1].tiles[0].line == 1);
    REQUIRE(result.states[1].textureMemory == 0);
    REQUIRE(result.states[2].textureMemory == 1);
}

TEST_CASE("state commands are decoded into the state", "[n64][rdp]") {
    const n64::RdpResult result = runOutputs({
        makeCommand(0xEF10'2000, 0x0055'2078),
        makeCommand(0xFC12'6A04, 0xFF0F'FFFF),
        makeCommand(0xFA00'0A80, 0x0102'0304),
        makeCommand(0xED00'0000, 0x0050'03C0),
        makeCommand(0xEC00'5E1F, 0xF85A'B6F6),
        makeCommand(0xEA12'3456, 0x1122'3344),
        makeTriangle(),
    });

    const n64::RdpState& state = result.states.at(0);
    REQUIRE(state.otherMode.getCycleType() == n64::CycleType::Two);
    REQUIRE(state.otherMode.low == 0x0055'2078);
    REQUIRE(state.combineMode == n64::makeCombineMode(0x0012'6A04, 0xFF0F'FFFF));
    REQUIRE(state.primitiveMinLevel == 0x0A);
    REQUIRE(state.primitiveLodFraction == 0x80);
    REQUIRE(state.scissor.right == 0x500);
    REQUIRE(state.scissor.bottom == 0x3C0);
    REQUIRE(state.convert == std::array<u16, 6>{2, 0x1E1, 0x1FF, 0x016, 0x15B, 0x0F6});
    REQUIRE(state.keyWidth[1] == 0x123);
    REQUIRE(state.keyCenter[2] == 0x33);
}

TEST_CASE("a fill rectangle is drawn with its corners", "[n64][rdp]") {
    const n64::RdpResult result = runOutputs({makeCommand(0xF650'03C0, 0x0001'0004)});

    const auto& rectangle = std::get<n64::FillRectangle>(result.draws.at(0).primitive);
    REQUIRE(rectangle.left == 0x010);
    REQUIRE(rectangle.top == 0x004);
    REQUIRE(rectangle.right == 0x500);
    REQUIRE(rectangle.bottom == 0x3C0);
}

TEST_CASE("a texture load that cannot be read is reported", "[n64][rdp]") {
    const n64::RdpResult result = runOutputs({
        makeCommand(0xFD10'0003, 0x8000),
        makeCommand(0xF300'0000, 0x0700'3000),
    });

    REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("the colour and depth images are recorded with the colour image's size", "[n64][rdp]") {
    const n64::RdpResult result = runOutputs({
        makeCommand(0xFF18'013F, 0x0020'0000),
        makeCommand(0xFE00'0000, 0x0030'0000),
        makeTriangle(),
    });

    const n64::RdpState& state = result.states.at(0);
    REQUIRE(state.colorImage == 0x0020'0000);
    REQUIRE(state.depthImage == 0x0030'0000);
    REQUIRE(state.colorImageSize == n64::TexelSize::Bits32);
}
