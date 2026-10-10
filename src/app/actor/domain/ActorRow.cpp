#include "ActorRow.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/DatabaseStore.h"
#include "app/core/domain/TextSearch.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <string_view>
#include <vector>

namespace toki::actor {

std::vector<ActorRow> listActorRows(const hackeroot::Tables& tables,
                                    const DatabaseSnapshot& databases) {
    std::vector<ActorRow> rows;
    rows.reserve(tables.actors.size());
    for (const hackeroot::ActorEntry& actor : tables.actors) {
        if (actor.isUnset) {
            continue;
        }
        ActorRow row{.id = actor.id, .enumName = actor.enumName, .name = actor.name};
        if (const std::optional<FoundEntry> found =
                findActorEntry(databases.shipped, databases.checkout, actor.enumName)) {
            row.origin = found->origin;
            if (!found->entry->name.empty()) {
                row.name = found->entry->name;
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool isRowShown(const ActorRow& row, const RowFilter filter, const std::string_view text) {
    const bool isFound = core::containsIgnoringCase(row.enumName, text) ||
                         core::containsIgnoringCase(row.name, text);
    if (!isFound) {
        return false;
    }
    switch (filter) {
    case RowFilter::All:
        return true;
    case RowFilter::WithEntry:
        return row.origin.has_value();
    case RowFilter::Missing:
        return !row.origin.has_value();
    case RowFilter::Checkout:
        return row.origin == EntryOrigin::Checkout;
    }
    return true;
}

} // namespace toki::actor
