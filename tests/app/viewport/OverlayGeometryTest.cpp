#include "app/viewport/domain/OverlayGeometry.h"

#include "app/viewport/domain/ClipTriangle.h"
#include "app/viewport/domain/ViewportOverlay.h"
#include "base/Types.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

using base::f32;
using base::usize;
using toki::viewport::clipToNearPlane;
using toki::viewport::LineStyle;
using toki::viewport::makeOutlines;
using toki::viewport::makeTriangleEdges;

namespace {

using Position = std::array<f32, 4>;

constexpr LineStyle Style{.color = {1, 2, 3, 4}, .width = 2.0F};

n64::Triangle makeTriangle(const Position& a, const Position& b, const Position& c,
                           const usize viewport = 0) {
    n64::Triangle triangle;
    triangle.vertices[0].position = a;
    triangle.vertices[1].position = b;
    triangle.vertices[2].position = c;
    triangle.viewport = viewport;
    return triangle;
}

n64::RspResult makeRspResult() {
    n64::RspResult result;
    result.viewports.push_back(n64::makeDefaultViewport());
    result.viewports.push_back(n64::Viewport{.scale = {10, 10, 10}, .translate = {10, 10, 10}});
    return result;
}

} // namespace

TEST_CASE("an edge two triangles share is drawn once", "[viewport][overlay]") {
    const Position a{0, 0, 0, 1};
    const Position b{1, 0, 0, 1};
    const Position c{0, 1, 0, 1};
    const Position d{1, 1, 0, 1};
    n64::RdpResult rdpResult;
    rdpResult.draws.push_back({.primitive = makeTriangle(a, b, c), .state = 0});
    rdpResult.draws.push_back({.primitive = makeTriangle(c, b, d), .state = 0});

    const std::vector<gfx::OverlayLine> lines =
        makeTriangleEdges(makeRspResult(), rdpResult, n64::makeDefaultViewport(), Style);

    REQUIRE(lines.size() == 5);
    REQUIRE(lines[0].color == Style.color);
    REQUIRE(lines[0].width == Style.width);
}

TEST_CASE("only triangles drawn through the viewport give edges", "[viewport][overlay]") {
    n64::RdpResult rdpResult;
    rdpResult.draws.push_back(
        {.primitive = makeTriangle({0, 0, 0, 1}, {1, 0, 0, 1}, {0, 1, 0, 1}, 1), .state = 0});

    REQUIRE(
        makeTriangleEdges(makeRspResult(), rdpResult, n64::makeDefaultViewport(), Style).empty());
}

TEST_CASE("a line is cut where it crosses the near plane", "[viewport][overlay]") {
    const gfx::OverlayLine line{.from = {0, 0, -3, 1}, .to = {0, 0, 1, 1}};

    const std::optional<gfx::OverlayLine> clipped = clipToNearPlane(line);

    REQUIRE(clipped.has_value());
    REQUIRE(clipped->to == line.to);
    REQUIRE(clipped->from[2] == Catch::Approx(-1.0F).margin(0.001F));
    REQUIRE(clipped->from[2] + clipped->from[3] > 0.0F);
}

TEST_CASE("a line wholly on the camera's side of the near plane is not drawn",
          "[viewport][overlay]") {
    REQUIRE_FALSE(clipToNearPlane({.from = {0, 0, -3, 1}, .to = {0, 0, -2, 1}}).has_value());
}

TEST_CASE("a line past the near plane is left whole", "[viewport][overlay]") {
    const gfx::OverlayLine line{.from = {0, 0, 0, 1}, .to = {1, 1, 0.5F, 2}};

    REQUIRE(clipToNearPlane(line) == line);
}

