#pragma once

#include "app/actor/Types.h"
#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"

#include <optional>
#include <string>
#include <vector>

namespace toki::actor {

/// Whether `entry` lets `placement` spawn where Link is `placement.age` and it
/// is `time`, and what it needs, as the panels say it. A condition the layer
/// does not say, and a flag, which only the game knows, are taken to hold.
struct SpawnCheck {
    bool isSpawned = true;
    std::vector<std::string> needs;
};

/// The spawn condition of the variant `placement` matches, or else of `entry`,
/// checked for `placement` at `time`.
[[nodiscard]] SpawnCheck checkSpawn(const ActorEntry& entry, const PlacementValues& placement,
                                    std::optional<TimeOfDay> time);

} // namespace toki::actor
