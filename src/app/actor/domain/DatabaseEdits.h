#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "hackeroot/Workspace.h"

#include <expected>
#include <string>
#include <vector>

namespace toki::actor {

/// "Add a variant for these params": a variant matching exactly the values
/// `placement` gives every param, drawn as the variant it matches now and kept
/// to that variant's age and scene, ahead of the others in `database`'s entry
/// for the actor, which is `current` when the database has none yet.
[[nodiscard]] std::expected<void, std::string>
addVariantForParams(ActorDatabase& database, const ActorEntry& current,
                    const PlacementValues& placement);

/// The entry for `entry.actor` becomes `entry`, whole.
[[nodiscard]] std::expected<void, std::string> setEntry(ActorDatabase& database, ActorEntry entry);

/// The entry for `actor` is removed, so the actor is drawn as a marker until
/// it is derived again.
[[nodiscard]] std::expected<void, std::string> removeEntry(ActorDatabase& database,
                                                           const std::string& actor);

/// "Derive": each of `actors`, by enum name, gets the entry Toki knows for it
/// (`deriveEntry`), in place of any it has.
[[nodiscard]] std::expected<void, std::string>
deriveEntries(ActorDatabase& database, const hackeroot::Workspace& workspace,
              const ActorDatabase& builtIn, const std::vector<std::string>& actors);

} // namespace toki::actor