TEST_CASE("an arrowhead points at a line's end, short of it, at the end's depth",
          "[viewport][overlay]") {
    toki::viewport::ViewportOverlay overlay;
    overlay.lines.push_back(
        {.from = {-0.5F, 0.0F, 0.2F}, .to = {0.5F, 0.0F, 0.4F}, .color = 0xFF332211U});

    const std::vector<gfx::OverlayLine> lines = toki::viewport::makeArrowLines(
        overlay, glm::mat4(1.0F), {.width = 200, .height = 100}, toki::viewport::ArrowStyle{});

    REQUIRE(lines.size() == 3);
    REQUIRE(lines[0].color == std::array<base::u8, 4>{0x11, 0x22, 0x33, 0xFF});
    for (usize i = 1; i < 3; ++i) {
        REQUIRE(lines[i].to[0] == Catch::Approx(0.41F));
        REQUIRE(lines[i].to[2] == Catch::Approx(0.4F));
        REQUIRE(lines[i].from[0] == Catch::Approx(0.3F));
    }
    REQUIRE(lines[1].from[1] == Catch::Approx(-lines[2].from[1]));
}

TEST_CASE("an arrowhead is left off a short line and off an end the near plane cut",
          "[viewport][overlay]") {
    toki::viewport::ViewportOverlay overlay;
    overlay.lines.push_back({.from = {0.0F, 0.0F, 0.0F}, .to = {0.1F, 0.0F, 0.0F}});
    overlay.lines.push_back({.from = {0.0F, 0.0F, 0.0F}, .to = {0.0F, 0.5F, -3.0F}});

    const std::vector<gfx::OverlayLine> lines = toki::viewport::makeArrowLines(
        overlay, glm::mat4(1.0F), {.width = 200, .height = 100}, toki::viewport::ArrowStyle{});

    REQUIRE(lines.size() == 2);
}

TEST_CASE("a mutual link has an arrowhead at both ends", "[viewport][overlay]") {
    toki::viewport::ViewportOverlay overlay;
    overlay.lines.push_back({.from = {-0.5F, 0.0F, 0.0F},
                             .to = {0.5F, 0.0F, 0.0F},
                             .hasArrowAtFrom = true,
                             .hasArrowAtTo = true});

    REQUIRE(toki::viewport::makeArrowLines(overlay, glm::mat4(1.0F), {.width = 200, .height = 100},
                                           toki::viewport::ArrowStyle{})
                .size() == 5);
}

TEST_CASE("the grid crosses the cell under the centre, its axes coloured", "[viewport][overlay]") {
    glm::mat4 flat(1.0F);
    flat[2][2] = 0.0F;

    const std::vector<gfx::OverlayLine> lines =
        toki::viewport::makeGridLines(flat, {40.0F, 0.0F, -30.0F});

    const usize count = (static_cast<usize>(toki::viewport::GridCells) * 2) + 1;
    REQUIRE(lines.size() == count * 2);
    const auto isZAxis = [](const gfx::OverlayLine& line) {
        return line.from[0] == 0.0F && line.to[0] == 0.0F;
    };
    const auto zAxis = std::ranges::find_if(lines, isZAxis);
    REQUIRE(zAxis != lines.end());
    REQUIRE(zAxis->color[2] > zAxis->color[0]);
}

TEST_CASE("an outline has each triangle's three edges, cut at the near plane",
          "[viewport][overlay]") {
    const toki::viewport::ClipTriangle front{
        {{0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F, 1.0F}}};
    const toki::viewport::ClipTriangle crossing{
        {{0.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -3.0F, -1.0F}}};

    const std::vector<gfx::OverlayLine> lines = makeOutlines({front, crossing}, Style);

    REQUIRE(lines.size() == 6);
    REQUIRE(lines[0].from == front[0]);
    REQUIRE(lines[0].to == front[1]);
    REQUIRE(lines[2].from == front[2]);
    REQUIRE(lines[2].to == front[0]);
    REQUIRE(lines[0].color == Style.color);
    REQUIRE(lines[0].width == Style.width);
    for (usize i = 3; i < lines.size(); ++i) {
        REQUIRE(lines[i].from[2] >= -lines[i].from[3] - 1.0e-3F);
        REQUIRE(lines[i].to[2] >= -lines[i].to[3] - 1.0e-3F);
    }
}
