#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "base/Types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view Example = R"(format = 1

[actors.ACTOR_EN_BOX]
object = "object_box"
shadow = { kind = "circle", size = 6, alpha = 180 }

[actors.ACTOR_EN_BOX.fields]
type = { kind = "enum", shift = 12, width = 4, values = { big = 0, room_clear = 1, small = 5, boss = 11 } }

[[actors.ACTOR_EN_BOX.variants]]
match = { type = ["small", 6], scene = ["SCENE_DEKU_TREE", "SCENE_BOTTOM_OF_THE_WELL"] }
steps = [
    { setup = 25 },
    { colors = { env = [0, 0, 0, 255] } },
    { segment = 8, symbol = "gTreasureChestEyeTex" },
    { transform = { rotation = [0, 180, 0], scale = 0.005 } },
    { skeleton = "gTreasureChestSkel", animation = "gTreasureChestAnim_00024C", frame = 0, limbs = { 1 = [{ display_list = "gTreasureChestBaseDL" }], 3 = [{ display_list = "gTreasureChestLidDL", pass = "translucent" }] } },
    { transform = { translation = [0, 52, 0], billboard = true, reset = true } },
    { display_list = "gEffFire1DL", object = "gameplay_keep", pass = "translucent" },
    { shadow = { kind = "circle", size = 12, alpha = 153 } },
]

[[actors.ACTOR_EN_BOX.variants]]
match = { type = "big" }
display_lists = [{ name = "gTreasureChestDL" }]
)";

[[nodiscard]] std::string describe(const std::vector<base::Diagnostic>& diagnostics) {
    std::string described;
    for (const base::Diagnostic& diagnostic : diagnostics) {
        described += base::describe(diagnostic) + "\n";
    }
    return described;
}

[[nodiscard]] bool isReported(const toki::actor::ActorDatabase& database,
                              const std::string_view text) {
    return std::ranges::any_of(database.diagnostics, [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    });
}

} // namespace

TEST_CASE("a variant's steps are read in order, limbs and all", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    INFO(describe(database.diagnostics));
    REQUIRE(database.diagnostics.empty());
    const toki::actor::ActorEntry& box = database.entries.at("ACTOR_EN_BOX");
    REQUIRE(box.shadow->alpha == 180);
    const std::vector<toki::actor::DrawStep>& steps = box.variants[0].steps;
    REQUIRE(steps.size() == 8);
    REQUIRE(std::get<toki::actor::SetupStep>(steps[0].step).index == 25);
    REQUIRE(std::get<toki::actor::SegmentStep>(steps[2].step).segment == 8);
    const auto& scale = std::get<toki::actor::TransformStep>(steps[3].step).placement.scale;
    REQUIRE(scale[1] == Catch::Approx(0.005F));
    const auto& skeleton = std::get<toki::actor::SkeletonStep>(steps[4].step);
    REQUIRE(skeleton.frame == 0U);
    REQUIRE(skeleton.limbs.size() == 2);
    REQUIRE(skeleton.limbs[1].limb == 3);
    const auto& lid = std::get<toki::actor::DisplayListStep>(skeleton.limbs[1].steps[0].step);
    REQUIRE(lid.pass == toki::actor::Pass::Translucent);
    const auto& flame = std::get<toki::actor::TransformStep>(steps[5].step);
    REQUIRE(flame.isBillboard);
    REQUIRE(flame.resets);
    REQUIRE(std::get<toki::actor::DisplayListStep>(steps[6].step).object == "gameplay_keep");
    REQUIRE(std::get<toki::actor::ShadowStep>(steps[7].step).shadow.alpha == 153);
}

TEST_CASE("steps written out read back the same", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    INFO(describe(again.diagnostics));
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(again.entries.at("ACTOR_EN_BOX").variants[0].steps.size() == 8);
}

TEST_CASE("a field matches any of a list, and every entry matches on its scene",
          "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& box = database.entries.at("ACTOR_EN_BOX");

    REQUIRE(toki::actor::findVariant(box, {.params = 0x5000, .scene = "SCENE_DEKU_TREE"}) ==
            &box.variants[0]);
    REQUIRE(toki::actor::findVariant(
                box, {.params = 0x6000, .scene = "SCENE_BOTTOM_OF_THE_WELL"}) == &box.variants[0]);
    REQUIRE(toki::actor::findVariant(box, {.params = 0x5000, .scene = "SCENE_KOKIRI_FOREST"}) ==
            nullptr);
    REQUIRE(toki::actor::findVariant(box, {.params = 0x5000}) == nullptr);
    REQUIRE(toki::actor::findVariant(box, {.params = 0x0000}) == &box.variants[1]);
}

TEST_CASE("what steps get wrong is reported", "[actor][database]") {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(R"(format = 1
[actors.ACTOR_EN_TEST.fields]
type = { kind = "integer", shift = 0, width = 4 }

[[actors.ACTOR_EN_TEST.variants]]
match = { type = [1, "two"], scene = [3] }
skeleton = "gTestSkel"
steps = [
    { setup = 99 },
    { segment = 4, symbol = "gTestTex" },
    { display_list = "gTestDL", skeleton = "gTestSkel" },
    { skeleton = "gTestSkel", limbs = { 1 = [{ shadow = { kind = "circle", size = 1 } }], x = [] } },
    { skeleton = "gOtherSkel" },
    { transform = { size = 2 } },
    { colour = 1 },
]
)",
                                                                                "actors.toml");

    INFO(describe(database.diagnostics));
    REQUIRE(isReported(database, "must be an index of sSetupDL"));
    REQUIRE(isReported(database, "must point a segment from 8 to 12"));
    REQUIRE(isReported(database, "must be exactly one of setup"));
    REQUIRE(isReported(database, "a limb's steps cannot draw a shadow"));
    REQUIRE(isReported(database, "must be a limb's index"));
    REQUIRE(isReported(database, "transform.size: is not a key"));
    REQUIRE(isReported(database, "steps[6].colour: is not a key"));
    REQUIRE(isReported(database, "its steps draw more than one skeleton"));
    REQUIRE(isReported(database, "has steps, which say how it is drawn"));
    REQUIRE(isReported(database, "match.type: only an enum's values have names"));
    REQUIRE(isReported(database, "a scene is matched by its enum name"));
}

TEST_CASE("a variant matching on other scenes cannot match in this one", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& box = database.entries.at("ACTOR_EN_BOX");

    REQUIRE(toki::actor::isMatchingScene(box.variants[0], std::string("SCENE_DEKU_TREE")));
    REQUIRE_FALSE(toki::actor::isMatchingScene(box.variants[0], std::string("SCENE_JABU_JABU")));
    REQUIRE_FALSE(toki::actor::isMatchingScene(box.variants[0], std::nullopt));
    REQUIRE(toki::actor::isMatchingScene(box.variants[1], std::nullopt));
}
