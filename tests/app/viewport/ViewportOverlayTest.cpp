#include "app/viewport/domain/ViewportOverlay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>

#include <optional>

TEST_CASE("a line is projected onto the screen in pixels from the top left",
          "[viewport][overlay]") {
    const std::optional<toki::viewport::ScreenSegment> segment = toki::viewport::projectSegment(
        glm::mat4(1.0F), {0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {200.0F, 100.0F});

    REQUIRE(segment.has_value());
    REQUIRE(segment->from.x == Catch::Approx(100.0F));
    REQUIRE(segment->from.y == Catch::Approx(50.0F));
    REQUIRE(segment->to.x == Catch::Approx(200.0F));
    REQUIRE(segment->to.y == Catch::Approx(0.0F));
    REQUIRE(segment->isFromWhole);
    REQUIRE(segment->isToWhole);
}

TEST_CASE("a line is cut where it passes behind the camera, and dropped when all of it is",
          "[viewport][overlay]") {
    const glm::mat4 projection = glm::perspective(1.0F, 2.0F, 1.0F, 1000.0F);

    const std::optional<toki::viewport::ScreenSegment> cut = toki::viewport::projectSegment(
        projection, {0.0F, 0.0F, -100.0F}, {0.0F, 0.0F, 50.0F}, {200.0F, 100.0F});
    REQUIRE(cut.has_value());
    REQUIRE(cut->isFromWhole);
    REQUIRE_FALSE(cut->isToWhole);
    REQUIRE(cut->from.x == Catch::Approx(100.0F));

    REQUIRE_FALSE(toki::viewport::projectSegment(projection, {0.0F, 0.0F, 10.0F},
                                                 {5.0F, 0.0F, 50.0F}, {200.0F, 100.0F})
                      .has_value());
}
