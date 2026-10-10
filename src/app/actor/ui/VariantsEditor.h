#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// An entry's variants, in the order they are tried, each headed by the
/// sentence of its condition: the condition's chips, a line of its own object,
/// scale, height offset and draw kind, its parts, the actors it spawns, and
/// the params it is placed new with. Variants move up, are copied, removed and
/// added. Returns whether anything changed.
[[nodiscard]] bool drawVariantsEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
