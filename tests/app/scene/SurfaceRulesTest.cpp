#include "app/scene/domain/SurfaceRules.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/CollisionEdits.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

using toki::scene::Collision;
using toki::scene::CollisionPolygon;
using toki::scene::SurfaceField;
using toki::scene::SurfaceFields;

namespace {

constexpr CollisionPolygon Floor{.type = 0, .normal = {0, 0x7FFF, 0}};
constexpr CollisionPolygon Wall{.type = 0, .normal = {0x7FFF, 0, 0}};

[[nodiscard]] Collision makeCollision(const SurfaceFields& fields,
                                      const CollisionPolygon& polygon) {
    Collision collision;
    collision.surfaceTypes.push_back(toki::scene::makeSurfaceType(fields));
    collision.polygons.push_back(polygon);
    return collision;
}

[[nodiscard]] std::map<SurfaceField, std::string> listIgnored(const SurfaceFields& fields,
                                                              const CollisionPolygon& polygon) {
    return toki::scene::listIgnoredSettings(makeCollision(fields, polygon), 0, nullptr);
}

} // namespace

TEST_CASE("bits 18 to 20 are always ignored", "[scene][collision]") {
    REQUIRE(listIgnored({}, Floor) == std::map<SurfaceField, std::string>{
                                          {SurfaceField::Unknown18, "Ignored. No code reads it."}});
}

TEST_CASE("the floor types that take an exit need one", "[scene][collision]") {
    const std::map<SurfaceField, std::string> ignored = listIgnored({.floorType = 10}, Floor);
    REQUIRE(ignored.at(SurfaceField::FloorType) == "Ignored. Requires an exit.");
    REQUIRE_FALSE(
        listIgnored({.exitIndex = 1, .floorType = 10}, Floor).contains(SurfaceField::FloorType));
    REQUIRE(listIgnored({.floorEffect = 2}, Floor).at(SurfaceField::FloorEffect) ==
            "Ignored. Requires an exit.");
    REQUIRE_FALSE(listIgnored({.floorEffect = 1}, Floor).contains(SurfaceField::FloorEffect));
}

TEST_CASE("what only a floor reads needs a floor polygon", "[scene][collision]") {
    const std::map<SurfaceField, std::string> ignored =
        listIgnored({.exitIndex = 1, .floorProperty = 5, .conveyorSpeed = 1}, Wall);
    REQUIRE(ignored.at(SurfaceField::Exit) == "Ignored. Requires a floor polygon.");
    REQUIRE(ignored.at(SurfaceField::FloorProperty) == "Ignored. Requires a floor polygon.");
    REQUIRE(ignored.at(SurfaceField::ConveyorSpeed) == "Ignored. Requires a floor polygon.");
    REQUIRE_FALSE(listIgnored({.exitIndex = 1}, Floor).contains(SurfaceField::Exit));
}

TEST_CASE("a conveyor's direction needs a speed", "[scene][collision]") {
    REQUIRE(listIgnored({.conveyorDirection = 4}, Floor).at(SurfaceField::ConveyorDirection) ==
            "Ignored. Requires a conveyor speed.");
    REQUIRE_FALSE(listIgnored({.conveyorSpeed = 1, .conveyorDirection = 4}, Floor)
                      .contains(SurfaceField::ConveyorDirection));
}

TEST_CASE("contact damage needs a wall or a burning floor", "[scene][collision]") {
    REQUIRE(listIgnored({.unk27 = true}, Floor).at(SurfaceField::ContactDamage) ==
            "Ignored. Requires a wall polygon, or floor type 2 or 3.");
    REQUIRE_FALSE(listIgnored({.unk27 = true}, Wall).contains(SurfaceField::ContactDamage));
    REQUIRE_FALSE(
        listIgnored({.floorType = 3, .unk27 = true}, Floor).contains(SurfaceField::ContactDamage));
}

TEST_CASE("a polygon's conveyor flag needs its surface type's speed", "[scene][collision]") {
    CollisionPolygon conveyor = Floor;
    conveyor.vertexData[1] = 0x2000;
    REQUIRE(toki::scene::findIgnoredFlag(makeCollision({}, conveyor), conveyor,
                                         toki::scene::PolygonFlag::Conveyor) ==
            "Ignored. Requires a conveyor speed.");
    REQUIRE_FALSE(toki::scene::findIgnoredFlag(makeCollision({.conveyorSpeed = 1}, conveyor),
                                               conveyor, toki::scene::PolygonFlag::Conveyor));
    REQUIRE_FALSE(toki::scene::findIgnoredFlag(makeCollision({}, Floor), Floor,
                                               toki::scene::PolygonFlag::Conveyor));
}
