#include "app/scene/domain/ActorDescription.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "app/scene/domain/Scene.h"
#include "app/scene/domain/SceneActors.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Tables.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

using base::s16;

namespace {

[[nodiscard]] toki::scene::SceneActorModel makeRoofMan() {
    toki::scene::SceneActorModel actor{.enumName = "ACTOR_EN_ANI",
                                       .source = toki::scene::ModelSource::Shipped};
    actor.entry = toki::actor::ActorEntry{
        .actor = "ACTOR_EN_ANI",
        .name = "Kakariko roof man",
        .object = "object_ani",
        .params = {toki::actor::ActorParam{.name = "Type", .mask = 0x000F}},
        .variants = {toki::actor::ActorVariant{
            .match = {toki::actor::ParamMatch{.param = "Type", .values = {base::s64{0}}}},
            .parts = {toki::actor::ActorPart{.kind = toki::actor::PartKind::Skeleton,
                                             .symbol = "gRoofManSkel"}}}},
    };
    actor.variants.push_back(
        toki::scene::ActorDrawing{.skeletons = {toki::scene::SkeletonDrawing{
                                      .pose = {.drawn = {toki::object::PosedLimb{}}}}}});
    return actor;
}

} // namespace

TEST_CASE("an actor is described by where its entry comes from and what its variant draws",
          "[scene][actors]") {
    const toki::scene::SceneActorModel actor = makeRoofMan();
    const std::array<std::string, 2> loaded{"gameplay_keep", "object_kanban"};

    const toki::scene::ActorDescription description =
        toki::scene::describeActor(actor, {.params = 0x0000}, std::span<const std::string>(loaded));

    REQUIRE(description.isDrawn);
    REQUIRE(description.name == "Kakariko roof man");
    REQUIRE(description.params.size() == 1);
    REQUIRE(description.params[0].text == "0");
    REQUIRE(description.canAddVariant);
    REQUIRE(description.lines[0] == "From the shipped actor database");
    REQUIRE(std::ranges::contains(description.lines,
                                  std::string("Skeleton gRoofManSkel in object_ani")));
    REQUIRE(std::ranges::contains(description.lines, std::string("At rest")));
    REQUIRE(description.problems.size() == 1);
    REQUIRE(description.problems[0].starts_with("object_ani is not in the room's object list"));
}

TEST_CASE("an actor whose params no variant matches is said so", "[scene][actors]") {
    const toki::scene::SceneActorModel actor = makeRoofMan();

    const toki::scene::ActorDescription description =
        toki::scene::describeActor(actor, {.params = 0x0003}, std::nullopt);

    REQUIRE_FALSE(description.isDrawn);
    REQUIRE(description.problems.front() == "no variant of its entry matches its params 0x0003");
}

TEST_CASE("a room has the keep objects and its object list loaded", "[scene][actors]") {
    hackeroot::Tables tables;
    tables.objects = {hackeroot::ObjectEntry{.id = 1, .name = "gameplay_keep"},
                      hackeroot::ObjectEntry{.id = 2, .name = "gameplay_field_keep"},
                      hackeroot::ObjectEntry{.id = 7, .name = "object_kanban"}};
    toki::scene::SceneLayer layer;
    layer.header.keepObjectId = 2;
    layer.rooms.push_back(toki::scene::RoomHeader{.objects = {s16{7}, s16{40}}});

    const std::vector<std::string> loaded = toki::scene::listLoadedObjects(layer, 0, tables);

    REQUIRE(loaded ==
            std::vector<std::string>{"gameplay_keep", "gameplay_field_keep", "object_kanban"});
}

TEST_CASE("Link's age is the scene layer's, and cutscene layers have none", "[scene][actors]") {
    REQUIRE(toki::scene::findLayerAge(1) == toki::actor::Age::Child);
    REQUIRE(toki::scene::findLayerAge(2) == toki::actor::Age::Adult);
    REQUIRE_FALSE(toki::scene::findLayerAge(4).has_value());
}
