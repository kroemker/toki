#include "app/actor/domain/DatabaseEdits.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorDatabaseFile.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DatabaseStore.h"
#include "base/Types.h"

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>

namespace {

constexpr std::string_view Shipped = R"(format = 1

[actors.ACTOR_OBJ_TSUBO]
status = "checked"
object = "gameplay_dangeon_keep"

[actors.ACTOR_OBJ_TSUBO.fields]
drop = { kind = "integer", shift = 0, width = 5 }
type = { kind = "enum", shift = 8, width = 1, values = { dungeon = 0, plain = 1 } }

[[actors.ACTOR_OBJ_TSUBO.variants]]
match = { type = "plain", age = "child" }
object = "object_tsubo"
display_lists = [{ name = "gPotPlainDL" }]

[[actors.ACTOR_OBJ_TSUBO.variants]]
display_lists = [{ name = "gPotDL" }]
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

TEST_CASE("a kept guess goes into the checkout's database, never over an entry", "[actor][edits]") {
    toki::actor::ActorDatabase checkout;
    const toki::actor::ActorEntry guess{.actor = "ACTOR_EN_ANI",
                                        .status = toki::actor::EntryStatus::Checked};

    REQUIRE(toki::actor::keepGuess(checkout, guess).has_value());
    REQUIRE(checkout.entries.at("ACTOR_EN_ANI").status == toki::actor::EntryStatus::Guessed);
    REQUIRE_FALSE(toki::actor::keepGuess(checkout, guess).has_value());
}

TEST_CASE("a variant for these params goes first, drawn as the one they match", "[actor][edits]") {
    toki::actor::ActorDatabase checkout;
    const toki::actor::ActorEntry pot = readPot();
    const toki::actor::PlacementValues placement{.params = 0x0103, .age = toki::actor::Age::Child};

    REQUIRE(toki::actor::addVariantForParams(checkout, pot, placement).has_value());

    const toki::actor::ActorEntry& entry = checkout.entries.at("ACTOR_OBJ_TSUBO");
    REQUIRE(entry.variants.size() == 3);
    const toki::actor::ActorVariant& added = entry.variants.front();
    REQUIRE(added.match.size() == 3);
    REQUIRE(added.match[0].field == "drop");
    REQUIRE(std::get<base::s64>(added.match[0].values.front()) == 3);
    REQUIRE(added.match[1].field == "type");
    REQUIRE(std::get<std::string>(added.match[1].values.front()) == "plain");
    REQUIRE(added.match[2].field == "age");
    REQUIRE(added.object == "object_tsubo");
    REQUIRE(added.defaultParams == base::u16{0x0103});
    REQUIRE(toki::actor::findVariant(entry, placement) == &added);
    REQUIRE(toki::actor::checkActorEntry(entry).empty());
}

TEST_CASE("an entry without fields cannot have a variant for its params", "[actor][edits]") {
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
        return toki::actor::keepGuess(database, pot);
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

    REQUIRE(toki::actor::setCheckoutEntry(checkout, pot).has_value());
    pot.name = "Pot";
    REQUIRE(toki::actor::setCheckoutEntry(checkout, pot).has_value());
    REQUIRE(checkout.entries.at("ACTOR_OBJ_TSUBO").name == "Pot");
    REQUIRE_FALSE(toki::actor::setCheckoutEntry(checkout, {}).has_value());
    REQUIRE(toki::actor::removeCheckoutEntry(checkout, "ACTOR_OBJ_TSUBO").has_value());
    REQUIRE_FALSE(toki::actor::removeCheckoutEntry(checkout, "ACTOR_OBJ_TSUBO").has_value());
}

TEST_CASE("the store reads both files, edits the checkout's, and reads them again",
          "[actor][edits]") {
    const std::filesystem::path folder = makeTemporaryFolder("database_store");
    const std::filesystem::path shipped = folder / "shipped.toml";
    std::filesystem::create_directories(folder);
    std::ofstream(shipped) << Shipped;
    const toki::actor::DatabaseFiles files{
        .shipped = shipped, .checkout = toki::actor::makeCheckoutDatabasePath(folder)};
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
        store.open(files);
        const toki::actor::DatabaseSnapshot opened = waitForSnapshot();
        REQUIRE(opened.shipped.entries.contains("ACTOR_OBJ_TSUBO"));
        REQUIRE(opened.checkout.entries.empty());

        for (int i = 0; i < 2; ++i) {
            store.edit([&pot](toki::actor::ActorDatabase& database) {
                return toki::actor::keepGuess(database, pot);
            });
        }
        const toki::actor::DatabaseSnapshot edited = waitForSnapshot();
        REQUIRE(edited.checkout.entries.size() == 1);
        REQUIRE(store.takeFailures().size() == 1);
        REQUIRE(store.takeFailures().empty());
    }
    std::error_code failure;
    std::filesystem::remove_all(folder, failure);
}
