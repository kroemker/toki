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
#include <cstddef>
#include <memory>
#include <optional>
#include <variant>
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
            .state = {.segments = {toki::scene::ActorSegment{
                          .segment = 0x08,
                          .target = toki::scene::ObjectAddress{.address = 0x0600'0800}}}}}}};
}

/// A pot of two kinds by its params' bit 8: opaque, or translucent.
[[nodiscard]] toki::scene::SceneActors makePots() {
    toki::scene::SceneActors actors;
    actors.objects.push_back(toki::scene::ActorObject{
        .name = "object_pot", .segment = 6, .image = std::make_shared<const z64::MemoryImage>()});
    toki::scene::SceneActorModel pot{.enumName = "ACTOR_OBJ_TSUBO"};
    pot.entry = toki::actor::ActorEntry{
        .actor = "ACTOR_OBJ_TSUBO",
        .params = {toki::actor::ActorParam{.name = "Kind", .mask = 0x0100}},
        .variants = {
            toki::actor::ActorVariant{
                .match = {toki::actor::ParamMatch{.param = "Kind", .values = {s64{0}}}}},
            toki::actor::ActorVariant{
                .match = {toki::actor::ParamMatch{.param = "Kind", .values = {s64{1}}}}},
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

TEST_CASE("an actor's children are drawn beside it, turned with it", "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::ActorDrawing ring{
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
    REQUIRE(draws.opaque.empty());
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

TEST_CASE("a skeleton is drawn by its limbs' matrices after its setup display list",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    const toki::scene::SkeletonDrawing skeleton{
        .object = 0,
        .pose = {.drawn = {toki::object::PosedLimb{.limb = 0, .displayList = 0x0600'0200}},
                 .matrices = {glm::mat4(1.0F)}},
        .state = {.setup = 44, .placement = glm::scale(glm::mat4(1.0F), glm::vec3(2.0F))}};
    toki::scene::SceneActorModel chest{.enumName = "ACTOR_EN_BOX"};
    chest.entry =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_BOX", .variants = {toki::actor::ActorVariant{}}};
    chest.variants = {toki::scene::ActorDrawing{.skeletons = {skeleton}, .scale = 1.0F}};
    actors.models.emplace(s16{4}, chest);
    const std::array placed{toki::scene::PlacedActor{.id = 4, .position = {100, 0, 0}}};
    std::vector<u32> setups(45);
    for (usize i = 0; i < setups.size(); ++i) {
        setups[i] = static_cast<u32>(0x0000'1000 + (i * 0x30));
    }

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 0, toki::scene::ActorSetup{.setupDisplayLists = setups});

    REQUIRE(draws.opaque.size() == 1);
    REQUIRE(draws.translucent.empty());
    const std::vector<u8>& bytes = draws.images.back()->segments.front().bytes;
    const auto readWord = [&bytes](const u32 address) {
        const usize at = address & 0x00FF'FFFFU;
        return (u32{bytes[at]} << 24) | (u32{bytes[at + 1]} << 16) | (u32{bytes[at + 2]} << 8) |
               u32{bytes[at + 3]};
    };
    REQUIRE(readWord(draws.opaque.front().displayList + 4) == setups[44]);
}

TEST_CASE("a built segment points at a list of its colours and tile sizes in the actor's image",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::ActorDrawing drawing = makeListDrawing(toki::actor::Pass::Opaque);
    drawing.displayLists.front().state.segments.push_back(toki::scene::ActorSegment{
        .segment = 0x09,
        .target = toki::actor::BuiltList{
            .colors = {.environment = std::array<u8, 4>{10, 20, 30, 255}},
            .tiles = {toki::actor::TileSize{.tile = 1, .x = 8, .width = 32, .height = 16}}}});
    toki::scene::SceneActorModel kokiri{.enumName = "ACTOR_EN_KO"};
    kokiri.entry =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_KO", .variants = {toki::actor::ActorVariant{}}};
    kokiri.variants = {drawing};
    actors.models.emplace(s16{5}, kokiri);
    const std::array placed{toki::scene::PlacedActor{.id = 5}};

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 0, toki::scene::ActorSetup{.setupDisplayLists = std::vector<u32>(26)});

    REQUIRE(draws.opaque.size() == 1);
    const toki::viewport::ViewportDraw& draw = draws.opaque.front();
    REQUIRE(draw.pointedSegments.size() == 2);
    const toki::viewport::SegmentPointer& built = draw.pointedSegments[1];
    REQUIRE(built.segment == 0x09);
    REQUIRE(built.image == draw.image);
    const std::vector<u8>& bytes = draws.images.back()->segments.front().bytes;
    const usize at = built.address & 0x00FF'FFFFU;
    REQUIRE(std::vector<u8>(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                            bytes.begin() + static_cast<std::ptrdiff_t>(at + 32)) ==
            std::vector<u8>{0xFB, 0x00, 0x00, 0x00, 0x0A, 0x14, 0x1E, 0xFF, 0xE8, 0x00, 0x00,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0xF2, 0x00, 0x80, 0x00, 0x01, 0x08,
                            0x40, 0x3C, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    REQUIRE(at + 32 <= (draw.displayList & 0x00FF'FFFFU));
}

TEST_CASE("a limb drawn as nothing is skipped, and one from another object is drawn on its own",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    actors.objects.push_back(toki::scene::ActorObject{
        .name = "object_head", .segment = 6, .image = std::make_shared<const z64::MemoryImage>()});
    const toki::scene::SkeletonDrawing skeleton{
        .object = 0,
        .pose = {.drawn = {toki::object::PosedLimb{.limb = 0, .displayList = 0x0600'0200},
                           toki::object::PosedLimb{.limb = 1, .displayList = 0},
                           toki::object::PosedLimb{.limb = 2, .displayList = 0x0600'0400}},
                 .matrices = {glm::mat4(1.0F), glm::mat4(1.0F), glm::mat4(1.0F)}},
        .limbObjects = {toki::scene::LimbObject{.drawn = 2, .object = 1}}};
    toki::scene::SceneActorModel kokiri{.enumName = "ACTOR_EN_KO"};
    kokiri.entry =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_KO", .variants = {toki::actor::ActorVariant{}}};
    kokiri.variants = {toki::scene::ActorDrawing{.skeletons = {skeleton}, .scale = 1.0F}};
    actors.models.emplace(s16{5}, kokiri);
    const std::array placed{toki::scene::PlacedActor{.id = 5}};

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 0, toki::scene::ActorSetup{.setupDisplayLists = std::vector<u32>(26)});

    REQUIRE(draws.opaque.size() == 2);
    REQUIRE(draws.opaque[0].alsoImages == std::vector<usize>{0});
    REQUIRE(draws.opaque[1].alsoImages == std::vector<usize>{1});
    const std::vector<u8>& bytes = draws.images.back()->segments.front().bytes;
    const auto listCalls = [&bytes](const u32 start) {
        std::vector<u32> calls;
        for (usize at = start & 0x00FF'FFFFU; at + 8 <= bytes.size(); at += 8) {
            const u32 word0 = (u32{bytes[at]} << 24) | (u32{bytes[at + 1]} << 16) |
                              (u32{bytes[at + 2]} << 8) | u32{bytes[at + 3]};
            const u32 word1 = (u32{bytes[at + 4]} << 24) | (u32{bytes[at + 5]} << 16) |
                              (u32{bytes[at + 6]} << 8) | u32{bytes[at + 7]};
            if (word0 >> 24 == 0xDF) {
                break;
            }
            if (word0 >> 24 == 0xDE) {
                calls.push_back(word1);
            }
        }
        return calls;
    };
    REQUIRE(listCalls(draws.opaque[0].displayList) == std::vector<u32>{0x0000'0000, 0x0600'0200});
    REQUIRE(listCalls(draws.opaque[1].displayList) == std::vector<u32>{0x0000'0000, 0x0600'0400});
}

TEST_CASE("a placement's drawing has every adjustment that matches it made, in order",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::SceneActorModel& pot = actors.models.at(7);
    pot.variants[0].displayLists.push_back(
        toki::scene::ActorDisplayList{.part = 1, .object = 0, .address = 0x0600'0300, .state = {}});
    pot.entry->params.push_back(toki::actor::ActorParam{.name = "Big", .mask = 0x0001});
    pot.entry->adjustments = {
        toki::actor::ActorAdjustment{
            .match = {toki::actor::ParamMatch{.param = "Big", .values = {s64{1}}}}},
        toki::actor::ActorAdjustment{},
    };
    pot.adjustments = {
        toki::scene::AdjustmentDrawing{.scale = 0.5F,
                                       .colors = {.environment = std::array<u8, 4>{1, 2, 3, 4}}},
        toki::scene::AdjustmentDrawing{
            .heightOffset = 7.0F,
            .colors = {.primitive = std::array<u8, 4>{9, 9, 9, 9}},
            .segments = {toki::scene::ActorSegment{
                .segment = 0x08,
                .target = toki::actor::BuiltList{.colors = {.environment =
                                                                std::array<u8, 4>{5, 6, 7, 8}}}}},
            .parts = {1}},
    };

    const std::optional<toki::scene::ActorDrawing> big = pot.makeDrawing({.params = 0x0001});
    const std::optional<toki::scene::ActorDrawing> small = pot.makeDrawing({.params = 0x0000});

    REQUIRE(big.has_value());
    REQUIRE(big->scale == Catch::Approx(0.5F));
    REQUIRE(big->heightOffset == Catch::Approx(7.0F));
    const toki::scene::DrawState& first = big->displayLists[0].state;
    const toki::scene::DrawState& second = big->displayLists[1].state;
    REQUIRE(first.colors.environment == std::array<u8, 4>{1, 2, 3, 4});
    REQUIRE_FALSE(first.colors.primitive.has_value());
    REQUIRE(first.segments.size() == 1);
    REQUIRE(std::holds_alternative<toki::scene::ObjectAddress>(first.segments[0].target));
    REQUIRE(second.colors.primitive == std::array<u8, 4>{9, 9, 9, 9});
    REQUIRE(second.segments.size() == 1);
    REQUIRE(std::holds_alternative<toki::actor::BuiltList>(second.segments[0].target));
    REQUIRE(small.has_value());
    REQUIRE(small->scale == Catch::Approx(actors.models.at(7).variants[0].scale));
    REQUIRE(small->heightOffset == Catch::Approx(7.0F));
}

TEST_CASE("an item picked by a param is drawn as the item of the placement's value",
          "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::SceneActorModel& pot = actors.models.at(7);
    const toki::actor::ActorParam item{.name = "Item", .mask = 0x00F0};
    pot.variants[0].items.push_back(toki::scene::ItemLists{
        .param = item,
        .byValue = {{2, {toki::scene::ActorDisplayList{.address = 0x0600'0A00}}},
                    {3,
                     {toki::scene::ActorDisplayList{.address = 0x0600'0B00},
                      toki::scene::ActorDisplayList{.address = 0x0600'0C00}}}}});

    const std::optional<toki::scene::ActorDrawing> second = pot.makeDrawing({.params = 0x0020});
    const std::optional<toki::scene::ActorDrawing> third = pot.makeDrawing({.params = 0x0030});
    const std::optional<toki::scene::ActorDrawing> none = pot.makeDrawing({.params = 0x0050});

    REQUIRE(second->displayLists.size() == 2);
    REQUIRE(second->displayLists[1].address == 0x0600'0A00);
    REQUIRE(third->displayLists.size() == 3);
    REQUIRE(third->displayLists[2].address == 0x0600'0C00);
    REQUIRE(none->displayLists.size() == 1);
    REQUIRE(second->items.empty());
}

TEST_CASE("a child takes its parent's param into its own, and is drawn by it", "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::SceneActorModel ring{.enumName = "ACTOR_OBJ_MURE2"};
    const toki::actor::ActorParam big{.name = "Big", .mask = 0x0001};
    ring.entry = toki::actor::ActorEntry{
        .actor = "ACTOR_OBJ_MURE2", .params = {big}, .variants = {toki::actor::ActorVariant{}}};
    ring.variants = {toki::scene::ActorDrawing{
        .kind = toki::actor::DrawKind::Children,
        .children = {toki::scene::ChildPlacement{
            .id = 7,
            .takes = {toki::scene::TakenParam{.from = big,
                                              .to = actors.models.at(7).entry->params.front()}}}}}};
    actors.models.emplace(s16{3}, ring);
    const std::array placed{toki::scene::PlacedActor{.id = 3, .params = 0x0001},
                            toki::scene::PlacedActor{.id = 3, .params = 0x0000}};

    const toki::scene::ActorDraws draws = toki::scene::makeActorDraws(
        actors, placed, 0, toki::scene::ActorSetup{.setupDisplayLists = std::vector<u32>(26)});

    REQUIRE(draws.translucent.size() == 1);
    REQUIRE(draws.opaque.size() == 1);
}

TEST_CASE("a hilit display list looks at its actor from the camera", "[scene][actors]") {
    toki::scene::SceneActors actors = makePots();
    toki::scene::ActorDrawing rupee = makeListDrawing(toki::actor::Pass::Opaque);
    rupee.displayLists.front().state.isHilit = true;
    toki::scene::SceneActorModel item{.enumName = "ACTOR_EN_ITEM00"};
    item.entry = toki::actor::ActorEntry{.actor = "ACTOR_EN_ITEM00",
                                         .variants = {toki::actor::ActorVariant{}}};
    item.variants = {rupee};
    actors.models.emplace(s16{5}, item);
    const std::array placed{toki::scene::PlacedActor{.id = 5, .position = {10, 20, 30}},
                            toki::scene::PlacedActor{.id = 7}};

    const toki::scene::ActorDraws draws =
        toki::scene::makeActorDraws(actors, placed, 0, toki::scene::ActorSetup{});

    REQUIRE(draws.opaque.size() == 2);
    REQUIRE(draws.opaque[0].lookAtTarget == glm::vec3(10.0F, 20.0F, 30.0F));
    REQUIRE_FALSE(draws.opaque[1].lookAtTarget.has_value());
}
