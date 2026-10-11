#include "app/actor/domain/GuessedEntry.h"

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"
#include "base/Diagnostic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

TEST_CASE("a guessed skeleton becomes an entry, naming the objects that are not the entry's",
          "[actor][guess]") {
    toki::actor::ActorModel model{.actorName = "En_Ko", .profileObject = "object_km1"};
    model.skeleton =
        toki::actor::SkeletonModel{.skeleton = {.name = "gKw1Skel", .object = "object_kw1"},
                                   .isFlexible = true,
                                   .animation = toki::actor::ObjectSymbol{
                                       .name = "gObjOsAnim_Idle", .object = "object_os_anime"}};
    model.segments = {toki::actor::SegmentSetting{.segment = 8, .symbol = {.name = "gEyeTex"}},
                      toki::actor::SegmentSetting{.segment = 9,
                                                  .pass = toki::actor::Pass::Translucent,
                                                  .symbol = {.name = "gGlowTex"}}};
    model.scale = 0.02F;
    model.scaleSource = base::SourceLocation{};

    const toki::actor::ActorEntry entry = toki::actor::makeGuessedEntry(model, "ACTOR_EN_KO");

    REQUIRE(entry.object == "object_km1");
    REQUIRE(entry.scale == Catch::Approx(0.02F));
    REQUIRE(entry.variants.size() == 1);
    const toki::actor::ActorPart& part = entry.variants.front().parts.front();
    REQUIRE(part.kind == toki::actor::PartKind::Skeleton);
    REQUIRE(part.object == "object_kw1");
    REQUIRE(part.symbol == "gKw1Skel");
    REQUIRE(part.pose == "gObjOsAnim_Idle");
    REQUIRE(part.poseObject == "object_os_anime");
    REQUIRE(part.segments.size() == 1);
    REQUIRE(part.segments.front().name == "gEyeTex");
}

TEST_CASE("guessed display lists keep their passes and segments, and no model is no variant",
          "[actor][guess]") {
    toki::actor::ActorModel lists{.actorName = "Obj_Test", .profileObject = "object_test"};
    lists.displayLists = {toki::actor::DisplayListDraw{
        .displayList = {.name = "gGlassDL", .object = "gameplay_keep"},
        .pass = toki::actor::Pass::Translucent,
        .segments = {toki::actor::SegmentSetting{
            .segment = 8, .pass = toki::actor::Pass::Translucent, .symbol = {.name = "gTex"}}}}};

    const toki::actor::ActorEntry entry = toki::actor::makeGuessedEntry(lists, "ACTOR_OBJ_TEST");
    const toki::actor::ActorEntry empty = toki::actor::makeGuessedEntry(
        toki::actor::ActorModel{.actorName = "En_None"}, "ACTOR_EN_NONE");

    const toki::actor::ActorPart& part = entry.variants.front().parts.front();
    REQUIRE(part.object == "gameplay_keep");
    REQUIRE(part.pass == toki::actor::Pass::Translucent);
    REQUIRE(part.segments.front().name == "gTex");
    REQUIRE_FALSE(entry.scale.has_value());
    REQUIRE(empty.variants.empty());
}

TEST_CASE("display lists picked out of a table are a variant for each entry", "[actor][guess]") {
    toki::actor::ActorModel model{.actorName = "Bg_Test", .profileObject = "object_test"};
    model.params = {toki::actor::ActorParam{.name = "Type", .mask = 0x0030}};
    model.displayListTables = {toki::actor::DisplayListTable{
        .table = "sDLists",
        .mask = 0x0030,
        .offset = -1,
        .entries = {toki::actor::ObjectSymbol{.name = "gTestDL", .object = "object_test"},
                    std::nullopt}}};

    const toki::actor::ActorEntry entry = toki::actor::makeGuessedEntry(model, "ACTOR_BG_TEST");

    REQUIRE(entry.variants.size() == 2);
    REQUIRE(entry.variants[0].match.front().param == "Type");
    REQUIRE(entry.variants[0].match.front().values.front() ==
            toki::actor::MatchValue(base::s64{1}));
    REQUIRE(entry.variants[0].parts.front().symbol == "gTestDL");
    REQUIRE_FALSE(entry.variants[0].parts.front().object.has_value());
    REQUIRE(entry.variants[1].match.front().values.front() ==
            toki::actor::MatchValue(base::s64{2}));
    REQUIRE(entry.variants[1].parts.empty());
}
