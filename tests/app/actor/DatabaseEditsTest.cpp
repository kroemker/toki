#include "app/actor/domain/DatabaseEdits.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/actor/domain/Derivation.h"
#include "base/Diagnostic.h"
#include "base/Types.h"
#include "hackeroot/Workspace.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace {

constexpr std::string_view Shipped = R"(format = 2

[actors.ACTOR_OBJ_TSUBO]
object = "gameplay_dangeon_keep"

[actors.ACTOR_OBJ_TSUBO.params]
"Drop" = { mask = 0x001F, kind = "number" }
"Type" = { mask = 0x0100, kind = "choice", values = { 0 = "Dungeon", 1 = "Plain" } }

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { "Type" = 1, age = "child" }
object = "object_tsubo"
parts = [{ display_list = "gPotPlainDL" }]

[[actors.ACTOR_OBJ_TSUBO.variants]]
parts = [{ display_list = "gPotDL" }]
)";

[[nodiscard]] toki::actor::ActorEntry readPot() {
    const toki::actor::ActorDatabase database = toki::actor::parseActorDatabase(Shipped, "a.toml");
    REQUIRE(database.diagnostics.empty());
    return database.entries.at("ACTOR_OBJ_TSUBO");
}

[[nodiscard]] std::filesystem::path makeTemporaryFolder(const std::string_view name) {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / "toki_tests" / name;
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
    return folder;
}

} // namespace

TEST_CASE("a variant for these params goes first, drawn as the one they match", "[actor][edits]") {
    toki::actor::ActorDatabase checkout;
    const toki::actor::ActorEntry pot = readPot();
    const toki::actor::PlacementValues placement{.params = 0x0103, .age = toki::actor::Age::Child};

    REQUIRE(toki::actor::addVariantForParams(checkout, pot, placement).has_value());

    const toki::actor::ActorEntry& entry = checkout.entries.at("ACTOR_OBJ_TSUBO");
    REQUIRE(entry.variants.size() == 3);
    const toki::actor::ActorVariant& added = entry.variants.front();
    REQUIRE(added.match.size() == 3);
    REQUIRE(added.match[0].param == "Drop");
    REQUIRE(std::get<base::s64>(added.match[0].values.front()) == 3);
    REQUIRE(added.match[1].param == "Type");
    REQUIRE(std::get<base::s64>(added.match[1].values.front()) == 1);
    REQUIRE(added.match[2].param == "age");
    REQUIRE(added.object == "object_tsubo");
    REQUIRE(added.defaultParams == base::u16{0x0103});
    REQUIRE(toki::actor::findVariant(entry, placement) == &added);
    REQUIRE(toki::actor::checkActorEntry(entry).empty());
}

TEST_CASE("an entry without params cannot have a variant for its params", "[actor][edits]") {
    toki::actor::ActorDatabase checkout;

    REQUIRE_FALSE(toki::actor::addVariantForParams(
                      checkout, toki::actor::ActorEntry{.actor = "ACTOR_EN_MD"}, {.params = 1})
                      .has_value());
    REQUIRE(checkout.entries.empty());
}

TEST_CASE("an edit reads the file, changes it and saves it, unless it reads badly",
          "[actor][edits]") {
    const std::filesystem::path folder = makeTemporaryFolder("database_edits");
    const std::filesystem::path file = toki::actor::makeCheckoutDatabasePath(folder);
    const toki::actor::ActorEntry pot = readPot();
    const auto addPot = [&pot](toki::actor::ActorDatabase& database) {
        if (database.entries.contains(pot.actor)) {
            return std::expected<void, std::string>(std::unexpected(std::string("already there")));
        }
        return toki::actor::setEntry(database, pot);
    };

    REQUIRE(toki::actor::applyEdit(file, addPot).has_value());
    REQUIRE(toki::actor::readActorDatabase(file).entries.contains("ACTOR_OBJ_TSUBO"));
    REQUIRE_FALSE(toki::actor::applyEdit(file, addPot).has_value());

    std::ofstream(file) << "format = 1\n[actors.ACTOR_X]\ncolour = 1\n";
    const std::expected<void, std::string> refused = toki::actor::applyEdit(file, addPot);
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().contains("does not read cleanly"));
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
}

TEST_CASE("an entry is set in the checkout whole, and removed again", "[actor][edits]") {
    toki::actor::ActorDatabase checkout;
    toki::actor::ActorEntry pot = readPot();

    REQUIRE(toki::actor::setEntry(checkout, pot).has_value());
    pot.name = "Pot";
    REQUIRE(toki::actor::setEntry(checkout, pot).has_value());
    REQUIRE(checkout.entries.at("ACTOR_OBJ_TSUBO").name == "Pot");
    REQUIRE_FALSE(toki::actor::setEntry(checkout, {}).has_value());
    REQUIRE(toki::actor::removeEntry(checkout, "ACTOR_OBJ_TSUBO").has_value());
    REQUIRE_FALSE(toki::actor::removeEntry(checkout, "ACTOR_OBJ_TSUBO").has_value());
}

