#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "hackeroot/Tables.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toki::actor {

/// One actor of the actor table, as the Actors panel lists it: by ID and enum
/// name, the name its entry gives it, and whether it has one.
struct ActorRow {
    usize id = 0;
    std::string enumName;
    std::string name;
    bool hasEntry = false;
};

/// Which rows the Actors panel shows.
enum class RowFilter : u8 {
    All,
    WithEntry,
    Missing,
};

/// Every actor the actor table defines, in ID order, unset slots left out.
[[nodiscard]] std::vector<ActorRow> listActorRows(const hackeroot::Tables& tables,
                                                  const ActorDatabase& database);

/// Whether `row` is shown under `filter` for `text`, which its enum name or
/// name contains, ignoring case.
[[nodiscard]] bool isRowShown(const ActorRow& row, RowFilter filter, std::string_view text);

} // namespace toki::actor
