#include "app/viewport/domain/RenderOverride.h"

#include "app/viewport/domain/RenderMode.h"
#include "base/Types.h"
#include "n64/CombineMode.h"
#include "n64/OtherMode.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>

#include <array>

using base::u32;
using base::u8;
using base::usize;
using n64::BlendAlpha;
using n64::BlendColor;
using n64::BlenderCycle;
using n64::BlendWeight;
using n64::CombinerInput;
using toki::viewport::applyRenderMode;
using toki::viewport::RenderMode;
using toki::viewport::Shading;

namespace {

constexpr u32 ForceBlend = 1U << 14;
constexpr u32 ThresholdCompare = 0x01;
constexpr u32 BilinearFilter = 0x02U << 12;
constexpr u32 TwoCycle = 1U << 20;

constexpr BlenderCycle FogShade{.first = BlendColor::Fog,
                                .firstWeight = BlendAlpha::ShadeAlpha,
                                .second = BlendColor::Pixel,
                                .secondWeight = BlendWeight::InverseAlpha};
constexpr BlenderCycle Translucent{.first = BlendColor::Pixel,
                                   .firstWeight = BlendAlpha::PixelAlpha,
                                   .second = BlendColor::Memory,
                                   .secondWeight = BlendWeight::InverseAlpha};
constexpr BlenderCycle PassThrough{.first = BlendColor::Pixel,
                                   .firstWeight = BlendAlpha::Zero,
                                   .second = BlendColor::Pixel,
                                   .secondWeight = BlendWeight::One};

u32 makeBlender(const usize cycle, const BlenderCycle& blender) {
    const u32 shift = cycle == 0 ? 2 : 0;
    return (u32{static_cast<u8>(blender.first)} << (28 + shift)) |
           (u32{static_cast<u8>(blender.firstWeight)} << (24 + shift)) |
           (u32{static_cast<u8>(blender.second)} << (20 + shift)) |
           (u32{static_cast<u8>(blender.secondWeight)} << (16 + shift));
}

n64::Triangle makeTriangle(const std::array<std::array<float, 4>, 3>& positions) {
    n64::Triangle triangle;
    for (usize i = 0; i < 3; ++i) {
        triangle.vertices[i].position = positions[i];
        triangle.vertices[i].color = {10, 20, 30, 77};
    }
    return triangle;
}

n64::RdpResult makeResult(const n64::OtherMode& otherMode, const n64::Triangle& triangle) {
    n64::RdpResult result;
    n64::RdpState state;
    state.otherMode = otherMode;
    state.combineMode.cycles[1].color = {CombinerInput::Texel0, CombinerInput::Zero,
                                         CombinerInput::Shade, CombinerInput::Zero};
    state.combineMode.cycles[1].alpha = {CombinerInput::Zero, CombinerInput::Zero,
                                         CombinerInput::Zero, CombinerInput::Texel0};
    result.states.push_back(state);
    result.draws.push_back(n64::Draw{.primitive = triangle, .state = 0});
    result.draws.push_back(n64::Draw{.primitive = triangle, .state = 0});
    return result;
}

n64::Triangle makeFlat() {
    return makeTriangle({{{0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}}});
}

const n64::Triangle& getTriangle(const n64::RdpResult& result, const usize draw) {
    return std::get<n64::Triangle>(result.draws[draw].primitive);
}

} // namespace

TEST_CASE("the game's render mode changes nothing", "[viewport][render-mode]") {
    const n64::RdpResult result = makeResult(
        n64::OtherMode{.high = BilinearFilter, .low = makeBlender(0, FogShade) | ThresholdCompare},
        makeFlat());

    const n64::RdpResult drawn = applyRenderMode(result, RenderMode{}, glm::mat4(1.0F));

    REQUIRE(drawn.states == result.states);
    REQUIRE(drawn.draws[0].state == 0);
}

TEST_CASE("fog off passes the cycle that reads fog through", "[viewport][render-mode]") {
    const n64::RdpResult drawn = applyRenderMode(
        makeResult(n64::OtherMode{.low = makeBlender(0, FogShade) | makeBlender(1, Translucent)},
                   makeFlat()),
        RenderMode{.fog = false}, glm::mat4(1.0F));

    REQUIRE(drawn.states[0].otherMode.getBlenderCycle(0) == PassThrough);
    REQUIRE(drawn.states[0].otherMode.getBlenderCycle(1) == Translucent);
}

