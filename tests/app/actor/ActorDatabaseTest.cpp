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

constexpr std::string_view Example = R"(format = 2
checked_against = "0123abc"

[actors.ACTOR_OBJ_TSUBO]
name = "Pot"
notes = "Breaks when cut."
object = "gameplay_dangeon_keep"
scale = 0.15

[actors.ACTOR_OBJ_TSUBO.params]
"Type"      = { mask = 0x0100, kind = "choice", values = { 0 = "Dungeon", 1 = "Field" } }
"Drop"      = { mask = 0x001F, kind = "number", description = "What it drops" }
"Breakable" = { mask = 0x0040, kind = "flag" }

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { "Type" = 0 }
parts = [{ display_list = "gPotDungeonDL" }]

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { "Type" = 1 }
object = "gameplay_field_keep"
parts = [{ display_list = "gPotFieldDL", pass = "translucent", translation = [0, 10, 0], billboard = true, prim = [255, 255, 170] }]

[actors.ACTOR_EN_ANI]
object = "object_ani"

[[actors.ACTOR_EN_ANI.variants]]
parts = [{ skeleton = "gRoofManSkel", pose = "gRoofManIdleAnim", segments = { 8 = "gRoofManEyeOpenTex" } }]
default_params = 0x0102

[actors.ACTOR_BG_HANEBASI]
rotates = { x = false, y = true, z = false }

[actors.ACTOR_BG_HANEBASI.params]
"Part" = { mask = 0x0003, source = "rotation_z", kind = "choice", values = { 0 = "Bridge", 1 = "Chain" } }
"Tilt" = { mask = 0x00FF, source = "rotation_x", kind = "number", signed = true }

[[actors.ACTOR_BG_HANEBASI.variants]]
match = { "Part" = 1, age = "adult" }
children = [{ actor = "ACTOR_EN_KUSA", params = 0x0003, offset = [0, 0, 80] }]

[actors.ACTOR_EN_LIGHT]
draw = "marker"
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
    const auto isMentioning = [text](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains(text);
    };
    return std::ranges::any_of(database.diagnostics, isMentioning) ||
           std::ranges::any_of(database.problems, isMentioning);
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
    REQUIRE(pot.name == "Pot");
    REQUIRE(pot.notes == "Breaks when cut.");
    REQUIRE(pot.params.size() == 3);
    REQUIRE(pot.params[0].name == "Drop");
    REQUIRE(pot.params[0].description == "What it drops");
    REQUIRE(pot.params[2].name == "Type");
    REQUIRE(pot.params[2].values[1].label == "Field");
    REQUIRE(pot.scale == Catch::Approx(0.15F));
    const toki::actor::ActorPart& field = pot.variants[1].parts.front();
    REQUIRE(field.kind == toki::actor::PartKind::DisplayList);
    REQUIRE(field.pass == toki::actor::Pass::Translucent);
    REQUIRE(field.placement.translation[1] == Catch::Approx(10.0F));
    REQUIRE(field.isBillboard);
    REQUIRE(field.colors.primitive == std::array<base::u8, 4>{255, 255, 170, 255});

    const toki::actor::ActorEntry& roofMan = database.entries.at("ACTOR_EN_ANI");
    const toki::actor::ActorPart& skeleton = roofMan.variants.front().parts.front();
    REQUIRE(skeleton.kind == toki::actor::PartKind::Skeleton);
    REQUIRE(skeleton.pose == "gRoofManIdleAnim");
    REQUIRE(skeleton.segments.front().segment == 8);
    REQUIRE(roofMan.variants.front().defaultParams == u16{0x0102});

    const toki::actor::ActorEntry& bridge = database.entries.at("ACTOR_BG_HANEBASI");
    REQUIRE_FALSE(bridge.rotates.x);
    REQUIRE(bridge.params[0].source == toki::actor::ParamSource::RotationX);
    REQUIRE(bridge.variants.front().children.front().params == 3);

    REQUIRE(database.entries.at("ACTOR_EN_LIGHT").draw == toki::actor::DrawKind::Marker);
}

