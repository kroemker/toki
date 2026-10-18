#include "app/viewport/domain/ScreenPick.h"

#include "app/viewport/domain/OrbitCamera.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
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
