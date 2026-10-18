#include "app/scene/domain/BinaryAngle.h"

#include <catch2/catch_test_macros.hpp>

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