TEST_CASE("an actor database written out reads back the same, in a stable order",
          "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");

    const std::string written = toki::actor::formatActorDatabase(database);
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");

    INFO(written);
    INFO(describe(again.diagnostics));
    REQUIRE(again.diagnostics.empty());
    REQUIRE(toki::actor::formatActorDatabase(again) == written);
    REQUIRE(written.contains("default_params = 0x102"));
    REQUIRE(written.contains("mask = 0x100"));
    REQUIRE(written.contains("= 'Field'"));
    REQUIRE(written.find("ACTOR_BG_HANEBASI") < written.find("ACTOR_OBJ_TSUBO"));
}

TEST_CASE("what an actor database gets wrong is reported by actor, key and line",
          "[actor][database]") {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(R"(format = 2
[actors.ACTOR_EN_TEST]
colour = "red"
scale = "big"

[actors.ACTOR_EN_TEST.params]
"Type"  = { mask = 0x0003, kind = "choice", values = { 0 = "A", 7 = "B" } }
"Other" = { mask = 0x0006, kind = "number" }
"Lamp"  = { mask = 0x8001, kind = "flag" }
"age"   = { mask = 0x0100, kind = "number" }
"Gap"   = { kind = "number" }
"Wide"  = { mask = 0x10000, kind = "number" }
"Twice" = { mask = 0x0600, kind = "choice", values = { 1 = "A", 0x1 = "B" } }

[[actors.ACTOR_EN_TEST.variants]]
parts = [{ display_list = "gTestDL" }]

[[actors.ACTOR_EN_TEST.variants]]
match = { "Type" = 2, "Missing" = 1, "Other" = true, "Lamp" = 1, "Twice" = [1, 2.5] }
parts = [{ display_list = "gTestDL", pose = "gTestAnim" }, { skeleton = "gTestSkel", display_list = "gTestDL" }]
)",
                                                                                "actors.toml");

    INFO(describe(database.diagnostics));
    REQUIRE(isReported(database, "ACTOR_EN_TEST.colour: is not a key"));
    REQUIRE(isReported(database, "ACTOR_EN_TEST.scale: must be a number"));
    REQUIRE(isReported(database, "7 ('B') does not fit in 2 bits"));
    REQUIRE(isReported(database, "params.Type: shares bits with 'Other'"));
    REQUIRE(isReported(database, "the bits of mask 0x8001 are not next to each other"));
    REQUIRE(isReported(database, "params.Lamp: a flag is one bit"));
    REQUIRE(isReported(database, "'age' is a param every actor has"));
    REQUIRE(isReported(database, "params.Gap: has no bits"));
    REQUIRE(isReported(database, "params.Wide.mask: must be a mask of up to 16 bits"));
    REQUIRE(isReported(database, "labels 1 again"));
    REQUIRE(isReported(database, "variants after it are never drawn"));
    REQUIRE(isReported(database, "match.Type: 2 is not one of its values"));
    REQUIRE(isReported(database, "match.Missing: is not a param"));
    REQUIRE(isReported(database, "match.Other: only a flag matches"));
    REQUIRE(isReported(database, "match.Lamp: a flag matches true or false"));
    REQUIRE(isReported(database, "match.Twice: lists something that is not"));
    REQUIRE(isReported(database, "only a skeleton stands in a pose"));
    REQUIRE(isReported(database, "must draw either a display_list or a skeleton"));
    REQUIRE_FALSE(std::ranges::any_of(database.diagnostics, [](const base::Diagnostic& diagnostic) {
        return diagnostic.message.contains("shares bits");
    }));
    const auto colour =
        std::ranges::find_if(database.diagnostics, [](const base::Diagnostic& diagnostic) {
            return diagnostic.message.contains("colour");
        });
    REQUIRE(colour->location.line == 3);
}

