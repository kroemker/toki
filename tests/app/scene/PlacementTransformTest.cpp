#include "app/scene/domain/PlacementTransform.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <variant>

using base::s16;
using toki::scene::ActorPlacement;
using toki::scene::Placement;
using toki::scene::TransitionActorPlacement;
using toki::viewport::TransformKind;

TEST_CASE("a transform moves a placement to its point and turns it as the result says",
          "[scene][transform]") {
    const Placement actor = ActorPlacement{.id = 5, .rotation = {0, 0x1000, 0}};
    const Placement moved =
        toki::scene::makeTransformedPlacement(actor, {10.4F, -3.6F, 70000.0F}, {});
    REQUIRE(std::get<ActorPlacement>(moved).position == std::array<s16, 3>{10, -4, 32767});
    REQUIRE(std::get<ActorPlacement>(moved).rotation[1] == 0x1000);

    const Placement turned = toki::scene::makeTransformedPlacement(
        actor, {}, {.kind = TransformKind::Rotate, .axis = 1, .degrees = 90.0F});
    REQUIRE(std::get<ActorPlacement>(turned).rotation[1] == 0x5000);

    const Placement door = TransitionActorPlacement{.rotationY = 0};
    const Placement tipped = toki::scene::makeTransformedPlacement(
        door, {}, {.kind = TransformKind::Rotate, .axis = 0, .degrees = 90.0F});
    REQUIRE(std::get<TransitionActorPlacement>(tipped).rotationY == 0);
}

TEST_CASE("a preview puts a placement into a copy of the layer", "[scene][transform]") {
    toki::scene::SceneLayer layer;
    layer.rooms.resize(1);
    layer.rooms[0].actors.resize(2);
    const Placement placed = ActorPlacement{.id = 9};
    REQUIRE(toki::scene::setLayerPlacement(layer, {.room = 0, .index = 1}, placed));
    REQUIRE(layer.rooms[0].actors[1].id == 9);
    REQUIRE_FALSE(toki::scene::setLayerPlacement(layer, {.room = 1, .index = 0}, placed));
    REQUIRE_FALSE(toki::scene::setLayerPlacement(
        layer, {.kind = toki::scene::PlacementKind::TransitionActor}, placed));
}
