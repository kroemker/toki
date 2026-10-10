#include "app/actor/domain/ActorRow.h"

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DatabaseStore.h"
#include "hackeroot/Tables.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace {

[[nodiscard]] hackeroot::Tables makeTables() {
    hackeroot::Tables tables;
    tables.actors = {
        hackeroot::ActorEntry{.id = 0, .name = "Player", .enumName = "ACTOR_PLAYER"},
        hackeroot::ActorEntry{.id = 1, .enumName = "ACTOR_UNSET_1", .isUnset = true},
        hackeroot::ActorEntry{.id = 2, .name = "En_Box", .enumName = "ACTOR_EN_BOX"},
        hackeroot::ActorEntry{.id = 3, .name = "En_Ani", .enumName = "ACTOR_EN_ANI"},
        hackeroot::ActorEntry{.id = 4, .name = "En_Mine", .enumName = "ACTOR_EN_MINE"},
    };
    return tables;
}

[[nodiscard]] toki::actor::DatabaseSnapshot makeDatabases() {
    toki::actor::DatabaseSnapshot databases;
    databases.shipped.entries["ACTOR_PLAYER"] =
        toki::actor::ActorEntry{.actor = "ACTOR_PLAYER", .name = "Link"};
    databases.shipped.entries["ACTOR_EN_BOX"] =
        toki::actor::ActorEntry{.actor = "ACTOR_EN_BOX", .name = "Treasure chest"};
    databases.checkout.entries["ACTOR_EN_MINE"] = toki::actor::ActorEntry{.actor = "ACTOR_EN_MINE"};
    return databases;
}

} // namespace

TEST_CASE("the actor list has every set actor, named and placed by its entry", "[actor][rows]") {
    const std::vector<toki::actor::ActorRow> rows =
        toki::actor::listActorRows(makeTables(), makeDatabases());

    REQUIRE(rows.size() == 4);
    REQUIRE(rows[0].name == "Link");
    REQUIRE(rows[0].origin == toki::actor::EntryOrigin::Shipped);
    REQUIRE(rows[2].name == "En_Ani");
    REQUIRE_FALSE(rows[2].origin.has_value());
    REQUIRE(rows[3].origin == toki::actor::EntryOrigin::Checkout);
}

TEST_CASE("the actor list is filtered by its entries and by name", "[actor][rows]") {
    const std::vector<toki::actor::ActorRow> rows =
        toki::actor::listActorRows(makeTables(), makeDatabases());
    const auto count = [&rows](const toki::actor::RowFilter filter, const char* text) {
        return std::ranges::count_if(rows, [&](const toki::actor::ActorRow& row) {
            return toki::actor::isRowShown(row, filter, text);
        });
    };

    REQUIRE(count(toki::actor::RowFilter::All, "") == 4);
    REQUIRE(count(toki::actor::RowFilter::WithEntry, "") == 3);
    REQUIRE(count(toki::actor::RowFilter::Missing, "") == 1);
    REQUIRE(count(toki::actor::RowFilter::Checkout, "") == 1);
    REQUIRE(count(toki::actor::RowFilter::All, "chest") == 1);
    REQUIRE(count(toki::actor::RowFilter::All, "en_") == 3);
}