TEST_CASE("a database of another format, or not TOML at all, is not read", "[actor][database]") {
    REQUIRE(isReported(toki::actor::parseActorDatabase("format = 1\n", "a.toml"),
                       "this build reads format 2"));
    REQUIRE(isReported(toki::actor::parseActorDatabase("[actors\n", "b.toml"),
                       "cannot be read as TOML"));
}

TEST_CASE("a placement picks the first variant its params match", "[actor][database]") {
    const toki::actor::ActorDatabase database =
        toki::actor::parseActorDatabase(Example, "actors.toml");
    const toki::actor::ActorEntry& pot = database.entries.at("ACTOR_OBJ_TSUBO");
    const toki::actor::ActorEntry& bridge = database.entries.at("ACTOR_BG_HANEBASI");

    REQUIRE(toki::actor::findVariant(pot, {.params = 0x0000}) == &pot.variants[0]);
    REQUIRE(toki::actor::findVariant(pot, {.params = 0x0105}) == &pot.variants[1]);
    REQUIRE(toki::actor::readParamValue(pot.params[0], {.params = 0x0105}) == 5);

    const toki::actor::PlacementValues chain{.rotation = {s16{-2}, 0, 1},
                                             .age = toki::actor::Age::Adult};
    REQUIRE(toki::actor::findVariant(bridge, chain) == &bridge.variants[0]);
    REQUIRE(toki::actor::readParamValue(bridge.params[0], chain) == s64{-2});
    REQUIRE(toki::actor::findVariant(bridge, {.rotation = {0, 0, 1}}) == nullptr);
    REQUIRE(toki::actor::findVariant(
                bridge, {.rotation = {0, 0, 1}, .age = toki::actor::Age::Child}) == nullptr);
}

TEST_CASE("a mask gives a param's shift and width, and must have its bits together",
          "[actor][database]") {
    REQUIRE(toki::actor::findMaskShift(0x0F00) == 8);
    REQUIRE(toki::actor::findMaskWidth(0x0F00) == 4);
    REQUIRE(toki::actor::findMaskWidth(0x8001) == 16);
    REQUIRE(toki::actor::isMaskContiguous(0x0F00));
    REQUIRE(toki::actor::isMaskContiguous(0xFFFF));
    REQUIRE_FALSE(toki::actor::isMaskContiguous(0x0101));
    REQUIRE_FALSE(toki::actor::isMaskContiguous(0));
}

TEST_CASE("a bit is owned by the param of its source whose mask has it", "[actor][database]") {
    const std::vector<toki::actor::ActorParam> params{
        toki::actor::ActorParam{.name = "Low", .mask = 0x000F},
        toki::actor::ActorParam{.name = "High", .mask = 0xF000},
        toki::actor::ActorParam{
            .name = "Turn", .source = toki::actor::ParamSource::RotationZ, .mask = 0x0001}};

    REQUIRE(toki::actor::findBitOwner(params, toki::actor::ParamSource::Params, 2) == 0U);
    REQUIRE(toki::actor::findBitOwner(params, toki::actor::ParamSource::Params, 13) == 1U);
    REQUIRE_FALSE(toki::actor::findBitOwner(params, toki::actor::ParamSource::Params, 8));
    REQUIRE_FALSE(toki::actor::findBitOwner(params, toki::actor::ParamSource::Params, 2, 0U));
    REQUIRE(toki::actor::findBitOwner(params, toki::actor::ParamSource::RotationZ, 0) == 2U);
}