TEST_CASE("translucency off draws a forced blend with memory opaque", "[viewport][render-mode]") {
    const n64::RdpResult drawn = applyRenderMode(
        makeResult(n64::OtherMode{.low = makeBlender(1, Translucent) | ForceBlend}, makeFlat()),
        RenderMode{.translucency = false}, glm::mat4(1.0F));

    REQUIRE(drawn.states[0].otherMode.getBlenderCycle(1) == PassThrough);
    REQUIRE_FALSE(drawn.states[0].otherMode.isForceBlend());
}

TEST_CASE("alpha cutout off and a point filter change the other mode", "[viewport][render-mode]") {
    const n64::RdpResult drawn = applyRenderMode(
        makeResult(n64::OtherMode{.high = BilinearFilter, .low = ThresholdCompare}, makeFlat()),
        RenderMode{.alphaCutout = false, .textureFilter = toki::viewport::TextureFilter::Point},
        glm::mat4(1.0F));

    REQUIRE(drawn.states[0].otherMode.getAlphaCompare() == n64::AlphaCompare::None);
    REQUIRE(drawn.states[0].otherMode.getTextureFilter() == n64::TextureFilter::Point);
}

TEST_CASE("shade only selects the shade and keeps the alpha", "[viewport][render-mode]") {
    const n64::RdpResult result = makeResult(n64::OtherMode{.high = TwoCycle}, makeFlat());

    const n64::RdpResult drawn =
        applyRenderMode(result, RenderMode{.shading = Shading::ShadeOnly}, glm::mat4(1.0F));

    REQUIRE(drawn.states.size() == 2);
    REQUIRE(drawn.draws[0].state == 1);
    REQUIRE(drawn.draws[1].state == 1);
    const n64::CombineMode& combine = drawn.states[1].combineMode;
    REQUIRE(combine.cycles[0].color[3] == CombinerInput::Shade);
    REQUIRE(combine.cycles[1].color[3] == CombinerInput::Combined);
    REQUIRE(combine.cycles[1].alpha == result.states[0].combineMode.cycles[1].alpha);
    REQUIRE(getTriangle(drawn, 0).shade);
    REQUIRE(getTriangle(drawn, 0).vertices[0].color == std::array<u8, 4>{10, 20, 30, 77});
}

TEST_CASE("texture only shades a triangle without a texture", "[viewport][render-mode]") {
    n64::Triangle textured = makeFlat();
    textured.textured = true;
    n64::RdpResult result = makeResult(n64::OtherMode{}, textured);
    result.draws[1].primitive = makeFlat();

    const n64::RdpResult drawn =
        applyRenderMode(result, RenderMode{.shading = Shading::TextureOnly}, glm::mat4(1.0F));

    REQUIRE(drawn.states[drawn.draws[0].state].combineMode.cycles[1].color[3] ==
            CombinerInput::Texel0);
    REQUIRE(drawn.states[drawn.draws[1].state].combineMode.cycles[1].color[3] ==
            CombinerInput::Shade);
}

TEST_CASE("normals shade a face by the way it faces", "[viewport][render-mode]") {
    const n64::RdpResult drawn =
        applyRenderMode(makeResult(n64::OtherMode{}, makeFlat()),
                        RenderMode{.shading = Shading::Normals}, glm::mat4(1.0F));

    for (const n64::ShadedVertex& vertex : getTriangle(drawn, 0).vertices) {
        REQUIRE(vertex.color == std::array<u8, 4>{127, 127, 255, 77});
    }
}

TEST_CASE("depth shades the nearest vertex white and the farthest black",
          "[viewport][render-mode]") {
    const n64::Triangle deep = makeTriangle({{{0, 0, 0, 10}, {1, 0, 0, 20}, {0, 1, 0, 30}}});

    const n64::RdpResult drawn = applyRenderMode(
        makeResult(n64::OtherMode{}, deep), RenderMode{.shading = Shading::Depth}, glm::mat4(1.0F));

    const n64::Triangle& triangle = getTriangle(drawn, 0);
    REQUIRE(triangle.vertices[0].color == std::array<u8, 4>{255, 255, 255, 77});
    REQUIRE(triangle.vertices[1].color == std::array<u8, 4>{127, 127, 127, 77});
    REQUIRE(triangle.vertices[2].color == std::array<u8, 4>{0, 0, 0, 77});
}
