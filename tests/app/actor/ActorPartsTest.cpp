#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "base/Types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
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
    { skeleton = "gTreasureChestSkel", pose = "gTreasureChestAnim_00024C", pose_object = "object_box_anime", rotation = [0, 180, 0], scale = 0.005, env = [0, 0, 0, 255], segments = { 8 = "gTreasureChestEyeTex", 9 = { env = [90, 140, 50], tiles = [{ tile = 1, x = 4, width = 32, height = 16 }] } } },
    { display_list = "gEffFire1DL", object = "gameplay_keep", translation = [0, 52, 0], billboard = true, setup = 66, pass = "translucent", hilite = true },
    { skeleton = "gKm1Skel", limbs = { 15 = { display_list = "gKm1HeadDL", object = "object_km1" }, 4 = false, 23 = "object_st_DL_004788" } },
    { display_list = "gWallmasterFingerDL", limb = 2, translation = [1600, -700, -1700] },
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
    REQUIRE(parts.size() == 4);
    REQUIRE(parts[0].kind == toki::actor::PartKind::Skeleton);
    REQUIRE(parts[0].poseObject == "object_box_anime");
    REQUIRE(parts[0].placement.scale[1] == Catch::Approx(0.005F));
    REQUIRE(parts[0].colors.environment.has_value());
    REQUIRE(parts[0].segments.size() == 2);
    REQUIRE(std::get<std::string>(parts[0].segments[0].target) == "gTreasureChestEyeTex");
    const auto& built = std::get<toki::actor::BuiltList>(parts[0].segments[1].target);
    REQUIRE(built.colors.environment == std::array<base::u8, 4>{90, 140, 50, 255});
    REQUIRE(built.tiles.size() == 1);
    REQUIRE(built.tiles[0].tile == 1);
    REQUIRE(built.tiles[0].x == 4);
    REQUIRE(built.tiles[0].y == 0);
    REQUIRE(built.tiles[0].width == 32);
    REQUIRE(built.tiles[0].height == 16);
    REQUIRE(parts[1].object == "gameplay_keep");
    REQUIRE(parts[1].isBillboard);
    REQUIRE(parts[1].setup == base::u8{66});
    REQUIRE(parts[1].pass == toki::actor::Pass::Translucent);
    REQUIRE(parts[1].isHilit);
    REQUIRE(parts[2].limbs.size() == 3);
    const auto findLimb = [&](const base::u8 number) {
        return *std::ranges::find(parts[2].limbs, number, &toki::actor::LimbOverride::limb);
    };
    REQUIRE(findLimb(15).displayList == "gKm1HeadDL");
    REQUIRE(findLimb(15).object == "object_km1");
    REQUIRE_FALSE(findLimb(4).displayList.has_value());
    REQUIRE(findLimb(23).displayList == "object_st_DL_004788");
    REQUIRE_FALSE(findLimb(23).object.has_value());
    REQUIRE(parts[3].limb == base::u8{2});
}

TEST_CASE("parts written out read back the same", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(again.entries.at("ACTOR_EN_BOX").variants[0].parts.size() == 4);
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
    { display_list = "gTestDL", segments = { 8 = { tiles = [{ tile = 9, height = 8 }], fog = 1 } } },
    { skeleton = "gTestSkel", limbs = { 0 = "gTestDL", 3 = true, 5 = { object = "object_test" } }, limb = 2 },
    { display_list = "gTestDL", limb = 0, limbs = { 1 = false } },
]
)",
                                                                                "actors.toml");

    REQUIRE(isReported(database, "must be an index of sSetupDL"));
    REQUIRE(isReported(database, "must be a segment from 8 to 12"));
    REQUIRE(isReported(database, "parts[2].colour: is not a key"));
    REQUIRE(isReported(database, "parts[3]: draws nothing"));
    REQUIRE(isReported(database, "segments.8.tiles[0].tile: must be a whole number from 0 to 7"));
    REQUIRE(isReported(database, "segments.8.tiles[0].width: must be a whole number from 1"));
    REQUIRE(isReported(database, "segments.8.fog: is not a key"));
    REQUIRE(isReported(database, "parts[6].limbs.0: must be a limb"));
    REQUIRE(isReported(database, "parts[6].limbs.3: must be a display list's name"));
    REQUIRE(isReported(database, "parts[6].limbs.5: names no display_list"));
    REQUIRE(isReported(database, "parts[6].limb: places a display list at a limb"));
    REQUIRE(isReported(database, "parts[7].limb: must be a limb"));
    REQUIRE(isReported(database, "parts[7].limbs: must be a table of a skeleton's limbs"));
    REQUIRE(isReported(database, "parts[4]: must be a table"));
    REQUIRE(isReported(database, "a scene is matched by its enum name"));
    REQUIRE(isReported(database, "an age is \"child\" or \"adult\""));
}
