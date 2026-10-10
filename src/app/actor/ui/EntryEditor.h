#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// Edits what an entry says of the whole actor: its name and notes, its
/// object, scale and height offset, how it is drawn, and the axes it turns by.
/// Returns whether anything changed.
[[nodiscard]] bool drawGeneralEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
