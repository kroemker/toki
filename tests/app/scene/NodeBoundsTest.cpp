#include "app/scene/domain/NodeBounds.h"

#include "app/scene/domain/Collision.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneSelection.h"

#include <catch2/catch_test_macros.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <optional>
#include <vector>

using toki::scene::NodeBounds;
using toki::scene::SceneNode;

namespace {

toki::scene::Scene makeScene() {
    toki::scene::Scene scene;
    toki::scene::SceneLayer layer;
    toki::scene::RoomHeader room;
    room.actors.push_back({.id = 1, .position = {10, 20, 30}});
    layer.rooms.push_back(room);
    layer.header.paths.push_back({.points = {{0, 0, 0}, {100, -50, 200}}});
    layer.collision = 0;
    toki::scene::Collision collision;
    collision.vertices = {{-10, 0, -10}, {10, 5, -10}, {0, 0, 40}};
    collision.polygons.push_back({.vertexData = {0, 1, 2}});
    collision.waterBoxes.push_back(
        {.xMin = -100, .ySurface = 15, .zMin = 50, .xLength = 200, .zLength = 20});
    scene.collisions.push_back(collision);
    scene.layers.push_back(layer);
    return scene;
}

} // namespace

TEST_CASE("a placement's bounds are its position", "[scene][bounds]") {
    const std::vector<SceneNode> nodes{toki::scene::PlacementRef{}};
    REQUIRE(toki::scene::findNodeBounds(makeScene(), nodes) ==
            NodeBounds{.min = {10.0F, 20.0F, 30.0F}, .max = {10.0F, 20.0F, 30.0F}});
}

TEST_CASE("a path, polygons and waterboxes span what they hold", "[scene][bounds]") {
    const toki::scene::Scene scene = makeScene();
    const std::vector<SceneNode> path{toki::scene::PathRef{}};
    REQUIRE(toki::scene::findNodeBounds(scene, path) ==
            NodeBounds{.min = {0.0F, -50.0F, 0.0F}, .max = {100.0F, 0.0F, 200.0F}});
    const std::vector<SceneNode> polygon{toki::scene::CollisionPolygonRef{}};
    REQUIRE(toki::scene::findNodeBounds(scene, polygon) ==
            NodeBounds{.min = {-10.0F, 0.0F, -10.0F}, .max = {10.0F, 5.0F, 40.0F}});
    const std::vector<SceneNode> both{toki::scene::CollisionPolygonRef{},
                                      toki::scene::WaterBoxRef{}};
    REQUIRE(toki::scene::findNodeBounds(scene, both) ==
            NodeBounds{.min = {-100.0F, 0.0F, -10.0F}, .max = {100.0F, 15.0F, 70.0F}});
}

TEST_CASE("what stands nowhere has no bounds", "[scene][bounds]") {
    const std::vector<SceneNode> nodes{toki::scene::LightSettingRef{}, toki::scene::RoomRef{}};
    REQUIRE_FALSE(toki::scene::findNodeBounds(makeScene(), nodes));
}
