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
#include <vector>

namespace {

constexpr std::string_view Example = R"(format = 2

[actors.ACTOR_EN_BOX]
object = "object_box"

[actors.ACTOR_EN_BOX.params]
"Type" = { mask = 0xF000, kind = "choice", values = { 0 = "Big", 5 = "Small", 6 = "Small, seen through the Lens", 11 = "Boss Key" } }

[[actors.ACTOR_EN_BOX.variants]]
match = { "Type" = [5, 6], scene = ["SCENE_DEKU_TREE", "SCENE_BOTTOM_OF_THE_WELL"] }
parts = [
    { skeleton = "gTreasureChestSkel", pose = "gTreasureChestAnim_00024C", pose_object = "object_box_anime", rotation = [0, 180, 0], scale = 0.005, env = [0, 0, 0, 255], segments = { 8 = "gTreasureChestEyeTex" } },
    { display_list = "gEffFire1DL", object = "gameplay_keep", translation = [0, 52, 0], billboard = true, setup = 66, pass = "translucent", hilite = true },
]

[[actors.ACTOR_EN_BOX.variants]]
match = { "Type" = 0 }
parts = [{ display_list = "gTreasureChestDL" }]
)";

[[nodiscard]] bool isReported(const toki::actor::ActorDatabase& database,
                              const std::string_view text) {
    const auto isMentioning = [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    };
    return std::ranges::any_of(database.diagnostics, isMentioning) ||
           std::ranges::any_of(database.problems, isMentioning);
}

} // namespace

TEST_CASE("a variant's parts are read with everything each is drawn with", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    REQUIRE(database.diagnostics.empty());
    const std::vector<toki::actor::ActorPart>& parts =
        database.entries.at("ACTOR_EN_BOX").variants[0].parts;
    REQUIRE(parts.size() == 2);
    REQUIRE(parts[0].kind == toki::actor::PartKind::Skeleton);
    REQUIRE(parts[0].poseObject == "object_box_anime");
    REQUIRE(parts[0].placement.scale[1] == Catch::Approx(0.005F));
    REQUIRE(parts[0].colors.environment.has_value());
    REQUIRE(parts[0].segments.front().name == "gTreasureChestEyeTex");
    REQUIRE(parts[1].object == "gameplay_keep");
    REQUIRE(parts[1].isBillboard);
    REQUIRE(parts[1].setup == base::u8{66});
    REQUIRE(parts[1].pass == toki::actor::Pass::Translucent);
    REQUIRE(parts[1].isHilit);
}

TEST_CASE("parts written out read back the same", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(again.entries.at("ACTOR_EN_BOX").variants[0].parts.size() == 2);
}

TEST_CASE("a param matches any of a list, and every entry matches on its scene",
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

TEST_CASE("a variant matching on other scenes cannot match in this one", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& box = database.entries.at("ACTOR_EN_BOX");

    REQUIRE(toki::actor::isMatchingScene(box.variants[0], std::string("SCENE_DEKU_TREE")));
    REQUIRE_FALSE(toki::actor::isMatchingScene(box.variants[0], std::string("SCENE_JABU_JABU")));
    REQUIRE_FALSE(toki::actor::isMatchingScene(box.variants[0], std::nullopt));
    REQUIRE(toki::actor::isMatchingScene(box.variants[1], std::nullopt));
}

TEST_CASE("what parts get wrong is reported", "[actor][database]") {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(R"(format = 2
[[actors.ACTOR_EN_TEST.variants]]
match = { scene = [3], age = "elder" }
parts = [
    { display_list = "gTestDL", setup = 99 },
    { display_list = "gTestDL", segments = { 4 = "gTestTex" } },
    { display_list = "gTestDL", colour = 1 },
    { display_list = "" },
    "gTestDL",
]
)",
                                                                                "actors.toml");

    REQUIRE(isReported(database, "must be an index of sSetupDL"));
    REQUIRE(isReported(database, "must be a segment from 8 to 12"));
    REQUIRE(isReported(database, "parts[2].colour: is not a key"));
    REQUIRE(isReported(database, "parts[3]: draws nothing"));
    REQUIRE(isReported(database, "parts[4]: must be a table"));
    REQUIRE(isReported(database, "a scene is matched by its enum name"));
    REQUIRE(isReported(database, "an age is \"child\" or \"adult\""));
}
