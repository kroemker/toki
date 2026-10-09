#pragma once

#include "app/actor/domain/ActorEntry.h"

#include <optional>
#include <span>

namespace toki::actor {

/// Edits what an actor needs to spawn: an age, a time of day, and a flag field
/// of `fields` that must be set or unset. Returns whether anything changed.
[[nodiscard]] bool drawSpawnEditor(std::optional<SpawnCondition>& spawn,
                                   std::span<const ParamField> fields);

} // namespace toki::actor
