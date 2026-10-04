#include "gfx/BlendSetup.h"

#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

using base::u32;
using gfx::BlendFactor;
using gfx::BlendPath;

namespace {

constexpr u32 ForceBlend = 0x4000;
constexpr u32 TwoCycle = 1U << 20;

u32 makeCycle(const u32 first, const u32 firstWeight, const u32 second, const u32 secondWeight) {
    return (first << 30) | (firstWeight << 26) | (second << 22) | (secondWeight << 18);
}

} // namespace

TEST_CASE("an unforced opaque mode needs no blending", "[gfx][blend]") {
    const auto setup = gfx::makeBlendSetup({.low = makeCycle(0, 0, 1, 1)});

    REQUIRE(setup.has_value());
    REQUIRE(setup->path == BlendPath::Shader);
    REQUIRE_FALSE(setup->enabled);
}

TEST_CASE("translucency becomes source-alpha blending", "[gfx][blend]") {
    const auto setup = gfx::makeBlendSetup({.low = ForceBlend | makeCycle(0, 0, 1, 0)});

    REQUIRE(setup.has_value());
    REQUIRE(setup->path == BlendPath::OverMemory);
    REQUIRE(setup->source == BlendFactor::SourceAlpha);
    REQUIRE(setup->destination == BlendFactor::OneMinusSourceAlpha);
}

TEST_CASE("additive blending keeps memory at full weight", "[gfx][blend]") {
    const auto setup = gfx::makeBlendSetup({.low = ForceBlend | makeCycle(0, 0, 1, 2)});

    REQUIRE(setup.has_value());
    REQUIRE(setup->destination == BlendFactor::One);
}

TEST_CASE("fog in the first cycle stays in the shader", "[gfx][blend]") {
    const u32 fog = makeCycle(3, 2, 0, 0) | ((0U << 28) | (0U << 24) | (1U << 20) | (1U << 16));
    const auto setup = gfx::makeBlendSetup({.high = TwoCycle, .low = fog});

    REQUIRE(setup.has_value());
    REQUIRE(setup->path == BlendPath::Shader);
}

TEST_CASE("memory weighed against memory is refused", "[gfx][blend]") {
    REQUIRE_FALSE(gfx::makeBlendSetup({.low = ForceBlend | makeCycle(1, 0, 1, 0)}).has_value());
    REQUIRE_FALSE(
        gfx::makeBlendSetup({.high = TwoCycle, .low = makeCycle(1, 0, 0, 0)}).has_value());
}
