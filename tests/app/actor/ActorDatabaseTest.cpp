#include "app/actor/domain/ActorDatabase.h"

#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "base/Diagnostic.h"
#include "base/Types.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using base::s16;
using base::s64;
using base::u16;

namespace {

constexpr std::string_view Example = R"(format = 1
checked_against = "0123abc"

[actors.ACTOR_OBJ_TSUBO]
status = "checked"
object = "gameplay_dangeon_keep"
scale = 0.15

[actors.ACTOR_OBJ_TSUBO.fields]
type      = { kind = "enum", shift = 8, width = 1, values = { dungeon = 0, field = 1 } }
drop      = { kind = "integer", shift = 0, width = 5 }
breakable = { kind = "flag", shift = 6 }

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { type = "dungeon" }
display_lists = [{ name = "gPotDungeonDL" }]

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { type = "field" }
object = "gameplay_field_keep"
display_lists = [{ name = "gPotFieldDL", object = "gameplay_field_keep", pass = "translucent", translation = [0, 10, 0], billboard = true, colors = { prim = [255, 255, 170] } }]

[actors.ACTOR_EN_ANI]
status = "guessed"
guessed_from = { file = "src/overlays/actors/ovl_En_Ani/z_en_ani.c", modified = "2026-10-06T21:14:03Z" }
object = "object_ani"
shadow = { kind = "circle", size = 22 }
text_ids = [0x1015]

[[actors.ACTOR_EN_ANI.variants]]
skeleton = "gRoofManSkel"
animation = "gRoofManIdleAnim"
frame = 4
segments = { 8 = "gRoofManEyeOpenTex" }
default_params = 0x0102

[actors.ACTOR_BG_HANEBASI]
rotates = { x = false, y = true, z = false }

[actors.ACTOR_BG_HANEBASI.fields]
part = { kind = "enum", source = "rotation_z", shift = 0, width = 2, values = { bridge = 0, chain = 1 } }
tilt = { kind = "integer", source = "rotation_x", shift = 0, width = 8, signed = true }

[[actors.ACTOR_BG_HANEBASI.variants]]
match = { part = "chain", age = "adult" }
children = [{ actor = "ACTOR_EN_KUSA", params = 0x0003, offset = [0, 0, 80] }]
spawn = { time = "night", flag = { field = "part", is = "unset" } }

[actors.ACTOR_EN_LIGHT]
draw = "marker"
lights = [{ color = [255, 160, 64], radius = 250, offset = [0, 60, 0] }]
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

TEST_CASE("an actor database is read with every key it may hold", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    INFO(describe(database.diagnostics));
    REQUIRE(database.diagnostics.empty());
    REQUIRE(database.checkedAgainst == "0123abc");
    REQUIRE(database.entries.size() == 4);

    const toki::actor::ActorEntry& pot = database.entries.at("ACTOR_OBJ_TSUBO");
    REQUIRE(pot.fields.size() == 3);
    REQUIRE(pot.scale == Catch::Approx(0.15F));
    REQUIRE(pot.variants.size() == 2);
    const toki::actor::DisplayListPart& field = pot.variants[1].displayLists.front();
    REQUIRE(field.object == "gameplay_field_keep");
    REQUIRE(field.pass == toki::actor::Pass::Translucent);
    REQUIRE(field.placement.translation[1] == Catch::Approx(10.0F));
    REQUIRE(field.isBillboard == true);
    REQUIRE(field.colors.primitive == std::array<base::u8, 4>{255, 255, 170, 255});

    const toki::actor::ActorEntry& roofMan = database.entries.at("ACTOR_EN_ANI");
    REQUIRE(roofMan.status == toki::actor::EntryStatus::Guessed);
    REQUIRE(roofMan.guessedFrom.has_value());
    REQUIRE(roofMan.shadow->kind == toki::actor::ShadowKind::Circle);
    REQUIRE(roofMan.textIds == std::vector<u16>{0x1015});
    REQUIRE(roofMan.variants.front().frame == 4U);
    REQUIRE(roofMan.variants.front().segments.front().segment == 8);
    REQUIRE(roofMan.variants.front().defaultParams == u16{0x0102});

    const toki::actor::ActorEntry& bridge = database.entries.at("ACTOR_BG_HANEBASI");
    REQUIRE_FALSE(bridge.rotates.x);
    REQUIRE(bridge.fields[0].source == toki::actor::FieldSource::RotationZ);
    REQUIRE(bridge.variants.front().children.front().params == 3);
    REQUIRE(bridge.variants.front().spawn->time == toki::actor::TimeOfDay::Night);
    REQUIRE_FALSE(bridge.variants.front().spawn->isFlagSet);

    const toki::actor::ActorEntry& light = database.entries.at("ACTOR_EN_LIGHT");
    REQUIRE(light.draw == toki::actor::DrawKind::Marker);
    REQUIRE(light.lights.front().color == std::array<base::u8, 3>{255, 160, 64});
}

TEST_CASE("an actor database written out reads back the same, in a stable order",
          "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(written.contains("default_params = 0x102"));
    REQUIRE(written.contains("scale = 0.15"));
    REQUIRE(written.find("ACTOR_BG_HANEBASI") < written.find("ACTOR_OBJ_TSUBO"));
}

TEST_CASE("what an actor database gets wrong is reported by actor, key and line",
          "[actor][database]") {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(R"(format = 1
[actors.ACTOR_EN_TEST]
colour = "red"
scale = "big"

[actors.ACTOR_EN_TEST.fields]
type = { kind = "enum", shift = 0, width = 2, values = { a = 0, b = 7 } }
other = { kind = "integer", shift = 1, width = 4 }
lamp = { kind = "flag", shift = 15, width = 3 }

[[actors.ACTOR_EN_TEST.variants]]
display_lists = [{ name = "gTestDL" }]

[[actors.ACTOR_EN_TEST.variants]]
match = { type = "c", missing = 1, other = true }
skeleton = "gTestSkel"
display_lists = [{ name = "gTestDL" }]
)",
                                                                                "actors.toml");

