#include "app/scene/domain/WaterBoxShape.h"

#include "app/scene/domain/Collision.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

using toki::scene::WaterBox;
using toki::scene::WaterBoxSide;

namespace {

constexpr WaterBox Box{.xMin = -100, .ySurface = 40, .zMin = 10, .xLength = 200, .zLength = 51};

} // namespace

TEST_CASE("a waterbox is moved by the middle of its surface", "[scene][waterbox]") {
    REQUIRE(toki::scene::findWaterBoxCenter(Box) == glm::vec3(0.0F, 40.0F, 35.5F));
    const WaterBox moved = toki::scene::makeMovedWaterBox(Box, glm::vec3(50.0F, -20.0F, 135.5F));
    REQUIRE(moved == WaterBox{.xMin = -50,
                              .ySurface = -20,
                              .zMin = 110,
                              .xLength = 200,
                              .zLength = 51,
                              .properties = Box.properties});
    REQUIRE(toki::scene::makeMovedWaterBox(Box, toki::scene::findWaterBoxCenter(Box)) == Box);
}

TEST_CASE("a waterbox's sides are the middles of its surface's edges", "[scene][waterbox]") {
    REQUIRE(toki::scene::findWaterBoxSidePoint(Box, WaterBoxSide::MinX) ==
            glm::vec3(-100.0F, 40.0F, 35.5F));
    REQUIRE(toki::scene::findWaterBoxSidePoint(Box, WaterBoxSide::MaxZ) ==
            glm::vec3(0.0F, 40.0F, 61.0F));
    REQUIRE(toki::scene::getWaterBoxSideDirection(WaterBoxSide::MinZ) ==
            glm::vec3(0.0F, 0.0F, -1.0F));
}

TEST_CASE("a waterbox's side moves alone", "[scene][waterbox]") {
    const WaterBox wider = toki::scene::makeResizedWaterBox(Box, WaterBoxSide::MinX, 30);
    REQUIRE(wider.xMin == -130);
    REQUIRE(wider.xLength == 230);
    const WaterBox shorter = toki::scene::makeResizedWaterBox(Box, WaterBoxSide::MaxZ, -11);
    REQUIRE(shorter.zMin == 10);
    REQUIRE(shorter.zLength == 40);
}

TEST_CASE("a waterbox is never less than a unit across", "[scene][waterbox]") {
    const WaterBox crossed = toki::scene::makeResizedWaterBox(Box, WaterBoxSide::MinX, -500);
    REQUIRE(crossed.xMin == 99);
    REQUIRE(crossed.xLength == 1);
    REQUIRE(toki::scene::makeResizedWaterBox(Box, WaterBoxSide::MaxZ, -500).zLength == 1);
    REQUIRE(toki::scene::makeResizedWaterBox(Box, WaterBoxSide::MaxX, 40000).xLength == 32767);
}
