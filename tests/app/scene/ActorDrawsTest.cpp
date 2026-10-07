#include "app/scene/domain/ActorDraws.h"

#include "app/actor/domain/ActorModel.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float4.hpp>

#include <array>
#include <memory>
#include <vector>

using base::s16;
using base::u32;
using base::usize;

TEST_CASE("an actor's matrix turns, raises and scales it as Actor_Draw does", "[scene][actors]") {
    const toki::scene::PlacedActor actor{
        .id = 1, .position = {100, 0, 0}, .rotation = {0, 0x4000, 0}};

    const glm::mat4 matrix = toki::scene::makeActorMatrix(actor, 0.5F, 100.0F);
    const glm::vec4 moved = matrix * glm::vec4(0.0F, 0.0F, 10.0F, 1.0F);

    REQUIRE(moved.x == Catch::Approx(105.0F));
    REQUIRE(moved.y == Catch::Approx(50.0F));
    REQUIRE(moved.z == Catch::Approx(0.0F).margin(1e-4));
}

TEST_CASE("placed actors with a drawing are drawn with their object and segments, the rest "
          "are left to be marked",
          "[scene][actors]") {
    toki::scene::SceneActors actors;
    actors.objects.push_back(toki::scene::ActorObject{
        .name = "object_test", .segment = 6, .image = std::make_shared<const z64::MemoryImage>()});
    toki::scene::SceneActorModel model;
    model.model.displayLists.push_back(toki::actor::DisplayListDraw{});
    model.drawing = toki::scene::ActorDrawing{
        .displayLists = {toki::scene::ActorDisplayList{
            .object = 0,
            .address = 0x0600'0100,
            .pass = toki::actor::Pass::Translucent,
            .segments = {toki::scene::ActorSegment{.segment = 0x08,
                                                   .pass = toki::actor::Pass::Translucent,
                                                   .object = 0,
                                                   .address = 0x0600'0800}}}}};
    actors.models.emplace(s16{7}, model);
    const std::array placed{toki::scene::PlacedActor{.id = 7}, toki::scene::PlacedActor{.id = 9}};

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 3,
        toki::scene::ActorSetup{.setupDisplayList = 0x0000'0200, .emptyDisplayList = 0x0000'0E50});

    REQUIRE(draws.images.size() == 2);
    REQUIRE(draws.opaque.empty());
    REQUIRE(draws.translucent.size() == 1);
    REQUIRE(draws.undrawn == std::vector<usize>{1});
    const toki::viewport::ViewportDraw& draw = draws.translucent.front();
    REQUIRE(draw.image == 4);
    REQUIRE(draw.alsoImages == std::vector<usize>{3});
    REQUIRE(draw.pointedSegments.size() == 5);
    REQUIRE(draw.pointedSegments[0].segment == 0x08);
    REQUIRE(draw.pointedSegments[0].image == 3);
    REQUIRE(draw.pointedSegments[0].address == 0x0600'0800);
    REQUIRE(draw.pointedSegments[1].image == 0);
    REQUIRE(draw.pointedSegments[1].address == 0x0000'0E50);
}
