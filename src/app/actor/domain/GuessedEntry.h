#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/domain/ActorModel.h"

#include <optional>
#include <string>

namespace toki::actor {

/// The entry `model`, as the rules read it from the actor's overlay, stands
/// for, so a guessed actor is drawn as one with an entry is: status guessed,
/// from `source`, with one variant drawing what the model draws. A symbol in
/// another object than the entry's names its own.
[[nodiscard]] ActorEntry makeGuessedEntry(const ActorModel& model, std::string enumName,
                                          std::optional<GuessSource> source);

} // namespace toki::actor
