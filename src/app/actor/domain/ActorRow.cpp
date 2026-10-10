#include "ActorRow.h"

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "app/core/domain/TextSearch.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <string_view>
#include <vector>

namespace toki::actor {

std::vector<ActorRow> listActorRows(const hackeroot::Tables& tables,
                                    const ActorDatabase& database) {
    std::vector<ActorRow> rows;
    rows.reserve(tables.actors.size());
    for (const hackeroot::ActorEntry& actor : tables.actors) {
        if (actor.isUnset) {
            continue;
        }
        ActorRow row{.id = actor.id, .enumName = actor.enumName, .name = actor.name};
        if (const ActorEntry* entry = findEntry(database, actor.enumName)) {
            row.hasEntry = true;
            if (!entry->name.empty()) {
                row.name = entry->name;
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
        return row.hasEntry;
    case RowFilter::Missing:
        return !row.hasEntry;
    }
    return true;
}

} // namespace toki::actor