TEST_CASE("a renamed param is renamed in every match", "[actor][database]") {
    toki::actor::ActorEntry entry{
        .params = {toki::actor::ActorParam{.name = "type", .mask = 0x0003}},
        .variants = {toki::actor::ActorVariant{
            .match = {toki::actor::ParamMatch{.param = "type", .values = {s64{1}}},
                      toki::actor::ParamMatch{.param = "age", .values = {std::string("child")}}}}}};

    toki::actor::renameParam(entry, "type", "Type");

    REQUIRE(entry.params.front().name == "Type");
    REQUIRE(entry.variants.front().match[0].param == "Type");
    REQUIRE(entry.variants.front().match[1].param == "age");
}

TEST_CASE("an entry is found by its actor's enum name", "[actor][database]") {
    toki::actor::ActorDatabase database;
    database.entries["ACTOR_A"] = toki::actor::ActorEntry{.actor = "ACTOR_A", .name = "mine"};

    REQUIRE(toki::actor::findEntry(database, "ACTOR_A")->name == "mine");
    REQUIRE(toki::actor::findEntry(database, "ACTOR_B") == nullptr);
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

    INFO(describe(shipped.diagnostics) + describe(shipped.problems));
    REQUIRE(shipped.diagnostics.empty());
    REQUIRE(shipped.problems.empty());
}

TEST_CASE("a param renamed by its own name is renamed in every match", "[actor][database]") {
    toki::actor::ActorEntry entry{
        .params = {toki::actor::ActorParam{.name = "type", .mask = 0x0003}},
        .variants = {toki::actor::ActorVariant{
            .match = {toki::actor::ParamMatch{.param = "type", .values = {s64{1}}}}}}};

    toki::actor::renameParam(entry, entry.params.front().name, "Type");

    REQUIRE(entry.params.front().name == "Type");
    REQUIRE(entry.variants.front().match.front().param == "Type");
}

TEST_CASE("a match without values is kept, and matches no placement", "[actor][database]") {
    toki::actor::ActorDatabase database;
    database.entries["ACTOR_A"] = toki::actor::ActorEntry{
        .actor = "ACTOR_A",
        .params = {toki::actor::ActorParam{.name = "Type", .mask = 0x0003}},
        .variants = {
            toki::actor::ActorVariant{.match = {toki::actor::ParamMatch{.param = "Type"},
                                                toki::actor::ParamMatch{.param = "scene"}}},
            toki::actor::ActorVariant{}}};

    const toki::actor::ActorDatabase read =
        toki::actor::parseActorDatabase(toki::actor::formatActorDatabase(database), "a.toml");

    INFO(describe(read.diagnostics));
    REQUIRE(read.diagnostics.empty());
    REQUIRE(isReported(read, "match.Type: has no values"));
    const toki::actor::ActorEntry& entry = read.entries.at("ACTOR_A");
    REQUIRE(entry.variants.front().match.size() == 2);
    REQUIRE(entry.variants.front().match.front().values.empty());
    REQUIRE(toki::actor::findVariant(entry, {.params = 0x0001, .scene = "SCENE_DEKU_TREE"}) ==
            &entry.variants[1]);
}

TEST_CASE("a param without bits and a choice with negative values are written as they are",
          "[actor][database]") {
    toki::actor::ActorDatabase database;
    database.entries["ACTOR_A"] = toki::actor::ActorEntry{
        .actor = "ACTOR_A",
        .params = {toki::actor::ActorParam{.name = "New"},
                   toki::actor::ActorParam{.name = "Side",
                                           .kind = toki::actor::ParamKind::Choice,
                                           .mask = 0x0300,
                                           .isSigned = true,
                                           .values = {{.value = -2, .label = "Left"},
                                                      {.value = 1, .label = "Right"}}}}};

    const toki::actor::ActorDatabase read =
        toki::actor::parseActorDatabase(toki::actor::formatActorDatabase(database), "a.toml");

    INFO(describe(read.diagnostics));
    REQUIRE(read.diagnostics.empty());
    const toki::actor::ActorEntry& entry = read.entries.at("ACTOR_A");
    REQUIRE(entry.params.size() == 2);
    REQUIRE(entry.params[0].mask == 0);
    REQUIRE(entry.params[1].values.size() == 2);
    REQUIRE(entry.params[1].values[0].value == -2);
    REQUIRE(entry.params[1].values[0].label == "Left");
}

