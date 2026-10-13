#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// An entry's adjustments, in the order they are made, each a card headed by
/// its condition and what it changes, with a WHEN band of the condition's
/// chips and a CHANGES band of its scale, height offset, the axes it turns by,
/// its colours, its segments and the parts they change. Adjustments move up,
/// are copied, removed and added. Returns whether anything changed.
[[nodiscard]] bool drawAdjustmentsEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
