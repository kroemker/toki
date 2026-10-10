#pragma once

#include "app/actor/domain/ActorEntry.h"
#include "app/actor/ui/EditWidgets.h"

namespace toki::actor {

/// An entry's variants, in the order they are tried, each a card headed by its
/// condition, or "Otherwise" for a last one that matches everything, with
/// bands down a labelled gutter: WHEN, the condition's chips; DRAWS, a row of
/// its object, scale, height offset, draw kind and the params it is placed new
/// with, then its parts; and SPAWNS, the actors it spawns, when it has any.
/// Variants move up, are copied, removed and added. Returns whether anything
/// changed.
[[nodiscard]] bool drawVariantsEditor(ActorEntry& entry, const EditContext& context);

} // namespace toki::actor
