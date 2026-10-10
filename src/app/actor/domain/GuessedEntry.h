#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"

#include <optional>
#include <string>

namespace toki::actor {

/// The entry `model`, as the rules read it from the actor's overlay, stands
/// for: one variant drawing what the model draws, a skeleton posed in the
/// animation `Init` starts it in, or its display lists. A symbol in another
/// object than the entry's names its own.
[[nodiscard]] ActorEntry makeGuessedEntry(const ActorModel& model, std::string enumName);

} // namespace toki::actor
