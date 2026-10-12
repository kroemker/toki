#include "n64/Combiner.h"

#include "base/Types.h"
#include "n64/Blender.h"
#include "n64/Color.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using base::u32;
using base::u8;
using n64::Color;
using n64::CombinerInput;

namespace {

constexpr u32 TwoCycle = 1U << 20;
constexpr u32 ColorTexel0 = 1;
constexpr u32 ColorShade = 4;
constexpr u32 ColorOne = 6;
constexpr u32 ColorZeroA = 8;
constexpr u32 ColorZeroC = 31;
constexpr u32 ColorZeroD = 7;
constexpr u32 ColorCombined = 0;
constexpr u32 AlphaTexel0 = 1;
constexpr u32 AlphaShade = 4;
constexpr u32 AlphaZero = 7;

struct Equation {
    u32 a = ColorZeroA;
    u32 b = ColorZeroA;
    u32 c = ColorZeroC;
    u32 d = ColorZeroD;
    u32 alphaA = AlphaZero;
    u32 alphaB = AlphaZero;
    u32 alphaC = AlphaZero;
    u32 alphaD = AlphaZero;
};

n64::CombineMode makeMode(const Equation& first, const Equation& second) {
    const u32 word0 = (first.a << 20) | (first.c << 15) | (first.alphaA << 12) |
                      (first.alphaC << 9) | (second.a << 5) | second.c;
    const u32 word1 = (first.b << 28) | (second.b << 24) | (second.alphaA << 21) |
                      (second.alphaC << 18) | (first.d << 15) | (first.alphaB << 12) |
                      (first.alphaD << 9) | (second.d << 6) | (second.alphaB << 3) | second.alphaD;
    return n64::makeCombineMode(word0, word1);
}

n64::CombineMode makeMode(const Equation& both) {
    return makeMode(both, both);
}

Color combine(const n64::CombineMode& mode, const n64::CombinerInputs& inputs,
              const n64::OtherMode& otherMode = {}) {
    return n64::evaluateCombiner(mode, otherMode, inputs).color;
}

} // namespace

TEST_CASE("selectors are named by the slot they sit in", "[n64][combiner]") {
    const n64::CombineMode mode = makeMode({}, {.a = 7, .b = 7, .c = 15, .d = 6, .alphaC = 0});

    REQUIRE(mode.cycles[1].color[0] == CombinerInput::Noise);
    REQUIRE(mode.cycles[1].color[1] == CombinerInput::ConvertK4);
    REQUIRE(mode.cycles[1].color[2] == CombinerInput::ConvertK5);
    REQUIRE(mode.cycles[1].color[3] == CombinerInput::One);
    REQUIRE(mode.cycles[1].alpha[2] == CombinerInput::LodFraction);
    REQUIRE(mode.cycles[0].color[2] == CombinerInput::Zero);
    REQUIRE(mode.cycles[0].alpha[0] == CombinerInput::Zero);
}

TEST_CASE("modulating rounds the way the RDP's multiplier does", "[n64][combiner]") {
    const n64::CombineMode modulate =
        makeMode({.a = ColorTexel0, .c = ColorShade, .alphaA = AlphaTexel0, .alphaC = AlphaShade});

    REQUIRE(combine(modulate, {.texel0 = {200, 255, 0, 255}, .shade = {128, 255, 9, 128}}) ==
            Color{100, 254, 0, 128});
}

TEST_CASE("sums past 255 clamp until they wrap into negative", "[n64][combiner]") {
    const n64::CombineMode doubled =
        makeMode({.a = ColorOne, .c = ColorShade, .d = ColorShade, .alphaD = AlphaShade});

    REQUIRE(combine(doubled, {.shade = {100, 150, 200, 255}}) == Color{200, 255, 0, 255});
}

