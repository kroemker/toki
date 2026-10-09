#include "app/actor/domain/FieldReading.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/SpawnCheck.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view Example = R"(format = 1

[actors.ACTOR_OBJ_SYOKUDAI]
spawn = { time = "night", flag = { field = "switch", is = "unset" } }

[actors.ACTOR_OBJ_SYOKUDAI.fields]
switch = { kind = "integer", shift = 0, width = 6, role = "switch_flag", meaning = "Set when it is lit" }
lit    = { kind = "flag", shift = 10 }
type   = { kind = "enum", shift = 12, width = 4, values = { golden = 0, timed = 1 } }
text   = { kind = "integer", source = "rotation_x", shift = 0, width = 16, hex = true }

[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
match = { type = "timed", age = "adult" }
spawn = { age = "adult" }

[[actors.ACTOR_OBJ_SYOKUDAI.variants]]
)";

[[nodiscard]] toki::actor::ActorEntry readTorch() {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(Example, "a.toml");
    REQUIRE(database.diagnostics.empty());
    return database.entries.at("ACTOR_OBJ_SYOKUDAI");
}

} // namespace

TEST_CASE("a placement's fields are read as the panels show them", "[actor][fields]") {
    const toki::actor::ActorEntry torch = readTorch();

    const std::vector<toki::actor::FieldReading> fields =
        toki::actor::readFields(torch, {.params = 0x1405, .rotation = {0x0300, 0, 0}});

    REQUIRE(fields.size() == 4);
    REQUIRE(fields[0].text == "5");
    REQUIRE(fields[0].role == toki::actor::FieldRole::SwitchFlag);
    REQUIRE(fields[0].meaning == "Set when it is lit");
    REQUIRE(fields[1].text == "yes");
    REQUIRE(fields[2].text == "timed");
    REQUIRE(fields[3].text == "0x300");
    REQUIRE(toki::actor::readFields(torch, {.params = 0x7000})[2].text == "7 (unnamed)");
}

TEST_CASE("the match for a placement names every field's value", "[actor][fields]") {
    const toki::actor::ActorEntry torch = readTorch();

    const std::vector<toki::actor::FieldMatch> match =
        toki::actor::makeFieldsMatch(torch, {.params = 0x1005});

    const auto findValue = [&match](const std::string_view field) {
        const auto found = std::ranges::find(match, field, &toki::actor::FieldMatch::field);
        REQUIRE(found != match.end());
        REQUIRE(found->values.size() == 1);
        return found->values.front();
    };
    REQUIRE(match.size() == 4);
    REQUIRE(std::get<base::s64>(findValue("switch")) == 5);
    REQUIRE_FALSE(std::get<bool>(findValue("lit")));
    REQUIRE(std::get<std::string>(findValue("type")) == "timed");
}

TEST_CASE("a placement spawns where its variant's or entry's conditions hold", "[actor][fields]") {
    const toki::actor::ActorEntry torch = readTorch();

    const toki::actor::SpawnCheck night =
        toki::actor::checkSpawn(torch, {.params = 0x0003}, toki::actor::TimeOfDay::Night);
    const toki::actor::SpawnCheck day =
        toki::actor::checkSpawn(torch, {.params = 0x0003}, toki::actor::TimeOfDay::Day);
    const toki::actor::SpawnCheck adult = toki::actor::checkSpawn(
        torch, {.params = 0x1000, .age = toki::actor::Age::Adult}, std::nullopt);

    REQUIRE(night.isSpawned);
    REQUIRE(night.needs == std::vector<std::string>{"night only", "switch 3 unset"});
    REQUIRE_FALSE(day.isSpawned);
    REQUIRE(adult.isSpawned);
    REQUIRE(adult.needs == std::vector<std::string>{"adult only"});
}
