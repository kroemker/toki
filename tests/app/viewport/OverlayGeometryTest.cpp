#include "app/viewport/domain/OverlayGeometry.h"

#include "base/Types.h"
#include "gfx/Overlay.h"
#include "n64/Rdp.h"
#include "n64/RspResult.h"
#include "n64/Viewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <vector>

using base::f32;
using base::usize;
using toki::viewport::clipToNearPlane;
using toki::viewport::LineStyle;
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
