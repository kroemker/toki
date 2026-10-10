#pragma once

#include "app/actor/domain/ActorDatabase.h"
#include "app/actor/domain/ActorEntry.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

#include <functional>
#include <string>
#include <vector>

namespace toki::actor {

/// What Toki knows of `actor`: its own entry for it when it has one, and
/// otherwise what reading the actor's overlay in `workspace` finds.
[[nodiscard]] ActorEntry deriveEntry(const hackeroot::Workspace& workspace,
                                     const ActorDatabase& builtIn,
                                     const hackeroot::ActorEntry& actor);

/// The actors the actor table lists, unset slots left out, that `database`
/// has no entry for, by enum name in table order.
[[nodiscard]] std::vector<std::string> listNewActors(const hackeroot::Tables& tables,
                                                     const ActorDatabase& database);

/// A checkout's database to start from: Toki's own, with every actor the
/// checkout's actor table lists that it does not cover derived from its
/// overlay. `reportProgress` is told how many of the actors to derive are
/// done, and of how many.
[[nodiscard]] ActorDatabase
makeSeededDatabase(const hackeroot::Workspace& workspace, const ActorDatabase& builtIn,
                   const std::function<void(usize done, usize total)>& reportProgress);

} // namespace toki::actor
