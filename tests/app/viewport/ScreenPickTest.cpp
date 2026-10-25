#include "app/viewport/domain/ScreenPick.h"

#include "app/viewport/domain/OrbitCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

using base::usize;

TEST_CASE("a point falls on the image from its top left, y down", "[viewport][pick]") {
    const glm::mat4 identity(1.0F);
    const glm::vec2 size{200.0F, 100.0F};

    REQUIRE(toki::viewport::findScreenPosition(identity, size, {0.0F, 0.0F, 0.0F}) ==
            glm::vec2(100.0F, 50.0F));
    REQUIRE(toki::viewport::findScreenPosition(identity, size, {-1.0F, 1.0F, 0.0F}) ==
            glm::vec2(0.0F, 0.0F));
}

TEST_CASE("the point picked is the one nearest the mouse, within the radius", "[viewport][pick]") {
    const glm::mat4 identity(1.0F);
    const glm::vec2 size{200.0F, 100.0F};
    const std::array<glm::vec3, 3> points{glm::vec3{-0.5F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 0.0F},
                                          glm::vec3{0.1F, 0.0F, 0.0F}};

    REQUIRE(toki::viewport::findPickedPoint(identity, size, {103.0F, 50.0F}, points, 8.0F) ==
            usize{1});
    REQUIRE(toki::viewport::findPickedPoint(identity, size, {109.0F, 50.0F}, points, 8.0F) ==
            usize{2});
    REQUIRE_FALSE(toki::viewport::findPickedPoint(identity, size, {100.0F, 90.0F}, points, 8.0F));
}

TEST_CASE("a point behind the camera is not picked", "[viewport][pick]") {
    toki::viewport::OrbitCamera camera;
    const glm::mat4 viewProjection =
        toki::viewport::OrbitCamera::makeProjectionMatrix(1.0F) * camera.makeViewMatrix();
    const glm::vec3 behind = camera.getEye() * 2.0F - camera.getTarget();

    REQUIRE_FALSE(toki::viewport::findScreenPosition(viewProjection, {100.0F, 100.0F}, behind));
    REQUIRE(
        toki::viewport::findScreenPosition(viewProjection, {100.0F, 100.0F}, camera.getTarget()));
}

TEST_CASE("a box picks the points that fall in it, whichever corner it was dragged from",
          "[viewport][pick]") {
    const glm::mat4 identity(1.0F);
    const glm::vec2 size{200.0F, 100.0F};
    const std::array<glm::vec3, 3> points{glm::vec3{-0.5F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 0.0F},
                                          glm::vec3{0.5F, 0.5F, 0.0F}};

    REQUIRE(toki::viewport::findPointsInRect(identity, size, {120.0F, 60.0F}, {40.0F, 40.0F},
                                             points) == std::vector<usize>{0, 1});
    REQUIRE(toki::viewport::findPointsInRect(identity, size, {0.0F, 0.0F}, {10.0F, 10.0F}, points)
                .empty());
}

TEST_CASE("an axis line runs from edge to edge of the image, cut where it passes the camera",
          "[viewport][pick]") {
    const glm::vec2 size{200.0F, 100.0F};
    const auto across = toki::viewport::findScreenLine(glm::mat4(1.0F), size, {0.0F, 0.0F, 0.0F},
                                                       {1.0F, 0.0F, 0.0F});
    REQUIRE(across.has_value());
    REQUIRE((*across)[0].x == Catch::Approx(0.0F).margin(1e-2));
    REQUIRE((*across)[1].x == Catch::Approx(200.0F).margin(1e-2));
    REQUIRE((*across)[0].y == Catch::Approx(50.0F));

    toki::viewport::OrbitCamera camera;
    const glm::mat4 viewProjection =
        toki::viewport::OrbitCamera::makeProjectionMatrix(2.0F) * camera.makeViewMatrix();
    const glm::vec3 beside = camera.getTarget() + glm::vec3(0.0F, 30.0F, 0.0F);
    const auto toward = toki::viewport::findScreenLine(viewProjection, size, beside,
                                                       camera.getEye() - camera.getTarget());
    REQUIRE(toward.has_value());
    if (toward) {
        for (const glm::vec2& end : *toward) {
            REQUIRE(end.x >= -1e-2F);
            REQUIRE(end.x <= 200.01F);
            REQUIRE(end.y >= -1e-2F);
            REQUIRE(end.y <= 100.01F);
        }
    }
    REQUIRE_FALSE(toki::viewport::findScreenLine(glm::mat4(1.0F), size, {0.0F, 3.0F, 0.0F},
                                                 {1.0F, 0.0F, 0.0F}));
}

TEST_CASE("a segment falls on the image, cut where it passes behind the camera",
          "[viewport][pick]") {
    const glm::vec2 size{200.0F, 100.0F};
    const auto flat = toki::viewport::findScreenSegment(glm::mat4(1.0F), size, {-0.5F, 0.0F, 0.0F},
                                                        {0.5F, 0.0F, 0.0F});
    REQUIRE(flat.has_value());
    REQUIRE((*flat)[0] == glm::vec2(50.0F, 50.0F));
    REQUIRE((*flat)[1] == glm::vec2(150.0F, 50.0F));

    toki::viewport::OrbitCamera camera;
    const glm::mat4 viewProjection =
        toki::viewport::OrbitCamera::makeProjectionMatrix(2.0F) * camera.makeViewMatrix();
    const glm::vec3 behind = camera.getEye() * 2.0F - camera.getTarget();
    const auto cut =
        toki::viewport::findScreenSegment(viewProjection, size, camera.getTarget(), behind);
    REQUIRE(cut.has_value());
    REQUIRE(std::isfinite((*cut)[1].x));
    REQUIRE_FALSE(toki::viewport::findScreenSegment(viewProjection, size, behind,
                                                    behind + glm::vec3(10.0F, 0.0F, 0.0F)));
}

TEST_CASE("a drag along an axis is measured as the axis looks on the image", "[viewport][pick]") {
    const glm::mat4 identity(1.0F);
    const glm::vec2 size{200.0F, 100.0F};
    const glm::vec3 origin{0.0F, 0.0F, 0.0F};

    REQUIRE(toki::viewport::findAxisDistance(identity, size, origin, {2.0F, 0.0F, 0.0F},
                                             {100.0F, 50.0F},
                                             {150.0F, 80.0F}) == Catch::Approx(0.5F));
    REQUIRE(toki::viewport::findAxisDistance(identity, size, origin, {0.0F, 1.0F, 0.0F},
                                             {100.0F, 50.0F},
                                             {100.0F, 75.0F}) == Catch::Approx(-0.5F));
    REQUIRE_FALSE(toki::viewport::findAxisDistance(identity, size, origin, {0.0F, 0.0F, 1.0F},
                                                   {100.0F, 50.0F}, {150.0F, 50.0F}));
}
