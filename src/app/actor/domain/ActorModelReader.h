#pragma once

#include "app/actor/domain/ActorModel.h"
#include "hackeroot/Tables.h"
#include "hackeroot/Workspace.h"

namespace toki::actor {

/// Reads how the actor the actor table lists as `actor` is drawn, from its
/// overlay in the workspace's checkout (`applyModelRules`). An actor without
/// an overlay of its own, such as one the game code defines, has no model and
/// a gap saying so.
[[nodiscard]] ActorModel readActorModel(const hackeroot::Workspace& workspace,
                                        const hackeroot::ActorEntry& actor);

} // namespace toki::actor
