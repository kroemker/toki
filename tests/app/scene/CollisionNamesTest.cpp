#include "app/scene/domain/CollisionNames.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using toki::scene::SurfaceSetting;

TEST_CASE("collision names are read per setting, an entry without a name left out",
          "[scene][collision]") {
    const toki::scene::CollisionNames names = toki::scene::parseCollisionNames(
        "format = 1\n"
        "[[floor_type]]\nvalue = 5\nconstant = \"FLOOR_TYPE_5\"\nname = \"Slippery\"\n"
        "tooltip = \"Link slides.\"\n"
        "[[floor_type]]\nvalue = 0\nname = \"Default\"\n"
        "[[material]]\nvalue = 2\n",
        "collision.toml");

    REQUIRE(names.diagnostics.size() == 1);
    REQUIRE(names.diagnostics.front().location.line == 10);
    const toki::scene::SurfaceValueName* slippery = names.findName(SurfaceSetting::FloorType, 5);
    REQUIRE(slippery != nullptr);
    REQUIRE(slippery->name == "Slippery");
    REQUIRE(slippery->constant == "FLOOR_TYPE_5");
    REQUIRE(slippery->tooltip == "Link slides.");
    REQUIRE(names.values.at(SurfaceSetting::FloorType).front().value == 0);
    REQUIRE(names.findName(SurfaceSetting::FloorType, 3) == nullptr);
    REQUIRE(names.findName(SurfaceSetting::WallType, 0) == nullptr);
}

TEST_CASE("collision names of another format or not TOML are a diagnostic and no names",
          "[scene][collision]") {
    REQUIRE(toki::scene::parseCollisionNames("format = 2\n", "collision.toml").values.empty());
    REQUIRE(toki::scene::parseCollisionNames("format = 2\n", "collision.toml").diagnostics.size() ==
            1);
    REQUIRE(toki::scene::parseCollisionNames("[[", "collision.toml").diagnostics.size() == 1);
}

TEST_CASE("Toki's own collision names read without a diagnostic", "[scene][collision]") {
    const toki::scene::CollisionNames names =
        toki::scene::readCollisionNames(std::filesystem::path(TOKI_SHIPPED_COLLISION));

    REQUIRE(names.diagnostics.empty());
    REQUIRE(names.findName(SurfaceSetting::Material, 2)->name == "Stone");
    REQUIRE(names.values.size() == 6);
}

TEST_CASE("a surface type is summed up by its material and what else it sets",
          "[scene][collision]") {
    const toki::scene::CollisionNames names =
        toki::scene::parseCollisionNames("format = 1\n[[material]]\nvalue = 2\nname = \"Stone\"\n"
                                         "[[floor_type]]\nvalue = 5\nname = \"Slippery\"\n",
                                         "collision.toml");

    REQUIRE(toki::scene::makeSurfaceSummary(names, {.material = 2}) == "Stone");
    REQUIRE(
        toki::scene::makeSurfaceSummary(
            names,
            {.exitIndex = 1, .floorType = 5, .wallType = 3, .material = 2, .canHookshot = true}) ==
        "Stone · Slippery · 3 · Exit 0 · Hookshotable");
}
