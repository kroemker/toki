#include "app/scene/domain/ActorDescription.h"

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

TEST_CASE("an actor is described by what it is drawn as and where each answer was read",
          "[scene][actors]") {
    toki::scene::SceneActorModel actor;
    actor.model.profileObject = "object_ani";
    actor.model.skeleton = toki::actor::SkeletonModel{
        .skeleton = {.name = "gRoofManSkel",
                     .object = "object_ani",
                     .location = {.file = "src/ovl_En_Ani/z_en_ani.c", .line = 88}},
        .isFlexible = true};
    actor.model.gaps.push_back(toki::actor::ModelGap{
        .kind = toki::actor::GapKind::ScaleNotLiteral,
        .diagnostic = base::makeNote({.file = "z_en_ani.c", .line = 90}, "the scale is not one")});
    actor.drawing = toki::scene::ActorDrawing{};
    const std::array<std::string, 2> loaded{"gameplay_keep", "object_kanban"};

    const toki::scene::ActorDescription description =
        toki::scene::describeActor(actor, std::span<const std::string>(loaded));

    REQUIRE(description.isDrawn);
    REQUIRE(description.lines.front() ==
            "Flexible skeleton gRoofManSkel in object_ani  (z_en_ani.c:88)");
    REQUIRE(std::ranges::contains(description.lines, std::string("At rest")));
    REQUIRE(std::ranges::contains(description.lines, std::string("Scale 0.01, the default")));
    REQUIRE(description.problems.size() == 2);
    REQUIRE(description.problems[0] == "the scale is not one  (z_en_ani.c:90)");
    REQUIRE(description.problems[1].starts_with("object_ani is not in the room's object list"));

    const toki::scene::ActorDescription unplaced = toki::scene::describeActor(actor, std::nullopt);

    REQUIRE(unplaced.problems.size() == 1);
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