TEST_CASE("a database path that is not a file is a diagnostic, not an empty database",
          "[actor][database]") {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "toki_tests" / "actor_database_folder";
    std::filesystem::create_directories(folder);

    REQUIRE(isReported(toki::actor::readActorDatabase(folder), "is not a file"));
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
}

TEST_CASE("a choice may take its values from a collection, after an offset", "[actor][database]") {
    const toki::actor::ActorDatabase read = toki::actor::parseActorDatabase(R"(format = 2
[actors.ACTOR_EN_KANBAN.params]
"Text" = { mask = 0x00FF, kind = "choice", values = "MessageID", offset = 0x300 }
"Bad"  = { mask = 0x0F00, kind = "choice", values = 3 }
"Kind" = { mask = 0xF000, kind = "number", values = "GetItemID" }
)",
                                                                            "actors.toml");

    REQUIRE(isReported(read, "params.Bad.values: must be a table of values"));
    REQUIRE(isReported(read, "params.Kind: only a choice takes its values from a collection"));
    REQUIRE_FALSE(isReported(read, "params.Text: a choice needs values"));
    const toki::actor::ActorEntry& sign = read.entries.at("ACTOR_EN_KANBAN");
    REQUIRE(sign.params[0].collection == "MessageID");
    REQUIRE(sign.params[0].offset == 0x300);
    REQUIRE(sign.params[0].values.empty());

    const std::string written = toki::actor::formatActorDatabase(read);
    REQUIRE(written.contains("values = 'MessageID'"));
    REQUIRE(written.contains("offset = 0x300"));
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");
    REQUIRE(again.diagnostics.empty());
    REQUIRE(again.entries.at("ACTOR_EN_KANBAN").params[0].offset == 0x300);
}

TEST_CASE("a number may be a scene flag the actor reads, sets or both", "[actor][database]") {
    const toki::actor::ActorDatabase read = toki::actor::parseActorDatabase(R"(format = 2
[actors.ACTOR_EN_BOX.params]
"Chest Flag" = { mask = 0x001F, kind = "number", scene_flag = "treasure", flag_use = "both" }
"Switch Flag" = { mask = 0x0FC0, kind = "number", scene_flag = "switch" }
"Lit" = { mask = 0x1000, kind = "flag", scene_flag = "switch" }
"Loose" = { mask = 0x2000, kind = "flag", flag_use = "sets" }
)",
                                                                            "actors.toml");

    REQUIRE(isReported(read, "params.Lit: only a number is a scene flag"));
    REQUIRE(isReported(read, "params.Loose.flag_use: says how a scene flag is used"));
    const std::vector<toki::actor::ActorParam>& params = read.entries.at("ACTOR_EN_BOX").params;
    REQUIRE(params[0].sceneFlag == toki::actor::SceneFlag::Treasure);
    REQUIRE(params[0].flagUse == toki::actor::FlagUse::Both);
    REQUIRE(params[1].sceneFlag == toki::actor::SceneFlag::Switch);
    REQUIRE(params[1].flagUse == toki::actor::FlagUse::Reads);

    const std::string written = toki::actor::formatActorDatabase(read);
    REQUIRE(written.contains("scene_flag = 'treasure'"));
    REQUIRE(written.contains("flag_use = 'both'"));
    const toki::actor::ActorDatabase again = toki::actor::parseActorDatabase(written, "again.toml");
    REQUIRE(again.entries.at("ACTOR_EN_BOX").params[1].sceneFlag == toki::actor::SceneFlag::Switch);
    REQUIRE(again.entries.at("ACTOR_EN_BOX").params[1].flagUse == toki::actor::FlagUse::Reads);
}
