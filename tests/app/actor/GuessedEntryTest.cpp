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

    const toki::actor::ActorEntry entry = toki::actor::makeGuessedEntry(
        model, "ACTOR_EN_KO", toki::actor::GuessSource{.file = "z_en_ko.c", .modified = "now"});

    REQUIRE(entry.status == toki::actor::EntryStatus::Guessed);
    REQUIRE(entry.guessedFrom->file == "z_en_ko.c");
    REQUIRE(entry.object == "object_km1");
    REQUIRE(entry.scale == Catch::Approx(0.02F));
    REQUIRE(entry.variants.size() == 1);
    const toki::actor::ActorVariant& variant = entry.variants.front();
    REQUIRE(variant.object == "object_kw1");
    REQUIRE(variant.skeleton == "gKw1Skel");
    REQUIRE(variant.animation == "gObjOsAnim_Idle");
    REQUIRE(variant.animationObject == "object_os_anime");
    REQUIRE(variant.segments.size() == 1);
    REQUIRE(variant.segments.front().name == "gEyeTex");
}

TEST_CASE("guessed display lists keep their passes and segments, and no model is no variant",
          "[actor][guess]") {
    toki::actor::ActorModel lists{.actorName = "Obj_Test", .profileObject = "object_test"};
    lists.displayLists = {toki::actor::DisplayListDraw{
        .displayList = {.name = "gGlassDL", .object = "gameplay_keep"},
        .pass = toki::actor::Pass::Translucent,
        .segments = {toki::actor::SegmentSetting{
            .segment = 8, .pass = toki::actor::Pass::Translucent, .symbol = {.name = "gTex"}}}}};

    const toki::actor::ActorEntry entry =
        toki::actor::makeGuessedEntry(lists, "ACTOR_OBJ_TEST", std::nullopt);
    const toki::actor::ActorEntry empty = toki::actor::makeGuessedEntry(
        toki::actor::ActorModel{.actorName = "En_None"}, "ACTOR_EN_NONE", std::nullopt);

    const toki::actor::DisplayListPart& part = entry.variants.front().displayLists.front();
    REQUIRE(part.object == "gameplay_keep");
    REQUIRE(part.pass == toki::actor::Pass::Translucent);
    REQUIRE(part.segments.front().name == "gTex");
    REQUIRE_FALSE(entry.scale.has_value());
    REQUIRE(empty.variants.empty());
}
