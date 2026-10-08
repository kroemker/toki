#include "app/scene/domain/ActorDraws.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/scene/domain/SceneActors.h"
#include "app/viewport/domain/ViewportFrame.h"
#include "base/Types.h"
#include "z64/MemoryImage.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <array>
#include <memory>
#include <vector>

using base::s16;
using base::s64;
using base::u32;
using base::u8;
using base::usize;

namespace {

[[nodiscard]] toki::scene::ActorDrawing makeListDrawing(const toki::actor::Pass pass) {
    return toki::scene::ActorDrawing{
        .displayLists = {toki::scene::ActorDisplayList{
            .object = 0,
            .address = 0x0600'0100,
            .pass = pass,
            .state = {.segments = {
                          toki::scene::ActorSegment{.segment = 0x08, .address = 0x0600'0800}}}}}};
}

/// A pot of two kinds by its params' bit 8: opaque, or translucent.
[[nodiscard]] toki::scene::SceneActors makePots() {
    toki::scene::SceneActors actors;
    actors.objects.push_back(toki::scene::ActorObject{
        .name = "object_pot", .segment = 6, .image = std::make_shared<const z64::MemoryImage>()});
    toki::scene::SceneActorModel pot{.enumName = "ACTOR_OBJ_TSUBO"};
    pot.entry = toki::actor::ActorEntry{
        .actor = "ACTOR_OBJ_TSUBO",
        .fields = {toki::actor::ParamField{
            .name = "kind", .kind = toki::actor::FieldKind::Integer, .shift = 8, .width = 1}},
        .variants = {
            toki::actor::ActorVariant{
                .match = {toki::actor::FieldMatch{.field = "kind", .values = {s64{0}}}}},
            toki::actor::ActorVariant{
                .match = {toki::actor::FieldMatch{.field = "kind", .values = {s64{1}}}}},
        }};
    pot.variants = {makeListDrawing(toki::actor::Pass::Opaque),
                    makeListDrawing(toki::actor::Pass::Translucent)};
    actors.models.emplace(s16{7}, pot);
    return actors;
}

} // namespace

TEST_CASE("an actor's matrix turns, raises and scales it as Actor_Draw does", "[scene][actors]") {
    const toki::scene::PlacedActor actor{
        .id = 1, .position = {100, 0, 0}, .rotation = {0, 0x4000, 0}};

    const glm::mat4 matrix = toki::scene::makeActorMatrix(actor, 0.5F, 100.0F);
    const glm::vec4 moved = matrix * glm::vec4(0.0F, 0.0F, 10.0F, 1.0F);

    REQUIRE(moved.x == Catch::Approx(105.0F));
    REQUIRE(moved.y == Catch::Approx(50.0F));
    REQUIRE(moved.z == Catch::Approx(0.0F).margin(1e-4));

    const glm::mat4 unturned =
        toki::scene::makeActorMatrix(actor, 0.5F, 100.0F, {.x = true, .y = false, .z = true});
    REQUIRE((unturned * glm::vec4(0.0F, 0.0F, 10.0F, 1.0F)).z == Catch::Approx(5.0F));
}

TEST_CASE("each placed actor is drawn by the variant its params match, and the rest are marked",
          "[scene][actors]") {
    const toki::scene::SceneActors actors = makePots();
    const std::array placed{toki::scene::PlacedActor{.id = 7, .params = 0x0000},
                            toki::scene::PlacedActor{.id = 7, .params = 0x0100},
                            toki::scene::PlacedActor{.id = 9}};

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 3,
        toki::scene::ActorSetup{.setupDisplayLists = std::vector<u32>(26, 0x0000'0200),
                                .emptyDisplayList = 0x0000'0E50});

    REQUIRE(draws.images.size() == 3);
    REQUIRE(draws.opaque.size() == 1);
    REQUIRE(draws.translucent.size() == 1);
    REQUIRE(draws.undrawn == std::vector<usize>{2});
    const toki::viewport::ViewportDraw& draw = draws.translucent.front();
    REQUIRE(draw.image == 5);
    REQUIRE(draw.alsoImages == std::vector<usize>{3});
    REQUIRE(draw.modelView.has_value());
    REQUIRE(draw.pointedSegments.size() == 5);
    REQUIRE(draw.pointedSegments[0].image == 3);
    REQUIRE(draw.pointedSegments[0].address == 0x0600'0800);
    REQUIRE(draw.pointedSegments[1].image == 0);
    REQUIRE(draw.pointedSegments[1].address == 0x0000'0E50);
}

TEST_CASE("an actor's shadow is drawn under it and its children beside it", "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::ActorDrawing ring{
        .shadow = toki::scene::ActorShadow{.object = 0, .address = 0x0400'0000, .size = 30.0F},
        .children = {toki::scene::ChildPlacement{.id = 7, .params = 0x0100, .offset = {0, 0, 80}}}};
    toki::scene::SceneActorModel parent{.enumName = "ACTOR_OBJ_MURE2"};
    parent.entry = toki::actor::ActorEntry{.actor = "ACTOR_OBJ_MURE2",
                                           .variants = {toki::actor::ActorVariant{}}};
    parent.variants = {ring};
    actors.models.emplace(s16{8}, parent);
    const std::array placed{
        toki::scene::PlacedActor{.id = 8, .position = {100, 0, 0}, .rotation = {0, 0x4000, 0}}};

    const toki::scene::ActorDraws draws =
        toki::scene::makeActorDraws(actors, placed, 0, toki::scene::ActorSetup{});

    REQUIRE(draws.undrawn.empty());
    REQUIRE(draws.opaque.size() == 1);
    REQUIRE((*draws.opaque.front().modelView)[0].x == Catch::Approx(0.3F));
    REQUIRE((*draws.opaque.front().modelView)[1].y == Catch::Approx(1.0F));
    REQUIRE(draws.translucent.size() == 1);
    const glm::vec4 child = *draws.translucent.front().modelView * glm::vec4(0, 0, 0, 1);
    REQUIRE(child.x == Catch::Approx(180.0F));
    REQUIRE(child.z == Catch::Approx(0.0F).margin(1e-3));
}

TEST_CASE("a billboard faces the camera and keeps its place and size", "[scene][actors]") {
    const glm::mat4 view = glm::rotate(glm::mat4(1.0F), 0.7F, glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 model =
        glm::scale(glm::translate(glm::mat4(1.0F), glm::vec3(5.0F, 6.0F, 7.0F)), glm::vec3(2.0F));

    const glm::mat4 billboard = toki::viewport::makeBillboardMatrix(model, view);
    const glm::mat4 seen = view * billboard;

    REQUIRE(billboard[3].x == Catch::Approx(5.0F));
    REQUIRE(billboard[3].z == Catch::Approx(7.0F));
    REQUIRE(seen[0].x == Catch::Approx(2.0F));
    REQUIRE(seen[0].z == Catch::Approx(0.0F).margin(1e-5));
    REQUIRE(seen[2].z == Catch::Approx(2.0F));
}

TEST_CASE("a skeleton is placed by its steps, and a limb's display list by the limb",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    const glm::mat4 limbMatrix = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 10.0F, 0.0F));
    toki::scene::SkeletonDrawing skeleton{
        .object = 0,
        .pose = {.drawn = {toki::object::PosedLimb{.limb = 0, .displayList = 0x0600'0200}},
                 .matrices = {glm::mat4(1.0F), limbMatrix}},
        .state = {.setup = 44, .placement = glm::scale(glm::mat4(1.0F), glm::vec3(2.0F))},
        .limbDisplayLists = {toki::scene::LimbDisplayList{
            .limb = 1,
            .displayList = {.object = 0,
                            .address = 0x0600'0300,
                            .pass = toki::actor::Pass::Translucent,
                            .state = {.placement = glm::translate(glm::mat4(1.0F),
                                                                  glm::vec3(0.0F, 0.0F, 5.0F))}}}}};
    toki::scene::SceneActorModel chest{.enumName = "ACTOR_EN_BOX"};
    chest.entry =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_BOX", .variants = {toki::actor::ActorVariant{}}};
    chest.variants = {toki::scene::ActorDrawing{
        .skeleton = skeleton,
        .scale = 1.0F,
        .shadow = toki::scene::ActorShadow{.color = {0, 0, 0, 180}, .size = 1.0F}}};
    actors.models.emplace(s16{4}, chest);
    const std::array placed{toki::scene::PlacedActor{.id = 4, .position = {100, 0, 0}}};
    std::vector<u32> setups(45);
    for (usize i = 0; i < setups.size(); ++i) {
        setups[i] = static_cast<u32>(0x0000'1000 + (i * 0x30));
    }

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 0, toki::scene::ActorSetup{.setupDisplayLists = setups});

    REQUIRE(draws.opaque.size() == 2);
    REQUIRE(draws.translucent.size() == 1);
    const glm::vec4 lid = *draws.translucent.front().modelView * glm::vec4(0, 0, 0, 1);
    REQUIRE(lid.x == Catch::Approx(100.0F));
    REQUIRE(lid.y == Catch::Approx(20.0F));
    REQUIRE(lid.z == Catch::Approx(10.0F));
    const std::vector<u8>& bytes = draws.images.back()->segments.front().bytes;
    const auto readWord = [&bytes](const u32 address) {
        const usize at = address & 0x00FF'FFFFU;
        return (u32{bytes[at]} << 24) | (u32{bytes[at + 1]} << 16) | (u32{bytes[at + 2]} << 8) |
               u32{bytes[at + 3]};
    };
    REQUIRE(readWord(draws.opaque.front().displayList + 4) == setups[44]);
}
