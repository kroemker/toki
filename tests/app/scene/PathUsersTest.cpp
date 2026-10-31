#include "app/scene/domain/PathUsers.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/domain/PlacementEdits.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneEditor.h"
#include "app/scene/domain/SceneHeader.h"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <variant>
#include <vector>

using base::s16;
using base::u32;
using base::usize;

namespace {

constexpr s16 WalkerId = 1;
constexpr s16 DoorId = 2;
constexpr u32 MainRoom = 0x03000000;
constexpr u32 AlternateRoom = 0x03000100;

[[nodiscard]] toki::actor::ActorParam makePathParam(const base::u16 mask) {
    return toki::actor::ActorParam{.name = "Path", .mask = mask, .isPath = true};
}

/// A scene of three paths in each of two layers: walkers in a room, by the
/// path in their high byte, and a door between rooms by the low nibble of its
/// params. Layer 1's room has a header of its own unless `isRoomShared`.
[[nodiscard]] toki::scene::Scene makeScene(const bool isRoomShared) {
    toki::scene::Scene scene;
    scene.actors.models[WalkerId] = toki::scene::SceneActorModel{
        .enumName = "ACTOR_EN_WALKER",
        .entry =
            toki::actor::ActorEntry{.actor = "ACTOR_EN_WALKER", .params = {makePathParam(0xFF00)}}};
    scene.actors.models[DoorId] = toki::scene::SceneActorModel{
        .enumName = "ACTOR_EN_DOOR",
        .entry =
            toki::actor::ActorEntry{.actor = "ACTOR_EN_DOOR", .params = {makePathParam(0x000F)}}};
    for (usize layer = 0; layer < 2; ++layer) {
        toki::scene::SceneLayer shown{.index = layer};
        shown.header.paths = {
            {.points = {{0, 0, 0}}}, {.points = {{1, 0, 0}}}, {.points = {{2, 0, 0}}}};
        shown.header.transitionActors = {
            toki::scene::TransitionActorPlacement{.id = DoorId, .params = 0x0072}};
        shown.rooms.push_back(toki::scene::RoomHeader{
            .address = layer == 0 || isRoomShared ? MainRoom : AlternateRoom,
            .actors = {toki::scene::ActorPlacement{.id = WalkerId, .params = 0x0012},
                       toki::scene::ActorPlacement{.id = WalkerId, .params = 0x0134},
                       toki::scene::ActorPlacement{.id = WalkerId, .params = 0x0256},
                       toki::scene::ActorPlacement{.id = WalkerId, .params = -1}}});
        scene.layers.push_back(std::move(shown));
    }
    return scene;
}

[[nodiscard]] s16 findParams(const toki::scene::PlacementChange& change) {
    return std::visit([](const auto& placed) { return placed.params; }, change.placement);
}

} // namespace

TEST_CASE("the actors that follow a layer's paths are listed by their path params",
          "[scene][paths]") {
    const toki::scene::Scene scene = makeScene(false);

    const std::vector<toki::scene::PathUser> users = toki::scene::listPathUsers(scene, 1);

    REQUIRE(users.size() == 4);
    REQUIRE(users[0].path == 0);
    REQUIRE(users[0].where.layer == 1);
    REQUIRE(users[0].param == "Path");
    REQUIRE(users[0].actorId == WalkerId);
    REQUIRE(users[2].path == 2);
    REQUIRE(users[3].where.kind == toki::scene::PlacementKind::TransitionActor);
    REQUIRE(users[3].path == 2);
}

TEST_CASE("a path param at its highest value or past the paths names none", "[scene][paths]") {
    toki::scene::Scene scene = makeScene(false);
    scene.layers[0].rooms[0].actors[0].params = 0x0712;

    const std::vector<toki::scene::PathUser> users = toki::scene::listPathUsers(scene, 0);

    REQUIRE(users.size() == 3);
    REQUIRE(users[0].path == 1);
    REQUIRE_FALSE(
        toki::scene::findPathNumber(toki::actor::ActorParam{.mask = 0xFF00}, {.params = 0x0100}));
}

TEST_CASE("removing a path renumbers the actors after it and leaves its own", "[scene][paths]") {
    const toki::scene::Scene scene = makeScene(false);

    const std::vector<toki::scene::PlacementChange> changes =
        toki::scene::listPathRemovalChanges(scene, 1, 1);

    REQUIRE(changes.size() == 2);
    REQUIRE(changes[0].list.kind == toki::scene::PlacementKind::Actor);
    REQUIRE(changes[0].list.layer == 1);
    REQUIRE(changes[0].index == 2);
    REQUIRE(findParams(changes[0]) == 0x0156);
    REQUIRE(changes[1].list.kind == toki::scene::PlacementKind::TransitionActor);
    REQUIRE(findParams(changes[1]) == 0x0071);
}

TEST_CASE("removing a path leaves the actors of a room header another layer reads",
          "[scene][paths]") {
    const toki::scene::Scene scene = makeScene(true);

    for (const usize layer : {0, 1}) {
        const std::vector<toki::scene::PlacementChange> changes =
            toki::scene::listPathRemovalChanges(scene, layer, 0);

        REQUIRE(changes.size() == 1);
        REQUIRE(changes[0].list.kind == toki::scene::PlacementKind::TransitionActor);
        REQUIRE(findParams(changes[0]) == 0x0071);
    }
}