TEST_CASE("the second cycle reads the first's result and its texels swapped", "[n64][combiner]") {
    const n64::OtherMode twoCycle{.high = TwoCycle};
    const n64::CombinerInputs inputs{.texel0 = {10, 20, 30, 40}, .texel1 = {50, 60, 70, 80}};
    const Equation passTexel0{.d = ColorTexel0, .alphaD = AlphaTexel0};
    const Equation passCombined{.d = ColorCombined, .alphaD = 0};

    REQUIRE(combine(makeMode(passTexel0, passCombined), inputs, twoCycle) == inputs.texel0);
    REQUIRE(combine(makeMode(passTexel0, passTexel0), inputs, twoCycle) == inputs.texel1);
    REQUIRE(combine(makeMode(passTexel0, passTexel0), inputs) == inputs.texel0);
}

TEST_CASE("alpha follows coverage when the other mode selects it", "[n64][combiner]") {
    const n64::CombineMode shade = makeMode({.d = ColorShade, .alphaD = AlphaShade});
    const n64::CombinerInputs half{.shade = {0, 0, 0, 200}, .coverage = 4};

    REQUIRE(combine(shade, half, {.low = 0x2000})[3] == 128);
    REQUIRE(combine(shade, half, {.low = 0x3000})[3] == 100);
    REQUIRE(combine(shade, half, {.low = 0x1000})[3] == 200);
}

TEST_CASE("the alpha compare tests against the threshold", "[n64][combiner]") {
    const n64::OtherMode threshold{.low = 0x1};

    REQUIRE(n64::passesAlphaCompare(threshold, 128, 128));
    REQUIRE_FALSE(n64::passesAlphaCompare(threshold, 127, 128));
    REQUIRE(n64::passesAlphaCompare({}, 0, 255));
}

TEST_CASE("coverage times alpha cuts out what is nearly transparent", "[n64][combiner]") {
    const n64::CombineMode texel = makeMode({.d = ColorTexel0, .alphaD = AlphaTexel0});
    const n64::OtherMode texEdge{.low = 0x3078};
    const auto computeCoverage = [&](const u8 alpha) {
        return n64::evaluateCombiner(texel, texEdge, {.texel0 = {9, 9, 9, alpha}}).coverage;
    };

    REQUIRE(computeCoverage(0) == 0);
    REQUIRE(computeCoverage(31) == 0);
    REQUIRE(computeCoverage(32) == 1);
    REQUIRE(computeCoverage(255) == 8);
    REQUIRE(n64::evaluateCombiner(texel, {}, {.texel0 = {9, 9, 9, 0}}).coverage == 8);
}

TEST_CASE("an anti-aliased pixel without coverage is not written", "[n64][combiner]") {
    const n64::OtherMode antiAliased{.low = 0x8};

    REQUIRE_FALSE(n64::isPixelWritten(antiAliased, 0));
    REQUIRE(n64::isPixelWritten(antiAliased, 1));
    REQUIRE(n64::isPixelWritten({}, 0));
}

TEST_CASE("an unforced final blender cycle passes its first colour through", "[n64][blender]") {
    const n64::OtherMode opaque{.low = (1U << 22) | (1U << 18)};

    REQUIRE(n64::evaluateBlender(opaque, {.pixel = {9, 8, 7, 255}, .memory = {1, 2, 3, 4}}) ==
            std::array<u8, 3>{9, 8, 7});
}

TEST_CASE("a forced blend weighs pixel against memory by alpha", "[n64][blender]") {
    const n64::OtherMode translucent{.low = 0x4000 | (1U << 22)};

    REQUIRE(n64::evaluateBlender(translucent,
                                 {.pixel = {200, 100, 0, 128}, .memory = {0, 64, 255, 255}}) ==
            std::array<u8, 3>{100, 82, 127});
}

TEST_CASE("fog in the first cycle blends by shade alpha", "[n64][blender]") {
    const u32 fogShade = (3U << 30) | (2U << 26);
    const n64::OtherMode fog{.high = TwoCycle, .low = fogShade};
    const n64::BlenderInputs inputs{.pixel = {64, 64, 64, 255}, .fog = {255, 0, 0, 255}};

    n64::BlenderInputs thick = inputs;
    thick.shadeAlpha = 255;

    REQUIRE(n64::evaluateBlender(fog, thick) == std::array<u8, 3>{249, 2, 2});
    REQUIRE(n64::evaluateBlender(fog, inputs) == std::array<u8, 3>{64, 64, 64});
}
