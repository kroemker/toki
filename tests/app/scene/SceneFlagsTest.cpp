#include "app/scene/domain/SceneFlags.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "app/scene/domain/SceneHeader.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

constexpr base::s16 SwitchId = 1;
constexpr base::s16 DoorId = 2;

[[nodiscard]] toki::scene::SceneActors makeActors() {
    toki::scene::SceneActors actors;
    actors.models[SwitchId] = toki::scene::SceneActorModel{
        .enumName = "ACTOR_OBJ_SWITCH",
        .entry = toki::actor::ActorEntry{
            .actor = "ACTOR_OBJ_SWITCH",
            .params = {toki::actor::ActorParam{.name = "Switch Flag",
                                               .mask = 0x3F00,
                                               .sceneFlag = toki::actor::SceneFlag::Switch,
                                               .flagUse = toki::actor::FlagUse::Sets}}}};
    actors.models[DoorId] = toki::scene::SceneActorModel{
        .enumName = "ACTOR_DOOR_SHUTTER",
        .entry = toki::actor::ActorEntry{
            .actor = "ACTOR_DOOR_SHUTTER",
            .params = {toki::actor::ActorParam{.name = "Switch Flag",
                                               .mask = 0x003F,
                                               .sceneFlag = toki::actor::SceneFlag::Switch}}}};
    return actors;
}

} // namespace

TEST_CASE("the actors of a layer that use one scene flag are listed together", "[scene][flags]") {
    toki::scene::SceneLayer layer;
    layer.rooms.push_back(toki::scene::RoomHeader{
        .actors = {toki::scene::ActorPlacement{.id = SwitchId, .params = 0x0500},
                   toki::scene::ActorPlacement{.id = SwitchId, .params = 0x2300},
                   toki::scene::ActorPlacement{.id = SwitchId, .params = 0x3F00}}});
    layer.header.transitionActors.push_back(
        toki::scene::TransitionActorPlacement{.id = DoorId, .params = 0x0005});

    const std::vector<toki::scene::SceneFlagUse> flags =
        toki::scene::listSceneFlags(layer, makeActors());

    REQUIRE(flags.size() == 2);
    REQUIRE(flags[0].number == 0x05);
    REQUIRE(flags[0].users.size() == 2);
    REQUIRE(flags[0].isSet());
    REQUIRE(flags[0].isRead());
    REQUIRE(flags[0].users[0].room == 0);
    REQUIRE_FALSE(flags[0].users[1].room.has_value());
    REQUIRE(flags[1].number == 0x23);
    REQUIRE_FALSE(flags[1].isRead());
}

TEST_CASE("switch and collectible flags from 0x20 are reset on leaving the scene",
          "[scene][flags]") {
    REQUIRE(toki::scene::isSavedSceneFlag(toki::actor::SceneFlag::Switch, 0x1F));
    REQUIRE_FALSE(toki::scene::isSavedSceneFlag(toki::actor::SceneFlag::Switch, 0x20));
    REQUIRE_FALSE(toki::scene::isSavedSceneFlag(toki::actor::SceneFlag::Collectible, 0x3E));
    REQUIRE(toki::scene::isSavedSceneFlag(toki::actor::SceneFlag::Treasure, 0x1E));

    const toki::actor::ActorParam wide{
        .name = "Flag", .mask = 0x007F, .sceneFlag = toki::actor::SceneFlag::Switch};
    REQUIRE(toki::scene::findFlagNumber(wide, {.params = 0x003F}) == 0x3F);
    REQUIRE_FALSE(toki::scene::findFlagNumber(wide, {.params = 0x0040}).has_value());
    REQUIRE_FALSE(toki::scene::findFlagNumber(wide, {.params = 0x007F}).has_value());
}

TEST_CASE("a flag links each actor that sets it to each that reads it", "[scene][flags]") {
    toki::scene::SceneFlagUse flag{.users = {{.use = toki::actor::FlagUse::Reads},
                                             {.use = toki::actor::FlagUse::Sets},
                                             {.use = toki::actor::FlagUse::Reads},
                                             {.use = toki::actor::FlagUse::Both}}};

    const std::vector<toki::scene::FlagLink> links = toki::scene::listFlagLinks(flag);

    REQUIRE(links.size() == 5);
    REQUIRE(links[0].setter == 1);
    REQUIRE(links[0].reader == 0);
    REQUIRE_FALSE(links[0].isMutual);
    REQUIRE(links[3].setter == 1);
    REQUIRE(links[3].reader == 3);
    REQUIRE(links[4].setter == 3);
    REQUIRE(links[4].reader == 2);
    const toki::scene::SceneFlagUse both{
        .users = {{.use = toki::actor::FlagUse::Both}, {.use = toki::actor::FlagUse::Both}}};
    REQUIRE(toki::scene::listFlagLinks(both).size() == 1);
    REQUIRE(toki::scene::listFlagLinks(both).front().isMutual);
}
