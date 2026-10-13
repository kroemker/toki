#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DatabaseEdits.h"
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

using base::s64;
using base::u8;
using base::usize;

namespace {

constexpr std::string_view Example = R"(format = 3

[actors.ACTOR_OBJ_OSHIHIKI]
object = "gameplay_dangeon_keep"

[actors.ACTOR_OBJ_OSHIHIKI.params]
Size = { mask = 0x000F, kind = "number" }
Color = { mask = 0x00C0, kind = "number" }

[[actors.ACTOR_OBJ_OSHIHIKI.variants]]
parts = [{ display_list = "gPushBlockDL" }, { display_list = "gPushBlockTopDL" }]

[[actors.ACTOR_OBJ_OSHIHIKI.adjustments]]
match = { Size = 1 }
scale = 0.2
height_offset = 5
rotates = { x = false, y = true, z = false }

[[actors.ACTOR_OBJ_OSHIHIKI.adjustments]]
match = { Color = 2, scene = "SCENE_FOREST_TEMPLE" }
env = [110, 86, 40]
segments = { 8 = { prim = [1, 2, 3, 4] } }
parts = [1]
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

TEST_CASE("adjustments are read with what each changes", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    REQUIRE(database.diagnostics.empty());
    REQUIRE(database.problems.empty());
    const std::vector<toki::actor::ActorAdjustment>& adjustments =
        database.entries.at("ACTOR_OBJ_OSHIHIKI").adjustments;
    REQUIRE(adjustments.size() == 2);
    REQUIRE(adjustments[0].scale == Catch::Approx(0.2F));
    REQUIRE(adjustments[0].heightOffset == Catch::Approx(5.0F));
    REQUIRE(adjustments[0].rotates.has_value());
    REQUIRE_FALSE(adjustments[0].rotates->x);
    REQUIRE(adjustments[0].parts.empty());
    REQUIRE(adjustments[1].colors.environment == std::array<u8, 4>{110, 86, 40, 255});
    REQUIRE(adjustments[1].segments.size() == 1);
    REQUIRE(std::holds_alternative<toki::actor::BuiltList>(adjustments[1].segments[0].target));
    REQUIRE(adjustments[1].parts == std::vector<usize>{1});
}

TEST_CASE("adjustments written out read back the same", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(again.entries.at("ACTOR_OBJ_OSHIHIKI").adjustments.size() == 2);
}

TEST_CASE("every adjustment that matches a placement is found, in order", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& entry = database.entries.at("ACTOR_OBJ_OSHIHIKI");

    REQUIRE(toki::actor::findAdjustments(
                entry, {.params = 0x0081, .scene = std::string("SCENE_FOREST_TEMPLE")}) ==
            std::vector<usize>{0, 1});
    REQUIRE(toki::actor::findAdjustments(entry, {.params = 0x0081}) == std::vector<usize>{0});
    REQUIRE(toki::actor::findAdjustments(entry, {.params = 0x0002}).empty());
}

TEST_CASE("what adjustments get wrong is reported", "[actor][database]") {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(R"(format = 3
[actors.ACTOR_EN_TEST.params]
Type = { mask = 0x000F, kind = "number" }

[[actors.ACTOR_EN_TEST.variants]]
parts = [{ display_list = "gTestDL" }]

[[actors.ACTOR_EN_TEST.adjustments]]
match = { Kind = 1 }
parts = [3]
fog = 1

[[actors.ACTOR_EN_TEST.adjustments]]
parts = "all"
)",
                                                                                "actors.toml");

    REQUIRE(isReported(database, "adjustments[0].match.Kind: is not a param"));
    REQUIRE(isReported(database, "adjustments[0].parts: changes part 3"));
    REQUIRE(isReported(database, "adjustments[0].fog: is not a key"));
    REQUIRE(isReported(database, "adjustments[1].parts: must be a list"));
}

TEST_CASE("renaming a param renames it in adjustments too", "[actor][database]") {
    toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(Example, "actors.toml");
    toki::actor::ActorEntry& entry = database.entries.at("ACTOR_OBJ_OSHIHIKI");

    toki::actor::renameParam(entry, "Size", "Block Size");

    REQUIRE(entry.adjustments[0].match.front().param == "Block Size");
}

TEST_CASE("an adjustment for a placement's params matches exactly them, and comes last",
          "[actor][database]") {
    toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry current = database.entries.at("ACTOR_OBJ_OSHIHIKI");

    REQUIRE(toki::actor::addAdjustmentForParams(database, current, {.params = 0x0042}));

    const toki::actor::ActorEntry& entry = database.entries.at("ACTOR_OBJ_OSHIHIKI");
    REQUIRE(entry.adjustments.size() == 3);
    REQUIRE(toki::actor::findAdjustments(entry, {.params = 0x0042}) == std::vector<usize>{2});
    REQUIRE(toki::actor::findAdjustments(entry, {.params = 0x0043}).empty());
}