    INFO(describe(database.diagnostics));
    REQUIRE(isReported(database, "ACTOR_EN_TEST.colour: is not a key"));
    REQUIRE(isReported(database, "ACTOR_EN_TEST.scale: must be a number"));
    REQUIRE(isReported(database, "'b' is 7, which does not fit in 2 bits"));
    REQUIRE(isReported(database, "fields.type: shares bits with 'other'"));
    REQUIRE(isReported(database, "do not fit in 16"));
    REQUIRE(isReported(database, "variants after it are never drawn"));
    REQUIRE(isReported(database, "match.type: 'c' is not a value"));
    REQUIRE(isReported(database, "match.missing: is not a field"));
    REQUIRE(isReported(database, "match.other: only a flag matches"));
    REQUIRE(isReported(database, "draws a skeleton and display lists both"));
    const auto colour =
        std::ranges::find_if(database.diagnostics, [](const base::Diagnostic& diagnostic) {
            return diagnostic.message.contains("colour");
        });
    REQUIRE(colour->location.line == 3);
}

TEST_CASE("a database of another format, or not TOML at all, is not read", "[actor][database]") {
    REQUIRE(isReported(toki::actor::parseActorDatabase("format = 2\n", "a.toml"),
                       "this build reads format 1"));
    REQUIRE(isReported(toki::actor::parseActorDatabase("[actors\n", "b.toml"),
                       "cannot be read as TOML"));
}

TEST_CASE("a placement picks the first variant its fields match", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& pot = database.entries.at("ACTOR_OBJ_TSUBO");
    const toki::actor::ActorEntry& bridge = database.entries.at("ACTOR_BG_HANEBASI");

    REQUIRE(toki::actor::findVariant(pot, {.params = 0x0000}) == &pot.variants[0]);
    REQUIRE(toki::actor::findVariant(pot, {.params = 0x0105}) == &pot.variants[1]);
    REQUIRE(toki::actor::readFieldValue(pot.fields[1], {.params = 0x0105}) == 5);

    const toki::actor::PlacementValues chain{.rotation = {s16{-2}, 0, 1},
                                             .age = toki::actor::Age::Adult};
    REQUIRE(toki::actor::findVariant(bridge, chain) == &bridge.variants[0]);
    REQUIRE(toki::actor::readFieldValue(bridge.fields[1], chain) == s64{-2});
    REQUIRE(toki::actor::findVariant(bridge, {.rotation = {0, 0, 1}}) == nullptr);
    REQUIRE(toki::actor::findVariant(
                bridge, {.rotation = {0, 0, 1}, .age = toki::actor::Age::Child}) == nullptr);
}

TEST_CASE("the checkout's entry replaces the shipped one whole", "[actor][database]") {
    toki::actor::ActorDatabase shipped;
    shipped.entries["ACTOR_A"] = toki::actor::ActorEntry{.actor = "ACTOR_A", .name = "shipped"};
    shipped.entries["ACTOR_B"] = toki::actor::ActorEntry{.actor = "ACTOR_B", .name = "shipped"};
    toki::actor::ActorDatabase checkout;
    checkout.entries["ACTOR_A"] = toki::actor::ActorEntry{.actor = "ACTOR_A", .name = "mine"};

    const auto mine = toki::actor::findActorEntry(shipped, checkout, "ACTOR_A");
    const auto theirs = toki::actor::findActorEntry(shipped, checkout, "ACTOR_B");

    REQUIRE(mine->origin == toki::actor::EntryOrigin::Checkout);
    REQUIRE(mine->entry->name == "mine");
    REQUIRE(theirs->origin == toki::actor::EntryOrigin::Shipped);
    REQUIRE_FALSE(toki::actor::findActorEntry(shipped, checkout, "ACTOR_C").has_value());
}

TEST_CASE("an actor database is saved and read back, and a missing one is empty",
          "[actor][database]") {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "toki_tests" / "actor_database";
    std::error_code failure;
    std::filesystem::remove_all(root, failure);
    const std::filesystem::path file = toki::actor::makeCheckoutDatabasePath(root);
    REQUIRE(toki::actor::readActorDatabase(file).entries.empty());

    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    REQUIRE(toki::actor::saveActorDatabase(database, file).has_value());
    const toki::actor::ActorDatabase read = toki::actor::readActorDatabase(file);

    REQUIRE(read.diagnostics.empty());
    REQUIRE(read.entries.size() == database.entries.size());
    REQUIRE_FALSE(std::filesystem::exists(file.string() + ".writing"));
    std::filesystem::remove_all(root, failure);
}

TEST_CASE("the shipped actor database reads without a diagnostic", "[actor][database]") {
    const toki::actor::ActorDatabase shipped = toki::actor::readActorDatabase(TOKI_SHIPPED_ACTORS);

    INFO(describe(shipped.diagnostics));
    REQUIRE(shipped.diagnostics.empty());
}