TEST_CASE("the store reads the database, edits it, and reads it again", "[actor][edits]") {
    const std::filesystem::path folder = makeTemporaryFolder("database_store");
    const std::filesystem::path builtIn = folder / "builtin.toml";
    std::filesystem::create_directories(folder);
    std::ofstream(builtIn) << Shipped;
    const toki::actor::DatabaseFiles files{
        .builtIn = builtIn, .checkout = toki::actor::makeCheckoutDatabasePath(folder)};
    const toki::actor::ActorEntry pot = readPot();
    {
        toki::actor::DatabaseStore store;
        const auto waitForSnapshot = [&store] {
            std::optional<toki::actor::DatabaseSnapshot> snapshot;
            while (store.isBusy() || !snapshot) {
                if (std::optional<toki::actor::DatabaseSnapshot> read = store.update()) {
                    snapshot = std::move(read);
                }
                std::this_thread::yield();
            }
            return *snapshot;
        };
        store.open(files, nullptr);
        const toki::actor::DatabaseSnapshot opened = waitForSnapshot();
        REQUIRE(opened.builtIn.entries.contains("ACTOR_OBJ_TSUBO"));
        REQUIRE(opened.database.entries.empty());

        for (int i = 0; i < 2; ++i) {
            store.edit([&pot](toki::actor::ActorDatabase& database) {
                if (database.entries.contains(pot.actor)) {
                    return std::expected<void, std::string>(
                        std::unexpected(std::string("already there")));
                }
                return toki::actor::setEntry(database, pot);
            });
        }
        const toki::actor::DatabaseSnapshot edited = waitForSnapshot();
        REQUIRE(edited.database.entries.size() == 1);
        REQUIRE(store.takeFailures().size() == 1);
        REQUIRE(store.takeFailures().empty());
    }
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
}

TEST_CASE("a checkout without a database is seeded from Toki's own and derived for the rest",
          "[actor][edits]") {
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded = hackeroot::Workspace::load(
        std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout", {});
    REQUIRE(loaded.has_value());
    const auto workspace = std::make_shared<const hackeroot::Workspace>(std::move(*loaded));
    const std::filesystem::path folder = makeTemporaryFolder("database_seeding");
    const std::filesystem::path builtIn = folder / "builtin.toml";
    std::filesystem::create_directories(folder);
    std::ofstream(builtIn) << "format = 2\n[actors.ACTOR_EN_BOX]\nname = \"Treasure chest\"\n";
    const toki::actor::DatabaseFiles files{.builtIn = builtIn, .checkout = folder / "actors.toml"};
    {
        toki::actor::DatabaseStore store;
        store.open(files, workspace);
        std::optional<toki::actor::DatabaseSnapshot> snapshot;
        while (store.isBusy() || !snapshot) {
            if (std::optional<toki::actor::DatabaseSnapshot> read = store.update()) {
                snapshot = std::move(read);
            }
            std::this_thread::yield();
        }
        REQUIRE(store.takeFailures().empty());
        REQUIRE(snapshot->database.entries.at("ACTOR_EN_BOX").name == "Treasure chest");
        REQUIRE(snapshot->database.entries.contains("ACTOR_PLAYER"));
        REQUIRE(toki::actor::listNewActors(workspace->getTables(), snapshot->database).empty());
    }
    REQUIRE(std::filesystem::exists(files.checkout));
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
}

TEST_CASE("an actor is derived from Toki's own entry, or else from its C", "[actor][edits]") {
    std::expected<hackeroot::Workspace, base::Diagnostic> loaded = hackeroot::Workspace::load(
        std::filesystem::path(TOKI_TEST_FIXTURES) / "hackeroot/fixtures/checkout", {});
    REQUIRE(loaded.has_value());
    toki::actor::ActorDatabase builtIn;
    builtIn.entries["ACTOR_EN_BOX"] =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_BOX", .name = "Treasure chest"};
    toki::actor::ActorDatabase database;
    database.entries["ACTOR_EN_BOX"] =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_BOX", .name = "Mine"};

    REQUIRE(toki::actor::listNewActors(loaded->getTables(), database) ==
            std::vector<std::string>{"ACTOR_PLAYER"});
    REQUIRE(toki::actor::deriveEntries(database, *loaded, builtIn, {"ACTOR_EN_BOX", "ACTOR_PLAYER"})
                .has_value());
    REQUIRE(database.entries.at("ACTOR_EN_BOX").name == "Treasure chest");
    REQUIRE(database.entries.at("ACTOR_PLAYER").variants.empty());
    REQUIRE_FALSE(
        toki::actor::deriveEntries(database, *loaded, builtIn, {"ACTOR_UNSET_1"}).has_value());
}
