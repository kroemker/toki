#include "app/scene/domain/BinaryAngle.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>

using toki::scene::convertToBinaryAngle;
using toki::scene::convertToDegrees;

TEST_CASE("binary angles read as degrees, a quarter turn 90", "[scene][angle]") {
    REQUIRE(convertToDegrees(0x4000) == 90.0F);
    REQUIRE(convertToDegrees(-0x8000) == -180.0F);
    REQUIRE(convertToDegrees(-0x4000) == -90.0F);
}

TEST_CASE("every binary angle survives a trip through degrees", "[scene][angle]") {
    for (int angle = -0x8000; angle <= 0x7FFF; ++angle) {
        const auto binary = static_cast<base::s16>(angle);
        REQUIRE(convertToBinaryAngle(convertToDegrees(binary)) == binary);
    }
}

TEST_CASE("degrees past a turn wrap, and round to the nearest unit", "[scene][angle]") {
    REQUIRE(convertToBinaryAngle(270.0F) == -0x4000);
    REQUIRE(convertToBinaryAngle(450.0F) == 0x4000);
    REQUIRE(convertToBinaryAngle(-360.0F) == 0);
    REQUIRE(convertToBinaryAngle(45.002F) == 0x2000);
}

TEST_CASE("a turn about Y adds to Y alone, and one about X or Z keeps the actor's orientation",
          "[scene][angle]") {
    using toki::scene::rotateBinaryAngles;
    using Rotation = std::array<base::s16, 3>;
    REQUIRE(rotateBinaryAngles({0x0100, 0x4000, 0x0200}, 1, 90.0F) ==
            Rotation{0x0100, -0x8000, 0x0200});
    REQUIRE(rotateBinaryAngles({0, 0, 0}, 0, 90.0F) == Rotation{0x4000, 0, 0});
    REQUIRE(rotateBinaryAngles({0, 0, 0}, 2, -45.0F) == Rotation{0, 0, -0x2000});

    const Rotation turned = rotateBinaryAngles({0, 0x4000, 0}, 0, 90.0F);
    const Rotation back = rotateBinaryAngles(turned, 0, -90.0F);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        REQUIRE(std::abs(back[axis] - Rotation{0, 0x4000, 0}[axis]) <= 1);
    }
}
