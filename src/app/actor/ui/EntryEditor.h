#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// Edits one whole entry: what it is called and says of itself, its status,
/// object, scale, height offset, draw kind and the axes it turns by; its
/// fields; its variants; its shadow, spawn condition, collision, lights and
/// text IDs. Returns whether anything changed.
[[nodiscard]] bool drawEntryEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
